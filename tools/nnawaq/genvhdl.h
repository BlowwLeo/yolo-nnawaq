
#pragma once

#include "nn_layers_utils.h"


// Helper class to ease generation of connections between layers
class GenVhdl_ConnHelper {

	public :

		// These variables are for connecting non-FIFO layers to their neighbour layers
	layer_t* layprev  = nullptr;
	layer_t* laynext  = nullptr;
	layer_t* fifonext = nullptr;

	char const * layprev_prefixl  = nullptr;
	char const * laynext_prefixl  = nullptr;
	char const * fifonext_prefixl = nullptr;

	char const * layer_in_data = nullptr;
	char const * layer_in_rdy  = nullptr;
	char const * layer_in_ack  = nullptr;

	char const * layer_param_wout = nullptr;
	char const * layer_out_data = nullptr;
	char const * layer_out_rdy  = nullptr;
	char const * layer_out_ack  = nullptr;
	char const * layer_out_room = nullptr;

	char const * layprev_out_data = nullptr;
	char const * layprev_out_rdy  = nullptr;
	char const * layprev_out_ack  = nullptr;

	char const * laynext_in_data = nullptr;
	char const * laynext_in_rdy  = nullptr;
	char const * laynext_in_ack  = nullptr;

	// If the previous (meaningful) layer is not a FIFO, then the layer is not in control of the data flow
	bool controls_flow = true;

	// Methods

	// Utility functions to ease connection between layers
	// Important : these functions must be called only from non-FIFO layers
	void init_prev_next_layers(Layer* layer);

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	void gen_common_connect(Layer* layer, FILE* Fo, bool selout);

};


// Global parameters
extern char const * vhdl_gen_prefix;
extern char* genvhdl_dump_dir;


int vhdl_gen(Network* network, const char* filename_in, const char* filename_out, const char* want_prefix, bool gen_clear);

int vhdl_gen_const_params(Network* network, const char* filename_in, const char* filename_out, bool gen_clear);

