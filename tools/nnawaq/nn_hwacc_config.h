
#pragma once

#include "nn_layers_utils.h"


// Data structure used to build the config data buffer
typedef struct {
	// The layer
	layer_t* layer;
	// To format the output
	unsigned nb32perblock;
	unsigned totalround;
	// The result
	std::vector<uint32_t>* arr;
	// Keep only some config lines
	// For neuron layers
	unsigned onlyitems_modulo;
	unsigned onlyitems_modulo_idx;
	unsigned onlyneurons_modulo;
	unsigned onlyneurons_modulo_idx;
	// Keep only some config lines
	// For recode, ReLU layers
	unsigned onlylines_modulo;
	unsigned onlylines_modulo_idx;
} nn_config_databuf_t;


int nn_config_layer_split_style0(nn_config_databuf_t* nn_data);
int nn_config_layer_split_style1(nn_config_databuf_t* nn_data);
int nn_config_layer_split_style2(nn_config_databuf_t* nn_data);

int nn_config_norm_split(nn_config_databuf_t* buf_data);
int nn_config_recode_split(nn_config_databuf_t* buf_data);

int nn_frames_loadfile(
	const char* filename, unsigned dataw, unsigned fsize, unsigned maxframes_nb,
	unsigned nb32perblock, unsigned totalround,
	uint32_t** pdatabuf, unsigned* pnb32
);

int nn_frames_loadfile_layer(
	const char* filename, layer_t* inlayer, unsigned inwdata,
	uint32_t** pdatabuf, unsigned* pnb32
);

void vhdl_dumpconfig(Network* network);

