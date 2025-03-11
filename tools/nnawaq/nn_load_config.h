
#pragma once

#include "nn_layers_utils.h"

// Reorder data inside arrays
int reorder_to_xfirst_dim2(int** data, unsigned nbframes, unsigned fsize, unsigned fx, unsigned fy, unsigned fz, unsigned parz=1);
int reorder_to_zfirst_dim2(int** data, unsigned nbframes, unsigned fsize, unsigned fx, unsigned fy, unsigned fz, unsigned padz);
int neurons_weights_reorder(layer_t* layer, unsigned winx, unsigned winy, unsigned fz, unsigned winz, unsigned nwinz);

int layer_loadcfg(layer_t* layer, unsigned nrow, unsigned ncol, unsigned alloc_nrow, unsigned alloc_ncol);
int layer_loadcfg_or_random(layer_t* layer, unsigned nrow, unsigned ncol, unsigned wdata, unsigned alloc_nrow, unsigned alloc_ncol);
int neurons_weights_reorder_for_prev_layers(layer_t* layer);

