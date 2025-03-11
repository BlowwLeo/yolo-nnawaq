// Convert layer config into binary data streams in the format expected by the accelerator
// Target HW may be FPGA accelerator with RIFFA PCIe interface, or a board with Zynq chip

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

#include "nnawaq_utils.h"
#include "load_config.h"

}  // extern "C"

#include "nn_layers_utils.h"
#include "nn_load_config.h"
#include "hwacc_common.h"
#include "nn_hwacc_config.h"

using namespace std;


//============================================
// Config for neuron layer
//============================================

// Load the configuration of neurons in a file
// Optionally, keep only some of the neurons and some of the values for each neurons
int nn_config_layer_split_style0(nn_config_databuf_t* nn_data) {
	layer_t* layer = nn_data->layer;

	unsigned splitf = layer->split_in;
	unsigned splitc = layer->split_out;
	unsigned fsize = (layer->fsize + splitf - 1) / splitf;
	unsigned nbneu = (layer->neurons + splitc - 1) / splitc;

	unsigned neu_per_bram   = layer->neu_per_bram;
	unsigned wrdata_per_neu = layer->neu_wrnb;
	unsigned nb32perblock   = nn_data->nb32perblock;

	// Important: data is written 18 neurons, 2 cells per neuron at a time (hence 72 bits)
	// Those 72 bits are carried with the specified number of 32-bits words

	// Note: Reserving extra cells per neuron to cover cases where nbneu is not a multiple of the number of neurons per bram
	signed char **cfgarray = (signed char**)malloc(neu_per_bram * sizeof(*cfgarray));
	cfgarray[0] = (signed char*)malloc(neu_per_bram * (fsize + wrdata_per_neu) * sizeof(**cfgarray));
	for(unsigned i=1; i<neu_per_bram; i++) cfgarray[i] = cfgarray[i-1] + (fsize + wrdata_per_neu);

	// Compute the number of 72-bits blocks for one block of 18 (or less) neurons
	unsigned block_nb72 = (fsize + wrdata_per_neu - 1) / wrdata_per_neu;
	// Compute the total number of 72-bits blocks
	unsigned total_nb72 = block_nb72 * ((nbneu + neu_per_bram - 1) / neu_per_bram);

	// Compute the allocation size
	unsigned alloc_nb32 = total_nb72 * nb32perblock;
	if(nn_data->totalround > 0) alloc_nb32 = uint_next_multiple(alloc_nb32, nn_data->totalround);

	// Allocate the data buffer + a safety margin for paranoia
	vector<uint32_t>& arr = *nn_data->arr;
	arr.reserve(arr.size() + alloc_nb32 + 128);
	unsigned long arrpos = arr.size();

	// Pre-computed masks
	unsigned wweight = layer->neu_wweight;
	uint64_t wmask = (~(uint64_t)0) >> (64 - wweight);

	// Indexes
	unsigned curneu_nb = 0;
	unsigned readneu_nb = 0;
	unsigned totalneu_nb = 0;

	do {

		// Local variables
		unsigned items_nb = 0;
		unsigned keepitems_nb = 0;
		signed char *ptrframe = NULL;

		// Get the config for one neuron
		// Note: nothing to do, all is already loaded
		int r = 0;
		if(readneu_nb >= layer->neurons) r = -1;
		if(r < 0) goto ENDNEURON;

		if(nn_data->onlyneurons_modulo > 1) {
			if(readneu_nb % nn_data->onlyneurons_modulo != nn_data->onlyneurons_modulo_idx) goto ENDNEURON;
		}

		// Parse the frame
		items_nb = 0;
		keepitems_nb = 0;

		ptrframe = cfgarray[curneu_nb];
		curneu_nb++;
		totalneu_nb++;

		for(unsigned i=0; i<layer->fsize; i++) {
			if(nn_data->onlyitems_modulo <= 1 || i % nn_data->onlyitems_modulo == nn_data->onlyitems_modulo_idx) {
				ptrframe[keepitems_nb++] = layer->cfg_data[readneu_nb][i];
			}
			items_nb++;
		}

		ENDNEURON:

		readneu_nb++;

		if(curneu_nb==0 && r<0) break;
		if(curneu_nb<neu_per_bram && totalneu_nb < nbneu && r>=0) continue;

		// Convert the current block data into 128-bit PCIe buffers
		// For each buffer: lower 36 bits are config for address i of the 18 neurons, next 36 bits are for address i+1
		for(unsigned i=0; i<fsize; i+=wrdata_per_neu) {
			uint64_t buf64l = 0;
			uint64_t buf64u = 0;
			for(int d=wrdata_per_neu-1; d>=0; d--) {
				for(int n=neu_per_bram-1; n>=0; n--) {
					buf64u = (buf64u << wweight) | (buf64l >> (64 - wweight));
					buf64l = (buf64l << wweight) | (cfgarray[n][i+d] & wmask);
				}
			}
			arr[arrpos+0] = buf64l;
			arr[arrpos+1] = buf64l >> 32;
			arr[arrpos+2] = buf64u;
			for(unsigned k=3; k<nb32perblock; k++) arr[arrpos+k] = 0;
			arrpos += nb32perblock;
		}
		// Clear the counter of neurons per block
		curneu_nb = 0;

		// Exit when the end of the file is reached, or all neurons got
		if(r < 0 || totalneu_nb >= nbneu) break;

	} while(1);  // Read the lines of the file

	if(totalneu_nb < nbneu) {
		printf("Warning: Got data for only %u neurons instead of %u\n", totalneu_nb, nbneu);
	}

	// Clean
	free(cfgarray[0]);
	free(cfgarray);

	return 0;
}

// Style 1: Pipeline is ternary mult - adder - accu, only one config write for the entire layer
int nn_config_layer_split_style1(nn_config_databuf_t* nn_data) {
	layer_t* layer = nn_data->layer;

	unsigned data_per_bram    = layer->neu_per_bram;
	unsigned wrnb             = layer->neu_wrnb;
	unsigned nb32_per_cfgword = nn_data->nb32perblock;

	unsigned layer_par_in  = layer->split_in;
	unsigned layer_par_out = layer->split_out;

	unsigned fsize = (layer->fsize + layer_par_in - 1) / layer_par_in;
	//unsigned nbneu = (layer->neurons + layer_par_out - 1) / layer_par_out;

	unsigned nbneu_per_po = (layer->neurons_max + layer_par_out - 1) / layer_par_out;

	unsigned neu_per_block = data_per_bram / layer_par_in;
	if(neu_per_block==0) neu_per_block = 1;
	unsigned blocks_per_neu = (layer_par_in + data_per_bram - 1) / data_per_bram;

	if(param_debug==true) {
		printf("DEBUG blocks_per_neu %u, neu_per_block %u\n", blocks_per_neu, neu_per_block);
	}

	unsigned totalblocks_po = blocks_per_neu * ((nbneu_per_po + neu_per_block - 1) / neu_per_block);
	unsigned totalblocks    = layer_par_out * totalblocks_po;

	unsigned cfgwords_per_block = (fsize + wrnb - 1) / wrnb;
	unsigned cfgwords_total     = totalblocks * cfgwords_per_block;

	// Allocate a temp buffer to store the config of one block
	signed char **cfgarray = (signed char**)malloc(data_per_bram * sizeof(*cfgarray));
	cfgarray[0] = (signed char*)malloc(data_per_bram * (fsize + wrnb) * sizeof(**cfgarray));
	for(unsigned i=1; i<data_per_bram; i++) cfgarray[i] = cfgarray[i-1] + (fsize + wrnb);

	// Compute the allocation size
	unsigned alloc_nb32 = cfgwords_total * nb32_per_cfgword;
	if(nn_data->totalround > 0) alloc_nb32 = uint_next_multiple(alloc_nb32, nn_data->totalround);

	// Allocate the data buffer + a safety margin for paranoia
	vector<uint32_t>& arr = *nn_data->arr;
	arr.reserve(arr.size() + alloc_nb32 + 128);
	unsigned long arrpos = arr.size();

	// Pre-computed masks
	unsigned wweight = layer->neu_wweight;
	bool     bin_sym = (wweight == 1) && ((layer->neu_sgnw & NEUSGN_SIGNED) != 0);
	uint64_t wmask = (~(uint64_t)0) >> (64 - wweight);

	// Indexes
	unsigned curpo_idx  = 0;     // Index to scan PAR_OUT
	unsigned curneu_idx = 0;     // Index to scan the neurons in one PAR_OUT
	unsigned curneu_in_blk = 0;  // Index to scan the neurons in one memory block
	unsigned curpi_idx  = 0;     // Index to scan PAR_IN for the current neuron
	unsigned curarr_idx = 0;     // Index inside the data array

	do {
		bool doflush = false;

		#if 1
		// Paranoia: Clear all at beginning of a RAM block
		if(curarr_idx==0) {
			for(unsigned n=0; n<data_per_bram; n++) {
				for(unsigned i=0; i<fsize+wrnb; i++) cfgarray[n][i] = 0;
			}
		}
		#endif

		// Get one data row
		unsigned neu_idx_in_cfg = curpo_idx + curneu_idx * layer_par_out;
		if(neu_idx_in_cfg < layer->neurons) {
			signed char *ptrarr = cfgarray[curarr_idx];
			int *ptrdata = layer->cfg_data[neu_idx_in_cfg];
			unsigned inframe_idx = curpi_idx;
			for(unsigned i=0; i<fsize; i++) { ptrarr[i] = ptrdata[inframe_idx]; inframe_idx += layer_par_in; }
		}

		#if 0
		printf("ADDARR arr %u, neu %u, data %u mod %u\n", curarr_idx, curpo_idx + curneu_idx * layer_par_out, curpi_idx, layer_par_in);
		#endif

		// Increment indexes
		curarr_idx ++;
		if(curarr_idx >= data_per_bram) doflush = true;
		curpi_idx ++;
		if(curpi_idx >= layer_par_in) {
			curpi_idx = 0;
			if(neu_per_block == 1 && blocks_per_neu == 1) doflush = true;
			else if(blocks_per_neu > 1) doflush = true;
			else if(neu_per_block > 1) {
				curneu_in_blk++;
				if(curneu_in_blk >= neu_per_block) { curneu_in_blk = 0; doflush = true; }
			}
			curneu_idx ++;
			if(curneu_idx >= nbneu_per_po) {
				curneu_idx = 0;
				curneu_in_blk = 0;
				curpo_idx ++;
				doflush = true;
			}
		}

		// Continue adding data in the array until one BRAM is full or needs to flush
		if(doflush==false) continue;

		#if 0
		printf("FLUSH arr %u, pi %u, neu %u neuinblk %u, po %u\n",
			curarr_idx, curpi_idx, curneu_idx, curneu_in_blk, curpo_idx
		);
		#endif

		// Reset the index in array
		curarr_idx = 0;

		// Convert the current block data into 128-bit PCIe buffers
		// For each buffer: lower 36 bits are config for address i of the 18 neurons, next 36 bits are for address i+1
		for(unsigned i=0; i<fsize; i+=wrnb) {
			uint64_t buf64l = 0;
			uint64_t buf64u = 0;
			for(int d=wrnb-1; d>=0; d--) {
				for(int n=data_per_bram-1; n>=0; n--) {
					int w = cfgarray[n][i+d];
					if(bin_sym == true) w = (w == -1);  // Stored 0 means +1, stored 1 means -1
					buf64u = (buf64u << wweight) | (buf64l >> (64 - wweight));
					buf64l = (buf64l << wweight) | (cfgarray[n][i+d] & wmask);
				}
			}
			arr[arrpos+0] = buf64l;
			arr[arrpos+1] = buf64l >> 32;
			arr[arrpos+2] = buf64u;
			arr[arrpos+3] = buf64u >> 32;
			for(unsigned k=4; k<nb32_per_cfgword; k++) arr[arrpos+k] = 0;
			arrpos += nb32_per_cfgword;
		}

		// Exit condition
		if(curpo_idx >= layer_par_out) break;

	} while(1);  // Read the lines of the file

	// Clean
	free(cfgarray[0]);
	free(cfgarray);

	return 0;
}

// Style 2: Pipeline is ternary mult - adder - accu, only one config write for the entire layer
// All weights are in one memory, write from address 0 to fsize-1, as many write vectors per address as necessary
int nn_config_layer_split_style2(nn_config_databuf_t* nn_data) {
	layer_t* layer = nn_data->layer;

	unsigned weights_per_cfgword = layer->neu_per_bram;
	unsigned nb32_per_cfgword    = nn_data->nb32perblock;

	unsigned layer_par_in  = layer->split_in;
	unsigned layer_par_out = layer->split_out;

	unsigned fsize = (layer->fsize + layer_par_in - 1) / layer_par_in;
	unsigned nbneu_phy = (layer->neurons_max + layer->neu_time_mux - 1) / layer->neu_time_mux;
	unsigned nbneu_per_po = (nbneu_phy + layer_par_out - 1) / layer_par_out;

	unsigned cfgwords_per_addr = (layer_par_out * nbneu_per_po * layer_par_in + weights_per_cfgword - 1) / weights_per_cfgword;
	unsigned cfgwords_total    = layer->neu_time_mux * fsize * cfgwords_per_addr;

	// Compute the allocation size
	unsigned alloc_nb32 = cfgwords_total * nb32_per_cfgword;
	if(nn_data->totalround > 0) alloc_nb32 = uint_next_multiple(alloc_nb32, nn_data->totalround);

	// Allocate the data buffer + a safety margin for paranoia
	vector<uint32_t>& arr = *nn_data->arr;
	arr.reserve(arr.size() + alloc_nb32 + 128);

	unsigned wweight = layer->neu_wweight;
	bool     bin_sym = (wweight == 1) && ((layer->neu_sgnw & NEUSGN_SIGNED) != 0);
	unsigned mask_weight = uint_genmask(wweight);

	// Variables to accumulate bits before committing them to the final array
	uint64_t cur64_data = 0;
	unsigned cur64_bits = 0;

	// Helper lambda function to append a specified amount of bits to the config array
	auto func_append = [&](unsigned val, unsigned bits, unsigned mask = ~0) {
		// Append to the accumulator
		cur64_data |= uint64_t(val & mask) << cur64_bits;
		cur64_bits += bits;
		// Commit the word if it is full
		if(cur64_bits >= 32) {
			arr.push_back(cur64_data);
			cur64_data >>= 32;
			cur64_bits -= 32;
		}
	};

	for(unsigned t=0; t<layer->neu_time_mux; t++) {
		for(unsigned fi=0; fi<fsize; fi++) {

			unsigned nb32_endrow = arr.size() + cfgwords_per_addr * nb32_per_cfgword;

			for(unsigned po=0; po<layer_par_out; po++) {
				for(unsigned no=0; no<nbneu_per_po; no++) {
					unsigned n = t * nbneu_phy + no * layer_par_out + po;
					for(unsigned pi=0; pi<layer_par_in; pi++) {

						// Get the weight value
						unsigned f = fi * layer_par_in + pi;
						int w = 0;
						if(n < layer->neurons && f < layer->fsize) w = layer->cfg_data[n][f];
						if(bin_sym == true) w = (w == -1);  // Stored 0 means +1, stored 1 means -1

						// Append the weight to the config
						func_append(w, wweight, mask_weight);

					}  // par_in
				}  // neurons inside one par_out
			}  // par_out

			// Append the end of the line
			if(cur64_bits > 0) {
				arr.push_back(cur64_data);
				cur64_data = 0;
				cur64_bits = 0;
			}
			for(unsigned i=arr.size(); i<nb32_endrow; i++) {
				arr.push_back(0);
			}

		}  // tmux
	}  // fsize

	return 0;
}



//============================================
// Convert configuration data into raw accelerator configuration stream
//============================================

int Layer::hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part) {
	// Default : No config to send
	arr.resize(0);
	code_part = 0;
	num_parts = 0;
	return 0;
}

int LayerNeu::hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part) {
	// Initialize returned fields
	arr.resize(0);
	code_part = 0;
	num_parts = 0;

	// To ease code refactoring
	Layer* layer = this;

	if(layer->cfg_data==NULL) {
		printf("Warning: Layer %s%u has no config data\n", layer->typenamel, layer->typeidx);
		return 0;
	}

	// FIXME These checks may not apply in all circumstances ?
	if(layer->neu_per_bram==0 || layer->neu_wrnb==0) {
		printf("Error: Layer %s%u: Unset fields, can't generate the configuration data\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}

	// Old hardware component style, many blocks
	if(layer->neu_style==0) {

		if(layer->neu_per_bram==0 || layer->neu_wrnb==0) {
			printf("Error: Layer %s%u: Unset fields, can't generate the configuration data\n", layer->typenamel, layer->typeidx);
			exit(EXIT_FAILURE);
		}

		unsigned splitf = layer->split_in;
		unsigned splitc = layer->split_out;

		unsigned f = idx_part % splitf;
		unsigned c = idx_part / splitf;

		unsigned frmw = uint_bitsnb(splitf - 1);
		if(splitf==1) frmw = 0;

		// Returned fields
		code_part = (c << frmw) + f;
		num_parts = splitf * splitc;

		// Build the data structure
		nn_config_databuf_t nn_data;
		memset(&nn_data, 0, sizeof(nn_data));
		// Set fields
		nn_data.layer                  = layer;
		nn_data.nb32perblock           = hwacc->accreg_ifw32;
		nn_data.totalround             = 0;
		nn_data.arr                    = &arr;
		nn_data.onlyitems_modulo       = splitf;
		nn_data.onlyitems_modulo_idx   = f;
		nn_data.onlyneurons_modulo     = splitc;
		nn_data.onlyneurons_modulo_idx = c;

		int z = nn_config_layer_split_style0(&nn_data);
		if(z != 0) return z;

	}  // Neuron style 0

	// Recent hardware component styles, only one configuration block
	else if(layer->neu_style==1 || layer->neu_style==2) {

		// Returned fields
		num_parts = 1;

		// Build the data structure
		nn_config_databuf_t nn_data;
		memset(&nn_data, 0, sizeof(nn_data));
		// Set fields
		nn_data.layer                  = layer;
		nn_data.nb32perblock           = hwacc->accreg_ifw32;
		nn_data.totalround             = 0;
		nn_data.arr                    = &arr;
		nn_data.onlyitems_modulo       = 0;
		nn_data.onlyitems_modulo_idx   = 0;
		nn_data.onlyneurons_modulo     = 0;
		nn_data.onlyneurons_modulo_idx = 0;

		// Get config
		int z = 0;
		if(layer->neu_style==1) z = nn_config_layer_split_style1(&nn_data);
		if(layer->neu_style==2) z = nn_config_layer_split_style2(&nn_data);
		if(z != 0) return z;

	}  // Style 1

	else {
		printf("Error: Layer %s%u: Unknown style %u\n", layer->typenamel, layer->typeidx, layer->neu_style);
		exit(EXIT_FAILURE);
	}

	return 0;
}

int LayerNeu_CM::hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part) {
	// Initialize returned fields
	arr.resize(0);
	code_part = 0;
	num_parts = 0;

	// To ease code refactoring
	Layer* layer = this;

	if(layer->cfg_data==NULL) {
		printf("Warning: Layer %s%u has no config data\n", layer->typenamel, layer->typeidx);
		return 0;
	}

	// FIXME These checks may not apply in all circumstances ?
	if(layer->neu_per_bram==0 || layer->neu_wrnb==0) {
		printf("Error: Layer %s%u: Unset fields, can't generate the configuration data\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}

	// Old hardware component style, many blocks
	if(layer->neu_style==0) {

		if(layer->neu_per_bram==0 || layer->neu_wrnb==0) {
			printf("Error: Layer %s%u: Unset fields, can't generate the configuration data\n", layer->typenamel, layer->typeidx);
			exit(EXIT_FAILURE);
		}

		unsigned splitf = layer->split_in;
		unsigned splitc = layer->split_out;

		unsigned f = idx_part % splitf;
		unsigned c = idx_part / splitf;

		unsigned frmw = uint_bitsnb(splitf - 1);
		if(splitf==1) frmw = 0;

		// Returned fields
		code_part = (c << frmw) + f;
		num_parts = splitf * splitc;

		// Build the data structure
		nn_config_databuf_t nn_data;
		memset(&nn_data, 0, sizeof(nn_data));
		// Set fields
		nn_data.layer                  = layer;
		nn_data.nb32perblock           = hwacc->accreg_ifw32;
		nn_data.totalround             = 0;
		nn_data.arr                    = &arr;
		nn_data.onlyitems_modulo       = splitf;
		nn_data.onlyitems_modulo_idx   = f;
		nn_data.onlyneurons_modulo     = splitc;
		nn_data.onlyneurons_modulo_idx = c;

		int z = nn_config_layer_split_style0(&nn_data);
		if(z != 0) return z;

	}  // Neuron style 0

	// Recent hardware component styles, only one configuration block
	else if(layer->neu_style==1 || layer->neu_style==2) {

		// Returned fields
		num_parts = 1;

		// Build the data structure
		nn_config_databuf_t nn_data;
		memset(&nn_data, 0, sizeof(nn_data));
		// Set fields
		nn_data.layer                  = layer;
		nn_data.nb32perblock           = hwacc->accreg_ifw32;
		nn_data.totalround             = 0;
		nn_data.arr                    = &arr;
		nn_data.onlyitems_modulo       = 0;
		nn_data.onlyitems_modulo_idx   = 0;
		nn_data.onlyneurons_modulo     = 0;
		nn_data.onlyneurons_modulo_idx = 0;

		// Get config
		int z = 0;
		if(layer->neu_style==1) z = nn_config_layer_split_style1(&nn_data);
		if(layer->neu_style==2) z = nn_config_layer_split_style2(&nn_data);
		if(z != 0) return z;

	}  // Style 1

	else {
		printf("Error: Layer %s%u: Unknown style %u\n", layer->typenamel, layer->typeidx, layer->neu_style);
		exit(EXIT_FAILURE);
	}

	return 0;
}

int LayerNorm::hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part) {
	// Initialize returned fields
	arr.resize(0);
	code_part = 0;
	num_parts = 1;

	// To ease code refactoring
	Layer* layer = this;

	if(layer->cfg_data==nullptr) {
		printf("Warning: Layer %s%u has no config data\n", layer->typenamel, layer->typeidx);
		return 0;
	}

	// Get the interface width, in number of 32b words
	unsigned ifw32 = (network->hwconfig_writewidth + 31) / 32;
	if(hwacc != nullptr) ifw32 = hwacc->accreg_ifw32;

	// Compute the number of bits per neuron
	unsigned ram_wdata = layer->norm_wbias + layer->norm_wmul + layer->norm_wshr;
	// Compute the number of data transfers to fill one memory line (may need to be multiple of more than 32b)
	unsigned transfers_per_line = (ram_wdata * layer->split_in + 32 * ifw32 - 1) / (32 * ifw32);
	unsigned nb32_per_line = ifw32 * transfers_per_line;

	// Variables to accumulate bits before committing them to the final array
	uint64_t cur64_data = 0;
	unsigned cur64_bits = 0;

	// Helper lambda function to append a specified amount of bits to the config array
	auto func_append = [&](unsigned val, unsigned bits, unsigned mask = ~0) {
		// Append to the accumulator
		cur64_data |= uint64_t(val & mask) << cur64_bits;
		cur64_bits += bits;
		// Commit the word if it is full
		if(cur64_bits >= 32) {
			arr.push_back(cur64_data);
			cur64_data >>= 32;
			cur64_bits -= 32;
		}
	};

	// Get the width of parameters
	unsigned norm_wbias = layer->norm_wbias;
	unsigned norm_wmul  = layer->norm_wmul;
	unsigned norm_wshr  = layer->norm_wshr;

	// Prepare masks
	unsigned mask_bias = uint_genmask(norm_wbias);
	unsigned mask_mul  = uint_genmask(norm_wmul);
	unsigned mask_shr  = uint_genmask(norm_wshr);

	// Determine the position of bias and shr columns
	unsigned col_bias = 0;
	unsigned col_mul = 0;
	unsigned col_shr = 0;
	unsigned col_nb = 0;
	col_bias = col_nb; col_nb += (norm_wbias > 0) ? 1 : 0;
	col_mul  = col_nb; col_nb += (norm_wmul  > 0) ? 1 : 0;
	col_shr  = col_nb; col_nb += (norm_wshr  > 0) ? 1 : 0;

	unsigned mem_lines = (layer->fsize + layer->split_in - 1) / layer->split_in;
	for(unsigned n=0; n<mem_lines; n++) {

		unsigned nb32_endrow = arr.size() + nb32_per_line;

		for(unsigned pi=0; pi<layer->split_in; pi++) {
			unsigned idx = n*layer->split_in + pi;
			int* ptr_data = layer->cfg_data[idx];

			if(norm_wbias > 0) func_append(ptr_data[col_bias], norm_wbias, mask_bias);
			if(norm_wmul  > 0) func_append(ptr_data[col_mul],  norm_wmul,  mask_mul);
			if(norm_wshr  > 0) func_append(ptr_data[col_shr],  norm_wshr,  mask_shr);

		}  // PAR

		// Append the end of the line
		if(cur64_bits > 0) {
			arr.push_back(cur64_data);
			cur64_data = 0;
			cur64_bits = 0;
		}
		for(unsigned i=arr.size(); i<nb32_endrow; i++) {
			arr.push_back(0);
		}

	}  // neu_per_par

	return 0;
}

int LayerTernarize::hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part) {
	// Initialize returned fields
	arr.resize(0);
	code_part = 0;
	num_parts = 1;

	// To ease code refactoring
	Layer* layer = this;

	if(layer->cfg_data==nullptr) {
		printf("Warning: Layer %s%u has no config data\n", layer->typenamel, layer->typeidx);
		return 0;
	}

	// Get the interface width, in number of 32b words
	unsigned ifw32 = (network->hwconfig_writewidth + 31) / 32;
	if(hwacc != nullptr) ifw32 = hwacc->accreg_ifw32;

	// Compute the number of bits per neuron
	unsigned ram_wdata = 2 * layer->wdata;
	if(layer->ter_out_static == false) ram_wdata += 3 * layer->out_wdata;
	// Compute the number of data transfers to fill one memory line (may need to be multiple of more than 32b)
	unsigned transfers_per_line = (ram_wdata * layer->split_in + 32 * ifw32 - 1) / (32 * ifw32);
	unsigned nb32_per_line = ifw32 * transfers_per_line;

	// Variables to accumulate bits before committing them to the final array
	uint64_t cur64_data = 0;
	unsigned cur64_bits = 0;

	// Helper lambda function to append a specified amount of bits to the config array
	auto func_append = [&](unsigned val, unsigned bits, unsigned mask = ~0) {
		// Append to the accumulator
		cur64_data |= uint64_t(val & mask) << cur64_bits;
		cur64_bits += bits;
		// Commit the word if it is full
		if(cur64_bits >= 32) {
			arr.push_back(cur64_data);
			cur64_data >>= 32;
			cur64_bits -= 32;
		}
	};

	unsigned datawin  = layer->wdata;
	unsigned datawout = layer->out_wdata;

	// Convert the mask into signed min/max
	unsigned maskin = uint_genmask(datawin);
	int th_min = ~(maskin >> 1);
	int th_max = maskin >> 1;

	unsigned maskout = uint_genmask(datawout);

	unsigned mem_lines = (layer->fsize + layer->split_in - 1) / layer->split_in;
	for(unsigned n=0; n<mem_lines; n++) {

		unsigned nb32_endrow = arr.size() + nb32_per_line;

		for(unsigned pi=0; pi<layer->split_in; pi++) {
			unsigned idx = n*layer->split_in + pi;
			int* ptr_data = layer->cfg_data[idx];

			// Clamp the threshold values to ensure they fit in the accelerator data path width
			int thlow = ptr_data[0];
			int thup  = ptr_data[1];
			if(thlow < th_min) thlow = th_min;
			if(thlow > th_max) thlow = th_max;
			if(thup  < th_min) thup  = th_min;
			if(thup  > th_max) thup  = th_max;

			// Append in the buffer
			func_append(thlow, datawin, maskin);
			func_append(thup,  datawin, maskin);

			// Append the threshold values if provided
			if(layer->ter_out_static == false) {
				func_append(ptr_data[2], datawout, maskout);
				func_append(ptr_data[3], datawout, maskout);
				func_append(ptr_data[4], datawout, maskout);
			}

		}  // PAR

		// Append the end of the line
		if(cur64_bits > 0) {
			arr.push_back(cur64_data);
			cur64_data = 0;
			cur64_bits = 0;
		}
		for(unsigned i=arr.size(); i<nb32_endrow; i++) {
			arr.push_back(0);
		}

	}  // neu_per_par

	return 0;
}

int LayerScatter::hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part) {
	// Default : No config to send
	arr.resize(0);
	code_part = 0;
	num_parts = 0;

	printf("Warning: Layer %s%u is missing HW Accelerator Config Generation implementation\n", typenamel, typeidx);

	return 0;
}

int LayerGather::hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part) {
	// Default : No config to send
	arr.resize(0);
	code_part = 0;
	num_parts = 0;

	printf("Warning: Layer %s%u is missing HW Accelerator Config Generation implementation\n", typenamel, typeidx);

	return 0;
}



//============================================
// Frames
//============================================

// FIXME Use function loadfile_oneframe() to reuse code
// FIXME Does not handle all data reordering and stuff
int nn_frames_loadfile(
	const char* filename, unsigned dataw, unsigned fsize, unsigned maxframes_nb,
	unsigned nb32perblock, unsigned totalround,
	uint32_t** pdatabuf, unsigned* pnb32
) {

	FILE* F = fopen(filename, "rb");
	if(F==NULL) {
		printf("ERROR: Can't open file '%s'\n", filename);
		return -1;
	}

	// Compute the number of 128-bit PCIe buffers per frame
	unsigned frame_blocks_nb = (((fsize * dataw) + (32 * nb32perblock) - 1) / (32 * nb32perblock));

	unsigned alloc_nb32 = maxframes_nb * frame_blocks_nb * nb32perblock;
	if(totalround > 0) alloc_nb32 = uint_next_multiple(alloc_nb32, totalround);

	// A buffer to read one line
	size_t linebuf_size = 2048;
	char* linebuf = (char*)malloc(linebuf_size);

	uint32_t* databuf_alloc = (uint32_t*)malloc(alloc_nb32 * sizeof(*databuf_alloc) + 128);
	*pdatabuf = databuf_alloc;
	*pnb32 = alloc_nb32;

	uint32_t* databuf_curptr = databuf_alloc;
	uint32_t* databuf_begframe = databuf_alloc;
	unsigned curframes_nb = 0;

	unsigned totaloverfull_nb = 0;

	do {

		// Declaration of local variables to allow jumps in the code
		char* ptr = linebuf;
		uint32_t buf32 = 0;
		unsigned buf32_nb = 0;  // Number of items in the current 32-bit word

		bool valneg = false;
		int32_t val = 0;
		unsigned items_nb = 0;

		char c = 0;

		// Get one line = one frame
		ssize_t r = getline(&linebuf, &linebuf_size, F);
		if(r < 0) goto ENDFRAME;

		ptr = linebuf;
		buf32 = 0;
		buf32_nb = 0;  // Number of items in the current 32-bit word

		// Parse the line
		valneg = false;
		val = 0;
		items_nb = 0;

		c = *(ptr++);
		while(c!=0) {

			// Skip until the next value
			if(c=='-') valneg = true;
			else if(c=='+') {}
			else if(c >= '0' && c <= '9') val = (c - '0');
			else if(c == 0) break;
			else { c = *(ptr++); continue; }

			// Here it is the beginning of a value. Check if it is within bounds.
			if(items_nb >= fsize) {
				if(totaloverfull_nb <= 10) {
					printf("Warning: Cropping overfull frame (more than %u items)\n", fsize);
					if(totaloverfull_nb == 10) {
						printf("  Note: This warning was printed %u times. Next occurrences will not be displayed.\n", totaloverfull_nb);
					}
					totaloverfull_nb++;
				}
				break;
			}

			// Read the rest of the value
			c = *(ptr++);
			while(c >= '0' && c <= '9') {
				val = 10 * val + (c - '0');
				c = *(ptr++);
			};

			// Here we have a value, apply the sign
			if(valneg==true) val = -val;

			// Enqueue in the current word
			buf32 = (val << (32 - dataw)) | (buf32 >> dataw);
			buf32_nb ++;
			if(buf32_nb == 32 / dataw) {
				*(databuf_curptr++) = buf32;
				buf32_nb = 0;
			}

			// Increment the number of read items
			items_nb ++;

			valneg = false;
			val = 0;

		}  // Loop that reads one line

		// If the buffer for values is not empty, add it to the frame data
		if(buf32_nb > 0) {
			// Fix alignment of last values read in the temp buffer
			if(buf32_nb < 32 / dataw) buf32 >>= 32 - buf32_nb * dataw;
			// Add the buffer to the frame data
			*(databuf_curptr++) = buf32;
			buf32_nb = 0;
		}

		ENDFRAME:

		// If the frame is not empty, add it to the buffer
		if(items_nb > 0) {
			if(items_nb < fsize) {
				printf("Warning: Underfull frame: only %u items instead of %u\n", items_nb, fsize);
			}
			// Here we have added the data of one frame in the big buffer. Move forward.
			curframes_nb ++;
			databuf_begframe += frame_blocks_nb * nb32perblock;
			databuf_curptr = databuf_begframe;
			items_nb = 0;
		}

		// If the big buffer contains enough frames, send that
		if(curframes_nb >= maxframes_nb) break;

		// Exit when the end of the file is reached
		if(r < 0) break;

	} while(1);  // Read the lines of the file

	// Clean
	free(linebuf);

	return 0;
}

int nn_frames_loadfile_layer(
	const char* filename, layer_t* inlayer, unsigned inwdata,
	uint32_t** pdatabuf, unsigned* pnb32
) {

	if(param_fn==0) {
		printf("ERROR: Number of frames is zero\n");
		return -1;
	}

	FILE* F = fopen(filename, "rb");
	if(F==NULL) {
		printf("ERROR: Can't open file '%s'\n", filename);
		return -1;
	}

	unsigned fsize = inlayer->fsize;
	unsigned wdata = inlayer->wdata;

	// Compute the number of bits per frame
	unsigned bits_per_frame = fsize * inwdata;
	unsigned total_nb32 = ((uint64_t)param_fn * bits_per_frame + 31) / 32;

	// Allocate some extra space just to make sure PCIe 128-bit buffers fit
	uint32_t* databuf = (uint32_t*)realloc(*pdatabuf, total_nb32 * sizeof(*databuf) + 128);
	int* framebuf = (int*)malloc(fsize * sizeof(*framebuf));

	unsigned databuf_nb32 = 0;
	unsigned curframes_nb = 0;

	// Merge items inside one 32-bit buffer
	uint32_t buf32 = 0;
	unsigned buf32_nb = 0;  // Number of items in the current 32-bit word
	uint32_t buf32_mask = 0;
	for(unsigned i=0; i<wdata; i++) buf32_mask = (buf32_mask << 1) | 0x01;

	load_warnings_clear();

	// Infinite loop that gets frames
	do {

		// Get one frame
		int r = loadfile_oneframe(F, framebuf, fsize, param_multiline);
		if(r < 0 && param_floop==true) {
			// Sanity check to avoid infinite loop
			if(curframes_nb==0) break;
			// Rewind the file
			rewind(F);
			continue;
		}

		// Add the frame to the big buffer
		if(r >= 0) {

			// If needed, reorder image data
			if(inlayer->fx > 1 || inlayer->fy > 1) {
				unsigned fx = inlayer->fx;
				unsigned fy = inlayer->fy;
				unsigned fz = inlayer->fz;
				// Reorder
				reorder_to_zfirst_dim2(&framebuf, 1, fsize, fx, fy, fz, 0);
			}

			// Enqueue the frame data to the big buffer of frames
			for(unsigned i=0; i<fsize; i++) {
				buf32 |= (framebuf[i] & buf32_mask) << (buf32_nb*wdata);
				buf32_nb ++;
				if(buf32_nb * wdata >= 32) {
					databuf[databuf_nb32++] = buf32;
					buf32 = 0;
					buf32_nb = 0;
				}
			}

			// Increment frame counter
			curframes_nb ++;
		}

		// If the big buffer contains enough frames, send that
		if( (curframes_nb >= param_fn) || (curframes_nb > 0 && r<0) ) {

			// If some items remain, add them to the big buffer
			if(buf32_nb > 0) {
				databuf[databuf_nb32++] = buf32;
				buf32 = 0;
				buf32_nb = 0;
			}

			break;
		}

		// Exit when the end of the file is reached
		if(r < 0) break;
		// Exit when enough frames have been sent
		if(curframes_nb >= param_fn) break;

	} while(1);  // Read the lines of the file

	// Clean
	free(framebuf);
	fclose(F);

	if(curframes_nb < param_fn) {
		printf("Warning: Got %u frames instead of %u\n", curframes_nb, param_fn);
	}

	*pdatabuf = databuf;
	*pnb32 = databuf_nb32;

	return 0;
}



//============================================
// Generation of data files for VHDL simulation
//============================================

extern char* genvhdl_dump_dir;

static int dumpvhdl(layer_t* layer, const char* filename, uint32_t* databuf, unsigned databuf_nb32, unsigned nb32perblock) {
	unsigned len = strlen(filename);
	if(genvhdl_dump_dir != NULL) len += strlen(genvhdl_dump_dir);
	char namebuf[len+32];

	char const * dir = ".";
	if(genvhdl_dump_dir != NULL) {
		dir = genvhdl_dump_dir;
		if(dir[0] != '/') {
			sprintf(namebuf, "mkdir -p '%s'/", dir);
			system(namebuf);
		}
	}

	sprintf(namebuf, "%s/%s", dir, filename);

	FILE* F = fopen(namebuf, "wb");
	if(F==NULL) {
		printf("Error: Layer %s%u: Can't open file %s\n", layer->typenamel, layer->typeidx, namebuf);
		exit(EXIT_FAILURE);
	}

	printf("Info: Writing data to file %s\n", namebuf);

	for(unsigned i=0; i<databuf_nb32; i+=nb32perblock) {
		for(int v=nb32perblock-1; v>=0; v--) fprintf(F, "%08x", databuf[i+v]);
		fprintf(F, "\n");
	}

	fclose(F);

	return 0;
}
static int dumpvhdl(layer_t* layer, const char* filename, vector<uint32_t>& arr, unsigned nb32perblock) {
	return dumpvhdl(layer, filename, arr.data(), arr.size(), nb32perblock);
}

int Layer::dump_config_vhdl(void) {
	// Nothing to do by default
	return 0;
}
int Layer::dump_config_vhdl_generic(void) {

	// This variable exists to ease code refactoring only
	layer_t* layer = this;

	if(layer->cfg_data==nullptr) {
		printf("Warning: Layer %s%u has no config data\n", layer->typenamel, layer->typeidx);
		return 0;
	}

	vector<uint32_t> arr;
	unsigned code_part = 0;
	unsigned num_parts = 1;

	// Build the large array of data
	for(unsigned idx_part=0; idx_part<num_parts; idx_part++) {
		int z = hwacc_genconfig(nullptr, arr, code_part, num_parts, idx_part);
		if(z != 0) return z;
	}

	unsigned nb32perblock = (network->hwconfig_writewidth + 31) / 32;

	// Write the file
	char namebuf[1024];
	sprintf(namebuf, "vhdldata-id%u-%s%u.dat", layer->cfg_id, layer->typenamel, layer->typeidx);
	dumpvhdl(layer, namebuf, arr, nb32perblock);

	return 0;
}

int LayerNeu::dump_config_vhdl(void) {

	// This variable exists to ease code refactoring only
	layer_t* layer = this;

	if(layer->type!=LAYER_NEU) {
		printf("Error line %u: Wrong layer type %u\n", __LINE__, layer->type);
		exit(EXIT_FAILURE);
	}

	if(layer->cfg_data==NULL) {
		printf("Warning: Layer %s%u has no config data\n", layer->typenamel, layer->typeidx);
		return 0;
	}

	// Get parameters
	unsigned splitf = layer->split_in;
	unsigned splitc = layer->split_out;

	if(layer->neu_per_bram==0 || layer->neu_wrnb==0) {
		printf("Error: Layer %s%u: Unset fiedls\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}

	unsigned nb32perblock = (network->hwconfig_writewidth + 31) / 32;

	vector<uint32_t> arr;

	// Build the data structure
	nn_config_databuf_t buf_data;
	memset(&buf_data, 0, sizeof(buf_data));
	// Set fields
	buf_data.layer                  = layer;
	buf_data.nb32perblock           = nb32perblock;
	buf_data.totalround             = 0;
	buf_data.arr                    = &arr;
	buf_data.onlyitems_modulo       = splitf;
	buf_data.onlyitems_modulo_idx   = 0;
	buf_data.onlyneurons_modulo     = splitc;
	buf_data.onlyneurons_modulo_idx = 0;

	char namebuf[1024];

	if(layer->neu_style==0) {

		for(unsigned c=0; c<splitc; c++) {
			for(unsigned f=0; f<splitf; f++) {

				arr.resize(0);
				buf_data.onlyitems_modulo_idx = f;
				buf_data.onlyneurons_modulo_idx = c;

				int z = nn_config_layer_split_style0(&buf_data);
				if(z != 0) return z;

				sprintf(namebuf, "vhdldata-id%u-%s%u-c%u-f%u.dat", layer->cfg_id, layer->typenamel, layer->typeidx, c, f);
				dumpvhdl(layer, namebuf, arr, nb32perblock);

			}  // Split frame
		}  // Split the chain of neurons

	}  // Style 0

	else if(layer->neu_style==1 || layer->neu_style==2) {

		// Get config
		int z = 0;
		if(layer->neu_style==1) z = nn_config_layer_split_style1(&buf_data);
		if(layer->neu_style==2) z = nn_config_layer_split_style2(&buf_data);
		if(z != 0) return z;

		sprintf(namebuf, "vhdldata-id%u-%s%u.dat", layer->cfg_id, layer->typenamel, layer->typeidx);
		dumpvhdl(layer, namebuf, arr, nb32perblock);

	}  // Style 1

	else {
		printf("Error: Layer %s%u: Unknown style %u\n", layer->typenamel, layer->typeidx, layer->neu_style);
		exit(EXIT_FAILURE);
	}

	return 0;
}

int LayerNeu_CM::dump_config_vhdl(void) {

	// This variable exists to ease code refactoring only
	layer_t* layer = this;

	if(layer->type!=LAYER_NEU) {
		printf("Error line %u: Wrong layer type %u\n", __LINE__, layer->type);
		exit(EXIT_FAILURE);
	}

	if(layer->cfg_data==NULL) {
		printf("Warning: Layer %s%u has no config data\n", layer->typenamel, layer->typeidx);
		return 0;
	}

	// Get parameters
	unsigned splitf = layer->split_in;
	unsigned splitc = layer->split_out;

	if(layer->neu_per_bram==0 || layer->neu_wrnb==0) {
		printf("Error: Layer %s%u: Unset fiedls\n", layer->typenamel, layer->typeidx);
		exit(EXIT_FAILURE);
	}

	unsigned nb32perblock = (network->hwconfig_writewidth + 31) / 32;

	vector<uint32_t> arr;

	// Build the data structure
	nn_config_databuf_t buf_data;
	memset(&buf_data, 0, sizeof(buf_data));
	// Set fields
	buf_data.layer                  = layer;
	buf_data.nb32perblock           = nb32perblock;
	buf_data.totalround             = 0;
	buf_data.arr                    = &arr;
	buf_data.onlyitems_modulo       = splitf;
	buf_data.onlyitems_modulo_idx   = 0;
	buf_data.onlyneurons_modulo     = splitc;
	buf_data.onlyneurons_modulo_idx = 0;

	char namebuf[1024];

	if(layer->neu_style==0) {

		for(unsigned c=0; c<splitc; c++) {
			for(unsigned f=0; f<splitf; f++) {

				arr.resize(0);
				buf_data.onlyitems_modulo_idx = f;
				buf_data.onlyneurons_modulo_idx = c;

				int z = nn_config_layer_split_style0(&buf_data);
				if(z != 0) return z;

				sprintf(namebuf, "vhdldata-id%u-%s%u-c%u-f%u.dat", layer->cfg_id, layer->typenamel, layer->typeidx, c, f);
				dumpvhdl(layer, namebuf, arr, nb32perblock);

			}  // Split frame
		}  // Split the chain of neurons

	}  // Style 0

	else if(layer->neu_style==1 || layer->neu_style==2) {

		// Get config
		int z = 0;
		if(layer->neu_style==1) z = nn_config_layer_split_style1(&buf_data);
		if(layer->neu_style==2) z = nn_config_layer_split_style2(&buf_data);
		if(z != 0) return z;

		sprintf(namebuf, "vhdldata-id%u-%s%u.dat", layer->cfg_id, layer->typenamel, layer->typeidx);
		dumpvhdl(layer, namebuf, arr, nb32perblock);

	}  // Style 1

	else {
		printf("Error: Layer %s%u: Unknown style %u\n", layer->typenamel, layer->typeidx, layer->neu_style);
		exit(EXIT_FAILURE);
	}

	return 0;
}

int LayerNorm::dump_config_vhdl(void) {
	return dump_config_vhdl_generic();
}

int LayerTernarize::dump_config_vhdl(void) {
	return dump_config_vhdl_generic();
}

static int nn_dumpframes(Network* network, const char* filename) {
	if(filename==NULL) {
		printf("Warning: No file is given for the input frames\n");
		return 0;
	}

	auto& layers = network->layers;

	// Get the first layer of the network
	layer_t* inlayer = NULL;
	for(layer_t* layer = layers.front(); layer != NULL; layer = layer->next) {
		if(layer->type != LAYER_FIFO) { inlayer = layer; break; }
	}

	unsigned errors_nb = 0;
	if(inlayer==NULL) {
		printf("ERROR: Could not find first layer\n");
		errors_nb ++;
	}
	if(errors_nb != 0) return -1;

	unsigned nb32perblock = (network->hwconfig_writewidth + 31) / 32;

	uint32_t* databuf = NULL;
	unsigned  databuf_nb32 = 0;

	nn_frames_loadfile_layer(
		filename, inlayer, uint_rndpow2_ceil(inlayer->wdata),
		&databuf, &databuf_nb32
	);

	char namebuf[1024];
	sprintf(namebuf, "input.dat");
	dumpvhdl(inlayer, namebuf, databuf, databuf_nb32, nb32perblock);

	// Clean
	free(databuf);

	return 0;
}

void vhdl_dumpconfig(Network* network) {
	auto& layers = network->layers;

	// Note : Better to call this on the entire network than on per-layer basis
	// To avoid network stats being counted twice
	network->hwconfig_finalize();

	// Dump config of layers
	for(auto layer : layers) {
		// Read the data files
		layer->load_config_files();
		// Write data in the expected format
		layer->dump_config_vhdl();
	}

	// Dump the frames
	nn_dumpframes(network, filename_frames);

}

