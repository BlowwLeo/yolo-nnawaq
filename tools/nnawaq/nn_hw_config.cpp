// Set internal layer configuration according to target hardware capabilities
// Select implementation style, distribute memory type usage for the layers that require it

extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>
#include <assert.h>

#include "nnawaq_utils.h"

}  // extern "C"

#include "nnawaq.h"
#include "nn_hwacc_config.h"
#include "nn_hw_config.h"
#include "hwacc_common.h"


//============================================
// Layer configuration
//============================================

static const char* neu_compr_filename_csv = "experimental_neu_weight_decomp_lut.csv";
static FILE* neu_compr_file_csv = nullptr;

void Layer::hwconfig_finalize(void) {
	// Nothing is done by default
}

void LayerWin::hwconfig_finalize(void) {

	// This variable is just to ease code refactoring
	layer_t* layer = this;

	Network* network = layer->network;

	// Apply global default implementation of mem style
	if(layer->mem.style == MemImplem::STYLE_NONE) {
		layer->mem.style = network->default_mem_implem_win;
	}

	// This component is special in the sense that its support for PAR_IN > 1 is a hack : it is a scaling factor for actual data width
	if(layer->split_in > 1) {
		if(layer->win_par_oz == 0) layer->win_par_oz = 1;
		// Some fields must be multiples of other fields
		unsigned errors_nb = 0;
		if(layer->win_par_oz % layer->split_in != 0) {
			printf("Error %s%u: PAR_OZ=%u is not a multiple of PAR_IN=%u\n", layer->typenameu, layer->typeidx, layer->win_par_oz, layer->split_in);
			errors_nb++;
		}
		if(layer->split_out % layer->win_par_oz != 0) {
			printf("Error %s%u: PAR_OUT=%u is not a multiple of PAR_OZ=%u\n", layer->typenameu, layer->typeidx, layer->split_out, layer->win_par_oz);
			errors_nb++;
		}
		if(layer->fz % layer->win_par_oz != 0) {
			printf("Error %s%u: in_fz=%u is not a multiple of PAR_OZ=%u\n", layer->typenameu, layer->typeidx, layer->fz, layer->win_par_oz);
			errors_nb++;
		}
		if(layer->out_fz % layer->win_par_oz != 0) {
			printf("Error %s%u: out_fz=%u is not a multiple of PAR_OZ=%u\n", layer->typenameu, layer->typeidx, layer->out_fz, layer->win_par_oz);
			errors_nb++;
		}
		if(errors_nb != 0) {
			printf("Error %s%u: Errors found with PAR_IN=%u PAR_OUT=%u PAR_OZ=%u in_fz=%u out_fz=%u\n", layer->typenameu, layer->typeidx,
				layer->split_in, layer->split_out, layer->win_par_oz, layer->fz, layer->out_fz
			);
			exit(EXIT_FAILURE);
		}
		// The non-PAR_OZ factor of PAR_OUT must be an appropriate divisor of the window size
		// FIXME When in ZFIRST mode, we can have multiple concurrent reads within FZ
		unsigned par_win = layer->split_out / layer->win_par_oz;
		if(
			(par_win <= layer->winx && layer->winx % par_win != 0) ||
			(par_win >= layer->winx && par_win % layer->winx != 0) ||
			(par_win >= layer->winx && (layer->winy % (par_win / layer->winx) != 0))
		) {
			printf("Error %s%u: PAR_WIN=%u win=%ux%u\n", layer->typenameu, layer->typeidx, par_win, layer->winx, layer->winy);
			exit(EXIT_FAILURE);
		}
	}

}

void LayerNeu::hwconfig_finalize(void) {

	// This variable is just to ease code refactoring
	layer_t* layer = this;

	Network* network = layer->network;

	unsigned fsize = (layer->fsize + layer->split_in - 1) / layer->split_in;
	unsigned nbneu = (layer->neurons_max + layer->split_out - 1) / layer->split_out;

	if(layer->neu_wweight <= 0) {
		printf("Error: layer %s%u: Weight width %u is not handled\n", layer->typenameu, layer->typeidx, layer->neu_wweight);
		exit(EXIT_FAILURE);
	}

	// FIXME In some cases it would be better to use rounding to upper power of 2, for the wweight
	unsigned ww = layer->neu_wweight;

	// FIXME Select the neuron implementation style
	layer->neu_style = 1;
	if((layer->neu_sgnd & NEUSGN_LOCKED) == 0) layer->neu_style = 0;
	if(network->hwconfig_neu_style >= 0) layer->neu_style = network->hwconfig_neu_style;

	// Force implem style for non-ternary weight
	if(layer->neu_wweight != 2) layer->neu_style = 2;
	if((layer->neu_sgnw & NEUSGN_LOCKED) == 0 || (layer->neu_sgnw & NEUSGN_SIGNED) == 0) layer->neu_style = 2;
	if(layer->win_dwconv == true) layer->neu_style = 2;
	if(layer->neu_time_mux > 1) layer->neu_style = 2;

	// Force implem style for ASIC mode
	// FIXME This asicmode is obsolete
	if(network->hwconfig_asicmode == true) layer->neu_style = 2;

	// Decide if using weight compression
	// Allow changing neuron layer style if possible
	if(layer->neu_comp_style > 0) {
		// Compression is already set, don't change
	}
	else if(network->default_comp_all_style > 0) {
		layer->neu_comp_style = network->default_comp_all_style;
		layer->neu_comp_nraw  = network->default_comp_all_nraw;
		layer->neu_comp_nbin  = network->default_comp_all_nbin;
	}
	else if(layer->mem.style == MemImplem::STYLE_BRAM && network->default_comp_bram_style > 0) {
		layer->neu_comp_style = network->default_comp_bram_style;
		layer->neu_comp_nraw  = network->default_comp_bram_nraw;
		layer->neu_comp_nbin  = network->default_comp_bram_nbin;
	}
	else if(layer->nbframes == 1 && network->default_comp_fc_style > 0) {
		layer->neu_comp_style = network->default_comp_fc_style;
		layer->neu_comp_nraw  = network->default_comp_fc_nraw;
		layer->neu_comp_nbin  = network->default_comp_fc_nbin;
	}
	// Cancel ternary compression if the weight is not ternary
	if(layer->neu_comp_style >= 2 && layer->neu_comp_style <= 5) {
		if(layer->neu_wweight != 2 || (layer->neu_sgnw & NEUSGN_SIGNED) == 0) {
			layer->neu_comp_style = 0;
		}
	}

	// Fix variables if there is no compression
	if(layer->neu_comp_style == 0) {
		layer->neu_comp_nraw = 1;
		layer->neu_comp_nbin = layer->neu_wweight;
	}
	// Promote the neuron layer style if compression is enabled
	else {
		layer->neu_style = 2;
	}

	if(layer->neu_style != 0) {
		if((layer->neu_sgnd & NEUSGN_LOCKED) == 0) {
			printf("Error: layer %s%u: Want style %u but incompatible with data signedness\n", layer->typenameu, layer->typeidx, layer->neu_style);
			exit(EXIT_FAILURE);
		}
	}

	unsigned waccu = layer->wdata;
	if(layer->neu_style==0) {
		waccu = layer->wdata + uint_bitsnb(fsize - 1);
		if(layer->split_in > 1) waccu = layer->wdata + uint_bitsnb(fsize);
		if(layer->wdata > 1) waccu--;
	}

	unsigned bram_size = 0;
	unsigned nperblk = 0;  // Weights on read side of one block
	unsigned wrnb = 0;

	unsigned luts_per_neu = 0;

	unsigned neu_per_blk = 1;
	unsigned blk_per_neu = 1;

	// Compute the number of LUTs per neuron
	if(layer->neu_style==0) luts_per_neu = waccu + (waccu + 1) / 2 + 1;
	else {
		luts_per_neu = layer->wdata + (layer->wdata + 1) / 2;
		unsigned nb_ternmult = layer->split_in;
		luts_per_neu += nb_ternmult;
		unsigned waccu_ternmult = 2;
		while(nb_ternmult > 1) {
			nb_ternmult = (nb_ternmult + 1) / 2;
			luts_per_neu += nb_ternmult * waccu_ternmult;
			waccu_ternmult++;
		}
	}
	//printf("Info: layer %s%u: %u lut/neu\n", layer->typenameu, layer->typeidx, luts_per_neu);

	// Apply global default implementation of mem style
	if(layer->mem.style == MemImplem::STYLE_NONE) {
		layer->mem.style = network->default_mem_implem_neu;
	}
	// Neuron style 0 must use bram
	if(layer->neu_style == 0) {
		layer->mem.style = MemImplem::STYLE_BRAM;
	}

	// Memory implem decision in specific cases
	if(layer->mem.style == MemImplem::STYLE_NONE || layer->mem.style == MemImplem::STYLE_AUTO) {
		// Compute the effective memory depth
		unsigned mem_depth = fsize;
		if(layer->neu_time_mux > 1) mem_depth *= layer->neu_time_mux;
		// Hardcoded implementation cases, FIXME no conflict check
		if(layer->neu_style == 0) {
			layer->mem.style = MemImplem::STYLE_BRAM;
		}
		else if(network->hwconfig_asicmode==true) {
			layer->mem.style = MemImplem::STYLE_LUTRAM;
		}
		// Possible implementations for style 1 (style 2 has decision later)
		else if(layer->neu_style == 1 && mem_depth <= 1) {
			layer->mem.style = MemImplem::STYLE_REG;
		}
		else if(layer->neu_style == 1 && mem_depth <= network->hwconfig_lut_threshold) {
			layer->mem.style = MemImplem::STYLE_LUTRAM;
		}
		else if(layer->neu_style == 1) {
			layer->mem.style = MemImplem::STYLE_BRAM;
		}
	}

	if(layer->mem.style == MemImplem::STYLE_LUTRAM) {

		// Get max LUTRAM width achievable with current interface
		unsigned ifw = (1 << HwAcc_Common::memreg_ifw->bits) * 8;
		if(ifw > network->hwconfig_writewidth) ifw = network->hwconfig_writewidth;
		nperblk = ifw / ww;
		unsigned max_nperblk = (1 << regfield_nperblk->bits);
		if(nperblk > max_nperblk) nperblk = max_nperblk;
		wrnb = 1;

		// Get the corresponding distribution of neurons
		neu_per_blk = nperblk / layer->split_in;
		if(neu_per_blk==0) neu_per_blk = 1;
		blk_per_neu = (layer->split_in + nperblk - 1) / nperblk;

		// Adjust LUTRAM width to best fit needs
		if(blk_per_neu > 1) nperblk = (layer->split_in + blk_per_neu - 1) / blk_per_neu;
		else nperblk = neu_per_blk * layer->split_in;

	}  // Using LUTRAM

	// If using BRAM, select the best configuration
	// FIXME Handle weight compression ratio in decision

	// First, try to fit neurons in BRAM18
	if(layer->neu_style != 2 && layer->mem.style == MemImplem::STYLE_BRAM && bram_size==0) {

		const unsigned arrwidth18d[] = {  1,  1,  2,  2,  4,  4,  8, 16, 32 };  // Depth : to multiply by 512
		const unsigned arrwidth18r[] = { 36, 32, 18, 16,  9,  8,  4,  2,  1 };  // Read width
		const unsigned arrwidth18w[] = { 36, 32, 36, 32, 36, 32, 32, 32, 32 };  // Write width
		const unsigned arrwidth18_nb = 9;

		for(unsigned i=0; i<arrwidth18_nb; i++) {
			unsigned depth = arrwidth18d[i] * 512;
			unsigned rdw   = arrwidth18r[i];
			unsigned wrw   = arrwidth18w[i];

			// Checks
			if(wrw > network->hwconfig_writewidth) wrw = network->hwconfig_writewidth;
			if(rdw > wrw) continue;
			if(rdw % ww != 0) continue;
			if(fsize > depth) continue;
			nperblk = rdw / ww;

			// If there are much more neurons at this layer, prefer BRAM36k
			// Because there is more control logic shared, and write time is lower
			if(layer->neu_style==0) {
				if(nbneu > 2 * nperblk) continue;
				// Check the amount of logic around the BRAM
				if(network->hwconfig_luts_bram_ratio > 0) {
					unsigned n = nbneu > nperblk ? nperblk : nbneu;
					if(n * luts_per_neu > network->hwconfig_luts_per_bram18 * network->hwconfig_luts_bram_ratio) continue;
				}
			}
			else {
				neu_per_blk = nperblk / layer->split_in;
				if(neu_per_blk==0) neu_per_blk = 1;
				blk_per_neu = (layer->split_in + nperblk - 1) / nperblk;
				if(network->hwconfig_luts_bram_ratio > 0) {
					double lut_per_block = (double)neu_per_blk * luts_per_neu / blk_per_neu;
					if(lut_per_block > network->hwconfig_luts_per_bram18 * network->hwconfig_luts_bram_ratio) continue;
				}
			}

			// Keep this configuration
			bram_size = 18;
			wrnb = wrw / (nperblk * ww);
			wrnb = uint_rndpow2_floor(wrnb);
			break;
		}  // Scan BRAM18 configs

	}  // Try BRAM18 storage

	// If BRAM18 was not selected, try to fit neurons in BRAM36
	if(layer->neu_style != 2 && layer->mem.style == MemImplem::STYLE_BRAM && bram_size==0) {

		const unsigned arrwidth36d[] = {  1,  1,  2,  2,  4,  4,  8,  8, 16, 32, 64 };  // Depth : to multiply by 512
		const unsigned arrwidth36r[] = { 72, 64, 36, 32, 18, 16,  9,  8,  4,  2,  1 };  // Read width
		const unsigned arrwidth36w[] = { 72, 64, 72, 64, 72, 64, 72, 64, 64, 64, 64 };  // Write width
		const unsigned arrwidth36_nb = 11;

		for(unsigned i=0; i<arrwidth36_nb; i++) {
			unsigned depth = arrwidth36d[i] * 512;
			unsigned rdw   = arrwidth36r[i];
			unsigned wrw   = arrwidth36w[i];

			// Checks
			if(wrw > network->hwconfig_writewidth) wrw = network->hwconfig_writewidth;
			if(rdw > wrw) continue;
			if(rdw % ww != 0) continue;
			if(fsize > depth) continue;
			nperblk = rdw / ww;

			// Check the amount of logic around the BRAM
			// Skip this check for the last tested config
			if(layer->neu_style==0) {
				if(network->hwconfig_luts_bram_ratio > 0 && i < arrwidth36_nb-1) {
					unsigned n = nbneu > nperblk ? nperblk : nbneu;
					if(n * luts_per_neu > network->hwconfig_luts_per_bram18 * 2 * network->hwconfig_luts_bram_ratio) continue;
				}
			}
			else {
				neu_per_blk = nperblk / layer->split_in;
				if(neu_per_blk==0) neu_per_blk = 1;
				blk_per_neu = (layer->split_in + nperblk - 1) / nperblk;
				if(network->hwconfig_luts_bram_ratio > 0 && i < arrwidth36_nb-1) {
					double lut_per_block = (double)neu_per_blk * luts_per_neu / blk_per_neu;
					if(lut_per_block > network->hwconfig_luts_per_bram18 * 2 * network->hwconfig_luts_bram_ratio) continue;
				}
			}

			// Keep this configuration
			bram_size = 36;
			wrnb = wrw / (nperblk * ww);
			wrnb = uint_rndpow2_floor(wrnb);
			break;

		}  // Scan BRAM36 configs

	}  // Try BRAM36 storage

	// Sanity check
	if(layer->neu_style != 2 && layer->mem.style == MemImplem::STYLE_BRAM && bram_size==0) {
		printf("Error: layer %s%u: fsize %u does not fit in BRAM36\n", layer->typenameu, layer->typeidx, fsize);
		exit(EXIT_FAILURE);
	}

	// Save dimensions of memories, for styles 0 and 1

	if(layer->neu_style == 0) {
		layer->mem.num   = layer->split_in * layer->split_out;
		layer->mem.lines = fsize;
		layer->mem.width = nperblk * ww;
	}
	else if(layer->neu_style == 1) {
		layer->mem.num   = layer->split_out * ((nbneu + neu_per_blk - 1) / neu_per_blk) * blk_per_neu;
		layer->mem.lines = fsize;
		layer->mem.width = layer->split_in * neu_per_blk * ww;
	}

	// Save dimensions of memories, for style 2 (packed weights)
	// Also handle time multiplexing and weight compression

	if(layer->neu_style == 2) {

		// Memory geometry
		unsigned nbweights_parallel = layer->split_out * nbneu * layer->split_in;
		unsigned mem_depth = fsize;
		if(layer->neu_time_mux > 1) {
			mem_depth *= layer->neu_time_mux;
			nbweights_parallel /= layer->neu_time_mux;
		}
		unsigned mem_width = nbweights_parallel * ww;

		// Select the write width, the max permitted by the data interface width
		// Here the destination field "neu_per_bram" indicates the size of the write channel, in number of weight values
		unsigned ifw = (1 << HwAcc_Common::memreg_ifw->bits) * 8;
		if(ifw > network->hwconfig_writewidth) ifw = network->hwconfig_writewidth;
		nperblk = ifw / ww;
		unsigned max_nperblk = (1 << regfield_nperblk->bits);
		if(nperblk > max_nperblk) nperblk = max_nperblk;

		// Handle compression : Round to upper multiple of number of compressed weights
		if(layer->neu_comp_style != 0 && layer->neu_comp_nraw > 1) {
			// Memory read width
			mem_width = ((nbweights_parallel + layer->neu_comp_nraw - 1) / layer->neu_comp_nraw) * layer->neu_comp_nbin;
			// Write side
			nperblk = network->hwconfig_writewidth / (layer->neu_comp_nraw * ww) * layer->neu_comp_nraw;
			if(nperblk > max_nperblk) nperblk = (max_nperblk / layer->neu_comp_nraw) * layer->neu_comp_nraw;
		}

		// Only one read address is written at a time
		wrnb = 1;
		// This field is not used
		waccu = 0;

		// Save memory properties in Layer
		this->mem.num   = 1;
		this->mem.lines = mem_depth;
		this->mem.width = mem_width;

		// Experimental :
		// Evaluate the size of decompressors for arbitrary LUT-based decompression
		if(layer->neu_comp_style > 5 && layer->neu_comp_nraw > 1) {

			if(neu_compr_file_csv == nullptr) {
				neu_compr_file_csv = fopen(neu_compr_filename_csv, "wb");
				if(neu_compr_file_csv != nullptr) {
					fprintf(neu_compr_file_csv, "layer_win win layer_neu num_decomp lines width total_bits total_blocks mem_type\n");
				}
			}

			MemImplem mem;
			mem.lines = (1 << layer->neu_comp_nbin);
			mem.width = layer->neu_comp_nraw * neu_wweight;
			mem.num   = (nbweights_parallel + layer->neu_comp_nraw - 1) / layer->neu_comp_nraw;
			mem.EvalBlocks(network->hwconfig_lut_threshold, network->hwconfig_use_uram);

			printf("EXPERIMENTAL : layer %s%u : decompressors : num %u, lines %u, width %u, total %lu %s\n",
				layer->typenameu, layer->typeidx,
				mem.num, mem.lines, mem.width,
				mem.blocks, mem.GetStyleName()
			);
			if(neu_compr_file_csv != nullptr) {
				Layer* layer_win = layer->prev;
				while(layer_win != nullptr && layer_win->type == LAYER_FIFO) layer_win = layer_win->prev;
				if(layer_win != nullptr && layer_win->type == LAYER_WIN) {
					fprintf(neu_compr_file_csv, "%s%u %ux%u ",
						layer_win->typenamel, layer_win->typeidx,
						layer_win->winx, layer_win->winy
					);
				}
				fprintf(neu_compr_file_csv, "%s%u %u %u %u %lu %lu %s\n",
					layer->typenamel, layer->typeidx,
					mem.num, mem.lines, mem.width,
					mem.EvalSizeTotal(), mem.blocks, mem.GetStyleName()
				);
			}
		}

	}  // Layer style 2

	layer->neu_per_bram = nperblk;
	layer->neu_wrnb     = wrnb;
	layer->neu_waccu    = waccu;

	// DWConv example :
	// Consider the case with no user-specified time multiplexing
	//   Win FZ = 30, PAR_OZ = 6
	//   Neurons = 30 logical, 6 physical, TMUX = 30/6=5
	// Now, apply extra time multiplexing to slow these layers down => apply extra factor 3x
	//   Win FZ = 30, PAR_OZ = 2
	//   Neurons = 30 logical, 2 physical, overall TMUX = 30/5*3=15
	unsigned dwconv_tmux = 1;
	if(win_dwconv == true) dwconv_tmux = fz / win_par_oz;

	// Note : maybe these stats should be exported to network with a dedicated Layer method

	network->total_neurons     += nbneu * layer->split_out / dwconv_tmux;
	network->total_neurons_phy += nbneu * layer->split_out / dwconv_tmux / layer->neu_time_mux;
	network->total_multipliers += nbneu * layer->split_out / dwconv_tmux / layer->neu_time_mux * layer->split_in;
	network->total_weights     += nbneu * layer->split_out * fsize * layer->split_in;
	network->total_weight_bits += nbneu * layer->split_out * fsize * layer->split_in * ww;

	unsigned long macs = 0;
	if(win_dwconv == true) {
		// No need to multiply by the number of neurons, this is already accounted for in nbframes
		macs = layer->nbframes * fsize * layer->split_in;
	}
	else {
		macs = layer->nbframes * nbneu * layer->split_out * fsize * layer->split_in;
	}

	//printf("Info: layer %s%u: MACs %lu\n", layer->typenameu, layer->typeidx, macs);
	network->total_macs += macs;

}
void LayerNeu_CM::hwconfig_finalize(void) {

	// This variable is just to ease code refactoring
	layer_t* layer = this;

	Network* network = layer->network;

	unsigned fsize = (layer->fsize + layer->split_in - 1) / layer->split_in;
	unsigned nbneu = (layer->neurons_max + layer->split_out - 1) / layer->split_out;

	if(layer->neu_wweight <= 0) {
		printf("Error: layer %s%u: Weight width %u is not handled\n", layer->typenameu, layer->typeidx, layer->neu_wweight);
		exit(EXIT_FAILURE);
	}

	// FIXME In some cases it would be better to use rounding to upper power of 2, for the wweight
	unsigned ww = layer->neu_wweight;

	// FIXME Select the neuron implementation style
	layer->neu_style = 1;
	if((layer->neu_sgnd & NEUSGN_LOCKED) == 0) layer->neu_style = 0;
	if(network->hwconfig_neu_style >= 0) layer->neu_style = network->hwconfig_neu_style;

	// Force implem style for non-ternary weight
	if(layer->neu_wweight != 2) layer->neu_style = 2;
	if((layer->neu_sgnw & NEUSGN_LOCKED) == 0 || (layer->neu_sgnw & NEUSGN_SIGNED) == 0) layer->neu_style = 2;
	if(layer->win_dwconv == true) layer->neu_style = 2;
	if(layer->neu_time_mux > 1) layer->neu_style = 2;

	// Force implem style for ASIC mode
	// FIXME This asicmode is obsolete
	if(network->hwconfig_asicmode == true) layer->neu_style = 2;

	// Decide if using weight compression
	// Allow changing neuron layer style if possible
	if(layer->neu_comp_style > 0) {
		// Compression is already set, don't change
	}
	else if(network->default_comp_all_style > 0) {
		layer->neu_comp_style = network->default_comp_all_style;
		layer->neu_comp_nraw  = network->default_comp_all_nraw;
		layer->neu_comp_nbin  = network->default_comp_all_nbin;
	}
	else if(layer->mem.style == MemImplem::STYLE_BRAM && network->default_comp_bram_style > 0) {
		layer->neu_comp_style = network->default_comp_bram_style;
		layer->neu_comp_nraw  = network->default_comp_bram_nraw;
		layer->neu_comp_nbin  = network->default_comp_bram_nbin;
	}
	else if(layer->nbframes == 1 && network->default_comp_fc_style > 0) {
		layer->neu_comp_style = network->default_comp_fc_style;
		layer->neu_comp_nraw  = network->default_comp_fc_nraw;
		layer->neu_comp_nbin  = network->default_comp_fc_nbin;
	}
	// Cancel ternary compression if the weight is not ternary
	if(layer->neu_comp_style >= 2 && layer->neu_comp_style <= 5) {
		if(layer->neu_wweight != 2 || (layer->neu_sgnw & NEUSGN_SIGNED) == 0) {
			layer->neu_comp_style = 0;
		}
	}

	// Fix variables if there is no compression
	if(layer->neu_comp_style == 0) {
		layer->neu_comp_nraw = 1;
		layer->neu_comp_nbin = layer->neu_wweight;
	}
	// Promote the neuron layer style if compression is enabled
	else {
		layer->neu_style = 2;
	}

	if(layer->neu_style != 0) {
		if((layer->neu_sgnd & NEUSGN_LOCKED) == 0) {
			printf("Error: layer %s%u: Want style %u but incompatible with data signedness\n", layer->typenameu, layer->typeidx, layer->neu_style);
			exit(EXIT_FAILURE);
		}
	}

	unsigned waccu = layer->wdata;
	if(layer->neu_style==0) {
		waccu = layer->wdata + uint_bitsnb(fsize - 1);
		if(layer->split_in > 1) waccu = layer->wdata + uint_bitsnb(fsize);
		if(layer->wdata > 1) waccu--;
	}

	unsigned bram_size = 0;
	unsigned nperblk = 0;  // Weights on read side of one block
	unsigned wrnb = 0;

	unsigned luts_per_neu = 0;

	unsigned neu_per_blk = 1;
	unsigned blk_per_neu = 1;

	// Compute the number of LUTs per neuron
	if(layer->neu_style==0) luts_per_neu = waccu + (waccu + 1) / 2 + 1;
	else {
		luts_per_neu = layer->wdata + (layer->wdata + 1) / 2;
		unsigned nb_ternmult = layer->split_in;
		luts_per_neu += nb_ternmult;
		unsigned waccu_ternmult = 2;
		while(nb_ternmult > 1) {
			nb_ternmult = (nb_ternmult + 1) / 2;
			luts_per_neu += nb_ternmult * waccu_ternmult;
			waccu_ternmult++;
		}
	}
	//printf("Info: layer %s%u: %u lut/neu\n", layer->typenameu, layer->typeidx, luts_per_neu);

	// Apply global default implementation of mem style
	if(layer->mem.style == MemImplem::STYLE_NONE) {
		layer->mem.style = network->default_mem_implem_neu;
	}
	// Neuron style 0 must use bram
	if(layer->neu_style == 0) {
		layer->mem.style = MemImplem::STYLE_BRAM;
	}

	// Memory implem decision in specific cases
	if(layer->mem.style == MemImplem::STYLE_NONE || layer->mem.style == MemImplem::STYLE_AUTO) {
		// Compute the effective memory depth
		unsigned mem_depth = fsize;
		if(layer->neu_time_mux > 1) mem_depth *= layer->neu_time_mux;
		// Hardcoded implementation cases, FIXME no conflict check
		if(layer->neu_style == 0) {
			layer->mem.style = MemImplem::STYLE_BRAM;
		}
		else if(network->hwconfig_asicmode==true) {
			layer->mem.style = MemImplem::STYLE_LUTRAM;
		}
		// Possible implementations for style 1 (style 2 has decision later)
		else if(layer->neu_style == 1 && mem_depth <= 1) {
			layer->mem.style = MemImplem::STYLE_REG;
		}
		else if(layer->neu_style == 1 && mem_depth <= network->hwconfig_lut_threshold) {
			layer->mem.style = MemImplem::STYLE_LUTRAM;
		}
		else if(layer->neu_style == 1) {
			layer->mem.style = MemImplem::STYLE_BRAM;
		}
	}

	if(layer->mem.style == MemImplem::STYLE_LUTRAM) {

		// Get max LUTRAM width achievable with current interface
		unsigned ifw = (1 << HwAcc_Common::memreg_ifw->bits) * 8;
		if(ifw > network->hwconfig_writewidth) ifw = network->hwconfig_writewidth;
		nperblk = ifw / ww;
		unsigned max_nperblk = (1 << regfield_nperblk->bits);
		if(nperblk > max_nperblk) nperblk = max_nperblk;
		wrnb = 1;

		// Get the corresponding distribution of neurons
		neu_per_blk = nperblk / layer->split_in;
		if(neu_per_blk==0) neu_per_blk = 1;
		blk_per_neu = (layer->split_in + nperblk - 1) / nperblk;

		// Adjust LUTRAM width to best fit needs
		if(blk_per_neu > 1) nperblk = (layer->split_in + blk_per_neu - 1) / blk_per_neu;
		else nperblk = neu_per_blk * layer->split_in;

	}  // Using LUTRAM

	// If using BRAM, select the best configuration
	// FIXME Handle weight compression ratio in decision

	// First, try to fit neurons in BRAM18
	if(layer->neu_style != 2 && layer->mem.style == MemImplem::STYLE_BRAM && bram_size==0) {

		const unsigned arrwidth18d[] = {  1,  1,  2,  2,  4,  4,  8, 16, 32 };  // Depth : to multiply by 512
		const unsigned arrwidth18r[] = { 36, 32, 18, 16,  9,  8,  4,  2,  1 };  // Read width
		const unsigned arrwidth18w[] = { 36, 32, 36, 32, 36, 32, 32, 32, 32 };  // Write width
		const unsigned arrwidth18_nb = 9;

		for(unsigned i=0; i<arrwidth18_nb; i++) {
			unsigned depth = arrwidth18d[i] * 512;
			unsigned rdw   = arrwidth18r[i];
			unsigned wrw   = arrwidth18w[i];

			// Checks
			if(wrw > network->hwconfig_writewidth) wrw = network->hwconfig_writewidth;
			if(rdw > wrw) continue;
			if(rdw % ww != 0) continue;
			if(fsize > depth) continue;
			nperblk = rdw / ww;

			// If there are much more neurons at this layer, prefer BRAM36k
			// Because there is more control logic shared, and write time is lower
			if(layer->neu_style==0) {
				if(nbneu > 2 * nperblk) continue;
				// Check the amount of logic around the BRAM
				if(network->hwconfig_luts_bram_ratio > 0) {
					unsigned n = nbneu > nperblk ? nperblk : nbneu;
					if(n * luts_per_neu > network->hwconfig_luts_per_bram18 * network->hwconfig_luts_bram_ratio) continue;
				}
			}
			else {
				neu_per_blk = nperblk / layer->split_in;
				if(neu_per_blk==0) neu_per_blk = 1;
				blk_per_neu = (layer->split_in + nperblk - 1) / nperblk;
				if(network->hwconfig_luts_bram_ratio > 0) {
					double lut_per_block = (double)neu_per_blk * luts_per_neu / blk_per_neu;
					if(lut_per_block > network->hwconfig_luts_per_bram18 * network->hwconfig_luts_bram_ratio) continue;
				}
			}

			// Keep this configuration
			bram_size = 18;
			wrnb = wrw / (nperblk * ww);
			wrnb = uint_rndpow2_floor(wrnb);
			break;
		}  // Scan BRAM18 configs

	}  // Try BRAM18 storage

	// If BRAM18 was not selected, try to fit neurons in BRAM36
	if(layer->neu_style != 2 && layer->mem.style == MemImplem::STYLE_BRAM && bram_size==0) {

		const unsigned arrwidth36d[] = {  1,  1,  2,  2,  4,  4,  8,  8, 16, 32, 64 };  // Depth : to multiply by 512
		const unsigned arrwidth36r[] = { 72, 64, 36, 32, 18, 16,  9,  8,  4,  2,  1 };  // Read width
		const unsigned arrwidth36w[] = { 72, 64, 72, 64, 72, 64, 72, 64, 64, 64, 64 };  // Write width
		const unsigned arrwidth36_nb = 11;

		for(unsigned i=0; i<arrwidth36_nb; i++) {
			unsigned depth = arrwidth36d[i] * 512;
			unsigned rdw   = arrwidth36r[i];
			unsigned wrw   = arrwidth36w[i];

			// Checks
			if(wrw > network->hwconfig_writewidth) wrw = network->hwconfig_writewidth;
			if(rdw > wrw) continue;
			if(rdw % ww != 0) continue;
			if(fsize > depth) continue;
			nperblk = rdw / ww;

			// Check the amount of logic around the BRAM
			// Skip this check for the last tested config
			if(layer->neu_style==0) {
				if(network->hwconfig_luts_bram_ratio > 0 && i < arrwidth36_nb-1) {
					unsigned n = nbneu > nperblk ? nperblk : nbneu;
					if(n * luts_per_neu > network->hwconfig_luts_per_bram18 * 2 * network->hwconfig_luts_bram_ratio) continue;
				}
			}
			else {
				neu_per_blk = nperblk / layer->split_in;
				if(neu_per_blk==0) neu_per_blk = 1;
				blk_per_neu = (layer->split_in + nperblk - 1) / nperblk;
				if(network->hwconfig_luts_bram_ratio > 0 && i < arrwidth36_nb-1) {
					double lut_per_block = (double)neu_per_blk * luts_per_neu / blk_per_neu;
					if(lut_per_block > network->hwconfig_luts_per_bram18 * 2 * network->hwconfig_luts_bram_ratio) continue;
				}
			}

			// Keep this configuration
			bram_size = 36;
			wrnb = wrw / (nperblk * ww);
			wrnb = uint_rndpow2_floor(wrnb);
			break;

		}  // Scan BRAM36 configs

	}  // Try BRAM36 storage

	// Sanity check
	if(layer->neu_style != 2 && layer->mem.style == MemImplem::STYLE_BRAM && bram_size==0) {
		printf("Error: layer %s%u: fsize %u does not fit in BRAM36\n", layer->typenameu, layer->typeidx, fsize);
		exit(EXIT_FAILURE);
	}

	// Save dimensions of memories, for styles 0 and 1

	if(layer->neu_style == 0) {
		layer->mem.num   = layer->split_in * layer->split_out;
		layer->mem.lines = fsize;
		layer->mem.width = nperblk * ww;
	}
	else if(layer->neu_style == 1) {
		layer->mem.num   = layer->split_out * ((nbneu + neu_per_blk - 1) / neu_per_blk) * blk_per_neu;
		layer->mem.lines = fsize;
		layer->mem.width = layer->split_in * neu_per_blk * ww;
	}

	// Save dimensions of memories, for style 2 (packed weights)
	// Also handle time multiplexing and weight compression

	if(layer->neu_style == 2) {

		// Memory geometry
		unsigned nbweights_parallel = layer->split_out * nbneu * layer->split_in;
		unsigned mem_depth = fsize;
		if(layer->neu_time_mux > 1) {
			mem_depth *= layer->neu_time_mux;
			nbweights_parallel /= layer->neu_time_mux;
		}
		unsigned mem_width = nbweights_parallel * ww;

		// Select the write width, the max permitted by the data interface width
		// Here the destination field "neu_per_bram" indicates the size of the write channel, in number of weight values
		unsigned ifw = (1 << HwAcc_Common::memreg_ifw->bits) * 8;
		if(ifw > network->hwconfig_writewidth) ifw = network->hwconfig_writewidth;
		nperblk = ifw / ww;
		unsigned max_nperblk = (1 << regfield_nperblk->bits);
		if(nperblk > max_nperblk) nperblk = max_nperblk;

		// Handle compression : Round to upper multiple of number of compressed weights
		if(layer->neu_comp_style != 0 && layer->neu_comp_nraw > 1) {
			// Memory read width
			mem_width = ((nbweights_parallel + layer->neu_comp_nraw - 1) / layer->neu_comp_nraw) * layer->neu_comp_nbin;
			// Write side
			nperblk = network->hwconfig_writewidth / (layer->neu_comp_nraw * ww) * layer->neu_comp_nraw;
			if(nperblk > max_nperblk) nperblk = (max_nperblk / layer->neu_comp_nraw) * layer->neu_comp_nraw;
		}

		// Only one read address is written at a time
		wrnb = 1;
		// This field is not used
		waccu = 0;

		// Save memory properties in Layer
		this->mem.num   = 1;
		this->mem.lines = mem_depth;
		this->mem.width = mem_width;

		// Experimental :
		// Evaluate the size of decompressors for arbitrary LUT-based decompression
		if(layer->neu_comp_style > 5 && layer->neu_comp_nraw > 1) {

			if(neu_compr_file_csv == nullptr) {
				neu_compr_file_csv = fopen(neu_compr_filename_csv, "wb");
				if(neu_compr_file_csv != nullptr) {
					fprintf(neu_compr_file_csv, "layer_win win layer_neu num_decomp lines width total_bits total_blocks mem_type\n");
				}
			}

			MemImplem mem;
			mem.lines = (1 << layer->neu_comp_nbin);
			mem.width = layer->neu_comp_nraw * neu_wweight;
			mem.num   = (nbweights_parallel + layer->neu_comp_nraw - 1) / layer->neu_comp_nraw;
			mem.EvalBlocks(network->hwconfig_lut_threshold, network->hwconfig_use_uram);

			printf("EXPERIMENTAL : layer %s%u : decompressors : num %u, lines %u, width %u, total %lu %s\n",
				layer->typenameu, layer->typeidx,
				mem.num, mem.lines, mem.width,
				mem.blocks, mem.GetStyleName()
			);
			if(neu_compr_file_csv != nullptr) {
				Layer* layer_win = layer->prev;
				while(layer_win != nullptr && layer_win->type == LAYER_FIFO) layer_win = layer_win->prev;
				if(layer_win != nullptr && layer_win->type == LAYER_WIN) {
					fprintf(neu_compr_file_csv, "%s%u %ux%u ",
						layer_win->typenamel, layer_win->typeidx,
						layer_win->winx, layer_win->winy
					);
				}
				fprintf(neu_compr_file_csv, "%s%u %u %u %u %lu %lu %s\n",
					layer->typenamel, layer->typeidx,
					mem.num, mem.lines, mem.width,
					mem.EvalSizeTotal(), mem.blocks, mem.GetStyleName()
				);
			}
		}

	}  // Layer style 2

	layer->neu_per_bram = nperblk;
	layer->neu_wrnb     = wrnb;
	layer->neu_waccu    = waccu;

	// DWConv example :
	// Consider the case with no user-specified time multiplexing
	//   Win FZ = 30, PAR_OZ = 6
	//   Neurons = 30 logical, 6 physical, TMUX = 30/6=5
	// Now, apply extra time multiplexing to slow these layers down => apply extra factor 3x
	//   Win FZ = 30, PAR_OZ = 2
	//   Neurons = 30 logical, 2 physical, overall TMUX = 30/5*3=15
	unsigned dwconv_tmux = 1;
	if(win_dwconv == true) dwconv_tmux = fz / win_par_oz;

	// Note : maybe these stats should be exported to network with a dedicated Layer method

	network->total_neurons     += nbneu * layer->split_out / dwconv_tmux;
	network->total_neurons_phy += nbneu * layer->split_out / dwconv_tmux / layer->neu_time_mux;
	network->total_multipliers += nbneu * layer->split_out / dwconv_tmux / layer->neu_time_mux * layer->split_in;
	network->total_weights     += nbneu * layer->split_out * fsize * layer->split_in;
	network->total_weight_bits += nbneu * layer->split_out * fsize * layer->split_in * ww;

	unsigned long macs = 0;
	if(win_dwconv == true) {
		// No need to multiply by the number of neurons, this is already accounted for in nbframes
		macs = layer->nbframes * fsize * layer->split_in;
	}
	else {
		macs = layer->nbframes * nbneu * layer->split_out * fsize * layer->split_in;
	}

	//printf("Info: layer %s%u: MACs %lu\n", layer->typenameu, layer->typeidx, macs);
	network->total_macs += macs;

}

void Network::hwconfig_finalize(void) {

	total_neurons     = 0;
	total_neurons_phy = 0;
	total_multipliers = 0;
	total_weights     = 0;
	total_weight_bits = 0;
	total_macs        = 0;

	for(auto layer : layers) {
		layer->hwconfig_finalize();
		// This triggers decision of memory implem
		layer->eval_mem_size();
	}

	// Report
	printf("Global network report :\n");
	unsigned long latency = eval_latency();
	printf("  Latency : %lu clock cycles\n", latency);
	printf("  Neuron layers : Total %lu MAC operations/image\n", total_macs);
	printf("  Neuron layers : Total %u neurons (%u physical), %u multipliers\n",
		total_neurons, total_neurons_phy, total_multipliers
	);
	printf("  Neuron layers : Total %lu weights, %lu weight bits\n",
		total_weights, total_weight_bits
	);

	// Experiment : Close the CSV file of compression details
	// This way there is no duplicate contents in case of multiple invocation of finalize
	if(neu_compr_file_csv != nullptr) {
		fclose(neu_compr_file_csv);
		neu_compr_file_csv = nullptr;
	}

}

