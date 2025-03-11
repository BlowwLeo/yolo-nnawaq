
extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

}

#include "nn_layers_utils.h"

using namespace std;


void compress_2t3b_test_neuron(layer_t* layer) {

	if(layer->cfg_data==NULL) {
		printf("Warning: Layer %s%u has no config data, skipping test\n", layer->typenamel, layer->typeidx);
		return;
	}

	// Allocate a 2D array with the geometry used inside the HW accelerator
	if(layer->split_in > 1) {
		printf("FIXME: Layer %s%u has no PAR_IN = %u, only 1 is handled for now\n", layer->typenamel, layer->typeidx, layer->split_in);
		return;
	}

	int **cfg_data = layer->cfg_data;
	unsigned fsize = layer->fsize;
	unsigned nbneu = layer->neurons;

	// Test: for each addr in frame, there must be no more than half weights at -1

	unsigned conflicts_addr = 0;
	unsigned nb_all_m1 = 0;

	for(unsigned i=0; i<fsize; i++) {
		unsigned count = 0;
		for(unsigned n=0; n<nbneu; n++) {
			if(cfg_data[n][i] == -1) count++;
		}
		if(count == fsize) {
			nb_all_m1++;
			count = 0;
			// WARNING This simplification requires tweaking recode thresholds
			for(unsigned n=0; n<nbneu; n++) cfg_data[n][i] = 0;
		}
		if(count >= (fsize+1)/2) conflicts_addr++;
	}

	printf("Layer %s%u: ", layer->typenamel, layer->typeidx);
	if(conflicts_addr == 0) {
		printf("No problem with density of -1 for all addresses in memory of weights\n");
	}
	else {
		printf("Warning: %u/%u addresses in weight memory have more than half weights at -1\n", conflicts_addr, fsize);
	}
	if(nb_all_m1 > 0) {
		printf("  Note: skipped %u addresses where all weight positions were at -1\n", nb_all_m1);
		printf("  These weights were replaced by 0\n");
	}

  // Test: for each neuron (assume par=1) check if there is at least one other neuron with which there is no pair (-1, -1)

	unsigned conflicts_pair = 0;

	for(unsigned n1=0; n1<nbneu; n1++) {
		bool pair_found = false;
		for(unsigned n2=0; n2<nbneu; n2++) {
			if(n2==n1) continue;
			bool conflict_found = false;
			for(unsigned i=0; i<fsize; i++) {
				if(cfg_data[n1][i] == -1 && cfg_data[n2][i] == -1) { conflict_found = true; break; }
			}
			if(conflict_found==false) { pair_found = true; break; }
		}  // second neuron
		if(pair_found==false) conflicts_pair++;
	}  // ref neuron

	printf("Layer %s%u: ", layer->typenamel, layer->typeidx);
	if(conflicts_pair == 0) {
		printf("No problem about forming pairs of weight positions\n");
	}
	else {
		printf("Warning: %u/%u weight positions can't be grouped with any other weight position\n", conflicts_pair, nbneu);
	}

}

void compress_2t3b_test(Network* network) {

	// Load config
	network->load_config_files();

	for(auto layer : network->layers) {

		if(layer->type==LAYER_NEU) {
			compress_2t3b_test_neuron(layer);
		}

	}

}

