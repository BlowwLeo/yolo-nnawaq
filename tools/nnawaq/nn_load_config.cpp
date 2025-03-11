// Load layer configuration files
// Reorder data according to hardware target specificities

extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>

#include "load_config.h"

}

#include "nn_layers_utils.h"
#include "nn_load_config.h"

using namespace std;



//============================================
// Reorder data inside arrays
//============================================

// For 3D frames: scan order Z-X-Y -> X-Y-Z
int reorder_to_xfirst_dim2(int** data, unsigned nbframes, unsigned fsize, unsigned fx, unsigned fy, unsigned fz, unsigned parz) {
	// Create a temp buffer large enough to store one frame
	static unsigned bufsize = 0;
	static int* buf = NULL;

	// Sanity check
	if(fx * fy * fz != fsize) {
		printf("Error: Frame size not coherent: %ux%ux%u -> %u\n", fx, fy, fz, fsize);
		return 1;
	}
	if(parz == 0) parz = 1;  // Paranoia
	if(fz % parz != 0) {
		printf("Error: Unsupported parameter PAR_OZ=%u that does not divide FZ=%u\n", parz, fz);
		return 1;
	}

	// Ensure the temp buffer is large enough to store one frame
	if(buf==NULL || fsize > bufsize) {
		bufsize = fsize;
		buf = (int*)realloc(buf, fsize * sizeof(*buf));
	}

	// Scan data for all frames
	for(unsigned n=0; n<nbframes; n++) {
		int* fdata = data[n];
		unsigned idx_buf = 0;
		// Generate the reordered data
		for(unsigned z=0; z<fz; z+=parz) {
			for(unsigned y=0; y<fy; y++) {
				for(unsigned x=0; x<fx; x++) {
					for(unsigned pz=0; pz<parz; pz++) {
						unsigned idx_xy = y * fx * fz + x * fz + z + pz;
						buf[idx_buf++] = fdata[idx_xy];
					}
				}
			}
		}
		// Save the new data
		memcpy(fdata, buf, fsize * sizeof(*buf));
	}  // Scan the frames

	// Clean
	//free(buf);

	return 0;
}

// For 3D frames: scan order X-Y-Z -> Z-X-Y
int reorder_to_zfirst_dim2(int** data, unsigned nbframes, unsigned fsize, unsigned fx, unsigned fy, unsigned fz, unsigned padz) {
	// Create a temp buffer large enough to store one frame
	static unsigned bufsize = 0;
	static int* buf = NULL;

	// Sanity check
	if(fx * fy * (fz + padz) != fsize) {
		printf("Error: Frame size not coherent: %ux%ux(%u+%u) -> %u\n", fx, fy, fz, padz, fsize);
		return 1;
	}

	// Ensure the temp buffer is large enough to store one frame
	if(buf==NULL || fsize > bufsize) {
		bufsize = fsize;
		buf = (int*)realloc(buf, fsize * sizeof(*buf));
	}

	// Scan data for all frames
	for(unsigned n=0; n<nbframes; n++) {
		int* fdata = data[n];
		unsigned idx_buf = 0;
		// Generate the reordered data
		for(unsigned y=0; y<fy; y++) {
			for(unsigned x=0; x<fx; x++) {
				for(unsigned z=0; z<fz; z++) {
					unsigned idx_xy = z * fx * fy + y * fx + x;
					buf[idx_buf++] = fdata[idx_xy];
				}
				for(unsigned z=0; z<padz; z++) {
					buf[idx_buf++] = 0;
				}
			}
		}
		// Save the new data
		memcpy(fdata, buf, fsize * sizeof(*buf));
	}  // Scan the frames

	// Clean
	//free(buf);

	return 0;
}

// For 3D frames: scan order X-Y-Z -> Z-X-Y, with partial Z-first for window style NORMAL
int reorder_to_partial_zfirst_dim2(int** data, unsigned nbframes, unsigned fsize, unsigned fx, unsigned fy, unsigned fz, unsigned winz, unsigned nwinz) {
	// Create a temp buffer large enough to store one frame
	static unsigned bufsize = 0;
	static int* buf = NULL;

	// Sanity check
	if(fx * fy * (nwinz * winz) != fsize) {
		printf("Error: Frame size not coherent: %ux%ux(%ux%u) -> %u\n", fx, fy, winz, nwinz, fsize);
		return 1;
	}

	// Ensure the temp buffer is large enough to store one frame
	if(buf==NULL || fsize > bufsize) {
		bufsize = fsize;
		buf = (int*)realloc(buf, fsize * sizeof(*buf));
	}

	// Scan data for all frames
	for(unsigned n=0; n<nbframes; n++) {
		int* fdata = data[n];
		unsigned idx_buf = 0;
		// Generate the reordered data
		for(unsigned nz=0; nz<nwinz; nz++) {
			for(unsigned y=0; y<fy; y++) {
				for(unsigned x=0; x<fx; x++) {
					for(unsigned z=0; z<winz; z++) {
						unsigned idx_xy = (nz * winz + z) * fx * fy + y * fx + x;
						buf[idx_buf++] = (nz * winz + z < fz) ? fdata[idx_xy] : 0;
					}
				}
			}
		}
		// Save the new data
		memcpy(fdata, buf, fsize * sizeof(*buf));
	}  // Scan the frames

	// Clean
	//free(buf);

	return 0;
}

// For 3D frames: Reorder the Z dimensions to fit to a previous layer CAT
int reorder_to_prev_cat(int** data, unsigned neu, unsigned fsize, unsigned fz, layer_t* layer_cat) {
	// Create a temp buffer large enough to store one frame
	static unsigned bufsize = 0;
	static int* buf = NULL;

	// Count the sum of all input PAR
	unsigned sumpar = 0;
	for(auto layer : layer_cat->arr_layers) {
		sumpar += layer->split_out;
	}

	// Sanity check
	if(fsize % fz != fsize) {
		printf("Error: Frame size %u not divisible by fz %u\n", fsize, fz);
		return 1;
	}
	// Compute the number of groups of these inputs in the Z dimension
	if(fz % sumpar != 0) {
		printf("Error: Frame size Z %u is not a multiple of sum of parallelisms %u at inut of previous CAT layer\n", fz, sumpar);
		return 1;
	}

	unsigned numfz = fsize / fz;
	unsigned numgroups = fz / sumpar;

	// Ensure the temp buffer is large enough to store one Fz dimension
	if(buf==NULL || fz > bufsize) {
		bufsize = fz;
		buf = (int*)realloc(buf, fz * sizeof(*buf));
	}

	// Reorder neuron weights
	for(unsigned n=0; n<neu; n++) {
		int* fdata = data[n];
		// Process weights by chunks of size Fz
		for(unsigned z=0; z<numfz; z++) {
			int* outptr = buf;
			for(unsigned g=0; g<numgroups; g++) {
				// Scan inputs of the CAT layer
				int* inptr = fdata;
				for(auto layer : layer_cat->arr_layers) {
					unsigned inpar = layer->split_out;
					memcpy(outptr, inptr+g*inpar, inpar*sizeof(*buf));
					outptr += inpar;
					inptr += numgroups * inpar;
				}  // Input of CAT
			}  // Groups of inputs
			// Replace the resulting Fz data
			memcpy(fdata, buf, fsize * sizeof(*buf));
			fdata += fz;
		}  // Fz
	}  // Neuron

	// Clean
	//free(buf);

	return 0;
}

// Wrapper that prints a message before reordering weights
int neurons_weights_reorder(layer_t* layer, unsigned winx, unsigned winy, unsigned fz, unsigned winz, unsigned nwinz) {
	// Check if some padding on Z axis is needed
	int padz = layer->fsize / (winx * winy) - fz;
	// Some information
	if(param_debug==true) {
		printf("INFO layer %s%u: Reordering weights: %ux%ux(%u%+i) -> %u\n",
			layer->typenameu, layer->typeidx,
			winx, winy, fz, padz, layer->fsize
		);
	}
	// Error check
	if(padz<0 && nwinz == 0) {
		printf("Error : Padding on Z can't be negative\n");
		exit(EXIT_FAILURE);
	}
	// Reorder
	int z = 0;
	if(true) {
		z = reorder_to_zfirst_dim2(layer->cfg_data, layer->neurons, layer->fsize, winx, winy, fz, padz);
	}
	else {
		z = reorder_to_partial_zfirst_dim2(layer->cfg_data, layer->neurons, layer->fsize, winx, winy, fz, winz, nwinz);
	}
	return z;
}



//============================================
// Read files as integer
//============================================

// Load the config file
int layer_loadcfg(layer_t* layer, unsigned nrow, unsigned ncol, unsigned alloc_nrow, unsigned alloc_ncol) {
	// Allocate the array
	if(alloc_nrow < nrow) alloc_nrow = nrow;
	if(alloc_ncol < ncol) alloc_ncol = ncol;
	layer->cfg_data = array_create_dim2(alloc_nrow, alloc_ncol);
	// Load from the specified file
	return loadfile(layer->cfg_data, layer->cfg_filename, nrow, ncol, param_multiline);
}
int layer_loadcfg_or_random(layer_t* layer, unsigned nrow, unsigned ncol, unsigned wdata, unsigned alloc_nrow, unsigned alloc_ncol) {
	// Allocate the array
	if(alloc_nrow < nrow) alloc_nrow = nrow;
	if(alloc_ncol < ncol) alloc_ncol = ncol;
	layer->cfg_data = array_create_dim2(alloc_nrow, alloc_ncol);

	// In case of missing config file, use random data
	if(layer->cfg_filename==NULL) {
		// FIXME Also optionally fill with a constant
		if(param_rand_given==false) {
			printf("Error: Layer %s%u: Missing configuration file\n", layer->typenamel, layer->typeidx);
			return 1;
		}
		array_fillrand_dim2(layer->cfg_data, nrow, ncol, wdata, param_rand_min, param_rand_max);
		return 0;
	}

	// Load from the specified file
	return loadfile(layer->cfg_data, layer->cfg_filename, nrow, ncol, param_multiline);
}

// For a given layer assumed to be neurons, apply reordering of weights if needed
// FIXME Reordering may import less data than legitimately expected by current fsize, in case there is rounding of fsize according to win and nwin
// So the layer object should keep the original user-desired value of fsize, and allocated another cfg_data matrix with final order and size
int neurons_weights_reorder_for_prev_layers(layer_t* layer) {
	// Paranoia
	if(layer->type!=LAYER_NEU && layer->type!=LAYER_NEU_CM) abort();

	if(layer->cfg_data == nullptr) {
		printf("WARNING %s%u: Missing config data, skipping reordering\n", layer->typenameu, layer->typeidx);
		return 1;
	}

	// If weights are known to be Z-first (or Keep order), just skip reordering
	Network* network = layer->network;
	if(layer->neu_worder == NEU_WORDER_NONE) {
		layer->neu_worder = network->default_neu_worder;
	}

	// Behaviour of order None (default) is equivalent to Keep
	// FIXME If previous layers are convolutional, a warning message could be printed
	if(layer->neu_worder == NEU_WORDER_NONE) return 0;

	// Skip if user explicitly provides the right order
	if(layer->neu_worder == NEU_WORDER_KEEP) return 0;

	// Scan previous layers
	layer_t* prevlayer = layer->prev;
	for( ; prevlayer != NULL; prevlayer = prevlayer->prev) {

		if(prevlayer->type==LAYER_WIN) {
			unsigned winx = prevlayer->winx;
			unsigned winy = prevlayer->winy;
			unsigned fz   = prevlayer->fz;
			unsigned fzo  = prevlayer->out_fz;
			unsigned parz = prevlayer->win_par_oz;

			printf("PARZ %u\n", parz);
			printf("DEBUG val  %u\n", param_debug);
			printf("DEBUG %u\n", __LINE__);

			// If this layer is just for REPEAT, order is unchanged, continue backward
			if(prevlayer->fx == 1 && prevlayer->fy == 1 && prevlayer->nwinx == 1 && prevlayer->nwiny == 1) {
				continue;
			}

			printf("DEBUG %u\n", __LINE__);

			// Reorder to XFIRST
			if(layer->neu_worder == NEU_WORDER_ZFIRST) {
				int z = reorder_to_xfirst_dim2(layer->cfg_data, layer->neurons, layer->fsize, winx, winy, fz, parz);
				if(z != 0) return z;
			}

			printf("DEBUG %u\n", __LINE__);

			// FIXME Provided frame size may be winx * winy * fz, but expected size is layer->fsize

			// Reorder if needed
			if(parz > 1 || (winx>1 || winy>1)) {
				int z = neurons_weights_reorder(layer, winx, winy, fz, parz, fzo / parz);
				if(z != 0) return z;
			}

			printf("DEBUG %u\n", __LINE__);

			// Exit reordering
			// FIXME Still need to scan previous layers in case there is a CAT
			return 0;
		}

		else if(prevlayer->type==LAYER_NEU) {
			// Neuron-to-neuron configuration, assume dense configuration
			// Only handled when Fx=Fy=1

			if(layer->fx>1 || layer->fy>1) {
				printf("WARNING %s%u: Backward traversal reached layer %s%u, order of weights is unknown with Fx=%u Fy=%u\n",
					layer->typenameu, layer->typeidx,
					prevlayer->typenameu, prevlayer->typeidx,
					layer->fx, layer->fy
				);
			}

			// At least, reorder to ZFIRST
			if(layer->neu_worder == NEU_WORDER_XFIRST) {
				if(layer->fx>1 || layer->fy>1) {
					int z = neurons_weights_reorder(layer, layer->fx, layer->fy, layer->fz, 0, 0);
					if(z != 0) return z;
				}
			}

			return 0;
		}

		else if(prevlayer->type==LAYER_POOL) {
			// Pooling layers generate ZFIRST order
			// Only handled when Fx=Fy=1

			// FIXME Layer POOL does not change order on Z, so we should traverse backward to search for layers that change Z order
			// Such as layer CAT, or WIN with style NORMAL and PAR_OZ > 1, ...

			if(layer->fx>1 || layer->fy>1) {
				printf("WARNING %s%u: Backward traversal reached layer %s%u, order of weights is unknown with Fx=%u Fy=%u\n",
					layer->typenameu, layer->typeidx,
					prevlayer->typenameu, prevlayer->typeidx,
					layer->fx, layer->fy
				);
			}

			// At least, reorder to ZFIRST
			if(layer->neu_worder == NEU_WORDER_XFIRST) {
				if(layer->fx>1 || layer->fy>1) {
					int z = neurons_weights_reorder(layer, layer->fx, layer->fy, layer->fz, 0, 0);
					if(z != 0) return z;
				}
			}

			return 0;
		}

		else if(prevlayer->type==LAYER_NORM) {
			// No impact on order of weights, continue scan of layers backward
		}

		else if(prevlayer->type==LAYER_TER) {
			// No impact on order of weights, continue scan of layers backward
		}

		else if(prevlayer->type==LAYER_RELU) {
			// No impact on order of weights, continue scan of layers backward
		}

		else if(prevlayer->type==LAYER_LEAKY) {
			// No impact on order of weights, continue scan of layers backward
		}

		else if(prevlayer->type==LAYER_FORK) {
			// No impact on order of weights, continue scan of layers backward
		}

		else if(prevlayer->type==LAYER_CAT) {

			// FIXME We assume that the layer already has weights loaded and reordered to zfirst if necessary
			// Need to change the code structure so that we always load neuron weights, then scan previous layers, find WIN and reorder, then find CAT and reorder

			int z = reorder_to_prev_cat(layer->cfg_data, layer->neurons, layer->fsize, layer->fz, prevlayer);
			if(z != 0) return z;

			// Exit reordering
			return 0;
		}

		else if(prevlayer->type==LAYER_FIFO) {
			// No impact on order of weights, transparent
		}

		else if(prevlayer->type==LAYER_FLATTEN) {
			// Assume ZFIRST before the Flatten, and dense configuration

			if(layer->neu_worder == NEU_WORDER_ZFIRST) return 0;

			if(prevlayer->fx>1 || prevlayer->fy>1) {

				int z = neurons_weights_reorder(layer, prevlayer->fx, prevlayer->fy, prevlayer->fz, 0, 0);
				if(z != 0) return z;
			}

			return 0;
		}

		else {
			// Don't know how to handle this type of layer
			printf("WARNING %s%u: Backward traversal reached layer %s%u whose impact on order of weights is unknown\n",
				layer->typenameu, layer->typeidx,
				prevlayer->typenameu, prevlayer->typeidx
			);
			break;
		}

	}  // Scan previous layers

	// Beginning of network reached
	// Assume data is provided ZFIRST
	if(layer->fsize != layer->fx * layer->fy * layer->fz) {
		printf("WARNING %s%u: Backward traversal reached beginning of network, image is %ux%ux%u but fsize=%u\n",
			layer->typenameu, layer->typeidx,
			layer->fx, layer->fy, layer->fz,
			layer->fsize
		);
	}
	else if(layer->fx>1 || layer->fy>1) {
		neurons_weights_reorder(layer, layer->fx, layer->fy, layer->fz, 0, 0);
	}

	return 0;
}



//============================================
// Load config files for all layers
//============================================

int Layer::load_config_files(void) {
	// Nothing to load by default
	return 0;
}

int LayerNeu::load_config_files(void) {

	// In case of missing config file, use random data
	// FIXME This should take into account special bounds of signed binary and ternary
	int z = 0;
	if(cfg_filename == nullptr) {
		printf("Warning: Layer %s%u: Missing config file, using random data\n", typenameu, typeidx);
		z = layer_loadcfg_or_random(this, neurons, fsize, wdata, 0, 0);
	}
	else {
		z = layer_loadcfg(this, neurons, fsize, 0, 0);
	}
	if(z != 0) return z;

	int errors_nb = 0;
	if(neu_wweight == 1 && (neu_sgnw & NEUSGN_SIGNED) != 0) {
		// Binary -1/+1
		errors_nb = array_check_data_bin_sym(cfg_data, neurons, 0, fsize);
	}
	else if(neu_wweight == 2 && (neu_sgnw & NEUSGN_SIGNED) != 0) {
		// Ternary, FIXME this interpretation of width and sign should be enabled in Network
		errors_nb = array_check_data_min_max(cfg_data, neurons, 0, fsize, -1, 1);
	}
	else {
		errors_nb = array_check_data_width(cfg_data, neurons, 0, fsize, neu_wweight, (neu_sgnw & NEUSGN_SIGNED) != 0);
	}
	if(errors_nb > 0) {
		printf("Warning: Layer %s%u: Some weights exceed the hardware capacity (%u values)\n", typenamel, typeidx, errors_nb);
	}

	// For sparsity stats, FIXME this should be isolated in a separate functionality ?
	unsigned long values_zeros_nb = 0;
	unsigned long values_total_nb = 0;

	for(unsigned n=0; n<neurons; n++) {
		for(unsigned f=0; f<fsize; f++) {
			values_zeros_nb += (cfg_data[n][f] == 0);
			values_total_nb += 1;
		}
	}

	printf("Layer %s%u : Sparsity : %f %%\n", typenameu, typeidx,
		100 * values_zeros_nb / (double)((values_total_nb > 0) ? values_total_nb : 1)
	);

	// Reorder weights, only if it is not generated random
	// Assume that the memory size for each neuron is large enough (corresponds to fsize)
	if(cfg_filename != nullptr) {
		z = neurons_weights_reorder_for_prev_layers(this);
		if(z != 0) return z;
	}

	return 0;
}

int LayerNeu_CM::load_config_files(void) {

	// In case of missing config file, use random data
	// FIXME This should take into account special bounds of signed binary and ternary
	int z = 0;
	if(cfg_filename == nullptr) {
		printf("Warning: Layer %s%u: Missing config file, using random data\n", typenameu, typeidx);
		z = layer_loadcfg_or_random(this, neurons, fsize, wdata, 0, 0);
	}
	else {
		z = layer_loadcfg(this, neurons, fsize, 0, 0);
	}
	if(z != 0) return z;

	int errors_nb = 0;
	if(neu_wweight == 1 && (neu_sgnw & NEUSGN_SIGNED) != 0) {
		// Binary -1/+1
		errors_nb = array_check_data_bin_sym(cfg_data, neurons, 0, fsize);
	}
	else if(neu_wweight == 2 && (neu_sgnw & NEUSGN_SIGNED) != 0) {
		// Ternary, FIXME this interpretation of width and sign should be enabled in Network
		errors_nb = array_check_data_min_max(cfg_data, neurons, 0, fsize, -1, 1);
	}
	else {
		errors_nb = array_check_data_width(cfg_data, neurons, 0, fsize, neu_wweight, (neu_sgnw & NEUSGN_SIGNED) != 0);
	}
	if(errors_nb > 0) {
		printf("Warning: Layer %s%u: Some weights exceed the hardware capacity (%u values)\n", typenamel, typeidx, errors_nb);
	}

	// For sparsity stats, FIXME this should be isolated in a separate functionality ?
	unsigned long values_zeros_nb = 0;
	unsigned long values_total_nb = 0;

	for(unsigned n=0; n<neurons; n++) {
		for(unsigned f=0; f<fsize; f++) {
			values_zeros_nb += (cfg_data[n][f] == 0);
			values_total_nb += 1;
		}
	}

	printf("Layer %s%u : Sparsity : %f %%\n", typenameu, typeidx,
		100 * values_zeros_nb / (double)((values_total_nb > 0) ? values_total_nb : 1)
	);
	
	// Reorder weights, only if it is not generated random
	// Assume that the memory size for each neuron is large enough (corresponds to fsize)
	if(cfg_filename != nullptr) {
		printf("Crash???\n");
		z = neurons_weights_reorder_for_prev_layers(this);
		if(z != 0) return z;
	}
	
	return 0;
}

int LayerNorm::load_config_files(void) {

	// Determine the position of bias and shr columns
	unsigned col_bias = 0;
	unsigned col_mul = 0;
	unsigned col_shr = 0;
	unsigned col_nb = 0;
	col_bias = col_nb; col_nb += (norm_wbias > 0) ? 1 : 0;
	col_mul  = col_nb; col_nb += (norm_wmul  > 0) ? 1 : 0;
	col_shr  = col_nb; col_nb += (norm_wshr  > 0) ? 1 : 0;
	if(col_nb == 0) return 0;

	// Actually load the data
	int z = layer_loadcfg_or_random(this, fsize, col_nb, wdata, 0, 0);
	if(z != 0) return 1;

	// In case of missing config file, random data is used, no checks needed
	if(cfg_filename==NULL) {
		return 0;
	}

	// Check width of loaded columns
	if(norm_wbias > 0) {
		int errors_nb = array_check_data_width(cfg_data, fsize, col_bias, col_bias+1, norm_wbias, sdata);
		if(errors_nb > 0) {
			printf("Warning: Layer %s%u: Some bias parameters exceed the hardware capacity (%u values)\n", typenamel, typeidx, errors_nb);
		}
	}
	if(norm_wmul > 0) {
		int errors_nb = array_check_data_width(cfg_data, fsize, col_mul, col_mul+1, norm_wmul, false);
		if(errors_nb > 0) {
			printf("Warning: Layer %s%u: Some mul parameters exceed the hardware capacity (%u values)\n", typenamel, typeidx, errors_nb);
		}
	}
	if(norm_wshr > 0) {
		int errors_nb = array_check_data_width(cfg_data, fsize, col_shr, col_shr+1, norm_wshr, false);
		if(errors_nb > 0) {
			printf("Warning: Layer %s%u: Some shr parameters exceed the hardware capacity (%u values)\n", typenamel, typeidx, errors_nb);
		}
	}

	return 0;
}

int LayerTernarize::load_config_files(void) {
	// Load the data
	int z = layer_loadcfg_or_random(this, fsize, 2, wdata, 0, 0);
	if(z != 0) return z;

	// Check the data
	int errors_nb = array_check_data_width(cfg_data, fsize, 0, 2, wdata, sdata);
	if(errors_nb > 0) {
		printf("Warning: Layer %s%u: Some threshold parameters exceed the hardware capacity (%u values)\n", typenamel, typeidx, errors_nb);
	}

	return errors_nb;
}

int LayerScatter::load_config_files(void) {

	// In case of missing config file, random data is used, no checks needed
	if(cfg_filename==NULL) {
		cfg_data = array_create_dim2(fsize, arr_layers.size());
		// Clear
		memset(cfg_data[0], 0, fsize * arr_layers.size() * sizeof(cfg_data[0][0]));
		// Initialize as many flags at 1 as necessary to match the successor fsize
		for(unsigned c=0; c<arr_layers.size(); c++) {
			Layer* layer_next = arr_layers[c];
			if(layer_next->fsize == fsize) {
				for(unsigned r=0; r<fsize; r++) cfg_data[r][c] = 1;
			}
			else {
				for(unsigned n=0; n<layer_next->fsize; ) {
					unsigned r = rand() % fsize;
					n += (cfg_data[r][c] == 0);
					cfg_data[r][c] = 1;
				}
			}
		}
		return 0;
	}

	// Actually load the data
	int z = layer_loadcfg(this, fsize, arr_layers.size(), 0, 0);
	if(z != 0) return 1;

	// Check width of loaded columns
	int errors_nb = array_check_data_width(cfg_data, fsize, 0, arr_layers.size(), 1, false);
	if(errors_nb > 0) {
		printf("Warning: Layer %s%u: Some flags are not 0-1 (%u flags)\n", typenamel, typeidx, errors_nb);
	}

	// Check the density of flags that are set against fsize
	errors_nb = 0;
	for(unsigned c=0; c<arr_layers.size(); c++) {
		Layer* layer_next = arr_layers[c];
		unsigned n = 0;
		for(unsigned r=0; r<fsize; r++) n += (cfg_data[r][c] != 0);
		if(n != layer_next->fsize) {
			printf("Error: Layer %s%u: Successor %u layer %s%u has fsize %u but there are %u config flags set\n",
				typenamel, typeidx, c, layer_next->typenamel, layer_next->typeidx, layer_next->fsize, n
			);
			errors_nb ++;
		}
	}
	if(errors_nb != 0) return 1;

	return 0;
}

int LayerGather::load_config_files(void) {

	// In case of missing config file, random data is used, no checks needed
	if(cfg_filename==NULL) {
		cfg_data = array_create_dim2(out_fsize, arr_layers.size());
		// Clear
		memset(cfg_data[0], 0, out_fsize * arr_layers.size() * sizeof(cfg_data[0][0]));
		// Initialize as many flags at 1 as necessary to match the successor fsize
		for(unsigned c=0; c<arr_layers.size(); c++) {
			Layer* layer_prev = arr_layers[c];
			if(layer_prev->out_fsize == out_fsize) {
				for(unsigned r=0; r<out_fsize; r++) cfg_data[r][c] = 1;
			}
			else {
				for(unsigned n=0; n<layer_prev->out_fsize; ) {
					unsigned r = rand() % out_fsize;
					n += (cfg_data[r][c] == 0);
					cfg_data[r][c] = 1;
				}
			}
		}
		return 0;
	}

	// Actually load the data
	int z = layer_loadcfg(this, out_fsize, arr_layers.size(), 0, 0);
	if(z != 0) return 1;

	// Check width of loaded columns
	int errors_nb = array_check_data_width(cfg_data, out_fsize, 0, arr_layers.size(), 1, false);
	if(errors_nb > 0) {
		printf("Warning: Layer %s%u: Some flags are not 0-1 (%u flags)\n", typenamel, typeidx, errors_nb);
	}

	// Ensure that there is at most one flag set per address
	errors_nb = 0;
	for(unsigned r=0; r<out_fsize; r++) {
		unsigned n = 0;
		for(unsigned c=0; c<arr_layers.size(); c++) n += (cfg_data[r][c] != 0);
		if(n > 1) {
			printf("Error: Layer %s%u: Config data has %u flags set for address %u\n", typenamel, typeidx, n, r);
			errors_nb ++;
		}
	}
	// Ensure that the density of flags corresponds to fsize of predecessors
	for(unsigned c=0; c<arr_layers.size(); c++) {
		Layer* layer_prev = arr_layers[c];
		unsigned n = 0;
		for(unsigned r=0; r<out_fsize; r++) n += (cfg_data[r][c] != 0);
		if(n != layer_prev->out_fsize) {
			printf("Error: Layer %s%u: Predecessor %u layer %s%u has out_fsize %u but there are %u config flags set\n",
				typenamel, typeidx, c, layer_prev->typenamel, layer_prev->typeidx, layer_prev->out_fsize, n
			);
			errors_nb ++;
		}
	}
	if(errors_nb != 0) return 1;

	return 0;
}

int Network::load_config_files(void) {
	int errors_nb = 0;

	printf("Loading config files for all layers\n");

	for(auto layer : layers) {
		if(layer->cfg_data != nullptr) continue;
		int res = layer->load_config_files();
		if(res != 0) errors_nb++;
	}

	return errors_nb;
}

