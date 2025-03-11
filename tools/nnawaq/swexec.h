
#pragma once

#include "nn_layers_utils.h"

extern unsigned swexec_param_mod;
extern bool     swexec_mode_tcam;
extern bool     swexec_gen_in;

extern double swexec_emulate_error_lin;

// Function used internally
int swexec_series_of_layers(layer_t* inlayer, layer_t* outlayer, int* bufin, int* bufout, unsigned f);

// Software execution
int swexec(Network* network, layer_t* outlayer);

