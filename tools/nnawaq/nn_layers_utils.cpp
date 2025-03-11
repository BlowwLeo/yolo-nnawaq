
extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <time.h>

#include "nnawaq_utils.h"

}

#include "nn_layers_utils.h"
#include "nn_hw_config.h"
#include "swexec.h"

// ASIC estimations
#ifndef LIMITED
#include "estimasic.h"
#include "genvhdl.h"
#include "compress.h"
#endif

#ifndef NOTCL
#include "tcl_parser.h"
#endif

#include "hwacc_common.h"

// For declaration of recursive lambda functions
#include <functional>

using namespace std;


//============================================
// TCAM dummy lines
//============================================

unsigned tcam_dl_max       = 0;    // Max number of DL per TCAM block
unsigned tcam_dl_mindist   = 1;    // Mimimum distance, in discharge rate, between two DL
unsigned tcam_dl_mindisch  = 0;    // Mimimum discharge rate, in number of cells
double   tcam_dl_minrate   = 0;    // Minimum discharge rate specified as ratio of line size
double   tcam_dl_maxrate   = 1;    // Maximum discharge rate specified as ratio of line size
double   tcam_prec_dbl     = 0.1;  // Global precision 10%

// Return the number of DL
unsigned tcam_dl_recode_array(unsigned linesize, int* recode_arr, int* dl_arr, bool nominmax) {
	unsigned count   = 0;
	unsigned prev_th = 0;

	// Paranoia
	if(tcam_dl_mindist==0) tcam_dl_mindist = 1;

	do {
		// Given the current threshold (T) and precision (P), get the max re-coding value (V)
		// We have V > T such that V - T = P * V
		// Hence V = T / (1 - P)
		unsigned rec_val = prev_th / (1 - tcam_prec_dbl) + 0.1;

		// Given the current re-coding value (V) and precision (P), get the closest next threshold (T)
		// We have T > V such that T - V = P * T
		// Hence T = V / (1 - P)
		unsigned next_th = rec_val / (1 - tcam_prec_dbl) + 0.1;
		if(next_th <= prev_th) next_th = prev_th + 1;

		// Apply minimum discharge rate => update next threshold
		if(nominmax==false) {
			if(next_th < prev_th + tcam_dl_mindist) next_th = prev_th + tcam_dl_mindist;
			if(next_th < tcam_dl_mindisch) next_th = tcam_dl_mindisch;
			if(tcam_dl_minrate > 0) {
				unsigned m = linesize * tcam_dl_minrate;
				if(next_th < m) next_th = m;
			}
		}
		// If the next threshold is out of the line size, use a closer re-coding value
		if(next_th > linesize) {
			next_th = linesize + 1;
			rec_val = (next_th + prev_th) / 2;
		}

		// Count this DL, save the discharge rate
		if(next_th <= linesize) {
			if(dl_arr!=NULL) dl_arr[count] = next_th;
			count++;
		}

		// Take into account saturation option: the last threshold can be lower than the line size
		if(nominmax==false && tcam_dl_maxrate > 0) {
			unsigned m = linesize * tcam_dl_maxrate;
			if(next_th >= m) next_th = linesize + 1;
		}

		// Write the re-coding array
		if(recode_arr!=NULL) {
			for(unsigned k=prev_th; k<next_th && k<=linesize; k++) recode_arr[k] = rec_val;
		}

		if(next_th > linesize) break;
		prev_th = next_th;

	} while(1);

	return count;
}


//============================================
// Helper fonctions to traverse transparent layers
//============================================

Layer* traverse_transparent_backward(Layer* layer_prev) {
	do {
		if(layer_prev == nullptr) break;
		else if(layer_prev->type == LAYER_FORK)    layer_prev = layer_prev->prev;
		else if(layer_prev->type == LAYER_SCATTER) layer_prev = layer_prev->prev;
		else if(layer_prev->type == LAYER_FIFO)    layer_prev = layer_prev->prev;
		else if(layer_prev->type == LAYER_FLATTEN) layer_prev = layer_prev->prev;
		else break;
	} while(1);
	return layer_prev;
}

Layer* traverse_transparent_forward(Layer* layer_next) {
	do {
		if(layer_next == nullptr) break;
		else if(layer_next->type == LAYER_FIFO)    layer_next = layer_next->next;
		else if(layer_next->type == LAYER_FLATTEN) layer_next = layer_next->next;
		else break;
	} while(1);
	return layer_next;
}


//============================================
// Propagate parameters
//============================================

// Param "what" is either data or weight
int neu_sign_check_set(layer_t* layer, const char* what, char want_sgn, char* p_layer_sgn) {
	char layer_sgn = *p_layer_sgn;
	// Keep current layer config if no parameter is given
	if((want_sgn & NEUSGN_VALID) == 0) return 0;
	// If the layer config is valid and locked, check
	if((layer_sgn & NEUSGN_VALID) != 0 && (layer_sgn & NEUSGN_LOCKED) != 0) {
		const char* msg_signed   = "signed";
		const char* msg_unsigned = "unsigned";
		const char* value_locked = (layer_sgn & NEUSGN_UNSIGNED) != 0 ? msg_unsigned : msg_signed;
		char const * value_want = nullptr;
		if((want_sgn & NEUSGN_UNSIGNED) != 0) value_want = msg_unsigned;
		if((want_sgn & NEUSGN_SIGNED) != 0)   value_want = msg_signed;
		if(value_want != NULL && value_want != value_locked) {
			printf("Error: layer %s%u: Want %s %s, but locked to %s\n", layer->typenamel, layer->typeidx, what, value_want, value_locked);
			return 1;
		}
	}
	// Check passed, set the new config
	*p_layer_sgn = want_sgn;
	return 0;
}

static int propag_params_layer_internal(Network* network, layer_t* layer);

// FIXME This should be extensible, be member function of Layer* of some sort
int propag_backward_par(Network* network, layer_t* layer_in) {
	layer_t* layer = layer_in;
	layer_t* layer_repropag = NULL;

	while(true) {
		layer_t* layer_prev = layer->prev;
		if(layer_prev == NULL) break;

		if(layer->split_in == 0) break;
		if(layer_prev->split_out == layer->split_in) break;
		//printf("DEBUG Set PAR_OUT of %s%u: %u -> %u\n", layer_prev->typenamel, layer_prev->typeidx, layer_prev->split_out, layer->split_in);
		layer_prev->split_out = layer->split_in;
		layer_repropag = layer_prev;

		if(layer_prev->type == LAYER_WIN) {
			if(layer_prev->split_in > layer_prev->split_out) {
				layer_prev->split_in = layer_prev->split_out;
				layer = layer_prev;
				continue;
			}
		}
		else if(
			layer_prev->type == LAYER_NORM || layer_prev->type == LAYER_TER || layer_prev->type == LAYER_RELU || layer_prev->type == LAYER_LEAKY || layer_prev->type == LAYER_CUSTOM ||
			layer_prev->type == LAYER_FLATTEN || layer_prev->type == LAYER_FIFO
		) {
			layer_prev->split_in = layer_prev->split_out;
			layer = layer_prev;
			continue;
		}

		// FIXME Missing handling of layers NEU, POOL ? ADD ?

		break;
	}

	if(layer_repropag != NULL) {
		while(layer_repropag != layer_in && layer_repropag != NULL) {
			propag_params_layer_internal(network, layer_repropag);
			layer_repropag = layer_repropag->next;
		}
	}

	return 0;
}

int propag_forward_par(Network* network, layer_t* layer) {
	// Launch local propagation in order to set a proper PAR_OUT
	propag_params_layer_internal(network, layer);
	// Propagate through next layers
	do {
		// Traverse all successors of the layer FORK
		if(layer->next_is_arr == true) {
			for(auto layer_next : layer->arr_layers) {
				propag_forward_par(network, layer_next);
			}
			break;
		}
		// Next layer
		if(layer->next == nullptr) break;
		layer = layer->next;
		// Stop at CAT layers because we don't know when to propagate from that CAT layer
		if(layer->prev_is_arr == true) break;
		// FIXME Here we can't check PAR_IN of next layer to decide when to stop propagating, because the previous propag may have updated that
		unsigned paro = layer->split_out;
		propag_params_layer_internal(network, layer);
		if(layer->split_out == paro) break;
	} while(1);
	return 0;
}

int Layer::propag_params_forward(void) {
	printf("Error function %s : Layer type %u not handled\n", __FUNCTION__, type);
	exit(EXIT_FAILURE);
	return 0;
}

int LayerWin::propag_params_forward(void) {
	// Variable to ease code refactoring
	Layer* layer = this;

	// Get the next neuron or maxpool layer
	int next_layer_type = LAYER_NONE;
	Layer* layer_next = traverse_transparent_forward(this->next);
	if(layer_next != nullptr && layer_next->type == LAYER_NEU)  next_layer_type = layer_next->type;
	if(layer_next != nullptr && layer_next->type == LAYER_POOL) next_layer_type = layer_next->type;

	// Restrictions on parallelism levels
	// PAR_OUT must be a multiple of PAR_OZ
	// PAR_OZ must be a multiple of PAR_IN

	// Legalize PAR_IN
	if(layer->split_in == 0) layer->split_in = 1;
	if(layer->split_in > layer->fz) layer->split_in = layer->fz;
	while(layer->fz % layer->split_in != 0) layer->split_in++;

	// Legalize PAR_OZ
	if(layer->win_par_oz == 0) layer->win_par_oz = 1;
	layer->win_par_oz = uint_round_up(layer->win_par_oz, layer->split_in);
	if(layer->win_par_oz > layer->fz) layer->win_par_oz = layer->fz;

	// Apply user-desired PAR_OZ
	if(user_par_oz > 0) {
		// Checks
		unsigned pz = user_par_oz;
		if(pz > layer->fz) pz = 0;
		else if(layer->fz % pz != 0) pz = 0;
		else if(pz % split_in != 0) pz = 0;
		if(pz == 0) {
			printf("Error : layer %s%u : User-specified PAR_OZ=%u is not possible with FZ=%u and PAR_IN=%u\n", layer->typenamel, layer->typeidx, user_par_oz, layer->fz, split_in);
			exit(EXIT_FAILURE);
		}
		// Apply
		win_par_oz = user_par_oz;
	}
	if(win_par_oz == 0) win_par_oz = 1;

	// Legalize PAR_OUT
	// Note : Don't try to convert it into multiple of PAR_IN, PAR_OZ, win sizes etc here : the proper condition are applied in code that follows
	if(layer->split_out == 0) layer->split_out = 1;
	if(layer->split_out > layer->fz * layer->winx * layer->winy) {
		layer->split_out = layer->fz * layer->winx * layer->winy;
	}

	// Objective : Move the largest part of PAR_OUT within PAR_OZ, stay close to the desired PAR_OUT number, keep the number of duplicate memories at the minimum
	// Example of corner cases :
	// - Lenet-v5 : win2 has fz=6, win=5x5 -> target PAR_OUT=8
	//   Best is to apply OZ=2 which leads to PAR_OUT=10
	// - vgg nn64 : win1 has fz=64, win=3x3 -> target PAR_OUT=128
	//   solution 1 : OZ=16, PAR_OUT=144 -> there are 9 duplicates of the in-layer memory, and limited overhead at neuron input
	//   solution 2 : OZ=64, PAR_OUT=192 -> there are 3 duplicates of the in-layer memory, and excessive overhead at neuron input
	// FIXME Maybe more options would be necessary to decide where the overhead should be, at memory duplicates in WIN layer of larger adder trees in NEU layer
	//   For usual convolution, probably it is best to reduce overhead at NEU layer
	//   For DWConv and Pooling, it may be better to have more (small) units in NEU/POOL and reduce the memory duplicates in WIN layer

	// This is for XYZ read order (only supported scan order of current HW implementation)
	#if 1
	unsigned min_pz = layer->split_in;
	unsigned max_pz = layer->fz;
	if(user_par_oz > 0) {
		min_pz = user_par_oz;
		max_pz = user_par_oz;
	}
	unsigned best_pz = 0;
	unsigned best_po = 0;
	for(unsigned pz=max_pz; pz>=min_pz; pz-=layer->split_in) {
		// PAR_OZ must divide FZ
		if(fz % pz != 0) continue;
		// Round reachable PAR_OUT to a multiple of this PAR_OZ
		unsigned po = uint_round_up(layer->split_out, pz);
		// Legalize : apply max reachable
		if(po > pz*winx*winy) {
			po = pz*winx*winy;
		}
		// Legalize : PAR_OUT is a multiple of PZ*WINX
		else if(po > pz*winx) {
			unsigned d = (po + pz*winx - 1) / (pz*winx);
			while(winy % d != 0) d ++;  // The factor must be a divisor of WINY
			po = d * pz*winx;
		}
		// Legalize : PAR_OUT is a multiple of PZ
		else if(po > pz) {
			unsigned d = (po + pz - 1) / pz;
			while(winx % d != 0) d ++;  // The factor must be a divisor of WINX
			po = d * pz;
		}
		else {
			po = pz;
		}
		if(po < layer->split_out) continue;  // We know there are better solutions than this
		if(best_pz == 0 || po < best_po || (po == best_po && pz < best_pz)) {
			best_pz = pz;
			best_po = po;
		}
	}
	layer->win_par_oz = best_pz;
	layer->split_out = best_po;
	#endif

	// This is for ZFIRST support, which is not implemented yet
	#if 0
	if(layer->split_out > layer->fz) {
		layer->split_out = uint_round_up(layer->split_out, layer->fz);
	}
	if(layer->split_out > layer->fz * layer->winx) {
		layer->split_out = uint_round_up(layer->split_out, layer->fz * layer->winx);
	}
	if(layer->split_out > layer->fz * layer->winx * layer->winy) {
		layer->split_out = layer->fz * layer->winx * layer->winy;
	}
	#endif

	// Note : For best hardware implementation, it is usually best to apply most of PAR_OUT on PAR_OZ
	// FIXME This code is currently conflicting with search of best PAR_OZ and user-specified PAR_OZ
	#if 0
	if(layer->win_par_oz < layer->fz && layer->split_out > layer->fz) {
		layer->win_par_oz = layer->fz;
		layer->split_out = uint_round_up(layer->split_out, layer->win_par_oz);
		unsigned full_fz = winx * winy * layer->fz;  // Note : not using fsize because not compatible with DWConv
		layer->split_out = GetMin(layer->split_out, full_fz);
	}
	#endif

	// End of checks

	// Set the number of clock cycles, input
	unsigned wcyclesz = (layer->fz + layer->split_in - 1) / layer->split_in;
	layer->cycles     = wcyclesz * layer->fx * layer->fy;

	// Formula for the output image size X and Y : the number of windows that fit within the input image+padding, assuming symmetric padding
	//   f + 2*pad = win + step*(nwin-1)
	// Hence :
	//   nwin = (f+2*pad-win)/step + 1

	unsigned f;

	f = layer->fx + 2*layer->begpadx - layer->winx;
	if(layer->nwinx == 0) {
		layer->nwinx = f / layer->stepx + 1;
		if(f % layer->stepx != 0) {
			printf("Warning : layer %s%u : Non-integer nwinx = %f is rounded to %u\n", layer->typenamel, layer->typeidx, f / (double)layer->stepx + 1, layer->nwinx);
		}
	}

	f = layer->fy + 2*layer->begpady - layer->winy;
	if(layer->nwiny == 0) {
		layer->nwiny = f / layer->stepy + 1;
		if(f % layer->stepy != 0) {
			printf("Warning : layer %s%u : Non-integer nwiny = %f is rounded to %u\n", layer->typenamel, layer->typeidx, f / (double)layer->stepy + 1, layer->nwiny);
		}
	}

	layer->out_fx = layer->nwinx;
	layer->out_fy = layer->nwiny;

	// The dimension Z is a special computation
	// Initialize default parameters assuming a neuron layer follows
	layer->nwinz = 1;
	layer->out_fz = layer->fz;
	// Set output frame size, assuming a neuron layer follows
	layer->out_fsize    = layer->winx * layer->winy * layer->out_fz;
	layer->out_nbframes = layer->nwinx * layer->nwiny;

	// Apply correction on the output image size
	if(layer->win_par_oz > 1) {
		// Round the output Z dimension to a multiple of PAR_OZ
		unsigned nwinz = (layer->fz + layer->win_par_oz - 1) / layer->win_par_oz;
		layer->out_fz = nwinz * layer->win_par_oz;
	}

	// Update the output fsize in case a correction above was done
	layer->out_fsize = layer->winx * layer->winy * layer->out_fz;

	// Take into account DWConv functionality
	if(win_dwconv == true) {
		layer->out_fsize    = layer->winx * layer->winy;
		layer->out_nbframes = layer->nwinx * layer->nwiny * layer->out_fz;
	}

	// Apply parameters on output "frame" size
	// FIXME Here the explicit reference to layer types is not good for extensivity of the tool
	if(next_layer_type == LAYER_NEU && win_dwconv == false) {
		// Output goes into neurons
		layer->nwinz = 1;
		// Set output frame size
		layer->out_fsize    = layer->winx * layer->winy * layer->out_fz;
		layer->out_nbframes = layer->nwinx * layer->nwiny;
	}
	else if(next_layer_type == LAYER_POOL || (next_layer_type == LAYER_NEU && win_dwconv == true)) {
		// Output goes into max pooling
		layer->nwinz = layer->out_fz;
		// Set output frame size
		layer->out_fsize    = layer->winx * layer->winy;
		layer->out_nbframes = layer->nwinx * layer->nwiny * layer->out_fz;
	}
	else {
		// Here we don't know what next layer this window will be feeding, the network is probably just being created
	}

	// The window is used with WINZ=1, the depth is adjusted with NWINZ=FZ
	layer->nwinz = layer->out_fz;

	// Set the number of clock cycles on output side
	if(layer->win_repeat <= 0) layer->win_repeat = 1;
	// Note : For next layer NEU non-DWConv, PAR_OUT is applied on FSIZE
	// Note : For next layer POOL and NEU DWConv, PAR_OUT is applied on NBFRAMES
	// The formula supports both cases
	layer->out_cycles = (layer->out_fsize * layer->out_nbframes) / layer->split_out * layer->win_repeat;
	layer->out_cycles_real = layer->out_cycles + 4 * layer->nwiny;

	return 0;
}

int LayerNeu::propag_params_forward(void) {
	// Variable to ease code refactoring
	Layer* layer = this;

	if(neu_style == 0 && network->hwconfig_neu_style >= 0) neu_style = network->hwconfig_neu_style;

	// Propagate data signedness, and check
	// The LOCKED parameter is kept - FIXME Missing spec about how this run-time configurable signedness blends in the whole network config
	char want_sgnd = (layer->sdata == true) ? NEUSGN_SIGNED : NEUSGN_UNSIGNED;
	want_sgnd |= (layer->neu_sgnd & NEUSGN_LOCKED);
	want_sgnd |= NEUSGN_VALID;  // This is needed to have the check done
	int z = neu_sign_check_set(layer, "data", want_sgnd, &layer->neu_sgnd);
	if(z != 0) exit(EXIT_FAILURE);

	// Safety check
	if(layer->neurons_max == 0) {
		printf("Error: layer %s%u: No specified maximum number of neurons\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}

	// Get the DWConv flag from previous WIN layer, if any
	layer_t* prev_win = nullptr;
	Layer* layer_prev = traverse_transparent_backward(this->prev);
	if(layer_prev != nullptr && layer_prev->type == LAYER_WIN) prev_win = layer_prev;
	if(prev_win != nullptr && prev_win->win_dwconv == true) {
		// Save parameters in local neuron layer
		win_dwconv = prev_win->win_dwconv;
		win_par_oz = prev_win->win_par_oz;
	}

	// Protection against wrong frame size
	if(layer->fsize == 0) {
		printf("Error: layer %s%u: Frame size is zero\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}
	if(layer->split_in > 1 && layer->fsize % layer->split_in != 0 && win_dwconv==false) {
		printf("Error: layer %s%u: Frame size (%u) is not a multiple of the parallelism (%u)\n",
			layer->typenamel, layer->typeidx, layer->fsize, layer->split_in
		);
		exit(EXIT_FAILURE);
	}
	if(layer->fsize > layer->fsize_max && network->param_cnn_origin==Network::CNN_ORIGIN_HARDWARE) {
		printf("Error: layer %s%u: the obtained frame size (%u) is higher than capacity (%u)\n",
			layer->typenamel, layer->typeidx, layer->fsize, layer->fsize_max
		);
		exit(EXIT_FAILURE);
	}

	// Propagate values
	if(layer->fsize_max < layer->fsize) {
		layer->fsize_max = layer->fsize;
	}

	// Fix the number of neurons
	if(layer->neurons==0) layer->neurons = layer->neurons_max;
	if(layer->neurons==0) {
		printf("Error: layer %s%u: the number of neurons is zero\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}
	if(layer->neurons_max == 0) layer->neurons_max = layer->neurons;
	if(layer->neurons_max > 0 && layer->neurons > layer->neurons_max) {
		printf("Error: layer %s%u: the number of neurons (%u) is higher than capacity (%u)\n",
			layer->typenamel, layer->typeidx, layer->neurons, layer->neurons_max
		);
		exit(EXIT_FAILURE);
	}

	if(win_dwconv == true) {
		// Example :
		// Consider the case with no user-specified time multiplexing
		//   Win FZ = 30, PAR_OZ = 6
		//   Neurons = 30 logical, 6 physical, TMUX = 30/6=5
		// Now, apply extra time multiplexing to slow these layers down => apply extra factor 3x
		//   Win FZ = 30, PAR_OZ = 2
		//   Neurons = 30 logical, 2 physical, TMUX = 30/5*3=15
		if(neurons != prev_win->out_fz) {
			printf("Error %s%u: For DWConv mode, inconsistency with NEU=%u and FZ=%u\n",
				layer->typenamel, layer->typeidx, neurons, fz
			);
			exit(EXIT_FAILURE);
		}
	}

	// Set output bit width
	// FIXME Missing proper handling of assymmetric range for negative/positive values
	uint64_t absrange_d = u64_genmask(layer->wdata);
	uint64_t absrange_w = u64_genmask(layer->neu_wweight);
	// Take into account signedness
	if(layer->wdata > 1       && (layer->neu_sgnd & NEUSGN_SIGNED) != 0) absrange_d = u64_genmask(layer->wdata - 1);
	if(layer->neu_wweight > 1 && (layer->neu_sgnw & NEUSGN_SIGNED) != 0) absrange_w = u64_genmask(layer->neu_wweight - 1);
	// Compute range of multiplication result
	uint64_t absrange_mul = absrange_d * absrange_w;
	if(layer->neu_custom_mul != 0 && layer->neu_custom_wmul > 0) {
		absrange_mul = u64_genmask(layer->neu_custom_wmul);
		if(layer->neu_custom_wmul > 1 && layer->neu_custom_smul != 0) absrange_mul = u64_genmask(layer->neu_custom_wmul - 1);
	}

	// Compute range of neuron accumulator results
	// Subtract 1 to save 1 bit in most cases, assuming at least one addition will not reach the max range
	// FIXME This is not the case for binary neurons with weights -1/+1, these add/sub something at every input
	uint64_t absrange_accu = layer->fsize_max * absrange_mul - 1;
	unsigned wout = u64_bitsnb(absrange_accu);
	unsigned sout = false;
	if(layer->neu_custom_mul != 0 && layer->neu_custom_wmul > 0 && layer->neu_custom_smul != 0) { wout++; sout = 1; }
	else if( (layer->neu_sgnd & NEUSGN_SIGNED) || (layer->neu_sgnw & NEUSGN_SIGNED) ) { wout++; sout = 1; }
	// Fix sign of custom multiplication when width was not specified (meaning is auto width and sign)
	if(layer->neu_custom_mul != 0 && layer->neu_custom_wmul == 0) layer->neu_custom_smul = sout;

	// Apply any user-specified max output width
	if(network->default_neu_wo > 0) {
		wout = network->default_neu_wo;
		sout = network->default_neu_so;
	}

	// Apply any user-specified output data width
	if(user_wout > 0) wout = user_wout;

	// Set output image size
	layer->out_fx       = layer->fx;
	layer->out_fy       = layer->fy;
	layer->out_fz       = layer->neurons;
	layer->out_wdata    = wout;
	layer->out_sdata    = sout;
	layer->out_fsize    = layer->neurons;
	layer->out_nbframes = layer->nbframes;

	// Fix out image when only one frame per image
	if(layer->out_nbframes==1) {
		layer->out_fx = 1;
		layer->out_fy = 1;
	}

	if(layer->neu_time_mux <= 0) layer->neu_time_mux = 1;

	// Set the number of clock cycles
	// Different computation for the case DWConv
	if(win_dwconv == true) {
		// There are NBNEU input frames for one output frame
		layer->neu_time_mux = layer->fz / layer->win_par_oz;
		layer->out_nbframes = layer->nbframes / layer->neurons;
		layer->cycles     = (layer->nbframes * layer->fsize + layer->split_in - 1) / layer->split_in;
		layer->out_cycles = (layer->nbframes + layer->split_out - 1) / layer->split_out;
	}
	else {
		unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;
		unsigned nbneu_split = (layer->neurons + layer->split_out - 1) / layer->split_out;
		layer->cycles     = fsize_split * layer->nbframes * layer->neu_time_mux;
		layer->out_cycles = nbneu_split * layer->nbframes;
	}

	return 0;
}

int LayerNeu_CM::propag_params_forward(void) {
	// Variable to ease code refactoring
	Layer* layer = this;

	if(neu_style == 0 && network->hwconfig_neu_style >= 0) neu_style = network->hwconfig_neu_style;

	// Propagate data signedness, and check
	// The LOCKED parameter is kept - FIXME Missing spec about how this run-time configurable signedness blends in the whole network config
	char want_sgnd = (layer->sdata == true) ? NEUSGN_SIGNED : NEUSGN_UNSIGNED;
	want_sgnd |= (layer->neu_sgnd & NEUSGN_LOCKED);
	want_sgnd |= NEUSGN_VALID;  // This is needed to have the check done
	int z = neu_sign_check_set(layer, "data", want_sgnd, &layer->neu_sgnd);
	if(z != 0) exit(EXIT_FAILURE);

	// Safety check
	if(layer->neurons_max == 0) {
		printf("Error: layer %s%u: No specified maximum number of neurons\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}

	// Get the DWConv flag from previous WIN layer, if any
	layer_t* prev_win = nullptr;
	Layer* layer_prev = traverse_transparent_backward(this->prev);
	if(layer_prev != nullptr && layer_prev->type == LAYER_WIN) prev_win = layer_prev;
	if(prev_win != nullptr && prev_win->win_dwconv == true) {
		// Save parameters in local neuron layer
		win_dwconv = prev_win->win_dwconv;
		win_par_oz = prev_win->win_par_oz;
	}

	// Protection against wrong frame size
	if(layer->fsize == 0) {
		printf("Error: layer %s%u: Frame size is zero\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}
	if(layer->split_in > 1 && layer->fsize % layer->split_in != 0 && win_dwconv==false) {
		printf("Error: layer %s%u: Frame size (%u) is not a multiple of the parallelism (%u)\n",
			layer->typenamel, layer->typeidx, layer->fsize, layer->split_in
		);
		exit(EXIT_FAILURE);
	}
	if(layer->fsize > layer->fsize_max && network->param_cnn_origin==Network::CNN_ORIGIN_HARDWARE) {
		printf("Error: layer %s%u: the obtained frame size (%u) is higher than capacity (%u)\n",
			layer->typenamel, layer->typeidx, layer->fsize, layer->fsize_max
		);
		exit(EXIT_FAILURE);
	}

	// Propagate values
	if(layer->fsize_max < layer->fsize) {
		layer->fsize_max = layer->fsize;
	}

	// Fix the number of neurons
	if(layer->neurons==0) layer->neurons = layer->neurons_max;
	if(layer->neurons==0) {
		printf("Error: layer %s%u: the number of neurons is zero\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}
	if(layer->neurons_max == 0) layer->neurons_max = layer->neurons;
	if(layer->neurons_max > 0 && layer->neurons > layer->neurons_max) {
		printf("Error: layer %s%u: the number of neurons (%u) is higher than capacity (%u)\n",
			layer->typenamel, layer->typeidx, layer->neurons, layer->neurons_max
		);
		exit(EXIT_FAILURE);
	}

	if(win_dwconv == true) {
		// Example :
		// Consider the case with no user-specified time multiplexing
		//   Win FZ = 30, PAR_OZ = 6
		//   Neurons = 30 logical, 6 physical, TMUX = 30/6=5
		// Now, apply extra time multiplexing to slow these layers down => apply extra factor 3x
		//   Win FZ = 30, PAR_OZ = 2
		//   Neurons = 30 logical, 2 physical, TMUX = 30/5*3=15
		if(neurons != prev_win->out_fz) {
			printf("Error %s%u: For DWConv mode, inconsistency with NEU=%u and FZ=%u\n",
				layer->typenamel, layer->typeidx, neurons, fz
			);
			exit(EXIT_FAILURE);
		}
	}

	// Set output bit width
	// FIXME Missing proper handling of assymmetric range for negative/positive values
	uint64_t absrange_d = u64_genmask(layer->wdata);
	uint64_t absrange_w = u64_genmask(layer->neu_wweight);
	// Take into account signedness
	if(layer->wdata > 1       && (layer->neu_sgnd & NEUSGN_SIGNED) != 0) absrange_d = u64_genmask(layer->wdata - 1);
	if(layer->neu_wweight > 1 && (layer->neu_sgnw & NEUSGN_SIGNED) != 0) absrange_w = u64_genmask(layer->neu_wweight - 1);
	// Compute range of multiplication result
	uint64_t absrange_mul = absrange_d * absrange_w;
	if(layer->neu_custom_mul != 0 && layer->neu_custom_wmul > 0) {
		absrange_mul = u64_genmask(layer->neu_custom_wmul);
		if(layer->neu_custom_wmul > 1 && layer->neu_custom_smul != 0) absrange_mul = u64_genmask(layer->neu_custom_wmul - 1);
	}

	// Compute range of neuron accumulator results
	// Subtract 1 to save 1 bit in most cases, assuming at least one addition will not reach the max range
	// FIXME This is not the case for binary neurons with weights -1/+1, these add/sub something at every input
	uint64_t absrange_accu = layer->fsize_max * absrange_mul - 1;
	unsigned wout = u64_bitsnb(absrange_accu);
	unsigned sout = false;
	if(layer->neu_custom_mul != 0 && layer->neu_custom_wmul > 0 && layer->neu_custom_smul != 0) { wout++; sout = 1; }
	else if( (layer->neu_sgnd & NEUSGN_SIGNED) || (layer->neu_sgnw & NEUSGN_SIGNED) ) { wout++; sout = 1; }
	// Fix sign of custom multiplication when width was not specified (meaning is auto width and sign)
	if(layer->neu_custom_mul != 0 && layer->neu_custom_wmul == 0) layer->neu_custom_smul = sout;

	// Apply any user-specified max output width
	if(network->default_neu_wo > 0) {
		wout = network->default_neu_wo;
		sout = network->default_neu_so;
	}

	// Apply any user-specified output data width
	if(user_wout > 0) wout = user_wout;

	// Set output image size
	layer->out_fx       = layer->fx;
	layer->out_fy       = layer->fy;
	layer->out_fz       = layer->neurons;
	layer->out_wdata    = wout;
	layer->out_sdata    = sout;
	layer->out_fsize    = layer->neurons;
	layer->out_nbframes = layer->nbframes;

	// Fix out image when only one frame per image
	if(layer->out_nbframes==1) {
		layer->out_fx = 1;
		layer->out_fy = 1;
	}

	if(layer->neu_time_mux <= 0) layer->neu_time_mux = 1;

	// Set the number of clock cycles
	// Different computation for the case DWConv
	if(win_dwconv == true) {
		// There are NBNEU input frames for one output frame
		layer->neu_time_mux = layer->fz / layer->win_par_oz;
		layer->out_nbframes = layer->nbframes / layer->neurons;
		layer->cycles     = (layer->nbframes * layer->fsize + layer->split_in - 1) / layer->split_in;
		layer->out_cycles = (layer->nbframes + layer->split_out - 1) / layer->split_out;
	}
	else {
		unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;
		unsigned nbneu_split = (layer->neurons + layer->split_out - 1) / layer->split_out;
		layer->cycles     = fsize_split * layer->nbframes * layer->neu_time_mux;
		layer->out_cycles = nbneu_split * layer->nbframes;
	}

	return 0;
}

int LayerPool::propag_params_forward(void) {
	// Variable to ease code refactoring
	Layer* layer = this;

	layer_t* prev_cat = nullptr;
	layer_t* prev_win = nullptr;

	if(pool_type == POOL_TYPE_ADD) {
		// Get the previous layer CAT, if any
		prev_cat = nullptr;
		Layer* layer_prev = traverse_transparent_backward(this->prev);
		if(layer_prev != nullptr && layer_prev->type == LAYER_CAT) prev_cat = layer_prev;

		if(prev_cat == nullptr) {
			printf("Error %s%u: Previous concat layer not found\n", layer->typenamel, layer->typeidx);
			exit(EXIT_FAILURE);
		}

		// FIXME Check sizes of previous branches of CAT, must be same PAR, this is the number of pooling units
		// FIXME Need to check fsize
		pool_units_nb = prev_cat->arr_layers[0]->split_out;

	}
	else {
		// Get the previous window layer, if any
		prev_win = nullptr;
		Layer* layer_prev = traverse_transparent_backward(this->prev);
		if(layer_prev != nullptr && layer_prev->type == LAYER_WIN) prev_win = layer_prev;

		if(prev_win == nullptr) {
			printf("Error %s%u: Previous sliding window layer not found\n", layer->typenamel, layer->typeidx);
			exit(EXIT_FAILURE);
		}

		// Get details from previous window layer
		layer->winx  = prev_win->winx;
		layer->winy  = prev_win->winy;
		layer->win_par_oz = prev_win->win_par_oz;
		layer->nwinx = prev_win->nwinx;
		layer->nwiny = prev_win->nwiny;
		layer->nwinz = prev_win->nwinz;

		// Ensure the fsize was set correctly (issues may happen because win layers are created before Pool layers)
		fsize = winx * winy;
		nbframes = nwinx * nwiny * fz;

		// Get the number of pooling units
		pool_units_nb = prev_win->win_par_oz;
	}

	// Set multiplication and shr factors
	if(pool_type == POOL_TYPE_AVG) {
		unsigned mult_even_shr = 0;
		unsigned mult_odd = fsize;
		while(mult_odd % 2 ==0) { mult_even_shr++; mult_odd /= 2; }
		if(mult_odd > 1) {
			// Compute the multiplication factor for a specific divisor
			// FIXME A different multiplier width may be desired, and some rounding too
			const unsigned max_mult_bits = 16;
			pool_avg_shr = max_mult_bits + uint_bitsnb(mult_odd) - 1;
			pool_avg_mult = (1 << pool_avg_shr) / mult_odd;
		}
	}

	out_wdata = wdata;
	out_sdata = sdata;

	// Output data is extended to fit the result
	if(pool_type == POOL_TYPE_ADD) {
		out_wdata = wdata + ((fsize > 1) ? uint_bitsnb(fsize-1) : 0);
	}
	// Apply any user-specified output data width
	if(pool_type == POOL_TYPE_AVG || pool_type == POOL_TYPE_ADD) {
		if(user_wout > 0) out_wdata = user_wout;
	}

	// The output parallelism must be a divisor of the physical number of pooling units
	if(split_out > pool_units_nb) {
		split_out = pool_units_nb;
	}
	while(pool_units_nb % split_out != 0) split_out++;

	// Set output image size
	layer->out_fx       = layer->nwinx;
	layer->out_fy       = layer->nwiny;
	layer->out_fz       = layer->fz;
	layer->out_fsize    = layer->out_fx * layer->out_fy * layer->out_fz;
	layer->out_nbframes = 1;
	// Set the number of clock cycles
	layer->cycles     = (layer->nwinx * layer->nwinx * layer->nwinz) * (layer->winx * layer->winy) / layer->split_in;
	layer->out_cycles = (layer->nwinx * layer->nwinx * layer->nwinz) / layer->split_out;

	return 0;
}

int LayerNorm::propag_params_forward(void) {
	// Note : Image size and parallelism are set by surrounding neuron layers

	// Variable to ease code refactoring
	Layer* layer = this;

	// Default : Quantization is unchanged
	layer->out_wdata = wdata;
	layer->out_sdata = sdata;

	// Apply any user-specified output data width
	if(user_wout > 0) out_wdata = user_wout;

	// Parallelism is unchanged
	layer->split_out = layer->split_in;

	// Protection against user-provided excessive frame size
	if(layer->fsize_max > 0 && layer->fsize > layer->fsize_max) {
		printf("Error: layer %s%u: the obtained frame size (%u) is higher than capacity (%u)\n",
			layer->typenamel, layer->typeidx, layer->fsize, layer->fsize_max
		);
		exit(EXIT_FAILURE);
	}

	// Propagate values
	if(layer->fsize_max == 0) {
		layer->fsize_max = layer->fsize;
	}

	// Set the number of clock cycles
	unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;
	layer->cycles     = fsize_split * layer->nbframes;
	layer->out_cycles = layer->cycles;

	return 0;
}

int LayerTernarize::propag_params_forward(void) {
	// Variable to ease code refactoring
	Layer* layer = this;

	// Set output image size
	layer->out_wdata = 2;
	layer->out_sdata = true;

	layer->split_out = layer->split_in;

	// Apply any user-specified output data width
	if(user_wout > 0) out_wdata = user_wout;

	// Protection against user-provided excessive frame size
	if(layer->fsize_max > 0 && layer->fsize > layer->fsize_max) {
		printf("Error: layer %s%u: the obtained frame size (%u) is higher than capacity (%u)\n",
			layer->typenamel, layer->typeidx, layer->fsize, layer->fsize_max
		);
		exit(EXIT_FAILURE);
	}

	// Propagate values
	if(layer->fsize_max == 0) {
		layer->fsize_max = layer->fsize;
	}

	// Set the number of clock cycles
	unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;
	layer->cycles     = fsize_split * layer->nbframes;
	layer->out_cycles = layer->cycles;

	return 0;
}

int LayerRelu::propag_params_forward(void) {
	// Note : Image size and parallelism are set by surrounding neuron layers

	// Variable to ease code refactoring
	Layer* layer = this;

	// Default : Quantization is unchanged
	layer->out_wdata = wdata;
	layer->out_sdata = sdata;

	// Generate the output size according to min/max thresholds
	unsigned width = 1;
	width = GetMax(width, uint_bitsnb(abs(layer->relu_min)));
	width = GetMax(width, uint_bitsnb(abs(layer->relu_max)));
	// Set output parameters
	layer->out_sdata = false;
	if(layer->relu_min < 0 || layer->relu_max < 0) {
		width++;
		layer->out_sdata = true;
	}
	layer->out_wdata = width;

	// Apply any user-specified output data width
	if(user_wout > 0) out_wdata = user_wout;

	// Parallelism is unchanged
	layer->split_out = layer->split_in;

	// Set the number of clock cycles
	unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;
	layer->cycles     = fsize_split * layer->nbframes;
	layer->out_cycles = layer->cycles;

	return 0;
}

int LayerLeaky::propag_params_forward(void) {
	// Note : Image size and parallelism are set by surrounding neuron layers

	// Variable to ease code refactoring
	Layer* layer = this;

	// Default : Quantization is unchanged
	layer->out_wdata = wdata;
	layer->out_sdata = sdata;

	// Generate the output size according to min/max thresholds
	unsigned width = 1;
	width = GetMax(width, uint_bitsnb(abs(layer->leaky_min)));
	width = GetMax(width, uint_bitsnb(abs(layer->leaky_max)));
	// Set output parameters
	layer->out_sdata = false;
	if(layer->leaky_min < 0 || layer->leaky_max < 0) {
		width++;
		layer->out_sdata = true;
	}
	layer->out_wdata = width;

	// Apply any user-specified output data width
	if(user_wout > 0) out_wdata = user_wout;

	// Parallelism is unchanged
	layer->split_out = layer->split_in;

	// Set the number of clock cycles
	unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;
	layer->cycles     = fsize_split * layer->nbframes;
	layer->out_cycles = layer->cycles;

	return 0;
}

int LayerAdd::propag_params_forward(void) {

	// Get the previous layer CAT, if any
	layer_t* prev_cat = nullptr;
	Layer* layer_prev = traverse_transparent_backward(this->prev);
	if(layer_prev != nullptr && layer_prev->type == LAYER_CAT) prev_cat = layer_prev;

	if(prev_cat == nullptr) {
		printf("Error %s%u: Previous concat layer not found\n", typenamel, typeidx);
		exit(EXIT_FAILURE);
	}

	// Get the PAR_OUT of predecessors
	unsigned max_par = prev_cat->arr_layers[0]->split_out;
	for(auto layer_prev : arr_layers) {
		max_par = GetMax(max_par, layer_prev->split_out);
	}
	// Increase parallelism to predecessors when needed
	// FIXME Should this really done here ?
	for(auto layer_prev : arr_layers) {
		if(layer_prev->split_out < max_par) {
			printf("Warning %s%u : Backpropagating PAR_OUT=%u to predecessor %s%u\n", typenamel, typeidx, max_par, layer_prev->typenamel, layer_prev->typeidx);
			layer_prev->split_out = max_par;
			propag_backward_par(layer_prev->network, layer_prev);
		}
	}

	// Check
	max_par = prev_cat->arr_layers[0]->split_out;
	for(auto layer_prev : arr_layers) {
		if(layer_prev->split_out != max_par) {
			printf("ERROR %s%u : Inconsistency of PAR_OUT between predecessors\n", typenamel, typeidx);
			exit(EXIT_FAILURE);
		}
	}

	unsigned add_par_in = prev_cat->arr_layers.size();

	// Output data is extended to fit the result
	out_wdata = wdata + ((add_par_in > 1) ? uint_bitsnb(add_par_in-1) : 0);
	out_sdata = sdata;
	// Apply any user-specified output data width
	if(user_wout > 0) out_wdata = user_wout;

	// Parallelism becomes same than the PAR_OUT of predecessors of previous CAT layer
	split_out = max_par;

	// Output size is decreased
	out_fz = fz / add_par_in;
	out_fsize = fsize / add_par_in;

	// Clock cycles is unchanged
	cycles = out_fx * out_fy * out_fz / split_out;
	out_cycles = cycles;

	return 0;
}

int LayerCustom::propag_params_forward(void) {
	// Note : Image size and parallelism are set by surrounding neuron layers

	// Variable to ease code refactoring
	Layer* layer = this;

	// Default : Quantization is unchanged
	layer->out_wdata = wdata;
	layer->out_sdata = sdata;

	// Apply any user-specified output data width
	if(user_wout > 0) out_wdata = user_wout;

	// Parallelism is unchanged
	layer->split_out = layer->split_in;

	// Set the number of clock cycles
	unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;
	layer->cycles     = fsize_split * layer->nbframes;
	layer->out_cycles = layer->cycles;

	return 0;
}

int LayerFork::propag_params_forward(void) {
	// Variable to ease code refactoring
	Layer* layer = this;

	layer->out_wdata = layer->wdata;
	layer->out_sdata = layer->sdata;
	// Set parallelism levels
	if(layer->prev!=NULL) layer->split_in = layer->prev->split_out;
	layer->split_out = layer->split_in;
	// Set clock cycles, for completeness
	layer->cycles     = layer->fsize / layer->split_in * layer->nbframes;
	layer->out_cycles = layer->cycles;

	return 0;
}

int LayerCat::propag_params_forward(void) {

	if(arr_layers.size() == 0) {
		printf("Error: layer %s%u: No predecessor layers\n", typenamel, typeidx);
		exit(EXIT_FAILURE);
	}

	// Minimalistic check of previous layers
	unsigned errors_nb = 0;
	for(unsigned i=0; i<arr_layers.size(); i++) {
		layer_t* layer_prev = arr_layers[i];
		if(layer_prev->out_fsize % layer_prev->out_fz != 0) {
			printf("Error %s%u: Inconsistency in previous layer %u: FSIZE=%u is not a multiple of FZ=%u\n", typenamel, typeidx, i, layer_prev->out_fsize, layer_prev->out_fz);
			errors_nb ++;
		}
		if(layer_prev->out_fz % layer_prev->split_out != 0) {
			printf("Error %s%u: Inconsistency in previous layer %u: FZ=%u is not a multiple of PAR_OUT=%u\n", typenamel, typeidx, i, layer_prev->out_fz, layer_prev->split_out);
			errors_nb ++;
		}
	}

	if(errors_nb != 0) {
		printf("Error %s%u: Inconsistencies with previous layers (%u errors)\n", typenamel, typeidx, errors_nb);
		exit(EXIT_FAILURE);
	}

	// Get the min number of cycles on Z dimension
	unsigned min_ncyclesw = 0;
	for(auto layer_prev : arr_layers) {
		unsigned loc_ncyclesw = layer_prev->out_fz / layer_prev->split_out;
		if(min_ncyclesw == 0 || loc_ncyclesw < min_ncyclesw) min_ncyclesw = loc_ncyclesw;
	}
	// Apply PAR to predecessors that are too slow
	// FIXME Should this really done here ?
	for(auto layer_prev : arr_layers) {
		unsigned need_po = layer_prev->out_fz / min_ncyclesw;
		if(need_po > layer_prev->split_out) {
			printf("Warning %s%u : Backpropagating PAR_OUT=%u to predecessor %s%u\n", typenamel, typeidx, need_po, layer_prev->typenamel, layer_prev->typeidx);
			layer_prev->split_out = need_po;
			propag_backward_par(layer_prev->network, layer_prev);
		}
	}

	// Check previous layers between each other
	// Note : Data width, signedness and parallelism can be different
	layer_t* layer_p0 = arr_layers[0];
	unsigned ncyclesw = layer_p0->out_fz / layer_p0->split_out;
	unsigned nzinsize = layer_p0->out_fsize / layer_p0->out_fz;
	for(unsigned i=1; i<arr_layers.size(); i++) {
		layer_t* layer_prev = arr_layers[i];
		if(layer_prev->out_fx != layer_p0->out_fx) {
			printf("Error %s%u: Inconsistency on FX between previous layers %u and %u, have %u and %u\n", typenamel, typeidx, 0, i, layer_p0->out_fx, layer_prev->out_fx);
			errors_nb ++;
		}
		if(layer_prev->out_fy != layer_p0->out_fy) {
			printf("Error %s%u: Inconsistency on FY between previous layers %u and %u, have %u and %u\n", typenamel, typeidx, 0, i, layer_p0->out_fy, layer_prev->out_fy);
			errors_nb ++;
		}
		if(layer_prev->out_fz / layer_prev->split_out != ncyclesw) {
			printf("Error %s%u: Inconsistency on FZ/PAR between previous layers %u and %u, corresponds to %u and %u cycles\n", typenamel, typeidx, 0, i, ncyclesw, layer_prev->out_fz / layer_prev->split_out);
			errors_nb ++;
		}
		if(layer_prev->out_fsize / layer_prev->out_fz != nzinsize) {
			printf("Error %s%u: Inconsistency on FSIZE/FZ between previous layers %u and %u, respectively %u and %u\n", typenamel, typeidx, 0, i, ncyclesw, layer_prev->out_fsize / layer_prev->out_fz);
			errors_nb ++;
		}
		if(layer_prev->out_nbframes != layer_p0->out_nbframes) {
			printf("Error %s%u: Inconsistency on nbframes between previous layers %u and %u, respectively %u and %u\n", typenamel, typeidx, 0, i, layer_p0->out_nbframes, layer_prev->out_nbframes);
			errors_nb ++;
		}
	}

	if(errors_nb != 0) {
		printf("Error %s%u: Inconsistencies with previous layers (%u errors)\n", typenamel, typeidx, errors_nb);
		exit(EXIT_FAILURE);
	}

	// Variable to ease code refactoring
	Layer* layer = this;

	// Get input image size from first prev layer, arbitrarily
	layer->fx       = layer_p0->out_fx;
	layer->fy       = layer_p0->out_fy;
	layer->fz       = layer_p0->out_fz;  // This is set correctly just after
	layer->nbframes = layer_p0->out_nbframes;

	// Compute total parallelism, data width and signedness
	split_out = 0;
	out_wdata = 0;
	out_sdata = false;
	for(auto layer_prev : layer->arr_layers) {
		// Parallelism
		split_out += layer_prev->split_out;
		// Data width and signedness
		if(layer_prev->out_sdata == true && out_sdata == false) { out_wdata++; out_sdata = true; }
		unsigned need_wdata = layer_prev->out_wdata + ((layer_prev->out_sdata == false && out_sdata == true) ? 1 : 0);
		out_wdata = GetMax(out_wdata, need_wdata);
	}

	// Set output parameters
	layer->out_fx       = layer->fx;
	layer->out_fy       = layer->fy;
	layer->out_fz       = ncyclesw * split_out;
	layer->out_fsize    = nzinsize * out_fz;
	layer->out_nbframes = layer_p0->out_nbframes;
	layer->out_cycles   = layer->out_fsize / layer->split_out * layer->out_nbframes;

	// Set input parameters for consistency purposes
	layer->split_in = split_out;
	layer->wdata    = out_wdata;
	layer->sdata    = out_sdata;
	layer->fsize    = out_fsize;
	layer->nbframes = out_nbframes;
	layer->cycles   = out_cycles;

	return 0;
}

int LayerScatter::propag_params_forward(void) {

	// Ensure that out_fsize of predecessors is always lower than local out_fsize
	unsigned errors_nb = 0;
	for(auto layer_next : arr_layers) {
		if(layer_next->fsize > fsize) {
			printf("Error %s%u: Next layer %s%u has FSIZE=%u, this must not be larger then local FSIZE=%u\n",
				typenamel, typeidx, layer_next->typenamel, layer_next->typeidx, layer_next->fsize, fsize
			);
			errors_nb ++;
		}
	}
	if(errors_nb != 0) {
		exit(EXIT_FAILURE);
	}

	// Data is unchanged
	out_wdata = wdata;
	out_sdata = sdata;

	// Parallelism is unchanged
	if(prev != nullptr) split_in = prev->split_out;
	split_out = split_in;

	// Set clock cycles, for completeness
	cycles     = fsize / split_in * nbframes;
	out_cycles = cycles;

	return 0;
}

int LayerGather::propag_params_forward(void) {

	if(arr_layers.size() == 0) {
		printf("Error: layer %s%u: No predecessor layers\n", typenamel, typeidx);
		exit(EXIT_FAILURE);
	}

	// Ensure that out_fsize of predecessors is always lower than local out_fsize
	unsigned errors_nb = 0;
	for(auto layer_prev : arr_layers) {
		if(layer_prev->out_fsize > out_fsize) {
			printf("Error %s%u: Previous layer %s%u has FSIZE=%u, this must not be larger then local FSIZE=%u\n",
				typenamel, typeidx, layer_prev->typenamel, layer_prev->typeidx, layer_prev->out_fsize, out_fsize
			);
			errors_nb ++;
		}
	}
	if(errors_nb != 0) {
		exit(EXIT_FAILURE);
	}

	// Output data is extended to fit all predecessors
	out_wdata = 0;
	out_sdata = false;
	for(auto layer_prev : arr_layers) {
		if(layer_prev->out_sdata == true && out_sdata == false) { out_wdata++; out_sdata = true; }
		unsigned need_wdata = layer_prev->out_wdata + ((layer_prev->out_sdata == false && out_sdata == true) ? 1 : 0);
		out_wdata = GetMax(out_wdata, need_wdata);
	}
	// Apply to input side just for completeness
	wdata = out_wdata;
	sdata = out_sdata;

	// Parallelism is unchanged
	split_in  = arr_layers[0]->split_out;
	split_out = split_in;

	// Set clock cycles, for completeness
	cycles     = fsize / split_in * nbframes;
	out_cycles = cycles;

	return 0;
}

int LayerFlatten::propag_params_forward(void) {
	// Variable to ease code refactoring
	Layer* layer = this;

	layer->out_wdata = layer->wdata;
	layer->out_sdata = layer->sdata;

	// Fix output frame size
	layer->out_fsize = layer->fsize * layer->nbframes;
	layer->out_nbframes = 1;
	layer->out_fx = 1;
	layer->out_fy = 1;
	layer->out_fz = out_fsize;

	// Set parallelism levels
	if(layer->prev!=NULL) layer->split_in = layer->prev->split_out;
	layer->split_out = layer->split_in;
	// Set clock cycles, for completeness
	layer->cycles = layer->fsize / layer->split_in * layer->nbframes;
	layer->out_cycles = layer->cycles;

	return 0;
}

int LayerSoftMax::propag_params_forward(void) {

	// There is no applicable fsize_max
	fsize_max = fsize;

	// Set output data width
	out_wdata = uint_bitsnb(fsize_max-1);
	out_sdata = false;
	// Apply any user-specified output data width
	if(user_wout > 0) out_wdata = user_wout;

	// Set output frame size
	out_fsize = 1;
	out_nbframes = nbframes;
	out_fx = 1;
	out_fy = 1;
	out_fz = 1;

	// Set output parallelism level
	split_out = 1;

	// Set clock cycles, for completeness
	cycles = fsize / split_in;
	out_cycles = 1;

	return 0;
}

int LayerFifo::propag_params_forward(void) {
	// Variable to ease code refactoring
	Layer* layer = this;

	layer->out_wdata = layer->wdata;
	layer->out_sdata = layer->sdata;
	// Set parallelism levels
	if(layer->prev!=NULL) layer->split_in = layer->prev->split_out;
	layer->split_out = layer->split_in;
	// Set clock cycles, for completeness
	layer->cycles = layer->fsize / layer->split_in * layer->nbframes;
	layer->out_cycles = layer->cycles;

	return 0;
}

// Propagate parameters from previous layer
// FIXME This should return something to indicate that an update was actually done, in order to stop propagation through layers
// FIXME This should not modify the next layer split_in
static int propag_params_layer_internal(Network* network, layer_t* layer) {

	// Get the previous and next layers, if any
	layer_t* layer_prev = layer->prev;
	layer_t* layer_next = layer->next;

	// Init first layer
	// FIXME Should this be inside layer-specific methods ?
	if(layer_prev==nullptr) {
		if(layer->wdata==0) {
			layer->wdata = network->param_win;
			layer->sdata = network->param_sin;
		}
		layer->nbframes = 1;
		if(layer->split_in==0) layer->split_in = network->param_inpar;
		if(layer->fsize==0) {
			layer->fx = network->param_fx;
			layer->fy = network->param_fy;
			layer->fz = network->param_fz;
			layer->fsize = layer->fx * layer->fy * layer->fz;
		}
	}
	else {
		// Propag input PAR to previous layer
		if(layer->split_in==0) layer->split_in = layer_prev->split_out;
		else propag_backward_par(network, layer);
		// Get input image size from prev layer
		layer->fx       = layer_prev->out_fx;
		layer->fy       = layer_prev->out_fy;
		if(layer->fz == 0 || layer_prev->type != LAYER_SCATTER) layer->fz = layer_prev->out_fz;
		layer->wdata    = layer_prev->out_wdata;
		layer->sdata    = layer_prev->out_sdata;
		if(layer->fsize == 0 || layer_prev->type != LAYER_SCATTER) layer->fsize = layer_prev->out_fsize;
		layer->nbframes = layer_prev->out_nbframes;
		layer->cycles   = layer_prev->out_cycles;
	}

	// Init output dimensions from input dimensions
	// FIXME This overrides user-specified values and hardware properties
	layer->out_fx       = layer->fx;
	layer->out_fy       = layer->fy;
	layer->out_fz       = layer->fz;
	layer->out_wdata    = layer->wdata;
	layer->out_sdata    = layer->sdata;
	layer->out_fsize    = layer->fsize;
	layer->out_nbframes = layer->nbframes;
	if(layer->split_out==0 && layer_next!=NULL) layer->split_out = layer_next->split_in;
	if(layer->split_out==0) layer->split_out = 1;
	// Clear previous number of cycles
	layer->out_cycles   = 0;

	// Call the layer-specific function
	layer->propag_params_forward();

	// Set stuff to next layers
	if(layer->next_is_arr == true) {
		for(auto layer_next : layer->arr_layers) {
			layer_next->split_in = layer->split_out;
		}
	}
	else if(layer_next!=nullptr) layer_next->split_in = layer->split_out;

	return 0;
}

int propag_params_layer(Network* network, layer_t* layer) {
	while(layer != nullptr) {
		propag_params_layer_internal(network, layer);
		layer = layer->next;
	}
	return 0;
}

int Network::propag_params(void) {

	// Clear traversal flags
	for(auto layer : layers) {
		layer->cat_cnt_fwd_propag = 0;
	}

	// Note : can't use type "auto" for this lambda function because it is recursive and needs its return type to be deduced before usage
	std::function<void(layer_t* layer)> propag_params_recurs = [&](layer_t* layer) -> void {
		while(layer != nullptr) {
			// The layer CAT is inserted only once all its predecessors have been inserted
			if(layer->prev_is_arr == true) {
				layer->cat_cnt_fwd_propag ++;
				if(layer->cat_cnt_fwd_propag < layer->arr_layers.size()) return;
				// Note : No update performed here, it is the call to propag_forward_par() below that does the job
			}
			// Update the local layer
			propag_params_layer_internal(this, layer);
			// Traverse all successors of the layer FORK
			if(layer->next_is_arr == true) {
				for(auto layer_next : layer->arr_layers) {
					propag_params_recurs(layer_next);
				}
			}
			// Continue traversal of the current chain of layers
			layer = layer->next;
		}
	};

	// Propagate through next layers
	propag_params_recurs(layer_first);

	return 0;
}

//============================================
// Manipulate number of output neurons
//============================================

// Set the number of neurons in the last neuron layer (classifier)
// Warning : This function assumes that the HW is not locked
void apply_outneu(Network* network, unsigned nbneu) {
	layer_t* layer_outneu = nullptr;

	for(layer_t* layer = network->layer_last; layer != nullptr; layer = layer->prev) {
		if(layer->type==LAYER_NEU) { layer_outneu = layer; break; }
	}

	// FIXME We should stop with error if no appropriate last neuron layer is found
	if(layer_outneu != nullptr) {
		unsigned errors_nb = 0;
		if(nbneu > layer_outneu->neurons_max) {
			printf("ERROR : layer %s%u : The number of neurons cannot be set to %u because the max is %u\n",
				layer_outneu->typenamel, layer_outneu->typeidx, nbneu, layer_outneu->neurons_max
			);
			errors_nb++;
		}
		if(nbneu % (layer_outneu->split_out * layer_outneu->neu_time_mux) != 0) {
			printf("ERROR : layer %s%u : The number of neurons cannot be set to %u because must be a multiple of PAR_OUT %u and TMUX %u\n",
				layer_outneu->typenamel, layer_outneu->typeidx, nbneu, layer_outneu->split_out, layer_outneu->neu_time_mux
			);
			errors_nb++;
		}
		if(errors_nb > 0) {
			exit(EXIT_FAILURE);
		}
		// Set the number of neurons to use
		layer_outneu->neurons = nbneu;
		// Propagate parameters
		propag_params_layer(network, layer_outneu);
	}
}


//============================================
// Manipulate parallelism
//============================================

unsigned maxcycles_per_layer(Network* network) {
	unsigned maxcy = 0;

	auto& layers = network->layers;
	for(auto layer : layers) {
		if(layer->cycles > maxcy) maxcy = layer->cycles;
		if(layer->out_cycles > maxcy) maxcy = layer->out_cycles;
	}

	return maxcy;
}

// FIXME This should be extensible, be member function of Layer* of some sort
// Apply the required parallelism to reach the desired number of clock cycles
static unsigned apply_parallelism_maxcy(Network* network, unsigned cycles_target) {
	unsigned maxcy = 0;
	unsigned changes_nb = 0;

	auto& layers = network->layers;
	for(auto layer : layers) {

		// Only target the main layers, other layers will be configured automatically
		if(layer->type == LAYER_WIN || layer->type == LAYER_NEU || layer->type == LAYER_POOL) {
			//printf("DEBUG : layer %s%u : par in %u out %u : cycles in %u out %u\n", layer->typenamel, layer->typeidx, layer->split_in, layer->split_out, layer->cycles, layer->out_cycles);

			if(layer->cycles > cycles_target) {
				unsigned pari = ceil(layer->split_in * ((double)layer->cycles / cycles_target));
				// Neuron layer
				// Note : For conv layers, reaching this is normally already handled in the previous WIN layer

				// FIXME Explicitly skip layers whose PAR is decided previously at WIN layer
				// FIXME For WIN layers followed by POOL, mark them as DWConv, this will reduce the amount of times we have to search for previous/next layers

				if(layer->type == LAYER_NEU) {
					// DWConv : Adjust to a divisor of FZ
					if(layer->win_dwconv == true) {
						for( ; pari <= layer->fz; pari++) if(layer->fz % pari == 0) break;
					}
					// Adjust to a divisor of the frame size
					else {
						for( ; pari <= layer->fsize_max; pari++) if(layer->fsize_max % pari == 0) break;
					}
				}
				// Adjust to a divisor of FZ
				// Note : reaching this is normally already handled in the previous WIN layer
				if(layer->type == LAYER_POOL) {
					for( ; pari <= layer->fz_max; pari++) if(layer->fz_max % pari == 0) break;
				}
				// Adjust to a divisor of the frame size on Z
				if(layer->type == LAYER_WIN) {
					// Note : Rounding to legal values is done inside propag_forward function
				}
				if(layer->split_in != pari) {
					unsigned prev_pi = layer->split_in;
					layer->split_in = pari;
					propag_backward_par(network, layer);
					printf("Note : layer %s%u : Increasing PAR_IN %u -> %u (requested is %u)\n", layer->typenamel, layer->typeidx, prev_pi, layer->split_in, pari);
					if(layer->split_in != pari) {
						// We can still continue if the PAR value is at least legal
						// FIXME This check does not apply to DWConv layers
						if(layer->type == LAYER_NEU && layer->win_dwconv == false) {
							if(layer->fsize_max % layer->split_in != 0) {
								printf("Error : layer %s%u : The parameter PAR_IN could not be computed automatically\n", layer->typenamel, layer->typeidx);
								exit(EXIT_FAILURE);
							}
						}
					}
					// Print saturation
					if(layer->split_in < pari) {
						printf("Warning : layer %s%u : Saturating PAR_IN to %u\n", layer->typenamel, layer->typeidx, layer->split_in);
					}
				}
			}

			if(layer->out_cycles > cycles_target) {
				// Adjust to a multiple of the number of neurons
				unsigned paro = ceil(layer->split_out * ((double)layer->out_cycles / cycles_target));
				if(layer->type == LAYER_NEU) {
					if(layer->win_dwconv == true) {
						if(paro <= layer->win_par_oz) {
							for( ; paro <= layer->win_par_oz; paro++) if(layer->win_par_oz % paro == 0) break;
						}
						else {
							paro = layer->win_par_oz;
							printf("Warning : layer %s%u : Saturating PAR_OUT to %u\n", layer->typenamel, layer->typeidx, paro);
						}
					}
					else {
						if(paro <= layer->neurons_max) {
							for( ; paro <= layer->neurons_max; paro++) if(layer->neurons_max % paro == 0) break;
						}
						else {
							paro = layer->neurons_max;
							printf("Warning : layer %s%u : Saturating PAR_OUT to %u\n", layer->typenamel, layer->typeidx, paro);
						}
					}
				}
				if(layer->type == LAYER_POOL) {
					// FIXME Actually need to compare gains win_par_oz, or the number of pooling units (normally similar contents)
					if(paro <= layer->split_in) {
						for( ; paro <= layer->split_in; paro++) if(layer->split_in % paro == 0) break;
					}
					else {
						paro = layer->split_in;
						printf("Warning : layer %s%u : Saturating PAR_OUT to %u\n", layer->typenamel, layer->typeidx, paro);
					}
				}
				// Sliding window layer
				if(layer->type == LAYER_WIN) {
					// Note : Rounding to legal values is done inside propag_forward function
				}
				unsigned prev_po = layer->split_out;
				// Apply
				layer->split_out = paro;
				propag_forward_par(network, layer);
				// Print saturation
				printf("Note : layer %s%u : Increasing PAR_OUT %u -> %u (requested is %u)\n", layer->typenamel, layer->typeidx, prev_po, layer->split_out, paro);
				if(layer->split_out < paro) {
					printf("Warning : layer %s%u : Saturating PAR_OUT to %u\n", layer->typenamel, layer->typeidx, layer->split_out);
				}
				// Count changes
				changes_nb++;
			}

		}

		// Get the new max number of cycles
		if(layer->cycles > maxcy) maxcy = layer->cycles;
		if(layer->out_cycles > maxcy) maxcy = layer->out_cycles;
	}

	// Adjust the parallelism of the input of the neural network
	if(network->param_inpar < network->layer_first->split_in) {
		network->param_inpar = network->layer_first->split_in;
	}

	// Perform a global network update to ensure parallel branches have consistent Fork/Cat
	if(changes_nb > 0) {
		network->propag_params();
	}

	return maxcy;
}

// Apply the required parallelism to reach the desired number of clock cycles
// If the target is zero, it means maximum multiplexing desired
static unsigned apply_time_mux_maxcy(Network* network, unsigned cycles_max) {

	auto& layers = network->layers;
	for(auto layer : layers) {
		// Focus on neuron layers
		if(layer->type != LAYER_NEU) continue;

		// Previous layer must be a window layer, style NORMAL
		layer_t* layer_prev = layer->prev;
		if(layer_prev != NULL && layer_prev->type == LAYER_FLATTEN) layer_prev = layer_prev->prev;
		if(layer_prev == NULL) continue;
		if(layer_prev->type != LAYER_WIN) continue;

		// Already-set parallelism or time multiplexing is not handled
		// FIXME tmux is not incompatible with input parallelism, actually can be a very efficient because a small mul-add tree is more efficient than having more physical neurons
		if(layer->neu_time_mux > 1 || layer_prev->win_repeat > 1) continue;
		//if(layer->split_in > 1 || layer->split_out > 1) continue;
		// FIXME DWConv is not supported (yet)
		if(layer->win_dwconv == true) continue;

		// Check the multiplexing level
		// It must be a divisor of the number of physical neurons
		unsigned tmux = 0;
		if(cycles_max == 0) tmux = layer->neurons_max;
		else tmux = cycles_max / layer->cycles;
		// Find an appropriate divisor of the number of physical neurons
		// FIXME Finding an exact divisor is not actually needed, we can round the number of phy neurons to next upper number needed
		if(tmux > layer->neurons_max) tmux = layer->neurons_max;
		else {
			for( ; tmux > 1; tmux--) {
				if(layer->neurons_max % tmux == 0) break;
			}
		}
		if(tmux <= 1) continue;
		printf("Note : layer %s%u : cycles_in %u, multiplexing factor %u\n", layer->typenamel, layer->typeidx, layer->cycles, tmux);

		// Apply
		layer_prev->win_repeat = tmux;
		layer->neu_time_mux = tmux;

		// Note : Propagation of parameters should be only local to the present series of layers
		// So it should be OK to launch it from here
		propag_forward_par(network, layer_prev);
	}

	return 0;
}

void apply_parallelism(Network* network, unsigned par) {
	unsigned cycles = maxcycles_per_layer(network);
	unsigned cycles_target = cycles / par;
	if(cycles_target < 1) cycles_target = 1;

	printf("Parallelism: max %u cycles/layer, want par %u -> expect max %u cycles/layer\n", cycles, par, cycles_target);

	unsigned cycles_prev = cycles;
	cycles = apply_parallelism_maxcy(network, cycles_target);

	printf("Parallelism: got max %u cycles/layer, speedup %f\n", cycles, (double)cycles_prev/cycles);
}

void apply_time_mux(Network* network) {
	unsigned cycles = maxcycles_per_layer(network);

	printf("Time multiplexing: max %u cycles/layer\n", cycles);

	apply_time_mux_maxcy(network, cycles);

	printf("Time multiplexing done\n");
}

void apply_time_mux_max(Network* network) {
	unsigned cycles = maxcycles_per_layer(network);

	printf("Time multiplexing: max %u cycles/layer\n", cycles);

	apply_time_mux_maxcy(network, 0);

	printf("Time multiplexing done\n");
}

void apply_parin_with_time_mux(Network* network) {
	//unsigned cycles = maxcycles_per_layer(network);

	auto& layers = network->layers;
	for(auto layer : layers) {
		// Focus on neuron layers
		// FIXME Pooling layers should also benefit from this feature
		if(layer->type != LAYER_NEU) continue;

		// The goal is to reduce the number of physical neurons in favor of more PAR_IN per neuron
		// The parameter PAR_OUT is kept unchanged

		// Take care that excessively high parallelism on window output will result in duplication of the underlying memory units,
		//   potentially making the overall design even larger
		// So, only consider PAR_OUT of window at most equal to FZ, in order to configure PAR_OZ = PAR_OUT
		// FIXME This may deserve a more thorough analysis and may depend on the memory size and implem, and available mem resources

		// FIXME DWConv is not supported (yet)
		if(layer->win_dwconv == true) continue;

		// Previous layer must be a window layer, style NORMAL
		layer_t* layer_prev = layer->prev;
		if(layer_prev != NULL && layer_prev->type == LAYER_FLATTEN) layer_prev = layer_prev->prev;
		if(layer_prev == NULL) continue;
		if(layer_prev->type != LAYER_WIN) continue;

		// Skip window layers that already have max output parallelism
		if(layer_prev->split_out >= layer_prev->out_fz) continue;
		if(layer_prev->win_par_oz >= layer_prev->out_fz) continue;
		// Skip neuron layers that already have max input parallelism (paranoia)
		if(layer->split_in >= layer->fsize) continue;
		// Skip neuron layers that already have 1 physical neuron per output scan chain
		unsigned phy_neu_per_po = layer->neurons / layer->split_out / layer->neu_time_mux;
		if(phy_neu_per_po <= 1) continue;

		// Find the lowest PAR value (PAR_OUT of window, PAR_IN of neurons) such that :
		//   PAR <= FZ of window
		//   PAR_IN for window divides PAR
		//   the resulting REPEAT in window results in overall cycles not exceed the max of the design
		//   the resulting TMUX in neurons enables to reduce the number of physical neurons per PAR_OUT to a minimum, ideally just 1

		// SUBOPTIMAL : This version just multiplies TMUX and PAR_IN with an integer factor instead of re-calculating them from scratch

		// Description :
		// Layer neurons, conv or not, but not DWConv
		//   skip layers with already max PAR_IN -> skip if PAR_IN >= fsize
		//   skip layers with already 1 physical neuron per PAR_OUT -> skip if neurons / PAR_OUT / tmux <= 1
		// In that case :
		//   Ensure neurons / PAR_OUT / tmux is an integer -> if not the case, emit warning and skip
		//   The max possible parallelism to add is : fsize / PAR_IN
		//   The additional PAR_IN we would like to add is : neurons / PAR_OUT / tmux
		//   Take the largest common divisor, D
		//   Do tmux *= D
		//   Do PAR_IN *= D

		unsigned factor_max_neu = layer->neurons / layer->split_out / layer->neu_time_mux;
		unsigned factor_max_win = layer_prev->fz / layer_prev->split_out;
		// Find the greatest divisor of both factors
		// FIXME This algorithm is not the best, but it is not supposed to be critical
		unsigned factor_want = GetMin(factor_max_neu, factor_max_win);
		for( ; factor_want > 1; factor_want--) {
			if(factor_max_neu % factor_want != 0) continue;
			if(factor_max_win % factor_want != 0) continue;
			break;
		}
		if(factor_want <= 1) continue;

		unsigned save_par_in = layer->split_in;
		unsigned save_tmux = layer->neu_time_mux;

		// First, apply parallelism at output of window
		layer_prev->win_repeat *= factor_want;
		layer_prev->split_out *= factor_want;
		layer_prev->win_par_oz = layer_prev->split_out;
		// Then, apply parallelism in neuron layer
		layer->neu_time_mux *= factor_want;
		layer->split_in     *= factor_want;

		printf("Note : layer %s%u : Increasing PAR_IN %u -> %u and TMUX %u -> %u to reduce the number of physical neurons\n",
			layer->typenamel, layer->typeidx, save_par_in, layer->split_in, save_tmux, layer->neu_time_mux
		);

		// Note : Propagation of parameters should be only local to the present series of layers
		// So it should be OK to launch it from here
		// FIXME There is probably exactly nothing to update actually
		propag_forward_par(network, layer_prev);
	}

}


//============================================
// Utility functions for hardware accelerator
//============================================

// Check if a layer can control/interrupt the flow of data
// Goal : skip creation of FIFOs where they would not be appropriate
// Example : Some layers don't require a FIFO : ReLU, BN, ADD, maybe POOL if clock cycles is small
//   For these, FIFOs are optional
// Example : Some layers don't support a FIFO : ADD
//   For these, FIFOs may make the network non-functional
bool layer_does_not_control_flow(Layer* layer) {
	char const* out_room_port = nullptr;
	layer->genvhdl_comp_get_out_room_port(out_room_port);
	if(out_room_port == nullptr) return true;
	return false;
}
static bool layer_is_empty(Layer* layer) {
	if(layer->type == LAYER_FLATTEN) return true;
	return false;
}

// FIXME This should be extensible, be member function of Layer* of some sort
// FIXME Add a virtual method in class Layer to return whether or not to insert a FIFO after
// FIXME Some layers will REQUIRE a FIFO on input side, such as Win / Neu / Pool / etc
//   But, in some cases these could be bypassed entirely if the processing time is bounded
//   Suggestion : for each layer after a FIFO, count the latency of all layers in series that are not separated by another FIFO
//   And check if the summed latency exceeds the FIFO depth, and if there are missing FIFOs due to inappropriate configuration or series of layers
// FIXME Some layers don't require a FIFO at all : ReLU, BN, ADD, maybe NEU and POOL if clock cycles is small
int Network::insert_fifos(void) {

	unsigned fifos_nb = 0;

	// Ensure the network begins with a FIFO
	if(layer_first != nullptr && layer_first->type != LAYER_FIFO) {
		layer_t* layer_fifo = layer_new_fromtype(LAYER_FIFO);
		layer_insert(layer_fifo, nullptr, layer_first);
		layer_first = layer_fifo;
		// Note : Important parameters should be obtained from global variables : data width, signedness, parallelism
		propag_params_layer_internal(this, layer_fifo);
		fifos_nb++;
	}

	// New layers will be appended to the array
	// So scan with indexes instead of iterators
	unsigned prev_num_layers = layers.size();
	for(unsigned i=0; i<prev_num_layers; i++) {
		Layer* layer = layers[i];
		// Some layers types don't need a FIFO after
		if(layer->type == LAYER_FIFO) continue;
		if(layer->type == LAYER_FLATTEN) continue;

		// Layers FORK, SCATTER need special scan
		if(layer->next_is_arr == true) {
			for(auto layer_next : layer->arr_layers) {
				if(layer_next->type == LAYER_FIFO) continue;
				// Insert the FIFO
				layer_t* layer_fifo = layer_new_fromtype(LAYER_FIFO);
				layer_fifo->fsize = layer_next->fsize;  // Cover the case where next layer does not have same fsize (for example if current layer is SCATTER)
				layer_insert_replace(layer_fifo, layer, layer_next);
				propag_params_layer_internal(this, layer_fifo);
				fifos_nb++;
			}
			continue;
		}

		// Layer non-FORK
		layer_t* layer_next = layer->next;
		while(layer_next != nullptr && layer_is_empty(layer_next)) layer_next = layer_next->next;
		if(layer_next == NULL) continue;
		// Skip creation of FIFO in some specific conditions
		if(nofifo_win_neu_th>0 && layer->type == LAYER_WIN && layer_next->type == LAYER_NEU) {
			if(layer_next->out_cycles / layer_next->out_nbframes <= nofifo_win_neu_th) continue;
		}
		if(nofifo_win_pool==true && layer->type == LAYER_WIN && layer_next->type == LAYER_POOL) continue;
		if(nofifo_neu_relu==true && nofifo_neu_leaky==true && layer->type == LAYER_NEU && (layer_next->type == LAYER_TER || layer_next->type == LAYER_RELU || layer_next->type == LAYER_LEAKY)) continue;
		if(nofifo_norm_relu==true && nofifo_norm_leaky==true && layer->type == LAYER_NORM && layer_next->type == LAYER_RELU && layer_next->type == LAYER_LEAKY) continue;
		if(layer_next->type == LAYER_FIFO) continue;

		// Insert the FIFO
		layer_t* layer_fifo = layer_new_fromtype(LAYER_FIFO);
		layer_insert_replace(layer_fifo, layer, layer->next);
		propag_params_layer_internal(this, layer_fifo);
		fifos_nb++;
	}

	// Ensure the network ends with a FIFO
	if(layer_last != nullptr && layer_last->type != LAYER_FIFO) {
		layer_t* layer_fifo = layer_new_fromtype(LAYER_FIFO);
		layer_insert(layer_fifo, layer_last, nullptr);
		layer_last = layer_fifo;
		propag_params_layer_internal(this, layer_fifo);
		fifos_nb++;
	}

	// Set the FIFO margin depending on the number of next layers that have no FIFO after them
	for(auto layer : layers) {
		layer->out_extra_fifo_room = 0;
		// Only apply to pertinent layer types
		// FIXME This deserves a field or method in Layer class
		if(layer->type == LAYER_FIFO) continue;
		if(layer_is_empty(layer)) continue;  // Skip layer Flatten
		if(layer->arr_layers.size() > 0) continue;  // Skip layers Fork, Cat, Scatter, Gather
		// Skip layers that have no FIFO before, they are not in control of the data flow
		// FIXME Handling of WIN -> FIFO -> FLATTEN -> NEU will cause issues
		if(layer->prev->type != LAYER_FIFO && layer_is_empty(layer->prev)==false) continue;
		// Scan next layers
		for(layer_t* layer_next = layer->next; layer->next != nullptr; layer_next=layer_next->next) {
			if(layer_next->type == LAYER_FIFO) break;
			// FIXME Layers CAT and GATHER are not neutral on control flow unless it is guaranteed that all branches are synchronized
			if(layer_next->arr_layers.size() > 0) break;  // Stop at layers Fork, Cat, Scatter, Gather
			if(layer_is_empty(layer_next)) continue;
			// FIXME Arbitrary extra room values
			// This is basically the number of cycles required between the update of the fifo_room value and the interruption of the emission of the outputs
			// Also this is rather worst case because it assumes the layer can emit one data per cycle during all this period
			layer->out_extra_fifo_room += 8;
			// Mark that next layer to skip creation of input buffers
			layer_next->flow_skip_inbuf = true;
		}
		// Warn about excessive extra fifo room that could exceed the fifo depth when added to the internal margin taken by the component
		// FIXME Actually need to change the FIFO depth according to the layer latency and the extra fifo room
		if(layer->out_extra_fifo_room > 48) {
			printf("WARNING %s%u : The extra fifo room margin is probably excessive (%u), the design may not be functional\n", layer->typenameu, layer->typeidx, layer->out_extra_fifo_room);
		}
	}

	printf("Fifos inserted : %u\n", fifos_nb);

	layers_reorder();

	return fifos_nb;
}


//============================================
// Integrity of the network
//============================================

void Network::layers_reorder(void) {
	if(layers.size() <= 1) return;

	// Create the reordered list in a different vector
	vector<layer_t*> layers_order;
	layers_order.reserve(layers.size());

	// Clear traversal flags
	for(auto layer : layers) {
		layer->cat_cnt_fwd_propag = 0;
	}

	// Launch reordering

	// Note : can't use type "auto" for this lambda function because it is recursive and needs its return type to be deduced before usage
	std::function<void(layer_t* layer)> copy_recurs = [&](layer_t* layer) -> void {
		while(layer != nullptr) {
			// The layer CAT is inserted only once all its predecessors have been inserted
			if(layer->prev_is_arr == true) {
				layer->cat_cnt_fwd_propag ++;
				if(layer->cat_cnt_fwd_propag < layer->arr_layers.size()) return;
			}
			// Insert the layer
			layers_order.push_back(layer);
			// Insert all successors of the layer FORK
			if(layer->next_is_arr == true) {
				for(auto layer_next : layer->arr_layers) {
					copy_recurs(layer_next);
				}
			}
			layer = layer->next;
		}
	};

	// Launch reordering
	copy_recurs(layer_first);

	// Checks
	if(layers_order.size() != layers.size() || layers_order.back() != layer_last) {
		printf("Error: Post-reordering network is not consistent\n");
		abort();
	}

	// Commit results
	layers.swap(layers_order);

	// Fix indexes of layers
	for(unsigned i=0; i<layers.size(); i++) {
		layers[i]->index = i;
	}
}

int Network::check_integrity(void) {
	unsigned errors_nb = 0;

	if(layers.size() == 0) return 0;

	// Check first and last layer
	if(layer_first != layers.front()) {
		printf("Error: First layer inconsistency\n");
		errors_nb ++;
	}
	if(layer_last != layers.back()) {
		printf("Error: Last layer inconsistency\n");
		errors_nb ++;
	}

	for(auto layer : layers) {

		// Check existence of the prev/next links
		if(layer != layer_first) {
			if((layer->prev_is_arr == false && layer->prev == NULL) || (layer->prev_is_arr == true && layer->arr_layers.size() == 0)) {
				printf("Error: Layer %s%u is not first and has no prev layer\n", layer->typenamel, layer->type);
				errors_nb ++;
			}
		}
		if(layer != layer_last) {
			if((layer->next_is_arr == false && layer->next == NULL) || (layer->next_is_arr == true && layer->arr_layers.size() == 0)) {
				printf("Error: Layer %s%u is not last and has no next layer\n", layer->typenamel, layer->type);
				errors_nb ++;
			}
		}

		// Check order of IDs - FIXME This can't be based on layer pointer
		#if 0
		if(layer->prev != NULL && layer->prev >= layer) {
			printf("Error: Layer %s%u has a prev layer that is after\n", layer->typenamel, layer->type);
			errors_nb ++;
		}
		if(layer->next != NULL && layer->next <= layer) {
			printf("Error: Layer %s%u has a next layer that is before\n", layer->typenamel, layer->type);
			errors_nb ++;
		}
		#endif

		// Check layers FORK / CAT
		if(layer->arr_layers.size() > 0 && layer->prev_is_arr == false && layer->next_is_arr == false) {
			printf("Error: Layer %s%u has a list of layers, but is of an incompatible type\n", layer->typenamel, layer->type);
			errors_nb ++;
		}
		if(layer->next_is_arr == true && layer->next != NULL) {
			printf("Error: Layer %s%u has a next layer \n", layer->typenamel, layer->type);
			errors_nb ++;
		}
		if(layer->prev_is_arr == true && layer->prev != NULL) {
			printf("Error: Layer %s%u has a prev layer\n", layer->typenamel, layer->type);
			errors_nb ++;
		}
		if((layer->prev_is_arr == true || layer->next_is_arr == true) && layer->arr_layers.size() == 0) {
			printf("Error: Layer %s%u has an empty list of linked layers\n", layer->typenamel, layer->type);
			errors_nb ++;
		}

		// Check integrity of prev/next links
		if(layer->prev != NULL) {
			layer_t* layer_prev = layer->prev;
			if(layer_prev->next != NULL && layer_prev->next != layer) {
				printf("Error: Layer %s%u has prev layer %s%u, which has a different next layer\n", layer->typenamel, layer->type, layer_prev->typenamel, layer_prev->type);
				errors_nb ++;
			}
			if(layer_prev->next_is_arr == true) {
				bool found = false; for(auto layer_next : layer_prev->arr_layers) if(layer_next == layer) { found = true; break; }
				if(found == false) {
					printf("Error: Layer %s%u is not in the list of layers of its prev layer %s%u\n", layer->typenamel, layer->type, layer_prev->typenamel, layer_prev->type);
					errors_nb ++;
				}
			}
		}
		if(layer->next != NULL) {
			layer_t* layer_next = layer->next;
			if(layer_next->prev != NULL && layer_next->prev != layer) {
				printf("Error: Layer %s%u has next layer %s%u, which has a different prev layer\n", layer->typenamel, layer->type, layer_next->typenamel, layer_next->type);
				errors_nb ++;
			}
			if(layer_next->prev_is_arr == true) {
				bool found = false; for(auto layer_prev : layer_next->arr_layers) if(layer_prev == layer) { found = true; break; }
				if(found == false) {
					printf("Error: Layer %s%u is not in the list of layers of its next layer %s%u\n", layer->typenamel, layer->type, layer_next->typenamel, layer_next->type);
					errors_nb ++;
				}
			}
		}

		// Some layers need same PAR at input and output
		// FIXME Not good to hardcode layers types here
		if(
			layer->type == LAYER_NORM || layer->type == LAYER_TER || layer->type == LAYER_RELU || layer->type == LAYER_LEAKY || layer->type == LAYER_CUSTOM ||
			layer->type == LAYER_FLATTEN || layer->type == LAYER_FIFO
		) {
			if(layer->split_in != layer->split_out) {
				printf("Error layer %s%u : Forbidden PAR_IN=%u PAR_OUT=%u\n", layer->typenamel, layer->typeidx, layer->split_in, layer->split_out);
				errors_nb ++;
			}
		}

		// Not more than one FIFO in a row
		// FORK can't follow FORK
		// CAT can't follow CAT
		// FIXME This is not a good way to check combinations, some of these may even not be forbidden after all
		if(
			(layer->type == LAYER_FIFO && layer->next != NULL && layer->next->type == LAYER_FIFO) ||
			(layer->type == LAYER_FORK && layer->next != NULL && layer->next->type == LAYER_FORK) ||
			(layer->type == LAYER_CAT && layer->next != NULL && layer->next->type == LAYER_CAT) ||
			(layer->type == LAYER_SCATTER && layer->next != NULL && layer->next->type == LAYER_SCATTER) ||
			(layer->type == LAYER_GATHER && layer->next != NULL && layer->next->type == LAYER_GATHER)
		) {
			printf("Error: Forbidden sequence of layers %s%u -> %s%u\n", layer->typenamel, layer->type, layer->next->typenamel, layer->next->type);
			errors_nb ++;
		}

	}  // Scan layers

	// The program can't continue if there were integrity issues
	if(errors_nb > 0) {
		printf("Error: %u errors found, exiting\n", errors_nb);
		exit(EXIT_FAILURE);
	}

	return errors_nb;
}


//============================================
// Computation of latency
//============================================

void Layer::eval_latency(Latency& lat) const {
	lat.nbin_before_begin   = 0;
	lat.cycles_to_first_out = 0;
	lat.cycles_to_last_out  = 0;
}

void LayerWin::eval_latency(Latency& lat) const {
	unsigned latency = 4;
	// The number of clock cycles to fill WINY planes XZ
	lat.nbin_before_begin   = (fz / split_in) * fx * winy;
	// Arbitrary number of clock cycles to initiate read, actual read, and handling of padding
	lat.cycles_to_first_out = latency;
	// Number of outputs for one series of windows
	lat.cycles_to_last_out  = latency + winx * winy * nwinx * fz / split_out * win_repeat;
}
void LayerNeu::eval_latency(Latency& lat) const {
	// latency of reading from memory (arbitrary here)
	unsigned latency = 2;
	// Latency of multipliers, in case these are separate from the ALUs of the accumulators
	latency ++;
	// Latency of adder trees (arbitrary here)
	// FIXME Need proper handling for dwconv for the depth of the tree
	if(split_in > 1) latency += ceil(log2(split_in-1));
	// Accumulator + scan chain
	latency += 2;

	lat.nbin_before_begin   = fsize / split_in;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency + out_fsize / split_out / neu_time_mux;
}
void LayerNeu_CM::eval_latency(Latency& lat) const {
	// latency of reading from memory (arbitrary here)
	unsigned latency = 2;
	// Latency of multipliers, in case these are separate from the ALUs of the accumulators
	latency ++;
	// Latency of adder trees (arbitrary here)
	// FIXME Need proper handling for dwconv for the depth of the tree
	if(split_in > 1) latency += ceil(log2(split_in-1));
	// Accumulator + scan chain
	latency += 2;

	lat.nbin_before_begin   = fsize / split_in;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency + out_fsize / split_out / neu_time_mux;
}
void LayerPool::eval_latency(Latency& lat) const {
	unsigned latency = 1;
	unsigned pool_par_in = split_in / pool_units_nb;
	if(pool_par_in > 1) latency += ceil(log2(pool_par_in-1));
	// Accumulator + scan chain, if these are present
	if(fsize == pool_par_in && split_out == pool_units_nb) {} else latency += 2;
	// Multiplication, for AvgPool
	if(pool_type == POOL_TYPE_AVG && uint_ispow2(pool_avg_mult)==false) latency += 1;

	lat.nbin_before_begin   = fsize / split_in;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency + (pool_units_nb / split_out - 1);
}

void LayerNorm::eval_latency(Latency& lat) const {
	unsigned latency = 2;
	latency += (this->norm_wbias > 0);
	latency += (this->norm_wmul > 0);
	latency += (this->norm_mul_cst > 0);
	latency += (this->norm_wshr > 0);
	lat.nbin_before_begin   = 0;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency;
}
void LayerTernarize::eval_latency(Latency& lat) const {
	unsigned latency = 4;
	lat.nbin_before_begin   = 0;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency;
}
void LayerRelu::eval_latency(Latency& lat) const {
	unsigned latency = 2;
	lat.nbin_before_begin   = 0;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency;
}
void LayerLeaky::eval_latency(Latency& lat) const {
	unsigned latency = 2;
	lat.nbin_before_begin   = 0;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency;
}
void LayerAdd::eval_latency(Latency& lat) const {
	unsigned latency = 1;
	unsigned par_add_in = split_in / split_out;
	if(par_add_in > 1) latency += ceil(log2(par_add_in-1));
	lat.nbin_before_begin   = 0;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency;
}
void LayerCustom::eval_latency(Latency& lat) const {
	unsigned latency = 1;
	if(custom_latency > 1) latency = custom_latency;
	lat.nbin_before_begin   = 0;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency;
}

void LayerSoftMax::eval_latency(Latency& lat) const {
	unsigned latency = 1;
	if(split_in > 1) latency += ceil(log2(split_in-1));  // Max tree
	if(split_in < fsize) latency += 2;  // Accumulator if present
	lat.nbin_before_begin   = fsize / split_in;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency;
}
void LayerFifo::eval_latency(Latency& lat) const {
	// Write into memory, counter propagation, read from memory
	unsigned latency = 3;
	lat.nbin_before_begin   = 0;
	lat.cycles_to_first_out = latency;
	lat.cycles_to_last_out  = latency;
}

// What is latency here :
// The number of clock cycles between the first input and the last output of the network
unsigned long Network::eval_latency(void) const {
	if(layers.size() <= 1) return 0;

	// Vectors of latency stats, per-layer
	vector<Latency> latency_layer(layers.size());
	// Latency results, after each layer
	vector<pair<unsigned long, unsigned long> > latency_cumul(layers.size());

	// Eval latency of all layers
	for(auto layer : layers) {
		layer->eval_latency(latency_layer[layer->index]);
	}

	// Clear traversal flags
	for(auto layer : layers) {
		layer->cat_cnt_fwd_propag = 0;
	}

	// Note : can't use type "auto" for this lambda function because it is recursive and needs its return type to be deduced before usage
	std::function<void(layer_t* layer)> eval_latency_recurs = [&](layer_t* layer) -> void {
		unsigned long lat_first_in = 0;
		unsigned long lat_last_in = 0;
		while(layer != nullptr) {
			// The layer CAT is inserted only once all its predecessors have been inserted
			if(layer->prev_is_arr == true) {
				layer->cat_cnt_fwd_propag ++;
				if(layer->cat_cnt_fwd_propag < layer->arr_layers.size()) return;
				// Get the max latency of all predecessors
				for(auto layer_prev : layer->arr_layers) {
					auto cumul = latency_cumul[layer_prev->index];
					lat_first_in = std::max(lat_first_in, cumul.first);
					lat_last_in  = std::max(lat_last_in, cumul.second);
				}
			}
			else if(layer->prev != nullptr) {
				auto cumul = latency_cumul[layer->prev->index];
				lat_first_in = cumul.first;
				lat_last_in  = cumul.second;
			}
			else {
				// First layer : input frame
				lat_first_in = 0;
				lat_last_in = layer->cycles;
			}

			// FIXME Assuming the nbin_before_begin inputs are obtained full speed
			unsigned long lat_first_out = lat_first_in + latency_layer[layer->index].nbin_before_begin + latency_layer[layer->index].cycles_to_first_out;

			// Eval latency to last output
			unsigned long lat_last_out = lat_last_in + latency_layer[layer->index].cycles_to_last_out;
			// Also get the largest with first output + cycles_out
			lat_last_out = std::max(lat_last_out, lat_first_in + layer->out_cycles);
			lat_last_out = std::max(lat_last_out, lat_first_in + layer->out_cycles_real);

			// FIXME No model for whether the layer accepts new inputs before the last output is emitted
			//   (layer WIN may be blocking if BUFY is too low, layers NEU and POOL are not blocking, etc)

			// Save the latency results for the current layer
			latency_cumul[layer->index] = make_pair(lat_first_out, lat_last_out);

			//printf("DEBUG LATENCY %s%u : first %lu last %lu clock cycles\n", layer->typenamel, layer->typeidx, lat_first_out, lat_last_out);

			// Traverse all successors of the layer FORK
			if(layer->next_is_arr == true) {
				for(auto layer_next : layer->arr_layers) {
					eval_latency_recurs(layer_next);
				}
			}
			// Continue traversal of the current chain of layers
			layer = layer->next;
		}
	};

	// Launch traversal
	eval_latency_recurs(layer_first);

	// The overall network latency is the date of the last output of the last layer
	return latency_cumul[layer_last->index].second;
}


//============================================
// Print network
//============================================

void Layer::print_extra_details(void) { }

void LayerWin::print_extra_details(void) {
	printf("win %ux%u nwin %u %u %u step %u %u pad %u %u fsize %u %u nbframes %u %u",
		winx, winy,
		nwinx, nwiny, nwinz,
		stepx, stepy,
		begpadx, begpady,
		fsize, out_fsize,
		nbframes, out_nbframes
	);
	if(win_par_oz > 1) {
		printf(" par_oz %u", win_par_oz);
	}
	if(win_dwconv == true) {
		printf(" dwconv 1");
	}
	if(win_repeat > 1) {
		printf(" repeat %u", win_repeat);
	}
}
void LayerNeu::print_extra_details(void) {
	printf("style %u", neu_style);
	printf(" sgnd:");
	if((neu_sgnd & NEUSGN_UNSIGNED) != 0) printf("u");
	if((neu_sgnd & NEUSGN_SIGNED) != 0) printf("s");
	if((neu_sgnd & NEUSGN_LOCKED) != 0) printf("l");
	if((neu_sgnd & NEUSGN_VALID) != 0) printf("v");
	printf(" fsize %u/%u neu %u/%u weights %u%c nperblk %u wrnb %u waccu %u",
		fsize, fsize_max,
		neurons, neurons_max,
		neu_wweight, (neu_sgnw & NEUSGN_SIGNED) ? 's' : 'u',
		neu_per_bram, neu_wrnb,
		neu_waccu
	);
	if(neu_style==2 && neu_comp_style != 0 && neu_comp_nraw >= 1) {
		printf(" compress %u %uw %ub", neu_comp_style, neu_comp_nraw, neu_comp_nbin);
	}
	if(neu_custom_mul != 0) {
		printf(" mul id%u %u%c",
			neu_custom_mul_id,
			neu_custom_wmul, (neu_custom_smul != 0) ? 's' : 'u'
		);
	}
	if(win_dwconv == true) {
		printf(" dwconv");
	}
	if(neu_time_mux > 1) {
		printf(" tmux %u", neu_time_mux);
	}
}
void LayerNeu_CM::print_extra_details(void) {
	printf("style %u", neu_style);
	printf(" sgnd:");
	if((neu_sgnd & NEUSGN_UNSIGNED) != 0) printf("u");
	if((neu_sgnd & NEUSGN_SIGNED) != 0) printf("s");
	if((neu_sgnd & NEUSGN_LOCKED) != 0) printf("l");
	if((neu_sgnd & NEUSGN_VALID) != 0) printf("v");
	printf(" fsize %u/%u neu %u/%u weights %u%c nperblk %u wrnb %u waccu %u",
		fsize, fsize_max,
		neurons, neurons_max,
		neu_wweight, (neu_sgnw & NEUSGN_SIGNED) ? 's' : 'u',
		neu_per_bram, neu_wrnb,
		neu_waccu
	);
	if(neu_style==2 && neu_comp_style != 0 && neu_comp_nraw >= 1) {
		printf(" compress %u %uw %ub", neu_comp_style, neu_comp_nraw, neu_comp_nbin);
	}
	if(neu_custom_mul != 0) {
		printf(" mul id%u %u%c",
			neu_custom_mul_id,
			neu_custom_wmul, (neu_custom_smul != 0) ? 's' : 'u'
		);
	}
	if(win_dwconv == true) {
		printf(" dwconv");
	}
	if(neu_time_mux > 1) {
		printf(" tmux %u", neu_time_mux);
	}
}
void LayerPool::print_extra_details(void) {
	printf("win %ux%u fsize %u nbframes %u pools=%u oper=", winx, winy, fsize, nbframes, pool_units_nb);
	if     (pool_type == POOL_TYPE_MAX) printf("max");
	else if(pool_type == POOL_TYPE_MAX) printf("min");
	else if(pool_type == POOL_TYPE_AVG) printf("avg");
	else if(pool_type == POOL_TYPE_ADD) printf("add");
	else printf("none");
	if(pool_type == POOL_TYPE_AVG) {
		printf(" mult=%u shr=%u", pool_avg_mult, pool_avg_shr);
		if(round_nearest == true) printf(" rnd=near");
	}
}

void LayerNorm::print_extra_details(void) {
	printf("fsize %u/%u", fsize, fsize_max);
	printf(" mulcst=%u", norm_mul_cst);
	printf(" shrcst=%u", norm_shr_cst);
	printf(" wbias=%u",  norm_wbias);
	printf(" wmul=%u",   norm_wmul);
	printf(" wshr=%u",   norm_wshr);
	if(round_nearest == true) printf(" rnd=near");
}
void LayerTernarize::print_extra_details(void) {
	printf("fsize %u/%u", fsize, fsize_max);
}
void LayerRelu::print_extra_details(void) {
	printf("fsize %u", fsize);
	printf(" min/max %i/%i", relu_min, relu_max);
}
void LayerLeaky::print_extra_details(void) {
	printf("fsize %u", fsize);
	printf(" min/max %i/%i", leaky_min, leaky_max);
}
void LayerCustom::print_extra_details(void) {
	printf("fsize %u", fsize);
	printf(" user id %u entity %s latency %u", custom_user_id, custom_entity, custom_latency);
}

void LayerFork::print_extra_details(void) {
	printf("next layers (%u) :", (unsigned)arr_layers.size());
	for(auto layer_next : arr_layers) {
		printf(" %s%u", layer_next->typenamel, layer_next->typeidx);
	}
}
void LayerCat::print_extra_details(void) {
	printf("prev layers (%u) :", (unsigned)arr_layers.size());
	for(auto layer_prev : arr_layers) {
		printf(" %s%u", layer_prev->typenamel, layer_prev->typeidx);
	}
}
void LayerScatter::print_extra_details(void) {
	printf("next layers (%u) :", (unsigned)arr_layers.size());
	for(auto layer_next : arr_layers) {
		printf(" %s%u (fsize %u)", layer_next->typenamel, layer_next->typeidx, layer_next->fsize);
	}
}
void LayerGather::print_extra_details(void) {
	printf("prev layers (%u) :", (unsigned)arr_layers.size());
	for(auto layer_prev : arr_layers) {
		printf(" %s%u (fsize %u)", layer_prev->typenamel, layer_prev->typeidx, layer_prev->out_fsize);
	}
}

void LayerSoftMax::print_extra_details(void) {
	printf("fsize %u/%u", fsize, fsize_max);
}

// Raw print, one line per layer

int nnprint_oneline_layer(layer_t* layer, const char* indent) {
	if(indent != nullptr) {
		printf("%s", indent);
	}

	printf("id %d %s%u wdata %u%c %u%c par %u %u img %ux%ux%u %ux%ux%u ",
		layer->id, layer->typenameu, layer->typeidx,
		layer->wdata,     layer->sdata     ? 's' : 'u',
		layer->out_wdata, layer->out_sdata ? 's' : 'u',
		layer->split_in, layer->split_out,
		layer->fx, layer->fy, layer->fz,
		layer->out_fx, layer->out_fy, layer->out_fz
	);
	//printf(" idx %u ", layer->regs_idx);

	layer->print_extra_details();
	printf("\n");

	return 0;
}

int nnprint_oneline(const vector<layer_t*>& layers, const char* indent) {

	printf("Overview of the network (%u layers):\n", (unsigned)layers.size());

	for(auto layer : layers) {
		nnprint_oneline_layer(layer, indent);
	}

	return 0;
}

// Pretty-print with a table

class nnprint_table_t {

	public :

	// Width of data in each column
	unsigned print_wlay_hwid = 0;
	unsigned print_wlay_name = 0;
	unsigned print_wlay_win  = 0;
	unsigned print_wlay_wout = 0;
	unsigned print_wlay_pari = 0;
	unsigned print_wlay_paro = 0;
	unsigned print_wlay_imgi = 0;
	unsigned print_wlay_imgo = 0;
	unsigned print_wlay_cyi  = 0;
	unsigned print_wlay_cyo  = 0;

	// Width of columns and double-columns with titles
	unsigned print_wcol_hwid  = 0;
	unsigned print_wcol_name  = 0;
	unsigned print_wcol_wdata = 0;
	unsigned print_wcol_par   = 0;
	unsigned print_wcol_img   = 0;
	unsigned print_wcol_cy    = 0;

	// Extra options
	unsigned options;

	inline unsigned get_opt_table(void)  { return options & NNPRINT_OPT_TABLE; }
	inline unsigned get_opt_cycles(void) { return options & NNPRINT_OPT_CYCLES; }
	inline unsigned get_opt_hwid(void)   { return options & NNPRINT_OPT_HWID; }

	inline unsigned chk_opt_table(void)  { return get_opt_table() != 0; }
	inline unsigned chk_opt_cycles(void) { return get_opt_cycles() != 0; }
	inline unsigned chk_opt_hwid(void)   { return get_opt_hwid() != 0; }

	// Methods
	void init_addlayer(layer_t* layer);
	void init_end(void);
  void print_line(char dash);
	void print_titles(void);
	void print_layer(layer_t* layer);

};

// Add the specified layer to the column width counters
void nnprint_table_t::init_addlayer(layer_t* layer) {
	if(layer->id >= 0) {
		print_wlay_hwid = GetMax(print_wlay_hwid, uint_digitsnb(layer->id));
	}
	print_wlay_name = GetMax(print_wlay_name, strlen(layer->typenamel) + uint_digitsnb(layer->typeidx));
	print_wlay_win  = GetMax(print_wlay_win,  uint_digitsnb(layer->wdata) + 1);
	print_wlay_wout = GetMax(print_wlay_wout, uint_digitsnb(layer->out_wdata) + 1);
	print_wlay_pari = GetMax(print_wlay_pari, uint_digitsnb(layer->split_in));
	print_wlay_paro = GetMax(print_wlay_paro, uint_digitsnb(layer->split_out));
	print_wlay_imgi = GetMax(print_wlay_imgi, uint_digitsnb(layer->fx) + uint_digitsnb(layer->fy) + uint_digitsnb(layer->fz) + 2);
	print_wlay_imgo = GetMax(print_wlay_imgo, uint_digitsnb(layer->out_fx) + uint_digitsnb(layer->out_fy) + uint_digitsnb(layer->out_fz) + 2);
	print_wlay_cyi  = GetMax(print_wlay_cyi,  uint_digitsnb(layer->cycles));
	print_wlay_cyo  = GetMax(print_wlay_cyo,  uint_digitsnb(layer->out_cycles));
}

// Apply max column width for titles
void nnprint_table_t::init_end(void) {
	print_wcol_hwid  = GetMax(print_wlay_hwid, 5);  // HW ID
	print_wcol_name  = GetMax(print_wlay_name, 4);  // Name
	print_wcol_wdata = GetMax(print_wlay_win+1+print_wlay_wout, 8);  // Data i/o
	print_wcol_par   = GetMax(print_wlay_pari+1+print_wlay_paro, 8);  // Par. i/o
	print_wcol_img   = GetMax(print_wlay_imgi+1+print_wlay_imgo, 8);  // Img. i/o
	print_wcol_cy    = GetMax(print_wlay_cyi+1+print_wlay_cyo, 10);  // Cycles i/o
}

// Helper functions to print the table
// FIXME Input image should not be printed for CAT layers

void nnprint_table_t::print_line(char dash) {
	if(chk_opt_hwid()) {
		putchar('+');
		for(unsigned i=0; i<print_wcol_hwid+2; i++) putchar(dash);
	}
	putchar('+');
	for(unsigned i=0; i<print_wcol_name+2; i++) putchar(dash);
	putchar('+');
	for(unsigned i=0; i<print_wcol_wdata+2; i++) putchar(dash);
	putchar('+');
	for(unsigned i=0; i<print_wcol_par+2; i++) putchar(dash);
	putchar('+');
	for(unsigned i=0; i<print_wcol_img+2; i++) putchar(dash);
	if(chk_opt_cycles()) {
		putchar('+');
		for(unsigned i=0; i<print_wcol_cy+2; i++) putchar(dash);
	}
	putchar('+');
	for(unsigned i=0; i<10; i++) putchar(dash);
	putchar('\n');
}

void nnprint_table_t::print_titles(void) {
	unsigned off = 0;
	// Column : HW ID
	if(chk_opt_hwid()) {
		printf("| %*s ", print_wcol_hwid, "HW ID");
	}
	// Column : Name
	printf("| Name");
	off = print_wcol_name - 4;
	for(unsigned i=0; i<off; i++) printf(" ");
	// Column : Data i/o
	printf(" | ");
	off = (print_wcol_wdata - 8) / 2;
	for(unsigned i=0; i<off; i++) printf(" ");
	printf("Data i/o");
	off = print_wcol_wdata - 8 - off;
	for(unsigned i=0; i<off; i++) printf(" ");
	// Column : Par. i/o
	printf(" | ");
	off = (print_wcol_par - 8) / 2;
	for(unsigned i=0; i<off; i++) printf(" ");
	printf("Par. i/o");
	off = print_wcol_par - 8 - off;
	for(unsigned i=0; i<off; i++) printf(" ");
	// Column : Img. i/o
	printf(" | ");
	off = (print_wcol_img - 8) / 2;
	for(unsigned i=0; i<off; i++) printf(" ");
	printf("Img. i/o");
	off = print_wcol_img - 8 - off;
	for(unsigned i=0; i<off; i++) printf(" ");
	// Column : Cycles. i/o
	if(chk_opt_cycles()) {
		printf(" | ");
		off = (print_wcol_cy - 10) / 2;
		for(unsigned i=0; i<off; i++) printf(" ");
		printf("Cycles i/o");
		off = print_wcol_cy - 10 - off;
		for(unsigned i=0; i<off; i++) printf(" ");
	}
	// Last column
	printf(" | Details\n");
}

void nnprint_table_t::print_layer(layer_t* layer) {
	unsigned off = 0;
	unsigned mid = 0;
	unsigned val = 0;
	static char buf[32];

	// Column : HW ID
	if(chk_opt_hwid()) {
		printf("| ");
		if(layer->id >=0) {
			printf("%*u", print_wcol_hwid, layer->id);
		}
		else {
			for(unsigned i=0; i<print_wcol_hwid; i++) printf(" ");
		}
		printf(" ");
	}

	// Column : Name
	printf("| %s%u", layer->typenamel, layer->typeidx);
	off = print_wcol_name - (strlen(layer->typenamel) + uint_digitsnb(layer->typeidx));
	for(unsigned i=0; i<off; i++) printf(" ");

	// Column : Data i/o
	printf(" | ");
	off = (print_wcol_wdata - (print_wlay_win + print_wlay_wout + 1)) / 2;
	for(unsigned i=0; i<off; i++) printf(" ");
	printf("%*u%c", print_wlay_win-1, layer->wdata, layer->sdata ? 's' : 'u');
	mid = print_wcol_wdata - (print_wlay_win + print_wlay_wout) - 2 * off;
	for(unsigned i=0; i<mid; i++) printf(" ");
	printf("%*u%c", print_wlay_wout-1, layer->out_wdata, layer->out_sdata ? 's' : 'u');
	for(unsigned i=0; i<off; i++) printf(" ");

	// Column : Par. i/o
	printf(" | ");
	off = (print_wcol_par - (print_wlay_pari + print_wlay_paro + 1)) / 2;
	for(unsigned i=0; i<off; i++) printf(" ");
	printf("%*u", print_wlay_pari, layer->split_in);
	mid = print_wcol_par - (print_wlay_pari + print_wlay_paro) - 2 * off;
	for(unsigned i=0; i<mid; i++) printf(" ");
	printf("%*u", print_wlay_paro, layer->split_out);
	for(unsigned i=0; i<off; i++) printf(" ");

	// Column : Img. i/o
	printf(" | ");
	off = (print_wcol_img - (print_wlay_imgi + print_wlay_imgo + 1)) / 2;
	for(unsigned i=0; i<off; i++) printf(" ");
	sprintf(buf, "%ux%ux%u", layer->fx, layer->fy, layer->fz);
	val = print_wlay_imgi - strlen(buf);
	for(unsigned i=0; i<val; i++) printf(" ");
	printf("%s", buf);
	mid = print_wcol_img - (print_wlay_imgi + print_wlay_imgo) - 2 * off;
	for(unsigned i=0; i<mid; i++) printf(" ");
	sprintf(buf, "%ux%ux%u", layer->out_fx, layer->out_fy, layer->out_fz);
	val = print_wlay_imgo - strlen(buf);
	for(unsigned i=0; i<val; i++) printf(" ");
	printf("%s", buf);
	for(unsigned i=0; i<off; i++) printf(" ");

	// Column : Cycles i/o
	if(chk_opt_cycles()) {
		printf(" | ");
		off = (print_wcol_cy - (print_wlay_cyi + print_wlay_cyo + 1)) / 2;
		for(unsigned i=0; i<off; i++) printf(" ");
		printf("%*u", print_wlay_cyi, layer->cycles);
		mid = print_wcol_cy - (print_wlay_cyi + print_wlay_cyo) - 2 * off;
		for(unsigned i=0; i<mid; i++) printf(" ");
		printf("%*u", print_wlay_cyo, layer->out_cycles);
		for(unsigned i=0; i<off; i++) printf(" ");
	}

	// Last column
	printf(" | ");

	// Append the layer-specific details
	layer->print_extra_details();
	printf("\n");
}

// Main pretty-print functions
// Assumption : The layers are in a meaningful topological order

int nnprint_layer(layer_t* layer, unsigned options) {

	if((options & NNPRINT_OPT_TABLE) == 0) {
		nnprint_oneline_layer(layer, nullptr);
		return 0;
	}

	nnprint_table_t printobj;
	printobj.options = options;

	// Initialize column widthes
	printobj.init_addlayer(layer);
	printobj.init_end();

	// Print header
	printobj.print_line('=');
	printobj.print_titles();
	printobj.print_line('=');

	// Print layer row
	printobj.print_layer(layer);

	// Print bottom of table
	printobj.print_line('=');

	return 0;
}

int nnprint(const vector<layer_t*>& layers, unsigned options, unsigned layer_type) {

	if((options & NNPRINT_OPT_TABLE) == 0) {
		nnprint_oneline(layers, nullptr);
		return 0;
	}

	printf("Overview of the network");
	if(layer_type != LAYER_NONE) printf(" (only layers %s)", Layer::get_type_id2nameu(layer_type));
	printf(" :\n");

	nnprint_table_t printobj;
	printobj.options = options;

	bool print_dashes = true;

	// Initialize column widthes
	for(auto layer : layers) {
		if(layer_type != LAYER_NONE && layer->type != (int)layer_type) {
			print_dashes = false;  // Intermediate dashes would be meaningless with partial network print
			continue;
		}
		// Take this layer into account
		printobj.init_addlayer(layer);
	}

	// Apply max column width for titles
	printobj.init_end();

	// Print header
	printobj.print_line('=');
	printobj.print_titles();
	printobj.print_line('=');

	// Print layers
	for(auto layer : layers) {
		if(layer_type != LAYER_NONE && layer->type != (int)layer_type) continue;
		// Print layer
		printobj.print_layer(layer);
		// Optionally print dashes
		if(print_dashes == true) {
			// This covers after a NEXT layer
			if(layer != layers.back() && layer->next == NULL) printobj.print_line('-');
			// This covers end of sections before a CAT layer
			if(layer->next != NULL && layer->next->prev == NULL) printobj.print_line('-');
		}
	}

	// Print bottom of table
	printobj.print_line('=');

	return 0;
}


//============================================
// Print network memory usage
//============================================

unsigned long Layer::eval_mem_size(void) {
	return mem.EvalSizeTotal();
}

unsigned long LayerWin::eval_mem_size(void) {
	unsigned loc_bufy = bufy;
	if(loc_bufy == 0) loc_bufy = winy + winy - 1;  // FIXME Assumption of how the VHDL implem behaves
	mem.lines = fx * (fz / win_par_oz) * loc_bufy;
	mem.width = wdata * win_par_oz;
	mem.num   = split_out / win_par_oz;
	return mem.EvalSizeTotal();
}
unsigned long LayerNeu::eval_mem_size(void) {
	// Memory geometry is supposed to be set already
	return mem.EvalSizeTotal();
}
unsigned long LayerNeu_CM::eval_mem_size(void) {
	// Memory geometry is supposed to be set already
	return mem.EvalSizeTotal();
}

unsigned long LayerNorm::eval_mem_size(void) {
	mem.lines = fsize / split_out;
	unsigned bits_per_neu = norm_wbias + norm_wmul + norm_wshr;
	mem.width = bits_per_neu * split_out;
	mem.num   = (bits_per_neu != 0);
	return mem.EvalSizeTotal();
}
unsigned long LayerTernarize::eval_mem_size(void) {
	mem.lines = fsize / split_out;
	unsigned bits_per_neu = 2*wdata + (ter_out_static == false ? 3*out_wdata : 0);
	mem.width = bits_per_neu * split_out;
	mem.num   = 1;
	return mem.EvalSizeTotal();
}

unsigned long LayerScatter::eval_mem_size(void) {
	mem.lines = fsize / split_in;
	mem.width = arr_layers.size();
	mem.num   = 1;
	return mem.EvalSizeTotal();
}
unsigned long LayerGather::eval_mem_size(void) {
	mem.lines = out_fsize / split_out;
	mem.width = arr_layers.size();
	mem.num   = 1;
	return mem.EvalSizeTotal();
}

unsigned long LayerFifo::eval_mem_size(void) {
	mem.lines = 64;  // FIXME Hardcoded FIFO size
	mem.width = wdata * split_out;
	mem.num   = 1;
	return mem.EvalSizeTotal();
}

// Raw print, one line per layer

int nnprint_mem_oneline_layer(layer_t* layer, const char* indent) {
	if(indent != nullptr) {
		printf("%s", indent);
	}

	printf("%s%u mem size %lu style %s\n",
		layer->typenameu, layer->typeidx,
		layer->mem.EvalSizeTotal(),
		layer->mem.GetStyleName()
	);

	return 0;
}

int nnprint_mem_oneline(const vector<layer_t*>& layers, const char* indent) {

	printf("Memory report :\n");

	for(auto layer : layers) {
		nnprint_mem_oneline_layer(layer, indent);
	}

	return 0;
}

// Pretty-print with a table

class nnprint_mem_table_t {

	public :

	// Width of data in each column
	unsigned print_wlay_name  = 0;
	unsigned print_wlay_num   = 0;
	unsigned print_wlay_lines = 0;
	unsigned print_wlay_width = 0;
	unsigned print_wlay_size  = 0;
	unsigned print_wlay_implem = 0;
	unsigned print_wlay_blocks = 0;

	// Width of columns and double-columns with titles
	unsigned print_wcol_name  = 0;
	unsigned print_wcol_num   = 0;
	unsigned print_wcol_lines = 0;
	unsigned print_wcol_width = 0;
	unsigned print_wcol_size  = 0;
	unsigned print_wcol_implem = 0;
	unsigned print_wcol_blocks = 0;

	// Sums of sizes of each memory type
	unsigned long total_size_reg   = 0;
	unsigned long total_size_lut   = 0;
	unsigned long total_size_bram  = 0;
	unsigned long total_size_uram  = 0;
	unsigned long total_size_other = 0;
	unsigned long total_size_all   = 0;

	// Number of HW blocks
	unsigned total_num_reg  = 0;
	unsigned total_num_lut  = 0;
	unsigned total_num_bram = 0;
	unsigned total_num_uram = 0;

	// Extra options
	unsigned options;

	inline unsigned get_opt_table(void)  { return options & NNPRINT_OPT_TABLE; }

	inline unsigned chk_opt_table(void)  { return get_opt_table() != 0; }

	// Methods
	void init_addlayer(layer_t* layer);
	void init_end(void);
  void print_line(char dash);
	void print_title_left(unsigned width, const char* title);
	void print_title_right(unsigned width, const char* title);
	void print_title_centered(unsigned width, const char* title);
	void print_titles(void);
	void print_layer(layer_t* layer);
	void print_total(void);

};

// Add the specified layer to the column width counters
void nnprint_mem_table_t::init_addlayer(layer_t* layer) {

	// Evaluate or update the memory implem style and size
	layer->mem.EvalBlocks(layer->network->hwconfig_lut_threshold, layer->network->hwconfig_use_uram);

	unsigned long size = layer->eval_mem_size();

	// Count total for each memory type
	if(layer->mem.style == MemImplem::STYLE_REG) {
		total_size_reg += size;
		total_num_reg += layer->mem.blocks;
	}
	else if(layer->mem.style == MemImplem::STYLE_LUTRAM) {
		total_size_lut += size;
		total_num_lut += layer->mem.blocks;
	}
	else if(layer->mem.style == MemImplem::STYLE_BRAM) {
		total_size_bram += size;
		total_num_bram += layer->mem.blocks;
	}
	else if(layer->mem.style == MemImplem::STYLE_URAM) {
		total_size_uram += size;
		total_num_uram += layer->mem.blocks;
	}
	else total_size_other += size;

	// Count column width
	print_wlay_name   = GetMax(print_wlay_name,   strlen(layer->typenamel) + uint_digitsnb(layer->typeidx));
	print_wlay_num    = GetMax(print_wlay_num,    u64_digitsnb(layer->mem.num));
	print_wlay_lines  = GetMax(print_wlay_lines,  u64_digitsnb(layer->mem.lines));
	print_wlay_width  = GetMax(print_wlay_width,  u64_digitsnb(layer->mem.width));
	print_wlay_size   = GetMax(print_wlay_size,   u64_digitsnb(size));
	print_wlay_implem = GetMax(print_wlay_implem, strlen(layer->mem.GetStyleName()));
	print_wlay_blocks = GetMax(print_wlay_blocks, u64_digitsnb(layer->mem.blocks));

}

// Apply max column width for titles
void nnprint_mem_table_t::init_end(void) {

	// Column widthes
	print_wcol_name  = GetMax(print_wlay_name,  4);  // Name
	print_wcol_num   = GetMax(print_wlay_num,   3);  // Num
	print_wcol_lines = GetMax(print_wlay_lines, 5);  // Lines
	print_wcol_width = GetMax(print_wlay_width, 5);  // Width
	print_wcol_size  = GetMax(print_wlay_size,  4);  // Size
	print_wcol_implem = GetMax(print_wlay_implem, 6);  // Implem
	print_wcol_blocks = GetMax(print_wlay_blocks, 6);  // Blocks

	// Total size of all memories
	total_size_all =
		total_size_reg +
		total_size_lut +
		total_size_bram +
		total_size_uram +
		total_size_other;

}

// Helper functions to print the table

void nnprint_mem_table_t::print_line(char dash) {
	putchar('+');
	for(unsigned i=0; i<print_wcol_name+2; i++) putchar(dash);
	putchar('+');
	for(unsigned i=0; i<print_wcol_num+2; i++) putchar(dash);
	putchar('+');
	for(unsigned i=0; i<print_wcol_lines+2; i++) putchar(dash);
	for(unsigned i=0; i<print_wcol_width+2; i++) putchar(dash);
	putchar('+');
	for(unsigned i=0; i<print_wcol_size+2; i++) putchar(dash);
	putchar('+');
	for(unsigned i=0; i<print_wcol_implem+2; i++) putchar(dash);
	for(unsigned i=0; i<print_wcol_blocks+2; i++) putchar(dash);
	putchar('+');
	putchar('\n');
}

void nnprint_mem_table_t::print_title_left(unsigned width, const char* title) {
	printf("%s", title);
	unsigned off = width - strlen(title);
	for(unsigned i=0; i<off; i++) putchar(' ');
}
void nnprint_mem_table_t::print_title_right(unsigned width, const char* title) {
	printf("%*s", width, title);
}
void nnprint_mem_table_t::print_title_centered(unsigned width, const char* title) {
	unsigned off = 0;
	unsigned len = strlen(title);
	off = (width - len) / 2;
	for(unsigned i=0; i<off; i++) putchar(' ');
	printf("%s", title);
	off = width - len - off;
	for(unsigned i=0; i<off; i++) putchar(' ');
}

void nnprint_mem_table_t::print_titles(void) {
	printf("| ");
	print_title_left(print_wcol_name, "Name");
	printf(" | ");
	print_title_centered(print_wcol_num, "Num");
	printf(" | ");
	print_title_right(print_wcol_lines, "Lines");
	printf("  ");
	print_title_right(print_wcol_width, "Width");
	printf(" | ");
	print_title_centered(print_wcol_size, "Size");
	printf(" | ");
	print_title_right(print_wcol_implem, "Implem");
	printf("  ");
	print_title_right(print_wcol_blocks, "Blocks");
	printf(" |\n");
}

void nnprint_mem_table_t::print_layer(layer_t* layer) {
	unsigned off = 0;

	// Column : Name
	printf("| %s%u", layer->typenamel, layer->typeidx);
	off = print_wcol_name - (strlen(layer->typenamel) + uint_digitsnb(layer->typeidx));
	for(unsigned i=0; i<off; i++) printf(" ");

	// Num, lines, width
	printf(" | %*u", print_wcol_num,   layer->mem.num);
	printf(" | %*u", print_wcol_lines, layer->mem.lines);
	printf("  %*u",  print_wcol_width, layer->mem.width);

	// Column : Size
	unsigned long size = layer->eval_mem_size();
	printf(" | %*lu" , print_wcol_size, size);

	// Column : Implem
	printf(" | %*s  ", print_wcol_implem,
		(layer->mem.style != MemImplem::STYLE_NONE) ? layer->mem.GetStyleName() : " "
	);
	if(layer->mem.blocks > 0) printf("%*lu", print_wcol_blocks, layer->mem.blocks);
	else { for(unsigned i=0; i<print_wcol_blocks; i++) putchar(' '); }

	// End
	printf(" |\n");
}

void nnprint_mem_table_t::print_total(void) {
	vector< pair<const char*, unsigned> > vector_sizes;

	// Insert in the array
	if(total_size_reg > 0)   vector_sizes.push_back( make_pair("reg",    total_size_reg) );
	if(total_size_lut > 0)   vector_sizes.push_back( make_pair("lutram", total_size_lut) );
	if(total_size_bram > 0)  vector_sizes.push_back( make_pair("bram",   total_size_bram) );
	if(total_size_uram > 0)  vector_sizes.push_back( make_pair("uram",   total_size_uram) );
	if(total_size_other > 0) vector_sizes.push_back( make_pair("other",  total_size_other) );
	vector_sizes.push_back( make_pair("all", total_size_all) );

	// Column widthes
	unsigned width_name = 0;
	unsigned width_size = 0;
	for(const auto& i : vector_sizes) {
		unsigned len_name = strlen(i.first);
		unsigned len_size = uint_digitsnb(i.second);
		width_name = GetMax(width_name, len_name);
		width_size = GetMax(width_size, len_size);
	}

	// Print
	for(const auto& i : vector_sizes) {
		printf("Total memory size, %*s : %*u bits\n", width_name, i.first, width_size, i.second);
	}

	// Now print the number of HW blocks used
	vector_sizes.resize(0);

	// Insert in the array
	if(total_num_reg > 0)   vector_sizes.push_back( make_pair("reg",    total_num_reg) );
	if(total_num_lut > 0)   vector_sizes.push_back( make_pair("lutram", total_num_lut) );
	if(total_num_bram > 0)  vector_sizes.push_back( make_pair("bram",   total_num_bram) );
	if(total_num_uram > 0)  vector_sizes.push_back( make_pair("uram",   total_num_uram) );

	// Column widthes
	width_name = 0;
	width_size = 0;
	for(const auto& i : vector_sizes) {
		unsigned len_name = strlen(i.first);
		unsigned len_size = uint_digitsnb(i.second);
		width_name = GetMax(width_name, len_name);
		width_size = GetMax(width_size, len_size);
	}

	// Print
	for(const auto& i : vector_sizes) {
		printf("Total memory blocks : %*u %s\n", width_size, i.second, i.first);
	}

}

// Main pretty-print functions

int nnprint_mem_layer(layer_t* layer, unsigned options) {

	if((options & NNPRINT_OPT_TABLE) == 0) {
		nnprint_mem_oneline_layer(layer, nullptr);
		return 0;
	}

	nnprint_mem_table_t printobj;
	printobj.options = options;

	// Initialize column widthes
	printobj.init_addlayer(layer);
	printobj.init_end();

	// Print header
	printobj.print_line('=');
	printobj.print_titles();
	printobj.print_line('=');

	// Print layer row
	printobj.print_layer(layer);

	// Print bottom of table
	printobj.print_line('=');

	return 0;
}

int nnprint_mem(const vector<layer_t*>& layers, unsigned options, unsigned layer_type) {

	printf("Memory report");
	if(layer_type != LAYER_NONE) printf(" (only layers %s)", Layer::get_type_id2nameu(layer_type));
	printf(" :\n");

	if((options & NNPRINT_OPT_TABLE) == 0) {
		nnprint_oneline(layers, nullptr);
		return 0;
	}

	nnprint_mem_table_t printobj;
	printobj.options = options;

	bool print_dashes = true;

	// Initialize column widthes
	for(auto layer : layers) {
		if(layer_type != LAYER_NONE && layer->type != (int)layer_type) {
			print_dashes = false;  // Intermediate dashes would be meaningless with partial network print
			continue;
		}
		unsigned long size = layer->eval_mem_size();
		if(size == 0) {
			print_dashes = false;  // Intermediate dashes would be meaningless with partial network print
			continue;
		}
		// Take this layer into account
		printobj.init_addlayer(layer);
	}

	// Apply max column width for titles
	printobj.init_end();

	// If desired, just print total sizes
	if((options & NNPRINT_OPT_TOTAL) != 0) {
		printobj.print_total();
		return 0;
	}

	// Print header
	printobj.print_line('=');
	printobj.print_titles();
	printobj.print_line('=');

	// Print layers
	for(auto layer : layers) {
		if(layer_type != LAYER_NONE && layer->type != (int)layer_type) continue;
		unsigned long size = layer->eval_mem_size();
		if(size == 0) continue;
		// Print layer
		printobj.print_layer(layer);
		// Optionally print dashes
		if(print_dashes == true) {
			// This covers after a NEXT layer
			if(layer != layers.back() && layer->next == NULL) printobj.print_line('-');
			// This covers end of sections before a CAT layer
			if(layer->next != NULL && layer->next->prev == NULL) printobj.print_line('-');
		}
	}

	// Print bottom of table
	printobj.print_line('=');

	// Print total sizes
	printobj.print_total();

	return 0;
}


//============================================
// Weight compression : Print decoder size as memory size
//============================================

int nnprint_weight_decomp_mem_oneline_layer(layer_t* layer, const char* indent) {
	if(indent != nullptr) {
		printf("%s", indent);
	}

	if(layer->type != LAYER_NEU) {
		printf("%s%u no decompression applicable\n",
			layer->typenameu, layer->typeidx
		);
	}
	else {

		// FIXME

		printf("%s%u mem size %lu style %s blocks %lu\n",
			layer->typenameu, layer->typeidx,
			layer->eval_mem_size(),
			layer->mem.GetStyleName(),
			layer->mem.blocks
		);
	}

	return 0;
}

int nnprint_weight_decomp_mem_oneline(const vector<layer_t*>& layers, const char* indent) {

	printf("Weight decompression memory report :\n");

	for(auto layer : layers) {
		nnprint_weight_decomp_mem_oneline_layer(layer, indent);
	}

	return 0;
}


//============================================
// Command-line parameters
//============================================

bool param_debug = false;
bool param_sci_notation = false;

char const * filename_frames = nullptr;
unsigned param_fn = 0;

char* filename_out = nullptr;
char* filename_out_cur = nullptr;
FILE* Fo = NULL;

bool param_rand_given = false;
int param_rand_min = 0;
int param_rand_max = 0;

bool     param_noout = false;
bool     param_multiline = false;
layer_t* param_out_layer = NULL;

// Timeout values for operations with hardware accelerator
unsigned long param_timeout_regs_us = 0;
unsigned long param_timeout_send_us = 0;
unsigned long param_timeout_recv_us = 0;

unsigned     param_out_nl = 0;
char const * param_out_sep = ",";
char const * param_out_format = "%i";
bool         param_out_mask = false;

bool param_freerun = false;
bool param_hw_blind = false;
bool param_floop = false;
unsigned param_bufsz_mb = 128;

bool param_print_time = false;

// Ensure the output file is opened
void chkoutfile() {
	if(param_noout==false && filename_out != NULL) {
		if(Fo != stdout) fclose(Fo);
		if(filename_out_cur != NULL) free(filename_out_cur);
		Fo = fopen(filename_out, "wb");
		if(Fo==NULL) {
			printf("Error: Can't open/create output file '%s'.\n", filename_out);
			exit(EXIT_FAILURE);
		}
		filename_out_cur = filename_out_cur;
		filename_out = NULL;;
	}
}

// Ensure we have a CNN
void chknonempty(Network* network) {
	auto& layers = network->layers;
	if(layers.size()==0) {
		printf("Error: There is no network built, the program can't continue\n");
		exit(EXIT_FAILURE);
	}
}

int decodeparam_width_sign(const char* str, unsigned* width_p, bool* sign_bool_p, unsigned* sign_uint_p) {
	unsigned width = 1;
	bool sign = false;
	if(strcasecmp(str, "bin") == 0) {
		width = 1;
		sign = false;
	}
	else if(strcasecmp(str, "ter") == 0) {
		width = 2;
		sign = true;
	}
	else {
		char* endptr = NULL;
		width = strtoul(str, &endptr, 0);
		if     (endptr != NULL && (*endptr) == 'u') sign = false;
		else if(endptr != NULL && (*endptr) == 's') sign = true;
		else return 1;
	}
	if(width_p != NULL) *width_p = width;
	if(sign_bool_p != NULL) *sign_bool_p = sign;
	if(sign_uint_p != NULL) *sign_uint_p = sign ? 1 : 0;
	return 0;
}

int decodeparam_ms(const char* str, unsigned long *ms_p) {
	char* endptr = NULL;
	long unsigned ms = strtoul(str, &endptr, 0);
	if     (ms == 0) ms = 0;  // 0 means no timeout, don't risk rounding to next integer
	else if(endptr != NULL && strcmp(endptr, "ns" ) == 0) ms = (ms + 1000000 - 1) / 1000000;
	else if(endptr != NULL && strcmp(endptr, "us" ) == 0) ms = (ms + 1000 - 1) / 1000;
	else if(endptr != NULL && strcmp(endptr, "ms" ) == 0) ms *= 1;
	else if(endptr != NULL && strcmp(endptr, "s"  ) == 0) ms *= 1000;
	else if(endptr != NULL && strcmp(endptr, "min") == 0) ms *= 1000 * 60;
	else if(endptr != NULL && strcmp(endptr, "h"  ) == 0) ms *= 1000 * 3600;
	else if(endptr != NULL && (*endptr) == 0            ) ms *= 1000;
	else return 1;
	if(ms_p != NULL) *ms_p = ms;
	return 0;
}

int decodeparam_us(const char* str, unsigned long *us_p) {
	char* endptr = NULL;
	long unsigned us = strtoul(str, &endptr, 0);
	if     (us == 0) us = 0;  // 0 means no timeout, don't risk rounding to next integer
	else if(endptr != NULL && strcmp(endptr, "ns" ) == 0) us = (us + 1000 - 1) / 1000;
	else if(endptr != NULL && strcmp(endptr, "us" ) == 0) us *= 1;
	else if(endptr != NULL && strcmp(endptr, "ms" ) == 0) us *= 1000;
	else if(endptr != NULL && strcmp(endptr, "s"  ) == 0) us *= 1000 * 1000;
	else if(endptr != NULL && strcmp(endptr, "min") == 0) us *= 1000 * 1000 * 60;
	else if(endptr != NULL && strcmp(endptr, "h"  ) == 0) us *= 1000u * 1000u * 3600u;
	else if(endptr != NULL && (*endptr) == 0            ) us *= 1000 * 1000;
	else return 1;
	if(us_p != NULL) *us_p = us;
	return 0;
}

