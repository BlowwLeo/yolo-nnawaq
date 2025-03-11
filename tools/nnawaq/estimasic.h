
#pragma once

#include "nn_layers_utils.h"


// Data structure the contains estimations for area and power
typedef struct {

	// Generic stats

	unsigned neurons;
	unsigned params;
	unsigned macs;

	// Hardware

	// SRAM resources
	unsigned sram_bits;
	double   sram_area;
	double   sram_ener;
	// Logic gates + ff resources, FIXME split those
	double   gate_area;
	double   gate_ener;
	// TCAM resources
	unsigned tcamneu_bits;
	double   tcamneu_area;
	double   tcamneu_ener;

	// Execution time

	double layer_freq;
	double layer_cycles;
	double layer_time;

} asic_estim_t;


// Shared variables

extern unsigned asic_verbose;

extern unsigned fifo_depth;
extern bool fifo_is_shift;
extern bool fifo_is_regfile;

extern bool tcam_rec_rom;
extern bool tcam_rec_sram;

extern unsigned tcamneu_blkwidth;
extern unsigned tcamneu_blkheight;


// Functions

void select_techno_st_ll_10();
void select_techno_st_ll_09();
void select_techno_st_ll_06();

asic_estim_t* estimasic_getdata(std::vector<layer_t*>& layers, layer_t* layer);

void estimasic_digital(Network* network);
void estimasic_analog(Network* network);
void estimasic_mixed(Network* network);

