
#pragma once

extern "C" {

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

}

#include <vector>
#include <map>
#include <string>

#include "nn_layers_create.h"


//============================================
// Print the network
//============================================

// Options that apply to pretty-print with table
#define NNPRINT_OPT_TABLE  0x01
#define NNPRINT_OPT_CYCLES 0x02
#define NNPRINT_OPT_HWID   0x04
#define NNPRINT_OPT_TOTAL  0x08

int nnprint_oneline_layer(layer_t* layer, const char* indent);
int nnprint_oneline(const std::vector<layer_t*>& layers, const char* indent);

int nnprint_layer(layer_t* layer, unsigned options = 0);
int nnprint(const std::vector<layer_t*>& layers, unsigned options = 0, unsigned layer_type = LAYER_NONE);

// Print memory size and style

int nnprint_mem_oneline_layer(layer_t* layer, const char* indent);
int nnprint_mem_oneline(const std::vector<layer_t*>& layers, const char* indent);

int nnprint_mem_layer(layer_t* layer, unsigned options);
int nnprint_mem(const std::vector<layer_t*>& layers, unsigned options = 0, unsigned layer_type = LAYER_NONE);


//============================================
// Miscellaneous shared variables
//============================================

extern bool param_debug;
extern bool param_sci_notation;

extern char const * filename_frames;
extern unsigned param_fn;
extern char* filename_out;

extern bool param_rand_given;
extern int  param_rand_min;
extern int  param_rand_max;

extern bool param_freerun;
extern bool param_hw_blind;
extern bool param_floop;
extern unsigned param_bufsz_mb;

extern bool param_print_time;

extern FILE*    Fo;
extern bool     param_noout;
extern bool     param_multiline;
extern layer_t* param_out_layer;

// Timeout values for operations with hardware accelerator
extern unsigned long param_timeout_regs_us;
extern unsigned long param_timeout_send_us;
extern unsigned long param_timeout_recv_us;

extern unsigned     param_out_nl;
extern char const * param_out_sep;
extern char const * param_out_format;
extern bool         param_out_mask;

extern unsigned tcam_dl_max;
extern unsigned tcam_dl_mindisch;
extern unsigned tcam_dl_mindist;
extern double   tcam_dl_minrate;
extern double   tcam_dl_maxrate;
extern double   tcam_prec_dbl;


//============================================
// Functions
//============================================

unsigned tcam_dl_recode_array(unsigned line_size, int* recode_arr, int* dl_arr, bool nominmax);

Layer* traverse_transparent_backward(Layer* layer_prev);
Layer* traverse_transparent_forward(Layer* layer_next);

int neu_sign_check_set(layer_t* layer, const char* what, char param_sgn, char* p_layer_sgn);
int propag_backward_par(Network* network, layer_t* layer);
int propag_forward_par(Network* network, layer_t* layer);
int propag_params_layer(Network* network, layer_t* layer);

void apply_outneu(Network* network, unsigned nbneu);

unsigned maxcycles_per_layer(Network* network);
void apply_parallelism(Network* network, unsigned par);
void apply_time_mux(Network* network);
void apply_time_mux_max(Network* network);
void apply_parin_with_time_mux(Network* network);

void chkoutfile();
void chknonempty(Network* network);

int decodeparam_width_sign(const char* str, unsigned* width_p, bool* sign_bool_p, unsigned* sign_uint_p);
int decodeparam_ms(const char* str, long unsigned *ms_p);
int decodeparam_us(const char* str, long unsigned *us_p);


