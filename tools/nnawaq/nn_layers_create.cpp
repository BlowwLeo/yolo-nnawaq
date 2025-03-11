
extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <time.h>

#include "nnawaq_utils.h"

}

#include "nn_layers_utils.h"
#include "nn_hw_config.h"
#include "swexec.h"

// ASIC estimations
#ifndef LIMITED
#include "estimasic.h"
#include "genvhdl.h"
#include "compress.h"
#endif

#ifndef NOTCL
#include "tcl_parser.h"
#endif

#include "hwacc_common.h"

// For declaration of recursive lambda functions
#include <functional>

using namespace std;


//============================================
// Layer class
//============================================

Layer::Layer(void) {
	// Note : All other fields are supposed to be initialized to zero, false, or something empty
	type = LAYER_NONE;
	id = -1;
}

Layer::~Layer(void) {
	if(vhdl_prefixl != NULL) free(vhdl_prefixl);
	if(vhdl_prefixu != NULL) free(vhdl_prefixu);
	if(cfg_filename != NULL) free(cfg_filename);
	if(cfg_data != NULL)   { free(cfg_data[0]); free(cfg_data); }
	if(swexec_output != NULL) free(swexec_output);
}

// Global list of layer types
map<string, unsigned> Layer::map_layer_name2id;
vector<Layer*> Layer::vec_layer_id2type;
map<string, unsigned> Layer::map_layer_entity2id;

int Layer::register_type(Layer* layer, const char* name) {
	unsigned errors_nb = 0;

	// Check availability of slots
	if((unsigned)layer->type >= vec_layer_id2type.size()) vec_layer_id2type.resize(layer->type + 1, nullptr);
	if(vec_layer_id2type[layer->type] != nullptr && vec_layer_id2type[layer->type] != layer) {
		printf("Error : Layer type %u is already registered\n", layer->type);
		errors_nb++;
	}

	string str = name;
	auto iter = map_layer_name2id.find(str);
	if(iter != map_layer_name2id.end() && iter->second != (unsigned)layer->type) {
		printf("Error : Layer type name '%s' is already registered by layer type %u\n", name, iter->second);
		errors_nb++;
	}

	// Ensure the entity name is unique
	if(layer->custom_entity != nullptr) {
		std::string str_entity = layer->custom_entity;
		if(map_layer_entity2id.insert(make_pair(str_entity, layer->type)).second == false) {
			printf("Error : Entity name '%s' is already registered\n", layer->custom_entity);
			errors_nb++;
		}
	}

	// Do nothing if slots are not available
	if(errors_nb > 0) return errors_nb;

	// Insert the layer type
	map_layer_name2id[str] = layer->type;
	vec_layer_id2type[layer->type] = layer;

	return 0;
}

int Layer::get_type_name2id(const char* type_name) {
	int type_id = LAYER_NONE;

	string str = type_name;
	auto iter = map_layer_name2id.find(str);
	if(iter != map_layer_name2id.end()) {
		type_id = iter->second;
	}

	return type_id;
}
int Layer::get_type_name2id_verbose(const char* type_name) {
	int type_id = get_type_name2id(type_name);
	if(type_id == LAYER_NONE) {
		printf("Error: Unknown layer type '%s'\n", type_name);
	}
	return type_id;
}

char const * Layer::get_type_id2namel(int type_id) {
	layer_t* ref_layer = nullptr;
	if((unsigned)type_id < Layer::vec_layer_id2type.size()) {
		ref_layer = Layer::vec_layer_id2type[type_id];
	}
	if(ref_layer == nullptr) return "unknown";
	return ref_layer->typenamel;
}
char const * Layer::get_type_id2nameu(int type_id) {
	layer_t* ref_layer = nullptr;
	if((unsigned)type_id < Layer::vec_layer_id2type.size()) {
		ref_layer = Layer::vec_layer_id2type[type_id];
	}
	if(ref_layer == nullptr) return "UNKNOWN";
	return ref_layer->typenameu;
}

Layer* Layer::create_new_from_id(unsigned type_id, const char* type_name) {
	layer_t* ref_layer = nullptr;
	if((unsigned)type_id < Layer::vec_layer_id2type.size()) {
		ref_layer = Layer::vec_layer_id2type[type_id];
	}
	if(ref_layer == nullptr) return nullptr;
	// Use a clone method to inherit all custom fields
	layer_t* layer = ref_layer->clone();
	int z = layer->params_from_type_name(type_name);
	if(z != 0) {
		delete layer;
		layer = nullptr;
	}
	return layer;
}
Layer* Layer::create_new_from_id_verbose(unsigned type_id, const char* type_name) {
	layer_t* layer = create_new_from_id(type_id, type_name);
	if(layer == nullptr) {
		printf("Error: Layer type ID %i is not a valid layer type\n", type_id);
	}
	return layer;
}


//============================================
// Classes for the NN layer types
//============================================

std::vector< std::vector<LayerRegField> > LayerWin::regs_fields;

// Reg 0
LayerRegField* LayerWin::regfield_dwconv = nullptr;
LayerRegField* LayerWin::regfield_symxy  = nullptr;
LayerRegField* LayerWin::regfield_repeat = nullptr;
// Reg 1
LayerRegField* LayerWin::regfield_fx     = nullptr;
LayerRegField* LayerWin::regfield_fx_max = nullptr;
// Reg 2
LayerRegField* LayerWin::regfield_fz     = nullptr;
LayerRegField* LayerWin::regfield_fz_max = nullptr;
// Reg 3
LayerRegField* LayerWin::regfield_stepx  = nullptr;
LayerRegField* LayerWin::regfield_winx   = nullptr;
LayerRegField* LayerWin::regfield_padx   = nullptr;
LayerRegField* LayerWin::regfield_nwinx  = nullptr;
// Reg 4
LayerRegField* LayerWin::regfield_nwinz  = nullptr;
LayerRegField* LayerWin::regfield_par_oz = nullptr;
// Reg 5
LayerRegField* LayerWin::regfield_fy     = nullptr;
LayerRegField* LayerWin::regfield_fy_max = nullptr;
// Reg 6
LayerRegField* LayerWin::regfield_stepy  = nullptr;
LayerRegField* LayerWin::regfield_winy   = nullptr;
LayerRegField* LayerWin::regfield_pady   = nullptr;
LayerRegField* LayerWin::regfield_nwiny  = nullptr;

std::vector< std::vector<LayerRegField> > LayerNeu::regs_fields;

// Reg 0
LayerRegField* LayerNeu::regfield_dwconv   = nullptr;
LayerRegField* LayerNeu::regfield_tmux     = nullptr;
// Reg 1
LayerRegField* LayerNeu::regfield_fsize    = nullptr;
LayerRegField* LayerNeu::regfield_fsize_max = nullptr;
// Reg 2
LayerRegField* LayerNeu::regfield_neu      = nullptr;
LayerRegField* LayerNeu::regfield_neu_max  = nullptr;
// Reg 3
LayerRegField* LayerNeu::regfield_nperblk  = nullptr;
LayerRegField* LayerNeu::regfield_wrnb     = nullptr;
LayerRegField* LayerNeu::regfield_wweight  = nullptr;
LayerRegField* LayerNeu::regfield_sdlock   = nullptr;
LayerRegField* LayerNeu::regfield_sdata    = nullptr;
LayerRegField* LayerNeu::regfield_swlock   = nullptr;
LayerRegField* LayerNeu::regfield_sweight  = nullptr;
LayerRegField* LayerNeu::regfield_style    = nullptr;
LayerRegField* LayerNeu::regfield_mul_id   = nullptr;

std::vector< std::vector<LayerRegField> > LayerNeu_CM::regs_fields;

// Reg 0
LayerRegField* LayerNeu_CM::regfield_dwconv   = nullptr;
LayerRegField* LayerNeu_CM::regfield_tmux     = nullptr;
// Reg 1
LayerRegField* LayerNeu_CM::regfield_fsize    = nullptr;
LayerRegField* LayerNeu_CM::regfield_fsize_max = nullptr;
// Reg 2
LayerRegField* LayerNeu_CM::regfield_neu      = nullptr;
LayerRegField* LayerNeu_CM::regfield_neu_max  = nullptr;
// Reg 3
LayerRegField* LayerNeu_CM::regfield_nperblk  = nullptr;
LayerRegField* LayerNeu_CM::regfield_wrnb     = nullptr;
LayerRegField* LayerNeu_CM::regfield_wweight  = nullptr;
LayerRegField* LayerNeu_CM::regfield_sdlock   = nullptr;
LayerRegField* LayerNeu_CM::regfield_sdata    = nullptr;
LayerRegField* LayerNeu_CM::regfield_swlock   = nullptr;
LayerRegField* LayerNeu_CM::regfield_sweight  = nullptr;
LayerRegField* LayerNeu_CM::regfield_style    = nullptr;
LayerRegField* LayerNeu_CM::regfield_mul_id   = nullptr;

std::vector< std::vector<LayerRegField> > LayerPool::regs_fields;

// Reg 0
LayerRegField* LayerPool::regfield_type    = nullptr;
LayerRegField* LayerPool::regfield_rndnear = nullptr;
LayerRegField* LayerPool::regfield_avgreg  = nullptr;
// Reg 1
LayerRegField* LayerPool::regfield_fsize   = nullptr;
// Reg 2
LayerRegField* LayerPool::regfield_mul     = nullptr;
LayerRegField* LayerPool::regfield_shr     = nullptr;

std::vector< std::vector<LayerRegField> > LayerNorm::regs_fields;

// Reg 0
LayerRegField* LayerNorm::regfield_enbias  = nullptr;
LayerRegField* LayerNorm::regfield_enmul   = nullptr;
LayerRegField* LayerNorm::regfield_wbias   = nullptr;
LayerRegField* LayerNorm::regfield_wmul    = nullptr;
LayerRegField* LayerNorm::regfield_wshr    = nullptr;
// Reg 1
LayerRegField* LayerNorm::regfield_fsize   = nullptr;
LayerRegField* LayerNorm::regfield_fsize_max = nullptr;
// Reg 2
LayerRegField* LayerNorm::regfield_cstmul  = nullptr;
LayerRegField* LayerNorm::regfield_cstshr  = nullptr;
LayerRegField* LayerNorm::regfield_rndtype = nullptr;

std::vector< std::vector<LayerRegField> > LayerTernarize::regs_fields;

// Reg 0
LayerRegField* LayerTernarize::regfield_out_static = nullptr;
LayerRegField* LayerTernarize::regfield_out_low = nullptr;
LayerRegField* LayerTernarize::regfield_out_med = nullptr;
LayerRegField* LayerTernarize::regfield_out_up  = nullptr;
// Reg 1
LayerRegField* LayerTernarize::regfield_fsize   = nullptr;
LayerRegField* LayerTernarize::regfield_fsize_max = nullptr;

std::vector< std::vector<LayerRegField> > LayerRelu::regs_fields;

// Reg 0
// (no extra fields)
// Reg 1
LayerRegField* LayerRelu::regfield_thmin = nullptr;
// Reg 2
LayerRegField* LayerRelu::regfield_thmax = nullptr;

std::vector< std::vector<LayerRegField> > LayerLeaky::regs_fields;

// Reg 0
// (no extra fields)
// Reg 1
LayerRegField* LayerLeaky::regfield_thmin = nullptr;
// Reg 2
LayerRegField* LayerLeaky::regfield_thmax = nullptr;

std::vector< std::vector<LayerRegField> > LayerAdd::regs_fields;

std::vector< std::vector<LayerRegField> > LayerCustom::regs_fields;

// Reg 0
LayerRegField* LayerCustom::regfield_func_id = nullptr;

std::vector< std::vector<LayerRegField> > LayerFork::regs_fields;

// Reg 0
LayerRegField* LayerFork::regfield_layers_nb = nullptr;

std::vector< std::vector<LayerRegField> > LayerCat::regs_fields;

// Reg 0
LayerRegField* LayerCat::regfield_layers_nb = nullptr;

std::vector< std::vector<LayerRegField> > LayerSoftMax::regs_fields;

// Reg 0
LayerRegField* LayerSoftMax::regfield_fsize = nullptr;

LayerWin::LayerWin(void) {

	type = LAYER_WIN;

	// FIXME The layer type name should not be set here
	typenamel = "win";
	typenameu = "WIN";

	mem.style = MemImplem::STYLE_NONE;

	// Default parameters
	winx      = 1;
	winy      = 1;
	stepx     = 1;
	stepy     = 1;
	begpadx   = 1;
	begpady   = 1;

}

LayerWin_CM::LayerWin_CM(void) {

	type = LAYER_WIN_CM;

	// FIXME The layer type name should not be set here
	typenamel = "win_cm";
	typenameu = "WIN_CM";

	mem.style = MemImplem::STYLE_NONE;

	// Default parameters
	winx      = 1;
	winy      = 1;
	stepx     = 1;
	stepy     = 1;
	begpadx   = 1;
	begpady   = 1;

}
LayerNeu::LayerNeu(void) {

	type = LAYER_NEU;

	// FIXME The layer type name should not be set here
	typenamel = "neu";
	typenameu = "NEU";

	mem.style = MemImplem::STYLE_NONE;

	neu_style = 0;

}
LayerNeu_CM::LayerNeu_CM(void) {

	type = LAYER_NEU_CM;

	// FIXME The layer type name should not be set here
	typenamel = "neu_cm";
	typenameu = "NEU_CM";

	mem.style = MemImplem::STYLE_NONE;

	neu_style = 2;

}
LayerPool::LayerPool(void) {

	type = LAYER_POOL;

	// FIXME The layer type name should not be set here
	typenamel = "pool";
	typenameu = "POOL";

	// Default parameters
	winx = 2;
	winy = 2;

	// This is intentionally initialized to an invalid value
	// Instantiation of the layer will set this correctly
	pool_type = POOL_TYPE_NONE;

}

LayerNorm::LayerNorm(void) {

	type = LAYER_NORM;

	// FIXME The layer type name should not be set here
	typenamel = "norm";
	typenameu = "NORM";

}

LayerNorm_CM::LayerNorm_CM(void) {

	type = LAYER_NORM_CM;

	// FIXME The layer type name should not be set here
	typenamel = "norm_cm";
	typenameu = "NORM_CM";

}
LayerTernarize::LayerTernarize(void) {

	type = LAYER_TER;

	// FIXME The layer type name should not be set here
	typenamel = "ter";
	typenameu = "TER";

}
LayerRelu::LayerRelu(void) {

	type = LAYER_RELU;

	// FIXME The layer type name should not be set here
	typenamel = "relu";
	typenameu = "RELU";

}
LayerLeaky::LayerLeaky(void) {

	type = LAYER_LEAKY;

	// FIXME The layer type name should not be set here
	typenamel = "leaky";
	typenameu = "LEAKY";

}
LayerAdd::LayerAdd(void) {

	type = LAYER_ADD;

	// FIXME The layer type name should not be set here
	typenamel = "add";
	typenameu = "ADD";

}
LayerCustom::LayerCustom(void) {

	// Note : The layer type and type name will be overridden at custom layer creation

	type = LAYER_CUSTOM;

	// FIXME The layer type name should not be set here
	typenamel = "cust";
	typenameu = "CUST";

}

LayerFork::LayerFork(void) {

	type = LAYER_FORK;

	// FIXME The layer type name should not be set here
	typenamel = "fork";
	typenameu = "FORK";

	// Successor layers are listed in array
	next_is_arr = true;

}
LayerCat::LayerCat(void) {

	type = LAYER_CAT;

	// FIXME The layer type name should not be set here
	typenamel = "cat";
	typenameu = "CAT";

	// Predecessor layers are listed in array
	prev_is_arr = true;

}
LayerScatter::LayerScatter(void) {

	type = LAYER_SCATTER;

	// FIXME The layer type name should not be set here
	typenamel = "sca";
	typenameu = "SCA";

	// Successor layers are listed in array
	next_is_arr = true;

}
LayerGather::LayerGather(void) {

	type = LAYER_GATHER;

	// FIXME The layer type name should not be set here
	typenamel = "gat";
	typenameu = "GAT";

	// Predecessor layers are listed in array
	prev_is_arr = true;

}

LayerFlatten::LayerFlatten(void) {

	type = LAYER_FLATTEN;

	// FIXME The layer type name should not be set here
	typenamel = "flat";
	typenameu = "FLAT";

}
LayerSoftMax::LayerSoftMax(void) {

	type = LAYER_SOFTMAX;

	// FIXME The layer type name should not be set here
	typenamel = "softmax";
	typenameu = "SOFTMAX";

}
LayerFifo::LayerFifo(void) {

	type = LAYER_FIFO;

	// FIXME The layer type name should not be set here
	typenamel = "fifo";
	typenameu = "FIFO";

}

void Layer::apply_network_defaults(Network* network) {
	// Nothing to do
}

void LayerWin::apply_network_defaults(Network* network) {
	if(network->default_mem_implem_win != MemImplem::STYLE_AUTO) {
		mem.style = network->default_mem_implem_win;
	}
	mem.opt_speed = network->hwconfig_bram_opt_speed;
}

void LayerNeu::apply_network_defaults(Network* network) {
	neu_wweight = network->default_neu_ww;
	neu_sgnd = NEUSGN_SIGNED | NEUSGN_LOCKED;
	neu_sgnw = (network->default_neu_sw == true) ? NEUSGN_SIGNED : 0;
	neu_sgnw |= NEUSGN_LOCKED;
	if(network->default_mem_implem_neu != MemImplem::STYLE_AUTO) {
		mem.style = network->default_mem_implem_neu;
	}
	mem.opt_speed = network->hwconfig_bram_opt_speed;
}

void LayerNeu_CM::apply_network_defaults(Network* network) {
	neu_wweight = network->default_neu_ww;
	neu_sgnd = NEUSGN_SIGNED | NEUSGN_LOCKED;
	neu_sgnw = (network->default_neu_sw == true) ? NEUSGN_SIGNED : 0;
	neu_sgnw |= NEUSGN_LOCKED;
	if(network->default_mem_implem_neu != MemImplem::STYLE_AUTO) {
		mem.style = network->default_mem_implem_neu;
	}
	mem.opt_speed = network->hwconfig_bram_opt_speed;
}

void LayerPool::apply_network_defaults(Network* network) {
	round_nearest = network->default_round_nearest;
}

void LayerNorm::apply_network_defaults(Network* network) {
	round_nearest = network->default_round_nearest;
	norm_mul_cst = network->default_norm_mul_cst;
	norm_shr_cst = network->default_norm_shr_cst;
	norm_wbias   = network->default_norm_wbias;
	norm_wmul    = network->default_norm_wmul;
	norm_wshr    = network->default_norm_wshr;
}

void LayerRelu::apply_network_defaults(Network* network) {
	relu_min = network->default_relu_min;
	relu_max = network->default_relu_max;
}
void LayerLeaky::apply_network_defaults(Network* network) {
	leaky_min = network->default_leaky_min;
	leaky_max = network->default_leaky_max;
}

// Methods to create a new instance of a Layer object

Layer* Layer::create_new(void) {
	// Creating an instance of the base Layer class is not allowed
	abort();
}
Layer* Layer::create_new(Network* network) {
	Layer* layer = create_new();
	layer->apply_network_defaults(network);
	return layer;
}

Layer* LayerWin::create_new(void) {
	return new LayerWin();
}
Layer* LayerWin_CM::create_new(void) {
	return new LayerWin_CM();
}
Layer* LayerNeu::create_new(void) {
	return new LayerNeu();
}
Layer* LayerNeu_CM::create_new(void) {
	return new LayerNeu_CM();
}
Layer* LayerPool::create_new(void) {
	return new LayerPool();
}

Layer* LayerNorm::create_new(void) {
	return new LayerNorm();
}
Layer* LayerNorm_CM::create_new(void) {
	return new LayerNorm_CM();
}
Layer* LayerTernarize::create_new(void) {
	return new LayerTernarize();
}
Layer* LayerRelu::create_new(void) {
	return new LayerRelu();
}
Layer* LayerLeaky::create_new(void) {
	return new LayerLeaky();
}
Layer* LayerAdd::create_new(void) {
	return new LayerAdd();
}
Layer* LayerCustom::create_new(void) {
	return new LayerCustom();
}

Layer* LayerFork::create_new(void) {
	return new LayerFork();
}
Layer* LayerCat::create_new(void) {
	return new LayerCat();
}
Layer* LayerScatter::create_new(void) {
	return new LayerScatter();
}
Layer* LayerGather::create_new(void) {
	return new LayerGather();
}

Layer* LayerFlatten::create_new(void) {
	return new LayerFlatten();
}
Layer* LayerSoftMax::create_new(void) {
	return new LayerSoftMax();
}
Layer* LayerFifo::create_new(void) {
	return new LayerFifo();
}

// Methods to create a new clone of a Layer object

Layer* Layer::clone(void) {
	// Creating an instance of the base Layer class is not allowed
	abort();
}

Layer* LayerWin::clone(void) {
	return new LayerWin(*this);
}
Layer* LayerWin_CM::clone(void) {
	return new LayerWin_CM(*this);
}
Layer* LayerNeu::clone(void) {
	return new LayerNeu(*this);
}
Layer* LayerNeu_CM::clone(void) {
	return new LayerNeu_CM(*this);
}
Layer* LayerPool::clone(void) {
	return new LayerPool(*this);
}

Layer* LayerNorm::clone(void) {
	return new LayerNorm(*this);
}
Layer* LayerNorm_CM::clone(void) {
	return new LayerNorm_CM(*this);
}
Layer* LayerTernarize::clone(void) {
	return new LayerTernarize(*this);
}
Layer* LayerRelu::clone(void) {
	return new LayerRelu(*this);
}
Layer* LayerLeaky::clone(void) {
	return new LayerLeaky(*this);
}
Layer* LayerAdd::clone(void) {
	return new LayerAdd(*this);
}
Layer* LayerCustom::clone(void) {
	return new LayerCustom(*this);
}

Layer* LayerFork::clone(void) {
	return new LayerFork(*this);
}
Layer* LayerCat::clone(void) {
	return new LayerCat(*this);
}
Layer* LayerScatter::clone(void) {
	return new LayerScatter(*this);
}
Layer* LayerGather::clone(void) {
	return new LayerGather(*this);
}

Layer* LayerFlatten::clone(void) {
	return new LayerFlatten(*this);
}
Layer* LayerSoftMax::clone(void) {
	return new LayerSoftMax(*this);
}
Layer* LayerFifo::clone(void) {
	return new LayerFifo(*this);
}

// Methods to re-parameterize a new layer (created with create_new or clone) with the user-specified layer name

int Layer::params_from_type_name(char const * type_name) {
	// Nothing to do by default
	return 0;
}

int LayerPool::params_from_type_name(char const * type_name) {
	// If name is unspecified, it means the layer is probably being created from config regs
	// The pooling type is not known at this stage
	if(type_name == nullptr) {
		return 0;
	}
	// Assign the pooling type
	unsigned type_wanted = POOL_TYPE_NONE;
	if     (strcmp(type_name, "max") == 0 || strcmp(type_name, "maxpool") == 0) type_wanted = POOL_TYPE_MAX;
	else if(strcmp(type_name, "min") == 0 || strcmp(type_name, "minpool") == 0) type_wanted = POOL_TYPE_MIN;
	else if(strcmp(type_name, "avg") == 0 || strcmp(type_name, "avgpool") == 0) type_wanted = POOL_TYPE_AVG;
	else if(strcmp(type_name, "add") == 0 || strcmp(type_name, "addpool") == 0) type_wanted = POOL_TYPE_ADD;
	else return 1;
	// Overwrite the pooling type
	pool_type = type_wanted;
	return 0;
}

// Methods to define per-layer config registers

void Layer::DefineConfigRegs(void) {
	// Nothing to do
}

void LayerWin::DefineConfigRegs(void) {

	regs_fields.reserve(7);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Register 0
	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(5);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_dwconv = &LayerRegField::AppendBit  (cur_reg, "dwconv",       16);  // constant
	regfield_symxy  = &LayerRegField::AppendBit  (cur_reg, "symxy",        17);  // constant
	regfield_repeat = &LayerRegField::AppendRange(cur_reg, "repeat",   31, 18);

	// Register 1
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_fx     = &LayerRegField::AppendRange(cur_reg, "fx",       15,  0);  // fsize x
	regfield_fx_max = &LayerRegField::AppendRange(cur_reg, "fx_max",   31, 16);  // fsize x max (constant)

	// Register 2
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_fz     = &LayerRegField::AppendRange(cur_reg, "fz",       15,  0);  // fsize z
	regfield_fz_max = &LayerRegField::AppendRange(cur_reg, "fz_max",   31, 16);  // fsize z max (constant)

	// Register 3
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(4);
	regfield_stepx  = &LayerRegField::AppendRange(cur_reg, "step_x",    5,  0);  // step x (6b)
	regfield_winx   = &LayerRegField::AppendRange(cur_reg, "win_x",    11,  6);  // win x (6b, constant)
	regfield_padx   = &LayerRegField::AppendRange(cur_reg, "pad_x",    15, 12);  // begpad x (4b)
	regfield_nwinx  = &LayerRegField::AppendRange(cur_reg, "nwin_x",   31, 16);  // nwin x (16b)

	// Register 4
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_nwinz  = &LayerRegField::AppendRange(cur_reg, "nwin_z",   15,  0);  // nwin z (16b)
	regfield_par_oz = &LayerRegField::AppendRange(cur_reg, "par_oz",   31, 16);  // par_oz (16b, constant)

	// Register 5
	// Optional register : Only present if the window is non-symmetrical on X/Y dimensions
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_fy     = &LayerRegField::AppendRange(cur_reg, "fy",       15,  0);  // fsize y
	regfield_fy_max = &LayerRegField::AppendRange(cur_reg, "fy_max",   31, 16);  // fsize y max (constant)

	// Register 6
	// Optional register : Only if the window is non-symmetrical on X/Y dimensions
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(4);
	regfield_stepy  = &LayerRegField::AppendRange(cur_reg, "step_y",    5,  0);  // step y (6b)
	regfield_winy   = &LayerRegField::AppendRange(cur_reg, "win_y",    11,  6);  // win y (6b, constant)
	regfield_pady   = &LayerRegField::AppendRange(cur_reg, "pad_y",    15, 12);  // begpad y (4b)
	regfield_nwiny  = &LayerRegField::AppendRange(cur_reg, "nwin_y",   31, 16);  // nwin y (16b)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerNeu::DefineConfigRegs(void) {

	regs_fields.reserve(4);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Register 0
	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(4);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_dwconv   = &LayerRegField::AppendBit  (cur_reg, "dwconv",         16);  // constant
	regfield_tmux     = &LayerRegField::AppendRange(cur_reg, "tmux",       31, 18);

	// Register 1
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_fsize     = &LayerRegField::AppendRange(cur_reg, "fsize",     15,  0);  // fsize
	regfield_fsize_max = &LayerRegField::AppendRange(cur_reg, "fsize_max", 31, 16);  // fsize max (constant)

	// Register 2
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_neu      = &LayerRegField::AppendRange(cur_reg, "neu",       15,  0);  // neurons
	regfield_neu_max  = &LayerRegField::AppendRange(cur_reg, "neu_max",   31, 16);  // neurons max (constant)

	// Register 3
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(10);
	regfield_nperblk  = &LayerRegField::AppendRange(cur_reg, "nperblk",    5,  0);  // number of neurons per block, minus 1 (6 bits, constant)
	regfield_wrnb     = &LayerRegField::AppendRange(cur_reg, "wrnb",      11,  6);  // number of values per neuron and per config word, minus 1 (6 bits, constant)
	regfield_wweight  = &LayerRegField::AppendRange(cur_reg, "wweight",   17, 12);  // weight width, minus 1 (6 bits, constant)
	regfield_sdlock   = &LayerRegField::AppendBit  (cur_reg, "sdlock",        18);  // data signedness, lock: 0=configurable, 1=locked (constant)
	regfield_sdata    = &LayerRegField::AppendBit  (cur_reg, "sdata",         19);  // data signedness, 0=unsigned, 1=signed
	regfield_swlock   = &LayerRegField::AppendBit  (cur_reg, "swlock",        20);  // weight signedness, lock: 0=configurable, 1=locked (constant)
	regfield_sweight  = &LayerRegField::AppendBit  (cur_reg, "sweight",       21);  // weight signedness, 0=unsigned, 1=signed
	regfield_style    = &LayerRegField::AppendRange(cur_reg, "style",     23, 22);  // layer style (constant)
	regfield_mul_id   = &LayerRegField::AppendRange(cur_reg, "mul_id",    31, 24);  // custom multiplication ID (8 bits, constant, 0 means normal operation)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

	// FIXME Add one bit to indicate weights are constants
	// FIXME Remove support for old styles 0 and 1, replace fields nperblk and wrnb by one field for write width
	// FIXME Add a field to indicate the type of compression (when compression has to be done offline)
	// FIXME Enable to not have a memory at all (weights may be generated by a custom state machine in place of the decompressor) ?
	//   Or just enable to provide a custom implementation for the "memory" of weights ? potentially with no write channel...
	// FIXME Enable decompressors to also receive the "read" address ? to further reduce the size of in-memory codes, and organize storage in recoding memory differently
	//   Consider propagating : counter within frame, tmux counter, full address in memory
	// Extra config register fields :
	//   16b : compression ID
	//     0 = no compression
	//     1 = reserved
	//     2 = ternary 2t3b (experimental)
	//     3 = ternary 3t5b
	//     4 = ternary 4t7b (experimental)
	//     5 = ternary 5t8b
	//     6-15 = reserved
	//     16 = Built-in type for custom generic LUT-based recoding
	//     Beyond : other custom types
  //   1b : indicate if compression is transparent (usefulness is unknown)
  //   1b : indicate if there is no compressed word to take into account (the decompressor is probably a state machine that generates the weights)
  //   5b : Size of a compressed word, minus 1
	//   4b : Number of weights per compressed word, minus 1

}

void LayerNeu_CM::DefineConfigRegs(void) {

	regs_fields.reserve(4);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Register 0
	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(4);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_dwconv   = &LayerRegField::AppendBit  (cur_reg, "dwconv",         16);  // constant
	regfield_tmux     = &LayerRegField::AppendRange(cur_reg, "tmux",       31, 18);

	// Register 1
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_fsize     = &LayerRegField::AppendRange(cur_reg, "fsize",     15,  0);  // fsize
	regfield_fsize_max = &LayerRegField::AppendRange(cur_reg, "fsize_max", 31, 16);  // fsize max (constant)

	// Register 2
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_neu      = &LayerRegField::AppendRange(cur_reg, "neu",       15,  0);  // neurons
	regfield_neu_max  = &LayerRegField::AppendRange(cur_reg, "neu_max",   31, 16);  // neurons max (constant)

	// Register 3
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(10);
	regfield_nperblk  = &LayerRegField::AppendRange(cur_reg, "nperblk",    5,  0);  // number of neurons per block, minus 1 (6 bits, constant)
	regfield_wrnb     = &LayerRegField::AppendRange(cur_reg, "wrnb",      11,  6);  // number of values per neuron and per config word, minus 1 (6 bits, constant)
	regfield_wweight  = &LayerRegField::AppendRange(cur_reg, "wweight",   17, 12);  // weight width, minus 1 (6 bits, constant)
	regfield_sdlock   = &LayerRegField::AppendBit  (cur_reg, "sdlock",        18);  // data signedness, lock: 0=configurable, 1=locked (constant)
	regfield_sdata    = &LayerRegField::AppendBit  (cur_reg, "sdata",         19);  // data signedness, 0=unsigned, 1=signed
	regfield_swlock   = &LayerRegField::AppendBit  (cur_reg, "swlock",        20);  // weight signedness, lock: 0=configurable, 1=locked (constant)
	regfield_sweight  = &LayerRegField::AppendBit  (cur_reg, "sweight",       21);  // weight signedness, 0=unsigned, 1=signed
	regfield_style    = &LayerRegField::AppendRange(cur_reg, "style",     23, 22);  // layer style (constant)
	regfield_mul_id   = &LayerRegField::AppendRange(cur_reg, "mul_id",    31, 24);  // custom multiplication ID (8 bits, constant, 0 means normal operation)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

	// FIXME Add one bit to indicate weights are constants
	// FIXME Remove support for old styles 0 and 1, replace fields nperblk and wrnb by one field for write width
	// FIXME Add a field to indicate the type of compression (when compression has to be done offline)
	// FIXME Enable to not have a memory at all (weights may be generated by a custom state machine in place of the decompressor) ?
	//   Or just enable to provide a custom implementation for the "memory" of weights ? potentially with no write channel...
	// FIXME Enable decompressors to also receive the "read" address ? to further reduce the size of in-memory codes, and organize storage in recoding memory differently
	//   Consider propagating : counter within frame, tmux counter, full address in memory
	// Extra config register fields :
	//   16b : compression ID
	//     0 = no compression
	//     1 = reserved
	//     2 = ternary 2t3b (experimental)
	//     3 = ternary 3t5b
	//     4 = ternary 4t7b (experimental)
	//     5 = ternary 5t8b
	//     6-15 = reserved
	//     16 = Built-in type for custom generic LUT-based recoding
	//     Beyond : other custom types
  //   1b : indicate if compression is transparent (usefulness is unknown)
  //   1b : indicate if there is no compressed word to take into account (the decompressor is probably a state machine that generates the weights)
  //   5b : Size of a compressed word, minus 1
	//   4b : Number of weights per compressed word, minus 1

}

void LayerPool::DefineConfigRegs(void) {

	regs_fields.reserve(2);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Register 0
	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(5);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_type    = &LayerRegField::AppendRange(cur_reg, "type",    19, 16);  // type of pooling (4b)
	regfield_rndnear = &LayerRegField::AppendBit  (cur_reg, "rndnear",     20);  // rounding to nearest, mostly for AvgPool
	regfield_avgreg  = &LayerRegField::AppendBit  (cur_reg, "avgreg",      21);  // have extra register 2, for fields mostly for AvgPool

	// Register 1
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(1);
	regfield_fsize = &LayerRegField::AppendRange(cur_reg, "fsize",    15,  0);  // fsize (16b)

	// Register 2
	// These fields are mostly for AvgPool
	// Optional register : Only present for AvgPool, if the bit avgreg is set
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_mul = &LayerRegField::AppendRange(cur_reg, "mul",    23,  0);  // multiplier (24b)
	regfield_shr = &LayerRegField::AppendRange(cur_reg, "shr",    28, 24);  // shift right (5b)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerNorm::DefineConfigRegs(void) {

	regs_fields.reserve(3);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Register 0
	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(7);
	LayerRegField::AppendRange(cur_reg, "layer_id",   7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",   15,  8);
	// Extra fields in first register
	regfield_enbias  = &LayerRegField::AppendBit  (cur_reg, "enbias",        16);  // bias support enabled (constant)
	regfield_enmul   = &LayerRegField::AppendBit  (cur_reg, "enmul",         17);  // multiplier support enabled (constant)
	regfield_wbias   = &LayerRegField::AppendRange(cur_reg, "wbias",     22, 18);  // width of bias field on config memory, minus 1 (5b, constant)
	regfield_wmul    = &LayerRegField::AppendRange(cur_reg, "wmul",      27, 23);  // width of multiplier field in config mem, minus 1 (5b, constant)
	regfield_wshr    = &LayerRegField::AppendRange(cur_reg, "wshr",      30, 28);  // width of shr field in config mem, zero means disabled (3b, constant)

	// Register 1
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_fsize     = &LayerRegField::AppendRange(cur_reg, "fsize",     15,  0);  // fsize
	regfield_fsize_max = &LayerRegField::AppendRange(cur_reg, "fsize_max", 31, 16);  // fsize max (constant)

	// Register 2
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(3);
	regfield_cstmul  = &LayerRegField::AppendRange(cur_reg, "cstmul",    23,  0);  // constant multiplier (24b, constant)
	regfield_cstshr  = &LayerRegField::AppendRange(cur_reg, "cstshr",    28, 24);  // constant shift (5b, constant)
	regfield_rndtype = &LayerRegField::AppendRange(cur_reg, "rndtype",   30, 29);  // type of rounding (2b)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerTernarize::DefineConfigRegs(void) {

	regs_fields.reserve(2);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(6);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_out_static = &LayerRegField::AppendBit(cur_reg, "out_static",  16);  // indicate outputs are static, not stored in memory
	regfield_out_low = &LayerRegField::AppendRange(cur_reg, "out_low",  19, 18);  // output below low threshold (2b)
	regfield_out_med = &LayerRegField::AppendRange(cur_reg, "out_med",  21, 20);  // output between thresholds (2b)
	regfield_out_up  = &LayerRegField::AppendRange(cur_reg, "out_up",   23, 22);  // output above upper threshold (2b)

	// Register 1
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	regfield_fsize     = &LayerRegField::AppendRange(cur_reg, "fsize",     15,  0);  // fsize
	regfield_fsize_max = &LayerRegField::AppendRange(cur_reg, "fsize_max", 31, 16);  // fsize max (constant)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerRelu::DefineConfigRegs(void) {

	regs_fields.reserve(3);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	// (no extra fields)

	// Register 1
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(1);
	regfield_thmin = &LayerRegField::AppendRange(cur_reg, "thmin",    31,  0);  // min threshold

	// Register 2
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(1);
	regfield_thmax = &LayerRegField::AppendRange(cur_reg, "thmax",    31,  0);  // max threshold

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}
void LayerLeaky::DefineConfigRegs(void) {

	regs_fields.reserve(3);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	// (no extra fields)

	// Register 1
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(1);
	regfield_thmin = &LayerRegField::AppendRange(cur_reg, "thmin",    31,  0);  // min threshold

	// Register 2
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(1);
	regfield_thmax = &LayerRegField::AppendRange(cur_reg, "thmax",    31,  0);  // max threshold

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerAdd::DefineConfigRegs(void) {

	regs_fields.reserve(1);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(2);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	// (no extra fields)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerCustom::DefineConfigRegs(void) {

	regs_fields.reserve(1);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(3);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_func_id = &LayerRegField::AppendRange(cur_reg, "func_id",  23, 16);  // custom function ID (8b, constant)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerFork::DefineConfigRegs(void) {

	// FORK layer :
	// Target layers will all read data at the same rate (common data valid signal)
	// reg0 : bits 31-16 : number of target layers
	// reg1 : bits 15-00 : ID of target layer 0
	//        bits 31-16 : ID of target layer 1
	// reg2 : bits 15-00 : ID of target layer 2 (if applicable)
	// etc

	// Optim possible : Drop the field layers_nb, instead use the number of registers specified in input channel config reg
	//   Decide that layer ID ~0 is invalid and marks the end of the list of layers
	// Or if need room for more flags, just reduce the local field layers_nb to the offset (unsigned) compared to max possible : regs_nb * 2 - 1

	regs_fields.reserve(1);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(3);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_layers_nb = &LayerRegField::AppendRange(cur_reg, "layers_nb", 31, 16);  // Number of target layers (16b, constant)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerCat::DefineConfigRegs(void) {

	// CAT layer
	// It is a concatenation of the outputs of the source layers
	// Source layers will all output data at the same rate (common data valid signal)
	// Source layers must have same output image size XY, can have different Z and PAR_OUT, outputs are concatenated in Z dimension
	// reg0 : bits 31-16 : number of source layers
	// reg1 : bits 15-00 : ID of target layer 0
	//        bits 31-16 : ID of target layer 1
	// reg2 : bits 15-00 : ID of target layer 2 (if applicable)
	// etc

	// Optim possible : Drop the field layers_nb, instead use the number of registers specified in input channel config reg
	//   Decide that layer ID ~0 is invalid and marks the end of the list of layers
	// Or if need room for more flags, just reduce the local field layers_nb to the offset (unsigned) compared to max possible : regs_nb * 2 - 1

	regs_fields.reserve(1);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(3);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_layers_nb = &LayerRegField::AppendRange(cur_reg, "layers_nb", 31, 16);  // Number of source layers (16b, constant)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

void LayerSoftMax::DefineConfigRegs(void) {

	regs_fields.reserve(1);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Mandatory fields
	regs_fields.emplace_back();
	cur_reg = &regs_fields.back();
	cur_reg->reserve(3);
	LayerRegField::AppendRange(cur_reg, "layer_id",  7,  0);
	LayerRegField::AppendRange(cur_reg, "regs_nb",  15,  8);
	// Extra fields in first register
	regfield_fsize = &LayerRegField::AppendRange(cur_reg, "fsize",    31, 16);  // fsize (16b)

	// Assign register index into fields
	LayerRegField::AssignRegIdx(regs_fields);

	// Check overlaps
	LayerRegField::EnsureNoOverlaps(regs_fields);

}

//============================================
// Network class
//============================================

Network* Network::singleton = nullptr;

// The only way of obtaining an HwAcc object
Network* Network::GetSingleton(void) {
	if(singleton == nullptr) {
		singleton = new Network();
	}
	// Ensure the common register fields are defined
	HwAcc_Common::DefineConfigRegs();
	return singleton;
}
void Network::DeleteSingleton(void) {
	if(singleton != nullptr) {
		delete singleton;
	}
	singleton = nullptr;
}

void Network::clear(void) {

	// Clear the contents of the layers
	for(auto layer : layers) {
		delete layer;
	}

	// Empty the vector of layers

	layers.clear();
	layer_first = nullptr;
	layer_last = nullptr;

	// Reset other fields

	// FIXMEEEE This variable is out of the scope of this class
	// Also need to null the global pointer to layers
	param_out_layer = nullptr;

	param_cnn_origin = CNN_ORIGIN_SCRIPT;

	// Reset the counters per layer type

	layers_idx.clear();

	layers_idxhw = 0;
	layers_idxcfg = 0;

}

layer_t* Network::getlayer_from_string_id(const char* strid) {
	if(strid==NULL || strid[0]==0) return NULL;
	if(layers.size()==0) return NULL;

	// Check if the string is all digits
	// FIXME Missing spec about what the ID means, and this way of getting a layer may not be useful
	#if 0
	bool alldigit = true;
	for(const char* ptrid=strid; (*ptrid)!=0; ptrid++) {
		if(isdigit(*ptrid)) continue;
		alldigit = false;
		break;
	}
	// Get the layer from integer ID, as index in the stack of layers
	if(alldigit==true) {
		int id = atoi(strid);
		if(id < 0 || id >= layers_nb) return NULL;
		return layers + id;
	}
	#endif

	// Handle special string identifiers
	if(strcasecmp(strid, "first")==0) {
		return layer_first;
	}
	if(strcasecmp(strid, "last")==0) {
		return layer_last;
	}

	// Search layer from <type><idx>
	char buf[100];
	for(auto layer : layers) {
		sprintf(buf, "%s%u", layer->typenamel, layer->typeidx);
		if(strcasecmp(buf, strid)==0) return layer;
	}

	return nullptr;
}

layer_t* Network::getlayer_from_nameidx(const char* type_name, unsigned typeidx) {
	for(auto layer : layers) {
		if(strcasecmp(layer->typenamel, type_name)!=0) continue;
		if(layer->typeidx!=typeidx) continue;
		return layer;
	}
	return nullptr;
}

layer_t* Network::getlayer_from_hwid(unsigned hwid) {
	for(auto layer : layers) {
		if(layer->id != (int)hwid) continue;
		return layer;
	}
	return nullptr;
}


//============================================
// Build NN
//============================================

int declare_builtin_layers(void) {
	int errors_nb = 0;
	Layer* layer = nullptr;

	layer = new LayerWin();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "window");
	layer->DefineConfigRegs();

	layer = new LayerWin_CM();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "window_cm");
	layer->DefineConfigRegs();

	layer = new LayerNeu();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "neuron");
	errors_nb += Layer::register_type(layer, "neurons");
	layer->DefineConfigRegs();
	
	layer = new LayerNeu_CM();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "neuron_cm");
	layer->DefineConfigRegs();

	layer = new LayerPool();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "maxpool");
	errors_nb += Layer::register_type(layer, "minpool");
	errors_nb += Layer::register_type(layer, "avgpool");
	errors_nb += Layer::register_type(layer, "addpool");
	layer->DefineConfigRegs();

	layer = new LayerNorm();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "norm");
	errors_nb += Layer::register_type(layer, "batchnorm");
	layer->DefineConfigRegs();

	layer = new LayerNorm_CM();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "norm_cm");
	layer->DefineConfigRegs();

	layer = new LayerTernarize();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "ternarize");
	errors_nb += Layer::register_type(layer, "recode_to_ter");
	layer->DefineConfigRegs();

	layer = new LayerRelu();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "relu");
	layer->DefineConfigRegs();

	layer = new LayerLeaky();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "leaky");
	layer->DefineConfigRegs();

	layer = new LayerAdd();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "add");
	layer->DefineConfigRegs();

	layer = new LayerFork();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "fork");
	layer->DefineConfigRegs();

	layer = new LayerCat();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "concat");
	layer->DefineConfigRegs();

	layer = new LayerScatter();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "scatter");
	layer->DefineConfigRegs();

	layer = new LayerGather();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "gather");
	layer->DefineConfigRegs();

	layer = new LayerFlatten();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "flatten");
	layer->DefineConfigRegs();

	layer = new LayerSoftMax();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "softmax");
	layer->DefineConfigRegs();

	layer = new LayerFifo();
	errors_nb += Layer::register_type(layer, layer->typenamel);
	errors_nb += Layer::register_type(layer, "fifo");
	layer->DefineConfigRegs();

	return errors_nb;
}

layer_t* Network::layer_new_fromtype(int type_id, char const * type_name) {

	// Get the reference layer type pointer
	layer_t* layer = Layer::create_new_from_id_verbose(type_id, type_name);
	if(layer == nullptr) {
		return nullptr;
	}
	layer->apply_network_defaults(this);

	// Assign IDs according to indexes of layer types
	layer->type = type_id;
	if((unsigned)type_id >= layers_idx.size()) layers_idx.resize(type_id + 1, 0);
	layer->typeidx = layers_idx[type_id]++;
	if(layer->requires_idxhw()) layer->id = layers_idxhw++;
	if(layer->requires_idxcfg()) layer->cfg_id = layers_idxcfg++;

	// Append in the array of layers
	layer->index = layers.size();
	layers.push_back(layer);
	layer->network = this;

	return layer;
}

Layer* Network::layer_new_fromtypename(char const * type_name) {
	int type_id = Layer::get_type_name2id_verbose(type_name);
	if(type_id == LAYER_NONE) return nullptr;
	return layer_new_fromtype(type_id, type_name);
}

static void layer_add_link(layer_t* layer, layer_t* layer_other) {
	layer->arr_layers.push_back(layer_other);
}

void layer_link(layer_t* layer_prev, layer_t* layer_next) {
	// Link in prev layer
	if(layer_prev->next_is_arr == true) layer_add_link(layer_prev, layer_next);
	else layer_prev->next = layer_next;
	// Link in next layer
	if(layer_next->prev_is_arr == true) layer_add_link(layer_next, layer_prev);
	else layer_next->prev = layer_prev;
}

static void layer_link_replace_prev(layer_t* layer, layer_t* layer_prev_old, layer_t* layer_prev_new) {
	if(layer->prev == layer_prev_old) layer->prev = layer_prev_new;
	if(layer->prev_is_arr == true) {
		for(unsigned i=0; i<layer->arr_layers.size(); i++) {
			if(layer->arr_layers[i] == layer_prev_old) layer->arr_layers[i] = layer_prev_new;
		}
	}
}
static void layer_link_replace_next(layer_t* layer, layer_t* layer_next_old, layer_t* layer_next_new) {
	if(layer->next == layer_next_old) layer->next = layer_next_new;
	if(layer->next_is_arr == true) {
		for(unsigned i=0; i<layer->arr_layers.size(); i++) {
			if(layer->arr_layers[i] == layer_next_old) layer->arr_layers[i] = layer_next_new;
		}
	}
}

// Assumptions :
// - prev/next are not connected to each other, or they are neither FORK nor CAT
// - the layer to insert has no connection
void layer_insert(layer_t* layer, layer_t* layer_prev, layer_t* layer_next) {
	if(layer_prev != nullptr) layer_link(layer_prev, layer);
	if(layer_next != nullptr) layer_link(layer, layer_next);
}

// Assumptions :
// - prev/next are connected to each other
// - the layer to insert has no connection, and is neither FORK nor CAT
void layer_insert_replace(layer_t* layer, layer_t* layer_prev, layer_t* layer_next) {
	// Link prev <-> layer
	layer_link_replace_next(layer_prev, layer_next, layer);
	layer->prev = layer_prev;
	// Link layer <-> next
	layer->next = layer_next;
	layer_link_replace_prev(layer_next, layer_prev, layer);
}

// Assume that the input layer is the last created one, not yet linked with its previous one in the vector
void Network::layer_enqueue(layer_t* layer) {
	layer_insert(layer, layer_last, nullptr);
	if(layer_first == nullptr) layer_first = layer;
	layer_last = layer;
}

layer_t* Network::layer_new_enqueue_fromtype(int type_id, char const * type_name) {
	layer_t* layer = layer_new_fromtype(type_id, type_name);
	if(layer != nullptr) layer_enqueue(layer);
	return layer;
}

