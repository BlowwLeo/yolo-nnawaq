// Generate VHDL sections into a template wrapper file

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

}

#include "nn_layers_utils.h"
#include "nn_hw_config.h"
#include "hwacc_common.h"  // For definitions of fields in common config registers
#include "genvhdl.h"

using namespace std;


// Global parameters
char const * vhdl_gen_prefix = NULL;

// Directory where to generate config dump files
char* genvhdl_dump_dir = NULL;



// Utility counters and functions to print warnings

static const unsigned warn_max_number = 5;
static map<LayerRegField*, unsigned> map_warn_field_exceed;

static unsigned warn_field_get_inc(LayerRegField* field) {
	// Get the entry in the map
	auto insert_res = map_warn_field_exceed.insert(make_pair(field, 0));
	auto iter = insert_res.first;
	if(iter->second >= warn_max_number) return iter->second + 1;
	// Increment occurrence count
	iter->second ++;
	return iter->second;
}

static void warn_print_max(void) {
	printf("  (other occurrences of this warning will be skipped)\n");
}

static void warn_field_exceed(Layer* layer, LayerRegField* field, unsigned value) {
	// Get the entry in the map
	auto insert_res = map_warn_field_exceed.insert(make_pair(field, 0));
	auto iter = insert_res.first;
	if(iter->second >= warn_max_number) return;

	// Print the warning
	printf("WARNING %s%u: Value ", layer->typenameu, layer->typeidx);
	if(field->sign) printf("%i", value);
	else            printf("%u", value);
	printf(" exceeds capacity of %u bits of field '%s' in configuration registers, the accelerator will only be usable in blind mode\n",
		field->bits, field->name
	);

	// Increment occurrence count
	iter->second ++;
	if(iter->second == warn_max_number) {
		warn_print_max();
	}
}

static inline void test_warn_field_exceed(Layer* layer, LayerRegField* field, unsigned value) {
	if(field->CheckCapacity(value) == 0) return;
	warn_field_exceed(layer, field, value);
}

//============================================
// Set the number of configuration registers
// + Documentation of per-layer configuration registers
//============================================

void Layer::genvhdl_set_config_regs_numbers(void) {
	regs_nb = 0;
}

void LayerWin::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
	if(win_sym_xy == true) regs_nb -= 2;
}
void LayerNeu::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}

void LayerNeu_CM::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}

void LayerPool::genvhdl_set_config_regs_numbers(void) {
	// Only 2 registers for most usage, there may be rescaling factors mainly for AvgPool
	regs_nb = regs_fields.size();
	if(pool_avg_mult <= 1 && pool_avg_shr == 0) regs_nb --;
}

void LayerNorm::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}
void LayerTernarize::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}
void LayerRelu::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}
void LayerLeaky::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}
void LayerAdd::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}
void LayerCustom::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}

void LayerFork::genvhdl_set_config_regs_numbers(void) {
	// reg0 : bits 15-00 : reserved
	//      : bits 31-16 : number of target layers
	// reg1 : bits 15-00 : ID of target layer 0
	//      : bits 31-16 : ID of target layer 1
	// etc
	regs_nb = 1 + (arr_layers.size() + 1) / 2;
}
void LayerCat::genvhdl_set_config_regs_numbers(void) {
	// reg0 : bits 15-00 : reserved
	//      : bits 31-16 : number of source layers
	// reg1 : bits 15-00 : ID of source layer 0
	//      : bits 31-16 : ID of source layer 1
	// etc
	regs_nb = 1 + (arr_layers.size() + 1) / 2;
}

void LayerFlatten::genvhdl_set_config_regs_numbers(void) {
	regs_nb = 1;
}
void LayerSoftMax::genvhdl_set_config_regs_numbers(void) {
	regs_nb = regs_fields.size();
}

void Network::genvhdl_set_config_regs_numbers(void) {

	// Set the number of configuration registers
	for(auto layer : layers) {
		layer->regs_nb = 0;
		layer->genvhdl_set_config_regs_numbers();
	}

}

//============================================
// Helper functions for config registers
//============================================

static bool has_layer_after(const Layer* layer) {
	if(layer->next == nullptr) return false;
	// Here next!=nullptr
	if(layer->next->type != LAYER_FIFO) return true;
	// Here next layer is a FIFO
	if(layer->next->next == nullptr) return false;
	// There is a layer after the FIFO
	return true;
}

static int vhdl_gen_endline(FILE* F, char** linebuf, size_t* linebuf_size, char const * endline) {
	unsigned len = strlen(endline);

	do {
		ssize_t r = getline(linebuf, linebuf_size, F);
		if(r < 0) break;

		// Get the first non-space character
		char* beg = *linebuf;
		while((*beg)!=0 && isspace(*beg)!=0) beg++;

		if(strncasecmp(beg, endline, len)==0) return 0;

	} while(1);

	printf("Error: End line not found: '%s'\n", endline);
	exit(EXIT_FAILURE);

	return -1;
}

static const char* config_reg_name = "config_regs";
static char const * glob_config_reg_name = "config_regs";
static char const * glob_config_asg = "<=";

static void print_cfg_reg(FILE* Fo, unsigned regidx, unsigned left, unsigned right) {
	fprintf(Fo, "%s(%u*32 + %2u downto %u*32 + %2u)", glob_config_reg_name, regidx, left, regidx, right);
}
static void print_cfg_reg_bit(FILE* Fo, unsigned regidx, unsigned bitidx) {
	fprintf(Fo, "%s(%u*32 + %2u)", glob_config_reg_name, regidx, bitidx);
}
static void print_cfg_reg_varwidth(FILE* Fo, unsigned regidx, unsigned right, char const * namewidth, char const * namewidth_suff) {
	fprintf(Fo, "%s(%u*32 + %2u + %s%s - 1 downto %u*32 + %2u)", glob_config_reg_name, regidx, right, namewidth, namewidth_suff, regidx, right);
}

static void print_cfg_regasg(FILE* Fo, char const * tabs, unsigned regidx, unsigned left, unsigned right) {
	fprintf(Fo, "%s", tabs);
	print_cfg_reg(Fo, regidx, left, right);
	fprintf(Fo, " %s ", glob_config_asg);
}
static void print_cfg_regasg_bit(FILE* Fo, char const * tabs, unsigned regidx, unsigned bitidx) {
	fprintf(Fo, "%s", tabs);
	print_cfg_reg_bit(Fo, regidx, bitidx);
	fprintf(Fo, " %s ", glob_config_asg);
}

// Just generate the register range
static void print_cfg_reg(FILE* Fo, unsigned regOff, const LayerRegField& field) {
	if(field.bits == 1) print_cfg_reg_bit(Fo, regOff + field.reg_idx, field.GetLo());
	else print_cfg_reg(Fo, regOff + field.reg_idx, field.GetHi(), field.GetLo());
}
static inline void print_cfg_reg(FILE* Fo, unsigned regOff, const LayerRegField* field) {
	print_cfg_reg(Fo, regOff, *field);
}
// Just generate the destination part, with operator := or <=
static void print_cfg_regasg(FILE* Fo, char const * tabs, unsigned regOff, const LayerRegField& field) {
	if(field.bits == 1) print_cfg_regasg_bit(Fo, tabs, regOff + field.reg_idx, field.GetLo());
	else print_cfg_regasg(Fo, tabs, regOff + field.reg_idx, field.GetHi(), field.GetLo());
}
static inline void print_cfg_regasg(FILE* Fo, char const * tabs, unsigned regOff, const LayerRegField* field) {
	print_cfg_regasg(Fo, tabs, regOff, *field);
}

// Generate the complete assignment line
static void print_cfg_regasg_from_cstbit(FILE* Fo, char const * tabs, const layer_t* layer, const LayerRegField& field, char const * cstbit, const char* eol_comment = nullptr) {
	if(field.bits == 1) {
		print_cfg_regasg_bit(Fo, tabs, layer->regs_idx + field.reg_idx, field.GetLo());
		fprintf(Fo, "to_std_logic(%s);", cstbit);
	}
	else {
		print_cfg_regasg(Fo, tabs, layer->regs_idx + field.reg_idx, field.GetHi(), field.GetLo());
		fprintf(Fo, "(others => %s);", cstbit);
	}
	if(eol_comment != nullptr) fprintf(Fo, "%s", eol_comment);
	fprintf(Fo, "\n");
}
static void print_cfg_regasg_from_cstval(FILE* Fo, char const * tabs, const layer_t* layer, const LayerRegField& field, unsigned cstval, const char* eol_comment = nullptr) {
	if(field.bits == 1) {
		print_cfg_regasg_bit(Fo, tabs, layer->regs_idx + field.reg_idx, field.GetLo());
		fprintf(Fo, "'%c';", (cstval != 0) ? '1' : '0');
	}
	else {
		print_cfg_regasg(Fo, tabs, layer->regs_idx + field.reg_idx, field.GetHi(), field.GetLo());
		if(field.sign) fprintf(Fo, "std_logic_vector(to_signed(%i, %u));", (int)cstval, field.bits);
		else           fprintf(Fo, "std_logic_vector(to_unsigned(%u, %u));", cstval, field.bits);
	}
	if(eol_comment != nullptr) fprintf(Fo, "%s", eol_comment);
	fprintf(Fo, "\n");
}
static void print_cfg_regasg_from_signal(FILE* Fo, char const * tabs, layer_t* layer, const LayerRegField& field, char const * suff_field, const char* eol_comment = nullptr) {
	if(field.bits == 1) {
		print_cfg_regasg_bit(Fo, tabs, layer->regs_idx + field.reg_idx, field.GetLo());
		fprintf(Fo, "to_std_logic(%s_%s);", layer->vhdl_prefixu, suff_field);
	}
	else {
		print_cfg_regasg(Fo, tabs, layer->regs_idx + field.reg_idx, field.GetHi(), field.GetLo());
		fprintf(Fo, "std_logic_vector(to_%s(%s_%s, %u));", field.sign ? "signed" : "unsigned", layer->vhdl_prefixu, suff_field, field.bits);
	}
	if(eol_comment != nullptr) fprintf(Fo, "%s", eol_comment);
	fprintf(Fo, "\n");
}
static void print_cfg_regasg_from_signal(FILE* Fo, char const * tabs, const layer_t* layer, const LayerRegField& field, char const * suff_field, int off, const char* eol_comment = nullptr) {
	if(field.bits == 1) {
		print_cfg_regasg_bit(Fo, tabs, layer->regs_idx + field.reg_idx, field.GetLo());
		fprintf(Fo, "to_std_logic(%s_%s%+i);", layer->vhdl_prefixu, suff_field, off);
	}
	else {
		print_cfg_regasg(Fo, tabs, layer->regs_idx + field.reg_idx, field.GetHi(), field.GetLo());
		fprintf(Fo, "std_logic_vector(to_%s(%s_%s%+i, %u));", field.sign ? "signed" : "unsigned", layer->vhdl_prefixu, suff_field, off, field.bits);
	}
	if(eol_comment != nullptr) fprintf(Fo, "%s", eol_comment);
	fprintf(Fo, "\n");
}

// Generate the config register that describes a channel between layers
// FIXME Enable to generate names of FIFO symbols when there is a physical FIFO

static void print_cfg_regasg_layer_in(FILE* Fo, char const * tabs, const layer_t* layer,
	char const * suff_wdata, char const * suff_sdata, char const * suff_par
) {
	if(tabs==nullptr) tabs = "	";
	unsigned reg_idx = layer->regs_idx - 1;

	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_enc);
	fprintf(Fo, "std_logic_vector(to_unsigned(0, %u));  -- Data encoding, always zero for now\n", HwAcc_Common::chanfield_wdata->bits);

	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_wdata);
	fprintf(Fo, "std_logic_vector(to_unsigned(%s_%s, %u));\n", layer->vhdl_prefixu, suff_wdata, HwAcc_Common::chanfield_wdata->bits);

	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_sdata);
	fprintf(Fo, "to_std_logic(%s_%s);\n", layer->vhdl_prefixu, suff_sdata);

	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_layer);
	fprintf(Fo, "to_std_logic(true);  -- Has layer after\n");

	bool is_phy_fifo = (layer->prev != nullptr) && (layer->prev->type == LAYER_FIFO);
	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_fifo);
	fprintf(Fo, "to_std_logic(%s);  -- Is a physical fifo\n", is_phy_fifo ? "true" : "false");

	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_par);
	fprintf(Fo, "std_logic_vector(to_unsigned(%s_%s, %u));\n", layer->vhdl_prefixu, suff_par, HwAcc_Common::chanfield_par->bits);
}

// This will do nothing if there is no layer after
static void print_cfg_regasg_layer_out(FILE* Fo, char const * tabs, const layer_t* layer,
	char const * suff_wdata, char const * suff_sdata, char const * suff_par
) {
	if(tabs==nullptr) tabs = "	";
	unsigned reg_idx = layer->regs_idx + layer->regs_nb;

	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_wdata);
	fprintf(Fo, "std_logic_vector(to_unsigned(%s_%s, %u));\n", layer->vhdl_prefixu, suff_wdata, HwAcc_Common::chanfield_wdata->bits);

	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_sdata);
	fprintf(Fo, "to_std_logic(%s_%s);\n", layer->vhdl_prefixu, suff_sdata);

	bool layer_after = has_layer_after(layer);
	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_layer);
	fprintf(Fo, "to_std_logic(%s);  -- Has layer after\n", layer_after ? "true" : "false");

	bool is_phy_fifo = (layer->next != nullptr) && (layer->next->type == LAYER_FIFO);
	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_fifo);
	fprintf(Fo, "to_std_logic(%s);  -- Is a physical fifo\n", is_phy_fifo ? "true" : "false");

	print_cfg_regasg(Fo, tabs, reg_idx, HwAcc_Common::chanfield_par);
	fprintf(Fo, "std_logic_vector(to_unsigned(%s_%s, %u));\n", layer->vhdl_prefixu, suff_par, HwAcc_Common::chanfield_par->bits);
}

// Generate the mandatory fields of the first config register of a layer
static void print_cfg_regasg_layer_reg(FILE* Fo, char const * tabs, const layer_t* layer) {
	if(tabs==nullptr) tabs = "	";
	print_cfg_regasg_from_cstval(Fo, tabs, layer, *HwAcc_Common::layerfield_type,   layer->type, "  -- Layer type");
	print_cfg_regasg_from_cstval(Fo, tabs, layer, *HwAcc_Common::layerfield_nbregs, layer->regs_nb, "  -- Number of layer-specific registers");
}

//============================================
// Declarations of constants
//============================================

void Layer::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	-- No need for specific constants\n");
}

void LayerWin::genvhdl_cst_decl(FILE* Fo) {
	// Specification : This component is used with WINZ=1, STEPZ=1, and NWINZ is adjusted to FZ
	fprintf(Fo, "	constant %s_WDATA   : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA   : boolean := %s;\n", vhdl_prefixu, sdata ? "true" : "false");  // Only useful for inter-layer config registers
	fprintf(Fo, "	constant %s_DIMX    : natural := %u;\n", vhdl_prefixu, fx);
	fprintf(Fo, "	constant %s_DIMY    : natural := %u;\n", vhdl_prefixu, fy);
	fprintf(Fo, "	constant %s_DIMZ    : natural := %u;\n", vhdl_prefixu, fz / win_par_oz);
	fprintf(Fo, "	constant %s_WINX    : natural := %u;\n", vhdl_prefixu, winx);
	fprintf(Fo, "	constant %s_WINY    : natural := %u;\n", vhdl_prefixu, winy);
	fprintf(Fo, "	constant %s_WINZ    : natural := %u;\n", vhdl_prefixu, 1);
	fprintf(Fo, "	constant %s_STEPX   : natural := %u;\n", vhdl_prefixu, stepx);
	fprintf(Fo, "	constant %s_STEPY   : natural := %u;\n", vhdl_prefixu, stepy);
	fprintf(Fo, "	constant %s_STEPZ   : natural := %u;\n", vhdl_prefixu, 1);
	fprintf(Fo, "	constant %s_NWINX   : natural := %u;\n", vhdl_prefixu, nwinx);
	fprintf(Fo, "	constant %s_NWINY   : natural := %u;\n", vhdl_prefixu, nwiny);
	fprintf(Fo, "	constant %s_NWINZ   : natural := %u;\n", vhdl_prefixu, out_fz / win_par_oz);
	fprintf(Fo, "	constant %s_PADX    : natural := %u;\n", vhdl_prefixu, begpadx);
	fprintf(Fo, "	constant %s_PADY    : natural := %u;\n", vhdl_prefixu, begpady);
	fprintf(Fo, "	constant %s_BUFY    : natural := %u;\n", vhdl_prefixu, bufy);
	fprintf(Fo, "	constant %s_USE_LUTRAM : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_LUTRAM ? "true" : "false");
	fprintf(Fo, "	constant %s_USE_BRAM   : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_BRAM   ? "true" : "false");
	fprintf(Fo, "	constant %s_USE_URAM   : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_URAM   ? "true" : "false");
	fprintf(Fo, "	constant %s_REPEAT  : natural := %u;\n", vhdl_prefixu, win_repeat);
	fprintf(Fo, "	constant %s_DWCONV  : boolean := %s;\n", vhdl_prefixu, win_dwconv ? "true" : "false");
	fprintf(Fo, "	constant %s_SYMXY   : boolean := %s;\n", vhdl_prefixu, win_sym_xy ? "true" : "false");
	fprintf(Fo, "	constant %s_PAR_IN  : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	constant %s_PAR_OUT : natural := %u;\n", vhdl_prefixu, split_out);
	fprintf(Fo, "	constant %s_PAR_OZ  : natural := %u;\n", vhdl_prefixu, win_par_oz);
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}
void LayerNeu::genvhdl_cst_decl(FILE* Fo) {

	unsigned loc_fsize     = (fsize       + split_in  - 1) / split_in;
	unsigned loc_fsize_max = (fsize_max   + split_in  - 1) / split_in;
	unsigned loc_nbneu     = (neurons     + split_out - 1) / split_out / neu_time_mux;
	unsigned loc_nbneu_max = (neurons_max + split_out - 1) / split_out / neu_time_mux;
	unsigned loc_tmux      = neu_time_mux;

	// DWConv example :
	// Consider the case with no user-specified time multiplexing
	//   Win FZ = 30, PAR_OZ = 6
	//   Neurons = 30 logical, 6 physical, TMUX = 30/6=5
	// Now, apply extra time multiplexing to slow these layers down => apply extra factor 3x
	//   Win FZ = 30, PAR_OZ = 2
	//   Neurons = 30 logical, 2 physical, overall TMUX = 30/5*3=15
	if(win_dwconv == true) {
		if(neu_time_mux != fz / win_par_oz) {
			printf("WARNING %s%u: Inconsistent TMUX parameter in DWConv mode\n", typenameu, typeidx);
		}
		loc_nbneu      = (neurons     + split_out - 1) / split_out / (fz / win_par_oz);
		loc_nbneu_max  = (neurons_max + split_out - 1) / split_out / (fz / win_par_oz);
	}

	// FIXME In case PAR_IN > 1 and DWCONV=1, inputs must be re-ordered
	// It is not clear where is the right place to reorder activations

	fprintf(Fo, "	-- Implementation style\n");
	fprintf(Fo, "	constant %s_STYLE      : natural := %u;\n", vhdl_prefixu, neu_style);
	fprintf(Fo, "	-- Parameters for the neurons\n");
	fprintf(Fo, "	constant %s_WDATA      : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA      : boolean := %s;\n", vhdl_prefixu, (neu_sgnd & NEUSGN_SIGNED) != 0 ? "true" : "false");
	if(neu_style==0) {
		fprintf(Fo, "	constant %s_SFIXED     : boolean := %s;\n", vhdl_prefixu, (neu_sgnd & NEUSGN_LOCKED) != 0 ? "true" : "false");
		fprintf(Fo, "	constant %s_WACCU      : natural := %u;\n", vhdl_prefixu, neu_waccu);
	}
	if(neu_style==1 || neu_style==2) {
		fprintf(Fo, "	constant %s_WWEIGHT    : natural := %u;\n", vhdl_prefixu, neu_wweight);
		fprintf(Fo, "	constant %s_SWEIGHT    : boolean := %s;\n", vhdl_prefixu, (neu_sgnw & NEUSGN_SIGNED) != 0 ? "true" : "false");
	}
	fprintf(Fo, "	constant %s_WOUT       : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	-- Parameters for BRAM usage\n");
	fprintf(Fo, "	constant %s_NPERBLK    : natural := %u;\n", vhdl_prefixu, neu_per_bram);
	fprintf(Fo, "	constant %s_WRNB       : natural := %u;\n", vhdl_prefixu, neu_wrnb);
	if(neu_style==1 || neu_style==2) {
		unsigned loc_wwrite = neu_wweight * neu_wrnb * neu_per_bram;
		fprintf(Fo, "	constant %s_WWRITE     : natural := %u;\n", vhdl_prefixu, loc_wwrite);
		fprintf(Fo, "	constant %s_REGS       : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_REG    ? "true" : "false");
		fprintf(Fo, "	constant %s_LUTRAM     : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_LUTRAM ? "true" : "false");
		fprintf(Fo, "	constant %s_BRAM       : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_BRAM   ? "true" : "false");
		fprintf(Fo, "	constant %s_URAM       : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_URAM   ? "true" : "false");
		fprintf(Fo, "	constant %s_CONST      : boolean := %s;\n", vhdl_prefixu, const_params ? "true" : "false");
		fprintf(Fo, "	constant %s_PACKED     : boolean := %s;\n", vhdl_prefixu, neu_style == 2 ? "true" : "false");
		fprintf(Fo, "	-- Identifier of multiplication operation\n");
		fprintf(Fo, "	constant %s_LAYER_ID : natural := %u;\n", vhdl_prefixu, cfg_id);
		fprintf(Fo, "	-- For compression of weights in memory\n");
		fprintf(Fo, "	constant %s_COMP_STYLE : natural := %u;\n", vhdl_prefixu, neu_comp_style);
		fprintf(Fo, "	constant %s_COMP_WRAW  : natural := %u;\n", vhdl_prefixu, neu_comp_nraw * neu_wweight);
		fprintf(Fo, "	constant %s_COMP_WENC  : natural := %u;\n", vhdl_prefixu, neu_comp_nbin);
		fprintf(Fo, "	constant %s_COMP_ENWR  : boolean := %s;\n", vhdl_prefixu, "true");
	}
	fprintf(Fo, "	-- Parameters for frame and number of neurons\n");
	fprintf(Fo, "	constant %s_FSIZE      : natural := %u;\n", vhdl_prefixu, loc_fsize);
	fprintf(Fo, "	constant %s_FSIZE_MAX  : natural := %u;\n", vhdl_prefixu, loc_fsize_max);
	fprintf(Fo, "	constant %s_NBNEU      : natural := %u;\n", vhdl_prefixu, loc_nbneu);
	fprintf(Fo, "	constant %s_NBNEU_MAX  : natural := %u;\n", vhdl_prefixu, loc_nbneu_max);
	fprintf(Fo, "	-- Level of time multiplexing of the neuron accumulators\n");
	fprintf(Fo, "	constant %s_TIME_MUX   : natural := %u;\n", vhdl_prefixu, loc_tmux);
	fprintf(Fo, "	-- Depth-Wise convolution mode : each activation goes to only one physical neuron\n");
	fprintf(Fo, "	constant %s_DWCONV     : boolean := %s;\n", vhdl_prefixu, win_dwconv==true ? "true" : "false");
	fprintf(Fo, "	-- Parameters for splitting frames and scan chain\n");
	fprintf(Fo, "	constant %s_PAR_IN     : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	constant %s_PAR_OUT    : natural := %u;\n", vhdl_prefixu, split_out);
	if(neu_style == 1 || neu_style == 2) {
		fprintf(Fo, "	-- Identifier of multiplication operation\n");
		fprintf(Fo, "	constant %s_CUSTOM_MUL_ID : natural := %u;\n", vhdl_prefixu, neu_custom_mul_id);
		fprintf(Fo, "	constant %s_CUSTOM_WMUL   : natural := %u;\n", vhdl_prefixu, neu_custom_wmul > 0 ? neu_custom_wmul : out_wdata);
		fprintf(Fo, "	constant %s_CUSTOM_SMUL   : boolean := %s;\n", vhdl_prefixu, neu_custom_smul != 0 ? "true" : "false");
	}
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}

void LayerNeu_CM::genvhdl_cst_decl(FILE* Fo) {

	unsigned loc_fsize     = (fsize       + split_in  - 1) / split_in;
	unsigned loc_fsize_max = (fsize_max   + split_in  - 1) / split_in;
	unsigned loc_nbneu     = (neurons     + split_out - 1) / split_out / neu_time_mux;
	unsigned loc_nbneu_max = (neurons_max + split_out - 1) / split_out / neu_time_mux;
	unsigned loc_tmux      = neu_time_mux;

	// DWConv example :
	// Consider the case with no user-specified time multiplexing
	//   Win FZ = 30, PAR_OZ = 6
	//   Neurons = 30 logical, 6 physical, TMUX = 30/6=5
	// Now, apply extra time multiplexing to slow these layers down => apply extra factor 3x
	//   Win FZ = 30, PAR_OZ = 2
	//   Neurons = 30 logical, 2 physical, overall TMUX = 30/5*3=15
	if(win_dwconv == true) {
		if(neu_time_mux != fz / win_par_oz) {
			printf("WARNING %s%u: Inconsistent TMUX parameter in DWConv mode\n", typenameu, typeidx);
		}
		loc_nbneu      = (neurons     + split_out - 1) / split_out / (fz / win_par_oz);
		loc_nbneu_max  = (neurons_max + split_out - 1) / split_out / (fz / win_par_oz);
	}

	// FIXME In case PAR_IN > 1 and DWCONV=1, inputs must be re-ordered
	// It is not clear where is the right place to reorder activations

	fprintf(Fo, "	-- Implementation style\n");
	fprintf(Fo, "	constant %s_STYLE      : natural := %u;\n", vhdl_prefixu, neu_style);
	fprintf(Fo, "	-- Parameters for the neurons\n");
	fprintf(Fo, "	constant %s_WDATA      : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA      : boolean := %s;\n", vhdl_prefixu, (neu_sgnd & NEUSGN_SIGNED) != 0 ? "true" : "false");
	if(neu_style==0) {
		fprintf(Fo, "	constant %s_SFIXED     : boolean := %s;\n", vhdl_prefixu, (neu_sgnd & NEUSGN_LOCKED) != 0 ? "true" : "false");
		fprintf(Fo, "	constant %s_WACCU      : natural := %u;\n", vhdl_prefixu, neu_waccu);
	}
	if(neu_style==1 || neu_style==2) {
		fprintf(Fo, "	constant %s_WWEIGHT    : natural := %u;\n", vhdl_prefixu, neu_wweight);
		fprintf(Fo, "	constant %s_SWEIGHT    : boolean := %s;\n", vhdl_prefixu, (neu_sgnw & NEUSGN_SIGNED) != 0 ? "true" : "false");
	}
	fprintf(Fo, "	constant %s_WOUT       : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	-- Parameters for BRAM usage\n");
	fprintf(Fo, "	constant %s_NPERBLK    : natural := %u;\n", vhdl_prefixu, neu_per_bram);
	fprintf(Fo, "	constant %s_WRNB       : natural := %u;\n", vhdl_prefixu, neu_wrnb);
	if(neu_style==1 || neu_style==2) {
		unsigned loc_wwrite = neu_wweight * neu_wrnb * neu_per_bram;
		fprintf(Fo, "	constant %s_WWRITE     : natural := %u;\n", vhdl_prefixu, loc_wwrite);
		fprintf(Fo, "	constant %s_REGS       : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_REG    ? "true" : "false");
		fprintf(Fo, "	constant %s_LUTRAM     : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_LUTRAM ? "true" : "false");
		fprintf(Fo, "	constant %s_BRAM       : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_BRAM   ? "true" : "false");
		fprintf(Fo, "	constant %s_URAM       : boolean := %s;\n", vhdl_prefixu, mem.style == MemImplem::STYLE_URAM   ? "true" : "false");
		fprintf(Fo, "	constant %s_CONST      : boolean := %s;\n", vhdl_prefixu, const_params ? "true" : "false");
		fprintf(Fo, "	constant %s_PACKED     : boolean := %s;\n", vhdl_prefixu, neu_style == 2 ? "true" : "false");
		fprintf(Fo, "	-- Identifier of multiplication operation\n");
		fprintf(Fo, "	constant %s_LAYER_ID : natural := %u;\n", vhdl_prefixu, cfg_id);
		fprintf(Fo, "	-- For compression of weights in memory\n");
		fprintf(Fo, "	constant %s_COMP_STYLE : natural := %u;\n", vhdl_prefixu, neu_comp_style);
		fprintf(Fo, "	constant %s_COMP_WRAW  : natural := %u;\n", vhdl_prefixu, neu_comp_nraw * neu_wweight);
		fprintf(Fo, "	constant %s_COMP_WENC  : natural := %u;\n", vhdl_prefixu, neu_comp_nbin);
		fprintf(Fo, "	constant %s_COMP_ENWR  : boolean := %s;\n", vhdl_prefixu, "true");
	}
	fprintf(Fo, "	-- Parameters for frame and number of neurons\n");
	fprintf(Fo, "	constant %s_FSIZE      : natural := %u;\n", vhdl_prefixu, loc_fsize);
	fprintf(Fo, "	constant %s_FSIZE_MAX  : natural := %u;\n", vhdl_prefixu, loc_fsize_max);
	fprintf(Fo, "	constant %s_NBNEU      : natural := %u;\n", vhdl_prefixu, loc_nbneu);
	fprintf(Fo, "	constant %s_NBNEU_MAX  : natural := %u;\n", vhdl_prefixu, loc_nbneu_max);
	fprintf(Fo, "	-- Level of time multiplexing of the neuron accumulators\n");
	fprintf(Fo, "	constant %s_TIME_MUX   : natural := %u;\n", vhdl_prefixu, loc_tmux);
	fprintf(Fo, "	-- Depth-Wise convolution mode : each activation goes to only one physical neuron\n");
	fprintf(Fo, "	constant %s_DWCONV     : boolean := %s;\n", vhdl_prefixu, win_dwconv==true ? "true" : "false");
	fprintf(Fo, "	-- Parameters for splitting frames and scan chain\n");
	fprintf(Fo, "	constant %s_PAR_IN     : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	constant %s_PAR_OUT    : natural := %u;\n", vhdl_prefixu, split_out);
	if(neu_style == 1 || neu_style == 2) {
		fprintf(Fo, "	-- Identifier of multiplication operation\n");
		fprintf(Fo, "	constant %s_CUSTOM_MUL_ID : natural := %u;\n", vhdl_prefixu, neu_custom_mul_id);
		fprintf(Fo, "	constant %s_CUSTOM_WMUL   : natural := %u;\n", vhdl_prefixu, neu_custom_wmul > 0 ? neu_custom_wmul : out_wdata);
		fprintf(Fo, "	constant %s_CUSTOM_SMUL   : boolean := %s;\n", vhdl_prefixu, neu_custom_smul != 0 ? "true" : "false");
	}
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}

void LayerPool::genvhdl_cst_decl(FILE* Fo) {
	unsigned loc_fsize = fsize / (split_in / win_par_oz);
	if(pool_type == POOL_TYPE_ADD) loc_fsize = 1;
	fprintf(Fo, "	constant %s_WDATA   : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA   : boolean := %s;\n", vhdl_prefixu, (sdata != 0) ? "true" : "false");
	fprintf(Fo, "	constant %s_WOUT    : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	constant %s_FSIZE   : natural := %u;\n", vhdl_prefixu, loc_fsize);
	fprintf(Fo, "	constant %s_NPOOL   : natural := %u;\n", vhdl_prefixu, pool_units_nb / split_out);
	fprintf(Fo, "	constant %s_TYPE    : natural := %u;\n", vhdl_prefixu, pool_type);  // Just for config regs
	fprintf(Fo, "	constant %s_OPMAX   : boolean := %s;\n", vhdl_prefixu, (pool_type == POOL_TYPE_MAX) ? "true" : "false");
	fprintf(Fo, "	constant %s_OPMIN   : boolean := %s;\n", vhdl_prefixu, (pool_type == POOL_TYPE_MIN) ? "true" : "false");
	fprintf(Fo, "	constant %s_OPADD   : boolean := %s;\n", vhdl_prefixu, (pool_type == POOL_TYPE_AVG || pool_type == POOL_TYPE_ADD) ? "true" : "false");
	fprintf(Fo, "	constant %s_MULT    : natural := %u;\n", vhdl_prefixu, pool_avg_mult + (pool_avg_mult == 0));
	fprintf(Fo, "	constant %s_SHR     : natural := %u;\n", vhdl_prefixu, pool_avg_shr);
	fprintf(Fo, "	constant %s_ROUND_NEAR : boolean := %s;\n", vhdl_prefixu, (round_nearest == true) ? "true" : "false");
	fprintf(Fo, "	constant %s_PAR_IN  : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	constant %s_PAR_OUT : natural := %u;\n", vhdl_prefixu, split_out);
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}

void LayerNorm::genvhdl_cst_decl(FILE* Fo) {
	unsigned loc_fsize = (fsize + split_in - 1) / split_in;
	unsigned loc_fsize_max = (fsize_max + split_in - 1) / split_in;
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_WOUT  : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	constant %s_SOUT  : boolean := %s;\n", vhdl_prefixu, out_sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_FSIZE : natural := %u;\n", vhdl_prefixu, loc_fsize);
	fprintf(Fo, "	constant %s_FSIZE_MAX : natural := %u;\n", vhdl_prefixu, loc_fsize_max);
	fprintf(Fo, "	constant %s_PAR   : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	-- Identifier of neuron layer, for the constant memory component if any\n");
	fprintf(Fo, "	constant %s_LAYER_ID : natural := %u;\n", vhdl_prefixu, cfg_id);
	fprintf(Fo, "	-- Parameters for memory usage\n");
	fprintf(Fo, "	constant %s_CONST_PARAMS : boolean := %s;\n", vhdl_prefixu, const_params != 0 ? "true" : "false");
	fprintf(Fo, "	-- Constant multiplication and shift parameters (zero means unused)\n");
	fprintf(Fo, "	constant %s_MUL_CST : natural := %u;\n", vhdl_prefixu, norm_mul_cst);
	fprintf(Fo, "	constant %s_SHR_CST : natural := %u;\n", vhdl_prefixu, norm_shr_cst);
	fprintf(Fo, "	-- The optional run-time bias parameter\n");
	fprintf(Fo, "	constant %s_BIAS_EN : boolean := %s;\n", vhdl_prefixu, (norm_wbias > 0) ? "true" : "false");
	fprintf(Fo, "	constant %s_WBIAS   : natural := %u;\n", vhdl_prefixu, (norm_wbias == 0) ? 1 : norm_wbias);  // Note : Ensuring >0 to avoid issue when doing -1 later
	fprintf(Fo, "	-- The optional run-time multiplication parameter\n");
	fprintf(Fo, "	constant %s_MUL_EN : boolean := %s;\n", vhdl_prefixu, (norm_wmul > 0) ? "true" : "false");
	fprintf(Fo, "	constant %s_WMUL   : natural := %u;\n", vhdl_prefixu, (norm_wmul == 0) ? 1 : norm_wmul);  // Note : Ensuring >0 to avoid issue when doing -1 later
	fprintf(Fo, "	-- The optional run-time shift right\n");
	fprintf(Fo, "	constant %s_SHR_EN : boolean := %s;\n", vhdl_prefixu, (norm_wshr > 0) ? "true" : "false");
	fprintf(Fo, "	constant %s_WSHR   : natural := %u;\n", vhdl_prefixu, norm_wshr);
	fprintf(Fo, "	-- Activate rounding to nearest integer (default is rounding is towards zero)\n");
	fprintf(Fo, "	constant %s_ROUND_NEAR : boolean := %s;\n", vhdl_prefixu, (round_nearest == true) ? "true" : "false");
	fprintf(Fo, "	-- Width of the write port\n");
	fprintf(Fo, "	constant %s_WWRITE : natural := %u;\n", vhdl_prefixu, network->hwconfig_writewidth);
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}
void LayerTernarize::genvhdl_cst_decl(FILE* Fo) {
	unsigned fsize     = (this->fsize + this->split_in - 1) / this->split_in;
	unsigned fsize_max = (this->fsize_max + this->split_in - 1) / this->split_in;
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, (sdata != 0) ? "true" : "false");
	fprintf(Fo, "	constant %s_WOUT  : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	constant %s_SOUT  : boolean := true;\n", vhdl_prefixu);   // For generation of inter-layer registers
	fprintf(Fo, "	constant %s_FSIZE : natural := %u;\n", vhdl_prefixu, fsize);
	fprintf(Fo, "	constant %s_FSIZE_MAX : natural := %u;\n", vhdl_prefixu, fsize_max);
	fprintf(Fo, "	constant %s_PAR   : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	-- Identifier of neuron layer, for the constant memory component if any\n");
	fprintf(Fo, "	constant %s_LAYER_ID : natural := %u;\n", vhdl_prefixu, cfg_id);
	fprintf(Fo, "	-- Parameters for memory usage\n");
	fprintf(Fo, "	constant %s_CONST_PARAMS : boolean := %s;\n", vhdl_prefixu, const_params != 0 ? "true" : "false");
	fprintf(Fo, "	-- To avoid storing outputs in memory\n");
	fprintf(Fo, "	constant %s_OUT_STATIC : boolean := %s;\n", vhdl_prefixu, ter_out_static ? "true" : "false");
	fprintf(Fo, "	-- Width of the write port\n");
	fprintf(Fo, "	constant %s_WWRITE : natural := %u;\n", vhdl_prefixu, network->hwconfig_writewidth);
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}
void LayerRelu::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_WOUT  : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	constant %s_SOUT  : boolean := %s;\n", vhdl_prefixu, out_sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_PAR   : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	-- Parameters for the ReLU operation\n");
	fprintf(Fo, "	constant %s_MIN   : integer := %u;\n", vhdl_prefixu, relu_min);
	fprintf(Fo, "	constant %s_MAX   : integer := %u;\n", vhdl_prefixu, relu_max);
	fprintf(Fo, "	-- Enable/disable input buffering and flow control\n");
	fprintf(Fo, "	constant %s_INBUF : boolean := %s;\n", vhdl_prefixu, (flow_skip_inbuf == true) ? "true" : "false");
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}
void LayerLeaky::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_WOUT  : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	constant %s_SOUT  : boolean := %s;\n", vhdl_prefixu, out_sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_PAR   : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	-- Parameters for the Leaky operation\n");
	fprintf(Fo, "	constant %s_MIN   : integer := %u;\n", vhdl_prefixu, leaky_min);
	fprintf(Fo, "	constant %s_MAX   : integer := %u;\n", vhdl_prefixu, leaky_max);
	fprintf(Fo, "	-- Enable/disable input buffering and flow control\n");
	fprintf(Fo, "	constant %s_INBUF : boolean := %s;\n", vhdl_prefixu, (flow_skip_inbuf == true) ? "true" : "false");
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}
void LayerAdd::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata ? "true" : "false");
	fprintf(Fo, "	constant %s_WOUT  : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	-- Parameters for input and output parallelism\n");
	fprintf(Fo, "	constant %s_PAR_IN  : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	constant %s_PAR_OUT : natural := %u;\n", vhdl_prefixu, split_out);
	fprintf(Fo, "	-- Enable/disable input buffering and flow control\n");
	fprintf(Fo, "	constant %s_INBUF : boolean := %s;\n", vhdl_prefixu, (flow_skip_inbuf == true) ? "true" : "false");
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}
void LayerCustom::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_WOUT  : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	constant %s_SOUT  : boolean := %s;\n", vhdl_prefixu, out_sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_PAR   : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	-- Identifier of layer (optional)\n");
	fprintf(Fo, "	constant %s_LAYER_ID : natural := %u;\n", vhdl_prefixu, id);
	fprintf(Fo, "	-- Identifier of user function (optional)\n");
	fprintf(Fo, "	constant %s_USER_ID : natural := %u;\n", vhdl_prefixu, custom_user_id);
	fprintf(Fo, "	-- Enable/disable input buffering and flow control\n");
	fprintf(Fo, "	constant %s_INBUF : boolean := %s;\n", vhdl_prefixu, (flow_skip_inbuf == true) ? "true" : "false");
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}

void LayerFork::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_PAR   : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	constant %s_LAYERS_NB : natural := %u;\n", vhdl_prefixu, (unsigned)arr_layers.size());
}
void LayerCat::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_PAR   : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	constant %s_LAYERS_NB : natural := %u;\n", vhdl_prefixu, (unsigned)arr_layers.size());
}

void LayerFlatten::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata * split_in);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata ? "true" : "false");
	fprintf(Fo, "	constant %s_PAR   : natural := %u;\n", vhdl_prefixu, split_in);
}
void LayerSoftMax::genvhdl_cst_decl(FILE* Fo) {
	unsigned loc_fsize = (fsize + split_in - 1) / split_in;
	fprintf(Fo, "	constant %s_WDATA : natural := %u;\n", vhdl_prefixu, wdata);
	fprintf(Fo, "	constant %s_SDATA : boolean := %s;\n", vhdl_prefixu, sdata != 0 ? "true" : "false");
	fprintf(Fo, "	constant %s_WOUT  : natural := %u;\n", vhdl_prefixu, out_wdata);
	fprintf(Fo, "	constant %s_SOUT  : boolean := false;\n", vhdl_prefixu);  // Just for generation of inter-layer registers
	fprintf(Fo, "	constant %s_FSIZE : natural := %u;\n", vhdl_prefixu, loc_fsize);
	fprintf(Fo, "	constant %s_PAR_IN : natural := %u;\n", vhdl_prefixu, split_in);
	fprintf(Fo, "	constant %s_PAR_OUT : natural := 1;\n", vhdl_prefixu);
	fprintf(Fo, "	-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "	constant %s_FIFOMARGIN : natural := %u;\n", vhdl_prefixu, out_extra_fifo_room);
}
void LayerFifo::genvhdl_cst_decl(FILE* Fo) {
	fprintf(Fo, "	constant %s_DATAW : natural := %u;\n", vhdl_prefixu, wdata * split_in);
	// FIXME Need to compute a more accurate depth, lower of larger according to needs
	fprintf(Fo, "	constant %s_DEPTH : natural := 64;\n", vhdl_prefixu);
	fprintf(Fo, "	constant %s_CNTW  : natural := 16;\n", vhdl_prefixu);
	if(this == network->layer_first) {
		fprintf(Fo, "	-- To ease talking to the first FIFO\n");
		fprintf(Fo, "	constant FIRSTFIFO_WDATA : natural := %u;\n", wdata);
		fprintf(Fo, "	constant FIRSTFIFO_SDATA : boolean := %s;\n", sdata ? "true" : "false");
		fprintf(Fo, "	constant FIRSTFIFO_PAR   : natural := %u;\n", split_in);
	}
	if(this == network->layer_last) {
		fprintf(Fo, "	-- To ease talking to the last FIFO\n");
		fprintf(Fo, "	constant LASTFIFO_WDATA : natural := %u;\n", wdata);
		fprintf(Fo, "	constant LASTFIFO_SDATA : boolean := %s;\n", sdata ? "true" : "false");
		fprintf(Fo, "	constant LASTFIFO_PAR   : natural := %u;\n", split_in);
	}
}

void Network::genvhdl_cst_decl(FILE* Fo) {

	for(auto layer : layers) {
		if(layer != layers.front()) fprintf(Fo, "\n");
		fprintf(Fo, "	-- %s\n", layer->vhdl_prefixu);
		layer->genvhdl_cst_decl(Fo);
	}

	unsigned writeID = 0;

	fprintf(Fo, "\n");
	fprintf(Fo, "	-- IDs to write config\n");
	for(auto layer : layers) {
		if(layer->requires_idxcfg() == false) continue;
		fprintf(Fo, "	constant CST_RECV_CFG_%s : natural := %u;\n", layer->vhdl_prefixu, writeID++);
	}

}

//============================================
// Declarations of components
//============================================

unsigned Layer::genvhdl_comp_decl_register(void) {
	// Nothing to register
	return 0;
}

unsigned LayerWin::genvhdl_comp_decl_register(void) {
	return 1;
}
unsigned LayerNeu::genvhdl_comp_decl_register(void) {
	if(neu_style==2) return 2;
	return 1;
}

unsigned LayerNeu_CM::genvhdl_comp_decl_register(void) {
	if(neu_style==2) return 2;
	return 1;
}
unsigned LayerPool::genvhdl_comp_decl_register(void) {
	return 1;
}

unsigned LayerNorm::genvhdl_comp_decl_register(void) {
	return 1;
}
unsigned LayerTernarize::genvhdl_comp_decl_register(void) {
	return 1;
}
unsigned LayerRelu::genvhdl_comp_decl_register(void) {
	return 1;
}
unsigned LayerLeaky::genvhdl_comp_decl_register(void) {
	return 1;
}
unsigned LayerAdd::genvhdl_comp_decl_register(void) {
	return 1;
}
unsigned LayerCustom::genvhdl_comp_decl_register(void) {
	return 1;
}

unsigned LayerSoftMax::genvhdl_comp_decl_register(void) {
	return 1;
}

// Note : The parameter "flags" enables to have different underlying RTL components

void Layer::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	// Generate nothing
}

void LayerWin::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	bool first = true;

	// normal window
	if(flags & 1) {

		if(first == false) fprintf(Fo, "\n");
		first = false;

		fprintf(Fo,
			"	component nnlayer_window is\n"
			"		generic (\n"
			"			-- Data\n"
			"			WDATA  : natural := 8;\n"
			"			-- Dimensions: width, height, depth\n"
			"			DIMX   : natural := 32;\n"
			"			DIMY   : natural := 32;\n"
			"			DIMZ   : natural := 3;\n"
			"			-- Window size\n"
			"			WINX   : natural := 3;\n"
			"			WINY   : natural := 3;\n"
			"			WINZ   : natural := 1;\n"
			"			-- Step/stride values\n"
			"			STEPX  : natural := 1;\n"
			"			STEPY  : natural := 1;\n"
			"			STEPZ  : natural := 1;\n"
			"			-- Number of times the window is used in each dimension\n"
			"			NWINX  : natural := 32;\n"
			"			NWINY  : natural := 32;\n"
			"			NWINZ  : natural := 3;\n"
			"			-- Padding size at the beginning of each dimension\n"
			"			PADX   : natural := 1;\n"
			"			PADY   : natural := 1;\n"
			"			-- The height on axis Y of the internal buffers, minimum is WINY, max is DIMY, 0 means auto\n"
			"			BUFY   : natural := 0;\n"
			"			-- Internal storage type, leave all to false for automatic decision\n"
			"			USE_LUTRAM : boolean := false;\n"
			"			USE_BRAM   : boolean := false;\n"
			"			USE_URAM   : boolean := false;\n"
			"			-- Number of times to repeat the window contents before going to the next window position\n"
			"			REPEAT : natural := 1;\n"
			"			-- Parallelism : number of cells to write at one time\n"
			"			PAR_IN : natural := 1;\n"
			"			-- Parallelism : number of cells to read at one time\n"
			"			-- PAR_OUT / PAR_OZ must be 1, or a divisor of WINX, or a multiple of WINX and a divisor of WINX*WINY\n"
			"			PAR_OUT : natural := 1;\n"
			"			-- Parallelism : Output side on Z dimension\n"
			"			-- This must be a divisor of PAR_OUT, and a multiple of PAR_IN\n"
			"			PAR_OZ : natural := 1;\n"
			"			-- Take extra margin on the FIFO level, in case there is something outside\n"
			"			FIFOMARGIN : natural := 0;\n"
			"			-- Lock the layer parameters to the generic parameter value\n"
			"			LOCKED : boolean := false\n"
			"		);\n"
			"		port (\n"
			"			clk             : in  std_logic;\n"
			"			clear           : in  std_logic;\n"
			"			-- Run-time frame dimensions\n"
			"			user_fsize_x    : in  std_logic_vector(15 downto 0);\n"
			"			user_fsize_y    : in  std_logic_vector(15 downto 0);\n"
			"			user_fsize_z    : in  std_logic_vector(15 downto 0);\n"
			"			-- Run-time window step on each dimension\n"
			"			user_step_x     : in  std_logic_vector(7 downto 0);\n"
			"			user_step_y     : in  std_logic_vector(7 downto 0);\n"
			"			user_step_z     : in  std_logic_vector(7 downto 0);\n"
			"			-- Run-time number of times the window is used in each dimension\n"
			"			user_nwin_x     : in  std_logic_vector(15 downto 0);\n"
			"			user_nwin_y     : in  std_logic_vector(15 downto 0);\n"
			"			user_nwin_z     : in  std_logic_vector(15 downto 0);\n"
			"			-- Run-time padding size at the beginning of each dimension\n"
			"			user_begpad_x   : in  std_logic_vector(7 downto 0);\n"
			"			user_begpad_y   : in  std_logic_vector(7 downto 0);\n"
			"			-- Data input\n"
			"			data_in         : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
			"			data_in_rdy     : out std_logic;\n"
			"			data_in_ack     : in  std_logic;\n"
			"			-- Data output\n"
			"			data_out        : out std_logic_vector(PAR_OUT*WDATA-1 downto 0);\n"
			"			data_out_rdy    : out std_logic;\n"
			"			data_out_room   : in  std_logic_vector(15 downto 0)\n"
			"		);\n"
			"	end component;\n"
		);

	}

	// parwindow
	if(flags & 2) {

		if(first == false) fprintf(Fo, "\n");
		first = false;

		fprintf(Fo,
			"	component nnlayer_parwindow is\n"
			"		generic (\n"
			"			-- Data\n"
			"			WDATA  : natural := 8;\n"
			"			-- Dimensions: width, height, depth\n"
			"			DIMX   : natural := 32;\n"
			"			DIMY   : natural := 32;\n"
			"			DIMZ   : natural := 128;\n"
			"			-- Window size\n"
			"			WINX   : natural := 3;\n"
			"			WINY   : natural := 3;\n"
			"			-- Step/stride values\n"
			"			STEPX  : natural := 1;\n"
			"			STEPY  : natural := 1;\n"
			"			STEPZ  : natural := 1;\n"
			"			-- Number of times the window is used in each dimension\n"
			"			NWINX  : natural := 32;\n"
			"			NWINY  : natural := 32;\n"
			"			NWINZ  : natural := 32;\n"
			"			-- Padding size at the beginning of each dimension\n"
			"			PADX   : natural := 1;\n"
			"			PADY   : natural := 1;\n"
			"			-- The height on axis Y of the internal buffers, minimum is WINY+1, max is DIMX, 0 means auto\n"
			"			BUFY   : natural := 0;\n"
			"			-- Scan order\n"
			"			ZFIRST : boolean := false;\n"
			"			-- Parallelism : number of cells to write at one time\n"
			"			PAR_IN : natural := 1;\n"
			"			-- Parallelism : number of cells to read at one time\n"
			"			PAR_OUT : natural := 1;\n"
			"			-- Take extra margin on the FIFO level, in case there is something outside\n"
			"			FIFOMARGIN : natural := 0;\n"
			"			-- Lock the layer parameters to the generic parameter value\n"
			"			LOCKED : boolean := false\n"
			"		);\n"
			"		port (\n"
			"			clk             : in  std_logic;\n"
			"			clear           : in  std_logic;\n"
			"			-- Run-time frame dimensions\n"
			"			user_fsize_x    : in  std_logic_vector(15 downto 0);\n"
			"			user_fsize_y    : in  std_logic_vector(15 downto 0);\n"
			"			user_fsize_z    : in  std_logic_vector(15 downto 0);\n"
			"			-- Run-time window dimension\n"
			"			user_winx       : in  std_logic_vector(7 downto 0);\n"
			"			user_winy       : in  std_logic_vector(7 downto 0);\n"
			"			-- Run-time window step on each dimension\n"
			"			user_step_x     : in  std_logic_vector(7 downto 0);\n"
			"			user_step_y     : in  std_logic_vector(7 downto 0);\n"
			"			-- Run-time number of times the window is used in each dimension\n"
			"			user_nwin_x     : in  std_logic_vector(15 downto 0);\n"
			"			user_nwin_y     : in  std_logic_vector(15 downto 0);\n"
			"			user_rcycles_z  : in  std_logic_vector(15 downto 0);\n"
			"			-- Run-time padding size at the beginning of each dimension\n"
			"			user_begpad_x   : in  std_logic_vector(7 downto 0);\n"
			"			user_begpad_y   : in  std_logic_vector(7 downto 0);\n"
			"			-- Data input\n"
			"			data_in         : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
			"			data_in_rdy     : out std_logic;\n"
			"			data_in_ack     : in  std_logic;\n"
			"			-- Data output\n"
			"			data_out        : out std_logic_vector(PAR_OUT*WDATA-1 downto 0);\n"
			"			data_out_rdy    : out std_logic;\n"
			"			data_out_room   : in  std_logic_vector(15 downto 0)\n"
			"		);\n"
			"	end component;\n"
		);

	}

	// parmulwindow
	if(flags & 4) {

		if(first == false) fprintf(Fo, "\n");
		first = false;

		fprintf(Fo,
			"	component nnlayer_parmulwindow is\n"
			"		generic (\n"
			"			-- Data\n"
			"			WDATA  : natural  := 8;\n"
			"			-- Dimensions: width, height, depth\n"
			"			DIMX   : natural  := 32;\n"
			"			DIMY   : natural  := 32;\n"
			"			DIMZ   : natural  := 128;\n"
			"			-- Window size\n"
			"			WINX   : natural  := 3;\n"
			"			WINY   : natural  := 3;\n"
			"			-- Step/stride values\n"
			"			STEPX  : natural := 1;\n"
			"			STEPY  : natural := 1;\n"
			"			STEPZ  : natural := 1;\n"
			"			-- Number of times the window is used in each dimension\n"
			"			NWINX  : natural := 32;\n"
			"			NWINY  : natural := 32;\n"
			"			NWINZ  : natural := 32;\n"
			"			-- Padding size at the beginning of each dimension\n"
			"			PADX   : natural := 1;\n"
			"			PADY   : natural := 1;\n"
			"			-- The height on axis Y of the internal buffers, minimum is WINY+1, max is DIMX, 0 means auto\n"
			"			BUFY   : natural := 0;\n"
			"			-- Scan order\n"
			"			ZFIRST : boolean := false;\n"
			"			-- Parallelism : number of cells to write at one time\n"
			"			PAR_IN : natural := 1;\n"
			"			-- Parallelism : number of cells to read at one time\n"
			"			PAR_OUT : natural := 1;\n"
			"			-- Parallelism : number of DIMX to read at the same time\n"
			"			PAR_MULX : natural := 1;\n"
			"			-- Take extra margin on the FIFO level, in case there is something outside\n"
			"			FIFOMARGIN : natural := 0;\n"
			"			-- Lock the layer parameters to the generic parameter value\n"
			"			LOCKED : boolean := false\n"
			"		);\n"
			"		port (\n"
			"			clk             : in  std_logic;\n"
			"			clear           : in  std_logic;\n"
			"			-- Run-time frame dimensions\n"
			"			user_fsize_x    : in  std_logic_vector(15 downto 0);\n"
			"			user_fsize_y    : in  std_logic_vector(15 downto 0);\n"
			"			user_fsize_z    : in  std_logic_vector(15 downto 0);\n"
			"			-- Run-time window dimension\n"
			"			user_winx       : in  std_logic_vector(7 downto 0);\n"
			"			user_winy       : in  std_logic_vector(7 downto 0);\n"
			"			-- Run-time window step on each dimension\n"
			"			user_step_x     : in  std_logic_vector(7 downto 0);\n"
			"			user_step_y     : in  std_logic_vector(7 downto 0);\n"
			"			-- Run-time number of times the window is used in each dimension\n"
			"			user_nwin_x     : in  std_logic_vector(15 downto 0);\n"
			"			user_nwin_y     : in  std_logic_vector(15 downto 0);\n"
			"			user_rcycles_z  : in  std_logic_vector(15 downto 0);\n"
			"			-- Run-time padding size at the beginning of each dimension\n"
			"			user_begpad_x   : in  std_logic_vector(7 downto 0);\n"
			"			user_begpad_y   : in  std_logic_vector(7 downto 0);\n"
			"			-- Data input\n"
			"			data_in         : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
			"			data_in_rdy     : out std_logic;\n"
			"			data_in_ack     : in  std_logic;\n"
			"			-- Data output\n"
			"			data_out        : out std_logic_vector(PAR_OUT*PAR_MULX*WDATA-1 downto 0);\n"
			"			data_out_rdy    : out std_logic;\n"
			"			data_out_room   : in  std_logic_vector(15 downto 0)\n"
			"		);\n"
			"	end component;\n"
		);

	}

}
void LayerNeu::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	bool first = true;

	if(flags & 1) {

		if(first == false) fprintf(Fo, "\n");
		first = false;

		fprintf(Fo,
			"	component nnlayer_neurons_ter_postadd_xilinx is\n"
			"		generic (\n"
			"			-- Parameters for the neurons\n"
			"			WDATA  : natural := 2;\n"
			"			SDATA  : boolean := true;\n"
			"			SFIXED : boolean := true;\n"
			"			WACCU  : natural := 12;\n"
			"			WOUT   : natural := 12;\n"
			"			-- Parameters for BRAM usage\n"
			"			NPERBLK : natural := 18;\n"
			"			WRNB    : natural := 2;\n"
			"			-- Parameters for frame and number of neurons\n"
			"			FSIZE  : natural := 1024;\n"
			"			NBNEU  : natural := 1024;\n"
			"			-- Parameters for input and output parallelism\n"
			"			PAR_IN  : natural := 1;\n"
			"			PAR_OUT : natural := 1;\n"
			"			-- Take extra margin on the FIFO level, in case there is something outside\n"
			"			FIFOMARGIN : natural := 0\n"
			"		);\n"
			"		port (\n"
			"			clk            : in  std_logic;\n"
			"			clear          : in  std_logic;\n"
			"			-- Ports for Write Enable\n"
			"			write_mode     : in  std_logic;\n"
			"			write_idx      : in  std_logic_vector(9 downto 0);\n"
			"			write_data     : in  std_logic_vector(2*NPERBLK*WRNB-1 downto 0);\n"
			"			write_enable   : in  std_logic;\n"
			"			write_end      : out std_logic;\n"
			"			-- The user-specified frame size and number of neurons\n"
			"			user_fsize     : in  std_logic_vector(15 downto 0);\n"
			"			user_nbneu     : in  std_logic_vector(15 downto 0);\n"
			"			-- Data input, 2 bits\n"
			"			data_in        : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
			"			data_in_signed : in  std_logic;\n"
			"			data_in_valid  : in  std_logic;\n"
			"			data_in_ready  : out std_logic;\n"
			"			-- Scan chain to extract values\n"
			"			data_out       : out std_logic_vector(PAR_OUT*WOUT-1 downto 0);\n"
			"			data_out_valid : out std_logic;\n"
			"			-- Indicate to the parent component that we are reaching the end of the current frame\n"
			"			end_of_frame   : out std_logic;\n"
			"			-- The output data enters a FIFO. This indicates the available room.\n"
			"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
			"		);\n"
			"	end component;\n"
		);

	}

	if(flags & 2) {

		if(first == false) fprintf(Fo, "\n");
		first = false;

		fprintf(Fo,
			"	component nnlayer_neurons is\n"
			"		generic (\n"
			"			-- Parameters for the neurons\n"
			"			WDATA   : natural := 2;     -- The data bit width\n"
			"			SDATA   : boolean := true;  -- The data signedness\n"
			"			WWEIGHT : natural := 2;     -- The weight bit width\n"
			"			SWEIGHT : boolean := true;  -- The weight signedness\n"
			"			WOUT    : natural := 12;    -- The accumulator bit width\n"
			"			-- Parameters for BRAM usage\n"
			"			NPERBLK : natural := 18;\n"
			"			WRNB    : natural := 2;\n"
			"			WWRITE  : natural := 64;\n"
			"			USE_REGS   : boolean := false;\n"
			"			USE_LUTRAM : boolean := false;\n"
			"			USE_BRAM   : boolean := false;\n"
			"			USE_URAM   : boolean := false;\n"
			"			USE_CONST  : boolean := false;\n"
			"			PACKED     : boolean := true;\n"
			"			-- Identifier of neuron layer, mostly to be passed to custom internal components\n"
			"			LAYER_ID   : natural := 0;\n"
			"			-- For compression of weights in memory\n"
			"			COMP_STYLE : natural := 0;  -- Compression style, 0 means no decoder on datapath\n"
			"			COMP_WRAW  : natural := 0;  -- Size of raw data (must be a multiple of WWEIGHT)\n"
			"			COMP_WENC  : natural := 0;  -- Size of an encoded word\n"
			"			COMP_ENWR  : boolean := false;  -- Compression is implemented on Write side to be transparent to the controlling SW\n"
			"			-- Parameters for frame and number of neurons\n"
			"			FSIZE  : natural := 16;\n"
			"			NBNEU  : natural := 16;\n"
			"			-- Level of time multiplexing of the neuron accumulators\n"
			"			TIME_MUX : natural := 1;\n"
			"			-- Depth-Wise convolution mode : each activation goes to only one physical neuron\n"
			"			DWCONV : boolean := false;\n"
			"			-- Parameters for input and output parallelism\n"
			"			PAR_IN  : natural := 1;\n"
			"			PAR_OUT : natural := 1;\n"
			"			-- Options for neuron output\n"
			"			SHREG_MUX       : boolean := false;\n"
			"			SHREG_MUX_RADIX : natural := 18;\n"
			"			-- Constant weights passed directly\n"
			"			CSTWEIGHTS_NB  : natural := 1;\n"
			"			CSTWEIGHTS_VEC : std_logic_vector(CSTWEIGHTS_NB*WWEIGHT-1 downto 0) := (others => '0');\n"
			"			-- Identifier of multiplication operation\n"
			"			CUSTOM_MUL_ID : natural := 0;\n"
			"			CUSTOM_WMUL   : natural := 8;      -- Bit width of the multiplication result\n"
			"			CUSTOM_SMUL   : boolean := false;  -- Signedness of the multiplication result\n"
			"			-- Take extra margin on the FIFO level, in case there is something outside\n"
			"			FIFOMARGIN : natural := 0;\n"
			"			-- Lock the layer parameters to the generic parameter value\n"
			"			LOCKED : boolean := false\n"
			"		);\n"
			"		port (\n"
			"			clk            : in  std_logic;\n"
			"			clear          : in  std_logic;\n"
			"			-- Ports for Write Enable\n"
			"			write_mode     : in  std_logic;\n"
			"			write_data     : in  std_logic_vector(WWRITE-1 downto 0);\n"
			"			write_enable   : in  std_logic;\n"
			"			write_end      : out std_logic;\n"
			"			-- The user-specified frame size and number of neurons\n"
			"			user_fsize     : in  std_logic_vector(15 downto 0);\n"
			"			user_nbneu     : in  std_logic_vector(15 downto 0);\n"
			"			-- Data input, 2 bits\n"
			"			data_in        : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
			"			data_in_valid  : in  std_logic;\n"
			"			data_in_ready  : out std_logic;\n"
			"			-- Scan chain to extract values\n"
			"			data_out       : out std_logic_vector(PAR_OUT*WOUT-1 downto 0);\n"
			"			data_out_valid : out std_logic;\n"
			"			-- Indicate to the parent component that we are reaching the end of the current frame\n"
			"			end_of_frame   : out std_logic;\n"
			"			-- The output data enters a FIFO. This indicates the available room.\n"
			"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
			"		);\n"
			"	end component;\n"
		);

	}

}

void LayerNeu_CM::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	bool first = true;

	if(flags & 1) {

		if(first == false) fprintf(Fo, "\n");
		first = false;

		fprintf(Fo,
			"	component nnlayer_neurons_ter_postadd_xilinx is\n"
			"		generic (\n"
			"			-- Parameters for the neurons\n"
			"			WDATA  : natural := 2;\n"
			"			SDATA  : boolean := true;\n"
			"			SFIXED : boolean := true;\n"
			"			WACCU  : natural := 12;\n"
			"			WOUT   : natural := 12;\n"
			"			-- Parameters for BRAM usage\n"
			"			NPERBLK : natural := 18;\n"
			"			WRNB    : natural := 2;\n"
			"			-- Parameters for frame and number of neurons\n"
			"			FSIZE  : natural := 1024;\n"
			"			NBNEU  : natural := 1024;\n"
			"			-- Parameters for input and output parallelism\n"
			"			PAR_IN  : natural := 1;\n"
			"			PAR_OUT : natural := 1;\n"
			"			-- Take extra margin on the FIFO level, in case there is something outside\n"
			"			FIFOMARGIN : natural := 0\n"
			"		);\n"
			"		port (\n"
			"			clk            : in  std_logic;\n"
			"			clear          : in  std_logic;\n"
			"			-- Ports for Write Enable\n"
			"			write_mode     : in  std_logic;\n"
			"			write_idx      : in  std_logic_vector(9 downto 0);\n"
			"			write_data     : in  std_logic_vector(2*NPERBLK*WRNB-1 downto 0);\n"
			"			write_enable   : in  std_logic;\n"
			"			write_end      : out std_logic;\n"
			"			-- The user-specified frame size and number of neurons\n"
			"			user_fsize     : in  std_logic_vector(15 downto 0);\n"
			"			user_nbneu     : in  std_logic_vector(15 downto 0);\n"
			"			-- Data input, 2 bits\n"
			"			data_in        : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
			"			data_in_signed : in  std_logic;\n"
			"			data_in_valid  : in  std_logic;\n"
			"			data_in_ready  : out std_logic;\n"
			"			-- Scan chain to extract values\n"
			"			data_out       : out std_logic_vector(PAR_OUT*WOUT-1 downto 0);\n"
			"			data_out_valid : out std_logic;\n"
			"			-- Indicate to the parent component that we are reaching the end of the current frame\n"
			"			end_of_frame   : out std_logic;\n"
			"			-- The output data enters a FIFO. This indicates the available room.\n"
			"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
			"		);\n"
			"	end component;\n"
		);

	}

	if(flags & 2) {

		if(first == false) fprintf(Fo, "\n");
		first = false;

		fprintf(Fo,
			"	component nnlayer_neurons is\n"
			"		generic (\n"
			"			-- Parameters for the neurons\n"
			"			WDATA   : natural := 2;     -- The data bit width\n"
			"			SDATA   : boolean := true;  -- The data signedness\n"
			"			WWEIGHT : natural := 2;     -- The weight bit width\n"
			"			SWEIGHT : boolean := true;  -- The weight signedness\n"
			"			WOUT    : natural := 12;    -- The accumulator bit width\n"
			"			-- Parameters for BRAM usage\n"
			"			NPERBLK : natural := 18;\n"
			"			WRNB    : natural := 2;\n"
			"			WWRITE  : natural := 64;\n"
			"			USE_REGS   : boolean := false;\n"
			"			USE_LUTRAM : boolean := false;\n"
			"			USE_BRAM   : boolean := false;\n"
			"			USE_URAM   : boolean := false;\n"
			"			USE_CONST  : boolean := false;\n"
			"			PACKED     : boolean := true;\n"
			"			-- Identifier of neuron layer, mostly to be passed to custom internal components\n"
			"			LAYER_ID   : natural := 0;\n"
			"			-- For compression of weights in memory\n"
			"			COMP_STYLE : natural := 0;  -- Compression style, 0 means no decoder on datapath\n"
			"			COMP_WRAW  : natural := 0;  -- Size of raw data (must be a multiple of WWEIGHT)\n"
			"			COMP_WENC  : natural := 0;  -- Size of an encoded word\n"
			"			COMP_ENWR  : boolean := false;  -- Compression is implemented on Write side to be transparent to the controlling SW\n"
			"			-- Parameters for frame and number of neurons\n"
			"			FSIZE  : natural := 16;\n"
			"			NBNEU  : natural := 16;\n"
			"			-- Level of time multiplexing of the neuron accumulators\n"
			"			TIME_MUX : natural := 1;\n"
			"			-- Depth-Wise convolution mode : each activation goes to only one physical neuron\n"
			"			DWCONV : boolean := false;\n"
			"			-- Parameters for input and output parallelism\n"
			"			PAR_IN  : natural := 1;\n"
			"			PAR_OUT : natural := 1;\n"
			"			-- Options for neuron output\n"
			"			SHREG_MUX       : boolean := false;\n"
			"			SHREG_MUX_RADIX : natural := 18;\n"
			"			-- Constant weights passed directly\n"
			"			CSTWEIGHTS_NB  : natural := 1;\n"
			"			CSTWEIGHTS_VEC : std_logic_vector(CSTWEIGHTS_NB*WWEIGHT-1 downto 0) := (others => '0');\n"
			"			-- Identifier of multiplication operation\n"
			"			CUSTOM_MUL_ID : natural := 0;\n"
			"			CUSTOM_WMUL   : natural := 8;      -- Bit width of the multiplication result\n"
			"			CUSTOM_SMUL   : boolean := false;  -- Signedness of the multiplication result\n"
			"			-- Take extra margin on the FIFO level, in case there is something outside\n"
			"			FIFOMARGIN : natural := 0;\n"
			"			-- Lock the layer parameters to the generic parameter value\n"
			"			LOCKED : boolean := false\n"
			"		);\n"
			"		port (\n"
			"			clk            : in  std_logic;\n"
			"			clear          : in  std_logic;\n"
			"			-- Ports for Write Enable\n"
			"			write_mode     : in  std_logic;\n"
			"			write_data     : in  std_logic_vector(WWRITE-1 downto 0);\n"
			"			write_enable   : in  std_logic;\n"
			"			write_end      : out std_logic;\n"
			"			-- The user-specified frame size and number of neurons\n"
			"			user_fsize     : in  std_logic_vector(15 downto 0);\n"
			"			user_nbneu     : in  std_logic_vector(15 downto 0);\n"
			"			-- Data input, 2 bits\n"
			"			data_in        : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
			"			data_in_valid  : in  std_logic;\n"
			"			data_in_ready  : out std_logic;\n"
			"			-- Scan chain to extract values\n"
			"			data_out       : out std_logic_vector(PAR_OUT*WOUT-1 downto 0);\n"
			"			data_out_valid : out std_logic;\n"
			"			-- Indicate to the parent component that we are reaching the end of the current frame\n"
			"			end_of_frame   : out std_logic;\n"
			"			-- The output data enters a FIFO. This indicates the available room.\n"
			"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
			"		);\n"
			"	end component;\n"
		);

	}

}
void LayerPool::genvhdl_comp_decl(FILE* Fo, unsigned flags) {

	fprintf(Fo,
		"	component nnlayer_pooling is\n"
		"		generic(\n"
		"			WDATA   : natural := 8;\n"
		"			SDATA   : boolean := true;\n"
		"			WOUT    : natural := 8;\n"
		"			-- Frame size and number of units\n"
		"			FSIZE   : natural := 4;\n"
		"			NPOOL   : natural := 1;\n"
		"			-- The type of pooling\n"
		"			OPMAX   : boolean := true;\n"
		"			OPMIN   : boolean := false;\n"
		"			OPADD   : boolean := false;\n"
		"			-- Parameters for Average pooling\n"
		"			MULT    : natural := 1;\n"
		"			SHR     : natural := 0;\n"
		"			-- Activate rounding to nearest integer (default is rounding is towards zero)\n"
		"			ROUND_NEAR : boolean := false;\n"
		"			-- Parameters for input and output parallelism\n"
		"			PAR_IN  : natural := 1;\n"
		"			PAR_OUT : natural := 1;\n"
		"			-- Take extra margin on the FIFO level, in case there is something outside\n"
		"			FIFOMARGIN : natural := 0;\n"
		"			-- Lock the layer parameters to the generic parameter value\n"
		"			LOCKED  : boolean := false\n"
		"		);\n"
		"		port(\n"
		"			clk           : in  std_logic;\n"
		"			clear         : in  std_logic;\n"
		"			-- Run-time frame dimensions\n"
		"			user_fsize    : in  std_logic_vector(15 downto 0);\n"
		"			-- Data input\n"
		"			in_data       : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
		"			in_rdy        : out std_logic;\n"
		"			in_ack        : in  std_logic;\n"
		"			-- Data output\n"
		"			out_data      : out std_logic_vector(PAR_OUT*WOUT-1 downto 0);\n"
		"			out_rdy       : out std_logic;\n"
		"			out_fifo_room : in  std_logic_vector(15 downto 0)\n"
		"		);\n"
		"	end component;\n"
	);

}

void LayerNorm::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	fprintf(Fo,
		"	component nnlayer_norm is\n"
		"		generic(\n"
		"			WDATA : natural := 8;\n"
		"			SDATA : boolean := false;\n"
		"			WOUT  : natural := 8;\n"
		"			FSIZE : natural := 1024;\n"
		"			PAR   : natural := 1;\n"
		"			-- Identifier of neuron layer, for the constant memory component if any\n"
		"			LAYER_ID : natural := 0;\n"
		"			-- Parameters for memory usage\n"
		"			CONST_PARAMS : boolean := false;\n"
		"			-- Constant multiplication and shift parameters (zero means unused)\n"
		"			MUL_CST : natural := 0;\n"
		"			SHR_CST : natural := 0;\n"
		"			-- The optional run-time bias parameter\n"
		"			BIAS_EN : boolean := false;\n"
		"			WBIAS   : natural := 0;\n"
		"			-- The optional run-time multiplication parameter\n"
		"			MUL_EN : boolean := false;\n"
		"			WMUL   : natural := 0;\n"
		"			-- The optional run-time shift right\n"
		"			SHR_EN : boolean := false;\n"
		"			WSHR   : natural := 0;\n"
		"			-- Activate rounding to nearest integer (default is rounding is towards zero)\n"
		"			ROUND_NEAR : boolean := false;\n"
		"			-- Width of the write port\n"
		"			WWRITE : natural := 32;\n"
		"			-- Take extra margin on the FIFO level, in case there is something outside\n"
		"			FIFOMARGIN : natural := 0;\n"
		"			-- Lock the layer parameters to the generic parameter value\n"
		"			LOCKED : boolean := false\n"
		"		);\n"
		"		port(\n"
		"			clk             : in  std_logic;\n"
		"			-- Ports for address control\n"
		"			addr_clear      : in  std_logic;\n"
		"			-- Ports for Write into memory (for bias)\n"
		"			write_mode      : in  std_logic;\n"
		"			write_data      : in  std_logic_vector(WWRITE-1 downto 0);\n"
		"			write_enable    : in  std_logic;\n"
		"			-- The user-specified frame size\n"
		"			user_fsize      : in  std_logic_vector(15 downto 0);\n"
		"			-- Data input\n"
		"			data_in         : in  std_logic_vector(PAR*WDATA-1 downto 0);\n"
		"			data_in_valid   : in  std_logic;\n"
		"			data_in_ready   : out std_logic;\n"
		"			-- Data output\n"
		"			data_out        : out std_logic_vector(PAR*WOUT-1 downto 0);\n"
		"			data_out_valid  : out std_logic;\n"
		"			-- The output data enters a FIFO. This indicates the available room.\n"
		"			out_fifo_room   : in  std_logic_vector(15 downto 0)\n"
		"		);\n"
		"	end component;\n"
	);
}
void LayerTernarize::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	fprintf(Fo,
		"	component nnlayer_recode_to_ter is\n"
		"		generic(\n"
		"			WDATA : natural := 12;\n"
		"			SDATA : boolean := true;\n"
		"			WOUT  : natural := 2;\n"
		"			FSIZE : natural := 1024;\n"
		"			PAR   : natural := 1;\n"
		"			-- Identifier of neuron layer, for the constant memory component if any\n"
		"			LAYER_ID : natural := 0;\n"
		"			-- Parameters for memory usage\n"
		"			CONST_PARAMS : boolean := false;\n"
		"			-- To avoid storing outputs in memory\n"
		"			OUT_STATIC : boolean := false;\n"
		"			-- Width of the write port\n"
		"			WWRITE : natural := 32;\n"
		"			-- Take extra margin on the FIFO level, in case there is something outside\n"
		"			FIFOMARGIN : natural := 0;\n"
		"			-- Lock the layer parameters to the generic parameter value\n"
		"			LOCKED : boolean := false\n"
		"		);\n"
		"		port(\n"
		"			clk             : in  std_logic;\n"
		"			-- Ports for address control\n"
		"			addr_clear      : in  std_logic;\n"
		"			-- Ports for Write into memory\n"
		"			write_mode      : in  std_logic;\n"
		"			write_data      : in  std_logic_vector(WWRITE-1 downto 0);\n"
		"			write_enable    : in  std_logic;\n"
		"			-- The user-specified frame size\n"
		"			user_fsize      : in  std_logic_vector(15 downto 0);\n"
		"			-- Data input\n"
		"			data_in         : in  std_logic_vector(PAR*WDATA-1 downto 0);\n"
		"			data_in_valid   : in  std_logic;\n"
		"			data_in_ready   : out std_logic;\n"
		"			-- Data output\n"
		"			data_out        : out std_logic_vector(PAR*WOUT-1 downto 0);\n"
		"			data_out_valid  : out std_logic;\n"
		"			-- The output data enters a FIFO. This indicates the available room.\n"
		"			out_fifo_room   : in  std_logic_vector(15 downto 0)\n"
		"		);\n"
		"	end component;\n"
	);
}
void LayerRelu::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	fprintf(Fo,
		"	component nnlayer_relu is\n"
		"		generic(\n"
		"			WDATA : natural := 8;\n"
		"			SDATA : boolean := false;\n"
		"			WOUT  : natural := 8;\n"
		"			PAR   : natural := 1;\n"
		"			-- Enable/disable input buffering and flow control\n"
		"			INBUF : boolean := true;\n"
		"			-- Take extra margin on the FIFO level, in case there is something outside\n"
		"			FIFOMARGIN : natural := 0;\n"
		"			-- Lock the layer parameters to the generic parameter value\n"
		"			LOCKED : boolean := false\n"
		"		);\n"
		"		port(\n"
		"			clk            : in  std_logic;\n"
		"			-- Ports for address control\n"
		"			clear          : in  std_logic;\n"
		"			-- The user-specified range for linear output\n"
		"			user_min       : in  std_logic_vector(WOUT downto 0);\n"
		"			user_max       : in  std_logic_vector(WOUT downto 0);\n"
		"			-- Data input\n"
		"			data_in        : in  std_logic_vector(PAR*WDATA-1 downto 0);\n"
		"			data_in_valid  : in  std_logic;\n"
		"			data_in_ready  : out std_logic;\n"
		"			-- Data output\n"
		"			data_out       : out std_logic_vector(PAR*WOUT-1 downto 0);\n"
		"			data_out_valid : out std_logic;\n"
		"			-- The output data enters a FIFO. This indicates the available room.\n"
		"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
		"		);\n"
		"	end component;\n"
	);
}
void LayerLeaky::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	fprintf(Fo,
		"	component nnlayer_leaky is\n"
		"		generic(\n"
		"			WDATA : natural := 8;\n"
		"			SDATA : boolean := false;\n"
		"			WOUT  : natural := 8;\n"
		"			PAR   : natural := 1;\n"
		"			-- Enable/disable input buffering and flow control\n"
		"			INBUF : boolean := true;\n"
		"			-- Take extra margin on the FIFO level, in case there is something outside\n"
		"			FIFOMARGIN : natural := 0;\n"
		"			-- Lock the layer parameters to the generic parameter value\n"
		"			LOCKED : boolean := false\n"
		"		);\n"
		"		port(\n"
		"			clk            : in  std_logic;\n"
		"			-- Ports for address control\n"
		"			clear          : in  std_logic;\n"
		"			-- The user-specified range for linear output\n"
		"			user_min       : in  std_logic_vector(WOUT downto 0);\n"
		"			user_max       : in  std_logic_vector(WOUT downto 0);\n"
		"			-- Data input\n"
		"			data_in        : in  std_logic_vector(PAR*WDATA-1 downto 0);\n"
		"			data_in_valid  : in  std_logic;\n"
		"			data_in_ready  : out std_logic;\n"
		"			-- Data output\n"
		"			data_out       : out std_logic_vector(PAR*WOUT-1 downto 0);\n"
		"			data_out_valid : out std_logic;\n"
		"			-- The output data enters a FIFO. This indicates the available room.\n"
		"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
		"		);\n"
		"	end component;\n"
	);
}
void LayerAdd::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	fprintf(Fo,
		"	component nnlayer_add is\n"
		"		generic(\n"
		"			WDATA  : natural := 8;\n"
		"			SDATA  : boolean := false;\n"
		"			WOUT   : natural := 10;\n"
		"			-- Parameters for input and output parallelism\n"
		"			PAR_IN : natural := 1;\n"
		"			PAR_OUT : natural := 1;\n"
		"			-- Enable/disable input buffering and flow control\n"
		"			INBUF : boolean := true;\n"
		"			-- Take extra margin on the FIFO level, in case there is something outside\n"
		"			FIFOMARGIN : natural := 0\n"
		"		);\n"
		"		port(\n"
		"			clk            : in  std_logic;\n"
		"			clear          : in  std_logic;\n"
		"			-- Data input\n"
		"			data_in        : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
		"			data_in_valid  : in  std_logic;\n"
		"			data_in_ready  : out std_logic;\n"
		"			-- Data output\n"
		"			data_out       : out std_logic_vector(PAR_OUT*WOUT-1 downto 0);\n"
		"			data_out_valid : out std_logic;\n"
		"			-- The output data enters a FIFO. This indicates the available room.\n"
		"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
		"		);\n"
		"	end component;\n"
	);
}
void LayerCustom::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	fprintf(Fo,
		"	component %s is\n"
		"		generic(\n"
		"			WDATA : natural := 8;\n"
		"			SDATA : boolean := false;\n"
		"			WOUT  : natural := 8;\n"
		"			PAR   : natural := 1;\n"
		"			-- Identifier of layer (optional)\n"
		"			LAYER_ID : natural := 0;\n"
		"			-- Identifier of user function (optional)\n"
		"			USER_ID : natural := 0;\n"
		"			-- Enable/disable input buffering and flow control\n"
		"			INBUF : boolean := true;\n"
		"			-- Take extra margin on the FIFO level, in case there is something outside\n"
		"			FIFOMARGIN : natural := 0;\n"
		"			-- Lock the layer parameters to the generic parameter value\n"
		"			LOCKED : boolean := false\n"
		"		);\n"
		"		port(\n"
		"			clk            : in  std_logic;\n"
		"			clear          : in  std_logic;\n"
		"			-- Data input\n"
		"			data_in        : in  std_logic_vector(PAR*WDATA-1 downto 0);\n"
		"			data_in_valid  : in  std_logic;\n"
		"			data_in_ready  : out std_logic;\n"
		"			-- Data output\n"
		"			data_out       : out std_logic_vector(PAR*WOUT-1 downto 0);\n"
		"			data_out_valid : out std_logic;\n"
		"			-- The output data enters a FIFO. This indicates the available room.\n"
		"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
		"		);\n"
		"	end component;\n", custom_entity
	);
}

void LayerSoftMax::genvhdl_comp_decl(FILE* Fo, unsigned flags) {
	fprintf(Fo,
		"	component nnlayer_softmax is\n"
		"		generic(\n"
		"			WDATA : natural := 8;\n"
		"			SDATA : boolean := false;\n"
		"			WOUT  : natural := 8;\n"
		"			FSIZE : natural := 10;\n"
		"			PAR_IN : natural := 1;\n"
		"			-- Take extra margin on the FIFO level, in case there is something outside\n"
		"			FIFOMARGIN : natural := 0;\n"
		"			-- Lock the layer parameters to the generic parameter value\n"
		"			LOCKED : boolean := false\n"
		"		);\n"
		"		port(\n"
		"			clk            : in  std_logic;\n"
		"			clear          : in  std_logic;\n"
		"			-- The user-specified frame size\n"
		"			user_fsize     : in  std_logic_vector(WOUT-1 downto 0);\n"
		"			-- Data input\n"
		"			data_in        : in  std_logic_vector(PAR_IN*WDATA-1 downto 0);\n"
		"			data_in_valid  : in  std_logic;\n"
		"			data_in_ready  : out std_logic;\n"
		"			-- Data output\n"
		"			data_out       : out std_logic_vector(WOUT-1 downto 0);\n"
		"			data_out_valid : out std_logic;\n"
		"			-- The output data enters a FIFO. This indicates the available room.\n"
		"			out_fifo_room  : in  std_logic_vector(15 downto 0)\n"
		"		);\n"
		"	end component;\n"
	);
}

void Network::genvhdl_comp_decl(FILE* Fo) {

	// Store flags for the generator of each layer type
	// FIXME The layer pointer is just to have a link to the layer type and call the function
	vector< pair<Layer*, unsigned> > layer_types_flags;

	for(auto layer : layers) {
		unsigned flags = layer->genvhdl_comp_decl_register();
		if(flags == 0) continue;
		if((unsigned)layer->type >= layer_types_flags.size()) layer_types_flags.resize(layer->type + 1, make_pair(nullptr, 0));
		layer_types_flags[layer->type].first = layer;
		layer_types_flags[layer->type].second |= flags;
	}

	bool first = true;

	for(auto& scan : layer_types_flags) {

		if(scan.second == 0) continue;

		if(first == false) fprintf(Fo, "\n");
		first = false;

		scan.first->genvhdl_comp_decl(Fo, scan.second);

	}

}

//============================================
// Declarations of signals
//============================================

void Layer::genvhdl_sig_decl(FILE* Fo) {
	fprintf(Fo, "	-- No need for specific signals\n");
}

void LayerWin::genvhdl_sig_decl(FILE* Fo) {
	// FIXME This was to ease code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;
	fprintf(Fo, "	signal inst_%s_clear           : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Run-time frame dimensions\n");
	fprintf(Fo, "	signal inst_%s_fsize_x         : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_fsize_y         : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_fsize_z         : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	-- Run-time window step on each dimension\n");
	fprintf(Fo, "	signal inst_%s_step_x          : std_logic_vector(7 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_step_y          : std_logic_vector(7 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_step_z          : std_logic_vector(7 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	-- Run-time number of times the window is used in each dimension\n");
	fprintf(Fo, "	signal inst_%s_nwin_x          : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_nwin_y          : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_nwin_z          : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	-- Run-time padding size at the beginning of each dimension\n");
	fprintf(Fo, "	signal inst_%s_begpad_x        : std_logic_vector(7 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_begpad_y        : std_logic_vector(7 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_data_in         : std_logic_vector(%s_PAR_IN*%s_WDATA-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_in_rdy     : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ack     : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_data_out        : std_logic_vector(%s_PAR_OUT*%s_WDATA-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_rdy    : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_data_out_room   : std_logic_vector(15 downto 0);\n", layer_prefixl);
}
void LayerNeu::genvhdl_sig_decl(FILE* Fo) {
	// FIXME This was to ease code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	signal inst_%s_clear          : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Ports for Write Enable\n");
	fprintf(Fo, "	signal inst_%s_write_mode     : std_logic;\n", layer_prefixl);
	if(neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "	signal inst_%s_write_idx      : std_logic_vector(9 downto 0);\n", layer_prefixl);
	}
	fprintf(Fo, "	signal inst_%s_write_data     : std_logic_vector(%s_WWRITE-1 downto 0);\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_write_enable   : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_write_end      : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The user-specified frame size and number of neurons\n");
	fprintf(Fo, "	signal inst_%s_user_fsize     : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_user_nbneu     : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	-- Data input, 2 bits\n");
	fprintf(Fo, "	signal inst_%s_data_in        : std_logic_vector(%s_PAR_IN*%s_WDATA-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	if(neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "	signal inst_%s_data_in_signed : std_logic;\n", layer_prefixl);
	}
	fprintf(Fo, "	signal inst_%s_data_in_valid  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Scan chain to extract values\n");
	fprintf(Fo, "	signal inst_%s_data_out       : std_logic_vector(%s_PAR_OUT*%s_WOUT-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Indicate to the parent component that we are reaching the end of the current frame\n");
	fprintf(Fo, "	signal inst_%s_end_of_frame   : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room  : std_logic_vector(15 downto 0);\n", layer_prefixl);
}
void LayerNeu_CM::genvhdl_sig_decl(FILE* Fo) {
	// FIXME This was to ease code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	signal inst_%s_clear          : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Ports for Write Enable\n");
	fprintf(Fo, "	signal inst_%s_write_mode     : std_logic;\n", layer_prefixl);
	if(neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "	signal inst_%s_write_idx      : std_logic_vector(9 downto 0);\n", layer_prefixl);
	}
	fprintf(Fo, "	signal inst_%s_write_data     : std_logic_vector(%s_WWRITE-1 downto 0);\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_write_enable   : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_write_end      : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The user-specified frame size and number of neurons\n");
	fprintf(Fo, "	signal inst_%s_user_fsize     : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_user_nbneu     : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	-- Data input, 2 bits\n");
	fprintf(Fo, "	signal inst_%s_data_in        : std_logic_vector(%s_PAR_IN*%s_WDATA-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	if(neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "	signal inst_%s_data_in_signed : std_logic;\n", layer_prefixl);
	}
	fprintf(Fo, "	signal inst_%s_data_in_valid  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Scan chain to extract values\n");
	fprintf(Fo, "	signal inst_%s_data_out       : std_logic_vector(%s_PAR_OUT*%s_WOUT-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Indicate to the parent component that we are reaching the end of the current frame\n");
	fprintf(Fo, "	signal inst_%s_end_of_frame   : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room  : std_logic_vector(15 downto 0);\n", layer_prefixl);
}

void LayerPool::genvhdl_sig_decl(FILE* Fo) {
	fprintf(Fo, "	signal inst_%s_clear         : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- Frame size\n");
	fprintf(Fo, "	signal inst_%s_user_fsize    : std_logic_vector(15 downto 0);\n", vhdl_prefixl);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_in_data       : std_logic_vector(%s_PAR_IN*%s_WDATA-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_in_rdy        : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_in_ack        : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_out_data      : std_logic_vector(%s_PAR_OUT*%s_WOUT-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_out_rdy       : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_out_fifo_room : std_logic_vector(15 downto 0);\n", vhdl_prefixl);
}

void LayerNorm::genvhdl_sig_decl(FILE* Fo) {
	// FIXME This was to ease code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	-- Ports for address control\n");
	fprintf(Fo, "	signal inst_%s_addr_clear      : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Ports for Write into memory\n");
	fprintf(Fo, "	signal inst_%s_write_mode      : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_write_data      : std_logic_vector(%s_WWRITE-1 downto 0);\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_write_enable    : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The user-specified frame size\n");
	fprintf(Fo, "	signal inst_%s_user_fsize      : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_data_in         : std_logic_vector(%s_WDATA*%s_PAR-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_in_valid   : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready   : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_data_out        : std_logic_vector(%s_WOUT*%s_PAR-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room   : std_logic_vector(15 downto 0);\n", layer_prefixl);
}
void LayerTernarize::genvhdl_sig_decl(FILE* Fo) {
	// FIXME This was to ease code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	-- Ports for address control\n");
	fprintf(Fo, "	signal inst_%s_addr_clear      : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Ports for Write into memory\n");
	fprintf(Fo, "	signal inst_%s_write_mode      : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_write_data      : std_logic_vector(%s_WWRITE-1 downto 0);\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_write_enable    : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The user-specified frame size\n");
	fprintf(Fo, "	signal inst_%s_user_fsize      : std_logic_vector(15 downto 0);\n", layer_prefixl);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_data_in         : std_logic_vector(%s_PAR*%s_WDATA-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_in_valid   : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready   : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_data_out        : std_logic_vector(%s_PAR*%s_WOUT-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room   : std_logic_vector(15 downto 0);\n", layer_prefixl);
}
void LayerRelu::genvhdl_sig_decl(FILE* Fo) {
	// FIXME This was to ease code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;
	fprintf(Fo, "	-- Ports for address control\n");
	fprintf(Fo, "	signal inst_%s_clear          : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The user-specified range for linear output\n");
	fprintf(Fo, "	signal inst_%s_user_min       : std_logic_vector(%s_WOUT downto 0);\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_user_max       : std_logic_vector(%s_WOUT downto 0);\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_data_in        : std_logic_vector(%s_WDATA*%s_PAR-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_in_valid  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_data_out       : std_logic_vector(%s_WOUT*%s_PAR-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room  : std_logic_vector(15 downto 0);\n", layer_prefixl);
}
void LayerLeaky::genvhdl_sig_decl(FILE* Fo) {
	// FIXME This was to ease code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;
	fprintf(Fo, "	-- Ports for address control\n");
	fprintf(Fo, "	signal inst_%s_clear          : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The user-specified range for linear output\n");
	fprintf(Fo, "	signal inst_%s_user_min       : std_logic_vector(%s_WOUT downto 0);\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_user_max       : std_logic_vector(%s_WOUT downto 0);\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_data_in        : std_logic_vector(%s_WDATA*%s_PAR-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_in_valid  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready  : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_data_out       : std_logic_vector(%s_WOUT*%s_PAR-1 downto 0);\n", layer_prefixl, layer_prefixu, layer_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid : std_logic;\n", layer_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room  : std_logic_vector(15 downto 0);\n", layer_prefixl);
}
void LayerAdd::genvhdl_sig_decl(FILE* Fo) {
	fprintf(Fo, "	-- Ports for address control\n");
	fprintf(Fo, "	signal inst_%s_clear          : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_data_in        : std_logic_vector(%s_WDATA*%s_PAR_IN-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_data_in_valid  : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready  : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_data_out       : std_logic_vector(%s_WOUT*%s_PAR_OUT-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room  : std_logic_vector(15 downto 0);\n", vhdl_prefixl);
}
void LayerCustom::genvhdl_sig_decl(FILE* Fo) {
	fprintf(Fo, "	-- Ports for address control\n");
	fprintf(Fo, "	signal inst_%s_clear          : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_data_in        : std_logic_vector(%s_WDATA*%s_PAR-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_data_in_valid  : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready  : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_data_out       : std_logic_vector(%s_WOUT*%s_PAR-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room  : std_logic_vector(15 downto 0);\n", vhdl_prefixl);
}

void LayerFlatten::genvhdl_sig_decl(FILE* Fo) {
	fprintf(Fo, "	signal inst_%s_out_data  : std_logic_vector(%s_WDATA-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_out_rdy   : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_out_ack   : std_logic;\n", vhdl_prefixl);
}
void LayerSoftMax::genvhdl_sig_decl(FILE* Fo) {
	fprintf(Fo, "	-- Ports for address control\n");
	fprintf(Fo, "	signal inst_%s_clear          : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- The user-specified frame size\n");
	fprintf(Fo, "	signal inst_%s_user_fsize     : std_logic_vector(15 downto 0);\n", vhdl_prefixl);
	fprintf(Fo, "	-- Data input\n");
	fprintf(Fo, "	signal inst_%s_data_in        : std_logic_vector(%s_WDATA*%s_PAR_IN-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_data_in_valid  : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_data_in_ready  : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- Data output\n");
	fprintf(Fo, "	signal inst_%s_data_out       : std_logic_vector(%s_WOUT-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_data_out_valid : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "	signal inst_%s_out_fifo_room  : std_logic_vector(15 downto 0);\n", vhdl_prefixl);
}
void LayerFifo::genvhdl_sig_decl(FILE* Fo) {
	fprintf(Fo, "	signal inst_%s_clear    : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_in_data  : std_logic_vector(%s_DATAW-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_in_rdy   : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_in_ack   : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_in_cnt   : std_logic_vector(%u-1 downto 0);\n", vhdl_prefixl, 16);
	fprintf(Fo, "	signal inst_%s_out_data : std_logic_vector(%s_DATAW-1 downto 0);\n", vhdl_prefixl, vhdl_prefixu);
	fprintf(Fo, "	signal inst_%s_out_rdy  : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_out_ack  : std_logic;\n", vhdl_prefixl);
	fprintf(Fo, "	signal inst_%s_out_cnt  : std_logic_vector(%u-1 downto 0);\n", vhdl_prefixl, 16);
}

void Network::genvhdl_sig_decl(FILE* Fo) {

	fprintf(Fo, "	-- Alias signals for first FIFO\n");
	fprintf(Fo, "	signal inst_firstfifo_in_data : std_logic_vector(FIRSTFIFO_PAR*FIRSTFIFO_WDATA-1 downto 0);\n");
	fprintf(Fo, "	signal inst_firstfifo_in_rdy  : std_logic;\n");
	fprintf(Fo, "	signal inst_firstfifo_in_ack  : std_logic;\n");
	fprintf(Fo, "	signal inst_firstfifo_in_cnt  : std_logic_vector(%u-1 downto 0);\n", 16);
	fprintf(Fo, "\n");

	fprintf(Fo, "	-- Alias signals for last FIFO\n");
	fprintf(Fo, "	signal inst_lastfifo_out_data : std_logic_vector(LASTFIFO_PAR*LASTFIFO_WDATA-1 downto 0);\n");
	fprintf(Fo, "	signal inst_lastfifo_out_rdy  : std_logic;\n");
	fprintf(Fo, "	signal inst_lastfifo_out_ack  : std_logic;\n");
	fprintf(Fo, "	signal inst_lastfifo_out_cnt  : std_logic_vector(%u-1 downto 0);\n", 16);
	fprintf(Fo, "\n");

	for(auto layer : layers) {

		if(layer != layers.front()) fprintf(Fo, "\n");
		fprintf(Fo, "	-- %s\n", layer->vhdl_prefixu);

		layer->genvhdl_sig_decl(Fo);

	}

}

//============================================
// Config registers
//============================================

void Layer::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	fprintf(Fo, "%s-- No config registers\n", tabs);
}
void Layer::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	fprintf(Fo, "%s-- No config registers\n", tabs);
}

void LayerWin::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	// Miscellaneous variables to ease code refactoring
	Layer* layer = this;
	// Warning messages
	test_warn_field_exceed(layer, regfield_repeat, layer->win_repeat);
	// Generate
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR_IN");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_dwconv, "DWCONV");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_symxy , "SYMXY");
	// FIXME These numbers should be the "max" dimensions of the usable size of the window, need to define the _MAX constants
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fx_max, "DIMX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fz_max, "DIMZ");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_winx,   "WINX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_par_oz, "PAR_OZ");
	// Optional fields for Y
	if(layer->win_sym_xy == false) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fy_max, "DIMY");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_winy,   "WINY");
	}
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WDATA", "SDATA", "PAR_OUT");
	}
}
void LayerWin::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fx,     "DIMX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fz,     "DIMZ");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_stepx,  "STEPX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_padx,   "PADX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_nwinx,  "NWINX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_nwinz,  "NWINZ");
	// Optional fields for Y
	if(layer->win_sym_xy == false) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fy,     "DIMY");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_stepy,  "STEPY");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_pady,   "PADY");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_nwiny,  "NWINY");
	}
}

void LayerNeu::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	// Miscellaneous variables to ease code refactoring
	Layer* layer = this;
	char* layer_prefixu = this->vhdl_prefixu;
	// Warning messages
	test_warn_field_exceed(layer, regfield_tmux, layer->neu_time_mux);
	// Generate
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR_IN");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	if(layer->neu_style > 0) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_dwconv, "DWCONV");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_tmux, "TIME_MUX");
	}
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize_max, "FSIZE_MAX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_neu_max,   "NBNEU_MAX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_nperblk,   "NPERBLK", -1);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_wrnb,      "WRNB", -1);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_wweight,   "WWEIGHT", -1);
	if(layer->neu_style==0) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sdlock, "SFIXED");
		fprintf(Fo, "%sif %s_SFIXED = true then\n", tabs, layer_prefixu);
		fprintf(Fo, "%s", tabs1);
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sdata, "SDATA");
		fprintf(Fo, "%send if;\n", tabs);
	}
	else {
		print_cfg_regasg_from_cstbit(Fo, tabs, layer, *regfield_sdlock, "true", "  -- Data signedness is not configurable");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sdata,  "SDATA");
	}
	print_cfg_regasg_from_cstbit(Fo, tabs, layer, *regfield_swlock,  "true", "  -- Weight signedness is not configurable");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sweight, "SWEIGHT");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_style,   "STYLE");
	// Custom multiplication identifiers
	if(layer->neu_style==1 || layer->neu_style==2) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_mul_id, "CUSTOM_MUL_ID");
	}

	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SDATA", "PAR_OUT");
	}
}

void LayerNeu::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize, "FSIZE");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_neu,   "NBNEU");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sdata, "SDATA");
}

void LayerNeu_CM::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	// Miscellaneous variables to ease code refactoring
	Layer* layer = this;
	char* layer_prefixu = this->vhdl_prefixu;
	// Warning messages
	test_warn_field_exceed(layer, regfield_tmux, layer->neu_time_mux);
	// Generate
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR_IN");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	if(layer->neu_style > 0) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_dwconv, "DWCONV");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_tmux, "TIME_MUX");
	}
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize_max, "FSIZE_MAX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_neu_max,   "NBNEU_MAX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_nperblk,   "NPERBLK", -1);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_wrnb,      "WRNB", -1);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_wweight,   "WWEIGHT", -1);
	if(layer->neu_style==0) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sdlock, "SFIXED");
		fprintf(Fo, "%sif %s_SFIXED = true then\n", tabs, layer_prefixu);
		fprintf(Fo, "%s", tabs1);
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sdata, "SDATA");
		fprintf(Fo, "%send if;\n", tabs);
	}
	else {
		print_cfg_regasg_from_cstbit(Fo, tabs, layer, *regfield_sdlock, "true", "  -- Data signedness is not configurable");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sdata,  "SDATA");
	}
	print_cfg_regasg_from_cstbit(Fo, tabs, layer, *regfield_swlock,  "true", "  -- Weight signedness is not configurable");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sweight, "SWEIGHT");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_style,   "STYLE");
	// Custom multiplication identifiers
	if(layer->neu_style==1 || layer->neu_style==2) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_mul_id, "CUSTOM_MUL_ID");
	}

	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SDATA", "PAR_OUT");
	}
}

void LayerNeu_CM::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize, "FSIZE");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_neu,   "NBNEU");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_sdata, "SDATA");
}

void LayerPool::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR_IN");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_type,    "TYPE");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_rndnear, "ROUND_NEAR");
	if(pool_avg_mult > 1 || pool_avg_shr > 0) {
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_mul, "MULT");
		print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_shr, "SHR");
	}
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SDATA", "PAR_OUT");
	}
}
void LayerPool::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize, "FSIZE");
}

void LayerNorm::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	// Miscellaneous variables to ease code refactoring
	Layer* layer = this;
	unsigned need_width = layer->norm_wbias + layer->norm_wmul + layer->norm_wshr;
	unsigned have_width = layer->network->hwconfig_writewidth;
	if(need_width > have_width) {
		printf("WARNING %s%u: Write channel should be at least %u bits wide, but it is %u bits. Results will be erroneous, unless parameters are hardcoded.\n",
			layer->typenameu, layer->typeidx, need_width, have_width
		);
	}
	// Generate
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_enbias,    "BIAS_EN");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_enmul,     "MUL_EN");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_wbias,     "WBIAS", -1);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_wmul,      "WMUL", -1);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_wshr,      "WSHR");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize_max, "FSIZE_MAX");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_cstmul,    "MUL_CST");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_cstshr,    "SHR_CST");
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SOUT", "PAR");
	}
}
void LayerNorm::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize, "FSIZE");
}

void LayerTernarize::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	// Miscellaneous variables to ease code refactoring
	Layer* layer = this;
	// Warning messages
	unsigned need_width = layer->wdata * 2;
	unsigned have_width = layer->network->hwconfig_writewidth;
	if(need_width > have_width) {
		printf("WARNING %s%u: Write channel should be at least %u bits wide, but it is %u bits. Results will be erroneous, unless parameters are hardcoded.\n",
			layer->typenameu, layer->typeidx, need_width, have_width
		);
	}
	// Generate
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_out_static, "OUT_STATIC");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize_max,  "FSIZE_MAX");
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SOUT", "PAR");
	}
}
void LayerTernarize::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize, "FSIZE");
}

void LayerRelu::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- (no layer-specific constants)\n", tabs);
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SOUT", "PAR");
	}
}
void LayerRelu::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_thmin, "MIN");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_thmax, "MAX");
}
void LayerLeaky::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- (no layer-specific constants)\n", tabs);
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SOUT", "PAR");
	}
}
void LayerLeaky::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_thmin, "MIN");
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_thmax, "MAX");
}

void LayerAdd::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR_IN");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- (no layer-specific constants)\n", tabs);
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SDATA", "PAR_OUT");
	}
}

void LayerCustom::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_func_id, "USER_ID");
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SOUT", "PAR");
	}
}

void LayerFork::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_layers_nb, "LAYERS_NB");
	unsigned cur_reg_idx = layer->regs_idx + 1;
	unsigned cur_inreg_nb = 0;
	for(auto linked_layer : arr_layers) {
		Layer* other_layer = (linked_layer->type == LAYER_FIFO) ? linked_layer->next : linked_layer;
		print_cfg_regasg(Fo, tabs, cur_reg_idx, cur_inreg_nb*16+15, cur_inreg_nb*16);
		fprintf(Fo, "std_logic_vector(to_unsigned(%u, 16));  -- Layer %s\n", other_layer->id, other_layer->vhdl_prefixu);
		cur_inreg_nb = (cur_inreg_nb + 1) % 2;
	}
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	}
}

void LayerCat::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- Layer-specific constants\n", tabs);
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_layers_nb, "LAYERS_NB");
	unsigned cur_reg_idx = layer->regs_idx + 1;
	unsigned cur_inreg_nb = 0;
	for(auto linked_layer : arr_layers) {
		Layer* other_layer = (linked_layer->type == LAYER_FIFO) ? linked_layer->prev : linked_layer;
		print_cfg_regasg(Fo, tabs, cur_reg_idx, cur_inreg_nb*16+15, cur_inreg_nb*16);
		fprintf(Fo, "std_logic_vector(to_unsigned(%u, 16));  -- Layer %s\n", other_layer->id, other_layer->vhdl_prefixu);
		cur_inreg_nb = (cur_inreg_nb + 1) % 2;
	}
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	}
}

void LayerFlatten::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- (no layer-specific constants)\n", tabs);
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WDATA", "SDATA", "PAR");
	}
}

void LayerSoftMax::genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	fprintf(Fo, "%s-- Input channel\n", tabs);
	print_cfg_regasg_layer_in(Fo, tabs, layer, "WDATA", "SDATA", "PAR_IN");
	fprintf(Fo, "%s-- Common layer fields\n", tabs);
	print_cfg_regasg_layer_reg(Fo, tabs, layer);
	fprintf(Fo, "%s-- (no layer-specific constants)\n", tabs);
	// Output channel
	if(has_layer_after(layer) == false) {
		fprintf(Fo, "%s-- Output channel\n", tabs);
		print_cfg_regasg_layer_out(Fo, tabs, layer, "WOUT", "SOUT", "PAR_OUT");
	}
}
void LayerSoftMax::genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1) {
	Layer* layer = this;
	print_cfg_regasg_from_signal(Fo, tabs, layer, *regfield_fsize, "FSIZE");
}

void Network::genvhdl_regs_setconst(FILE* Fo) {
	char const * tabs = "		";
	char const * tabs1 = "	";

	// Set the name of the destination VHDL signal or variable
	glob_config_reg_name = "config_regs_var";
	glob_config_asg = ":=";

	for(auto layer : layers) {
		if(layer != layers.front()) fprintf(Fo, "\n");
		fprintf(Fo, "%s-- %s\n", tabs, layer->vhdl_prefixu);
		layer->genvhdl_regs_setconst(Fo, tabs, tabs1);
	}

	// Restore the name of the config regs signal
	glob_config_reg_name = config_reg_name;
	glob_config_asg = "<=";
}

// Write reset functionality
// Or, systematically write that config if the registers are locked or not implemented
void Network::genvhdl_regs_setconst_locked(FILE* Fo) {
	char const * tabs  = "		";
	char const * tabs1 = "	";

	// Set the name of the destination VHDL signal or variable
	glob_config_reg_name = "config_regs_var";
	glob_config_asg = ":=";

	for(auto layer : layers) {

		if(layer != layers.front()) fprintf(Fo, "\n");
		fprintf(Fo, "%s-- %s\n", tabs, layer->vhdl_prefixu);

		layer->genvhdl_regs_setconst_locked(Fo, tabs, tabs1);

	}

	// Restore the name of the config regs signal
	glob_config_reg_name = config_reg_name;
	glob_config_asg = "<=";
}

//============================================
// Instantiations of components
//============================================

// Utility functions to ease connection between layers
// Important : these functions must be called only from non-FIFO layers

void Layer::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	// This default method should not be called
	abort();
}
void Layer::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	// This default method should not be called
	abort();
}
void Layer::genvhdl_comp_get_out_room_port(char const*& name_port) {
	// Default : No input port for room in next FIFO
}
void Layer::genvhdl_comp_get_wout_param(char const*& name_param) {
	// This default method should not be called
	abort();
}

void LayerWin::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_rdy";
	name_ack  = "data_in_ack";
}
void LayerWin::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_rdy";
}
void LayerWin::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "data_out_room";
}
void LayerWin::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WDATA";
}

void LayerNeu::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerNeu::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerNeu::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerNeu::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}

void LayerNeu_CM::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerNeu_CM::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerNeu_CM::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerNeu_CM::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}

void LayerPool::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "in_data";
	name_rdy  = "in_rdy";
	name_ack  = "in_ack";
}
void LayerPool::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "out_data";
	name_rdy  = "out_rdy";
}
void LayerPool::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerPool::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WDATA";
}

void LayerNorm::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerNorm::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerNorm::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerNorm::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}

void LayerTernarize::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerTernarize::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerTernarize::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerTernarize::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}

void LayerRelu::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerRelu::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerRelu::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerRelu::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}
void LayerLeaky::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerLeaky::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerLeaky::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerLeaky::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}

void LayerAdd::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerAdd::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerAdd::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerAdd::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}

void LayerCustom::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerCustom::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerCustom::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerCustom::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}

void LayerFlatten::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "out_data";
	name_rdy  = "out_ack";
	name_ack  = "out_rdy";
}
void LayerFlatten::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "out_data";
	name_rdy  = "out_rdy";
	name_ack  = "out_ack";
}
void LayerFlatten::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WDATA";
}

void LayerSoftMax::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_in";
	name_rdy  = "data_in_ready";
	name_ack  = "data_in_valid";
}
void LayerSoftMax::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "data_out";
	name_rdy  = "data_out_valid";
}
void LayerSoftMax::genvhdl_comp_get_out_room_port(char const*& name_port) {
	name_port = "out_fifo_room";
}
void LayerSoftMax::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "WOUT";
}

void LayerFifo::genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "in_data";
	name_rdy  = "in_rdy";
	name_ack  = "in_ack";
}
void LayerFifo::genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack) {
	name_data = "out_data";
	name_rdy  = "out_rdy";
	name_ack  = "out_ack";
}
void LayerFifo::genvhdl_comp_get_wout_param(char const*& name_param) {
	name_param = "DATAW";
}

//Methods of connection helper class

void GenVhdl_ConnHelper::init_prev_next_layers(Layer* layer) {
	// Get previous and next layers
	if(layer->prev == NULL || layer->next == NULL) {
		printf("Error line %u: No prev or next layer for layer %s\n", __LINE__, layer->vhdl_prefixu);
		exit(EXIT_FAILURE);
	}
	layprev = layer->prev;
	laynext = layer->next;
	layprev_prefixl = layprev->vhdl_prefixl;
	laynext_prefixl = laynext->vhdl_prefixl;
	// Initialize the in/out port names, for direct no-FIFO layer-to-layer connection
	layer->genvhdl_comp_get_in_ports   (layer_in_data,    layer_in_rdy,    layer_in_ack);
	layer->genvhdl_comp_get_out_ports  (layer_out_data,   layer_out_rdy,   layer_out_ack);
	layprev->genvhdl_comp_get_out_ports(layprev_out_data, layprev_out_rdy, layprev_out_ack);
	laynext->genvhdl_comp_get_in_ports (laynext_in_data,  laynext_in_rdy,  laynext_in_ack);
	// The name of the generic parameter for output data width
	layer->genvhdl_comp_get_wout_param(layer_param_wout);
	// The name of the input port for the room available in next FIFO
	layer->genvhdl_comp_get_out_room_port(layer_out_room);
	// Identify the next FIFO in pipeline
	fifonext = laynext;
	while(fifonext!=NULL && fifonext->type!=LAYER_FIFO) fifonext = fifonext->next;
	if(fifonext == NULL) {
		printf("Error line %u: No FIFO was found after layer %s\n", __LINE__, layer->vhdl_prefixu);
		exit(EXIT_FAILURE);
	}
	fifonext_prefixl = fifonext->vhdl_prefixl;
	// Also identify the previous FIFO in the pipeline
	layer_t* layer_prev = layer->prev;
	while(layer_prev != nullptr && layer_prev->type == LAYER_FLATTEN) layer_prev = layer_prev->prev;
	controls_flow = false;
	if(layer_prev != nullptr && layer_prev->type == LAYER_FIFO) controls_flow = true;
};

// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
void GenVhdl_ConnHelper::gen_common_connect(Layer* layer, FILE* Fo, bool selout) {
	// FIXME This is just to ease transition due to code refactoring
	char* layer_prefixl = layer->vhdl_prefixl;
	char* layer_prefixu = layer->vhdl_prefixu;

	// FIXME An option would be useful to enable generation of this debug code
	// Warning: This requires that library textio is used in the destination VHDL file
	if(false) {
		fprintf(Fo,
			"	-- DEBUG : count/save outputs of layer %s\n"
			"	-- Note : Need to manually enable the use of the library std.textio\n"
			"	gen_%s_out : if SIMU_DEBUG = true generate\n",
			layer_prefixu, layer_prefixl
		);
		fprintf(Fo,
			"		signal cnt : integer := 0;\n"
			"	begin\n"
			"\n"
			"		process(clk)\n"
		);
		fprintf(Fo,
			"			file out_file : text open write_mode is \"out-simu-%s.txt\";\n"
			"			variable out_line : line;\n"
			"			constant inline_max : natural := %u;\n",
			layer_prefixl, layer->out_fsize
		);
		fprintf(Fo,
			"			variable inline_cur : natural := 0;\n"
			"		begin\n"
			"			if rising_edge(clk) then\n"
			"				if inst_%s_%s = '1' then\n"
			"					cnt <= cnt + 1;\n",
			layer_prefixl, layer_out_rdy
		);
		fprintf(Fo,
			"					for i in 0 to %u-1 loop\n"
			"						if inline_max > 1 and inline_cur > 0 then\n"
			"							write(out_line, ',');\n"
			"						end if;\n"
			"						hwrite(out_line, inst_%s_%s((i+1)*%u-1 downto i*%u));\n",
			layer->split_out,
			layer_prefixl, layer_out_data,
			layer->out_wdata, layer->out_wdata
		);
		fprintf(Fo,
			"						inline_cur := inline_cur + 1;\n"
			"					end loop;\n"
			"					if inline_cur >= inline_max then\n"
			"						writeline(out_file, out_line);\n"
			"						inline_cur := 0;\n"
			"					end if;\n"
			"				end if;\n"
			"			end if;\n"
			"		end process;\n"
			"\n"
			"	end generate;\n"
			"\n"
		);
	}

	fprintf(Fo, "	-- Link with previous layer\n");
	fprintf(Fo, "	inst_%s_%s <= inst_%s_%s;\n", layer_prefixl, layer_in_data, layprev_prefixl, layprev_out_data);
	fprintf(Fo, "	inst_%s_%s <= inst_%s_%s;\n", layer_prefixl, layer_in_ack, layprev_prefixl, layprev_out_rdy);
	if(layprev->type==LAYER_FIFO) {
		if(layer_in_rdy != nullptr) {
			fprintf(Fo, "	inst_%s_%s <= inst_%s_%s;\n", layprev_prefixl, layprev_out_ack, layer_prefixl, layer_in_rdy);
		}
		else {
			printf("ERROR layer %s : No input-side validation signal to send to previous FIFO\n", layer_prefixu);
			// Paranoia : Permanently validate the FIFO output
			fprintf(Fo, "	inst_%s_%s <= '1';\n", layprev_prefixl, layprev_out_ack);
		}
	}

	if(layer_out_room != nullptr) {
		fprintf(Fo, "\n");
		fprintf(Fo, "	-- The available room in next FIFO\n");
		if(controls_flow == false) {
			fprintf(Fo, "	-- Note : This layer does not control the data flow, so setting an arbitrarily high room value here\n");
			fprintf(Fo, "	inst_%s_%s <= std_logic_vector(to_unsigned(32, inst_%s_%s'length));\n", layer_prefixl, layer_out_room, layer_prefixl, layer_out_room);
		}
		else if(selout == true) {
			fprintf(Fo, "	%s_out_room : if CONFIG_SELOUT = true generate\n", layer_prefixl);
			fprintf(Fo, "		inst_%s_%s <= inst_%s_in_cnt when selout_en_out(%u) = '0' else selout_sca_out(%u*16+15 downto %u*16);\n",
				layer_prefixl, layer_out_room, fifonext_prefixl, layer->id, layer->id, layer->id
			);
			fprintf(Fo, "	else generate\n");
			fprintf(Fo, "		inst_%s_%s <= inst_%s_in_cnt;\n", layer_prefixl, layer_out_room, fifonext_prefixl);
			fprintf(Fo, "	end generate;\n");
		}
		else {
			fprintf(Fo, "	inst_%s_%s <= inst_%s_in_cnt;\n", layer_prefixl, layer_out_room, fifonext_prefixl);
		}
	}

	if(layer_out_ack != nullptr || laynext->type==LAYER_FIFO) {
		fprintf(Fo, "\n");
		fprintf(Fo, "	-- Link with next layer\n");
		if(layer_out_ack != nullptr) {
			fprintf(Fo, "	inst_%s_%s <= inst_%s_%s;\n", layer_prefixl, layer_out_ack, laynext_prefixl, laynext_in_rdy);
		}
		if(laynext->type==LAYER_FIFO) {
			fprintf(Fo, "	inst_%s_%s <= inst_%s_%s;\n", laynext_prefixl, laynext_in_data, layer_prefixl, layer_out_data);
			fprintf(Fo, "	inst_%s_%s <= inst_%s_%s;\n", laynext_prefixl, laynext_in_ack,  layer_prefixl, layer_out_rdy);
		}
	}

	if(selout == true) {
		// Safety : check that the component has an assigned ID
		if(layer->requires_idxhw() == false) {
			printf("ERROR layer %s : Generic connection generation is used, but the component has no ID assigned\n", layer_prefixu);
		}
		// Generate the code
		fprintf(Fo, "\n");
		fprintf(Fo, "	-- Link with the data selection component\n");
		fprintf(Fo, "	%s_selout : if CONFIG_SELOUT = true generate\n", layer_prefixl);
		fprintf(Fo, "		selout_gat_in(%u*33+32 downto %u*33) <= inst_%s_%s & ", layer->id, layer->id, layer_prefixl, layer_out_rdy);
		fprintf(Fo, "std_logic_vector(resize(%s(inst_%s_%s(%s_%s-1 downto 0)), 32));\n",
			layer->out_sdata==true ? "signed" : "unsigned", layer_prefixl, layer_out_data, layer_prefixu, layer_param_wout
		);
		fprintf(Fo, "	end generate;\n");
	}
};

// Main component instantiation methods

void Layer::genvhdl_comp_inst(FILE* Fo) {
	fprintf(Fo, "	-- No need for specific component instance\n");
}

void LayerWin::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	Layer* layer = this;
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	i_%s : nnlayer_window\n", layer_prefixl);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			-- Data\n");
	fprintf(Fo, "			WDATA  => %s_WDATA,\n", layer_prefixu);
	fprintf(Fo, "			-- Dimensions: width, height, depth\n");
	fprintf(Fo, "			DIMX   => %s_DIMX,\n", layer_prefixu);
	fprintf(Fo, "			DIMY   => %s_DIMY,\n", layer_prefixu);
	fprintf(Fo, "			DIMZ   => %s_DIMZ,\n", layer_prefixu);
	fprintf(Fo, "			-- Window size\n");
	fprintf(Fo, "			WINX   => %s_WINX,\n", layer_prefixu);
	fprintf(Fo, "			WINY   => %s_WINY,\n", layer_prefixu);
	fprintf(Fo, "			WINZ   => %s_WINZ,\n", layer_prefixu);
	fprintf(Fo, "			-- Step/stride values\n");
	fprintf(Fo, "			STEPX  => %s_STEPX,\n", layer_prefixu);
	fprintf(Fo, "			STEPY  => %s_STEPY,\n", layer_prefixu);
	fprintf(Fo, "			STEPZ  => %s_STEPZ,\n", layer_prefixu);
	fprintf(Fo, "			-- Number of times the window is used in each dimension\n");
	fprintf(Fo, "			NWINX  => %s_NWINX,\n", layer_prefixu);
	fprintf(Fo, "			NWINY  => %s_NWINY,\n", layer_prefixu);
	fprintf(Fo, "			NWINZ  => %s_NWINZ,\n", layer_prefixu);
	fprintf(Fo, "			-- Padding size at the beginning of each dimension\n");
	fprintf(Fo, "			PADX   => %s_PADX,\n", layer_prefixu);
	fprintf(Fo, "			PADY   => %s_PADY,\n", layer_prefixu);
	fprintf(Fo, "			-- The height on axis Y of the internal buffers, minimum is WINY, max is DIMY, 0 means auto\n");
	fprintf(Fo, "			BUFY   => %s_BUFY,\n", layer_prefixu);
	fprintf(Fo, "			-- Internal storage type, leave all to false for automatic decision\n");
	fprintf(Fo, "			USE_LUTRAM => %s_USE_LUTRAM,\n", layer_prefixu);
	fprintf(Fo, "			USE_BRAM   => %s_USE_BRAM,\n", layer_prefixu);
	fprintf(Fo, "			USE_URAM   => %s_USE_URAM,\n", layer_prefixu);
	fprintf(Fo, "			-- Number of times to repeat the window contents before going to the next window position\n");
	fprintf(Fo, "			REPEAT => %s_REPEAT,\n", layer_prefixu);
	fprintf(Fo, "			-- Parallelism : number of cells to write at one time\n");
	fprintf(Fo, "			PAR_IN => %s_PAR_IN,\n", layer_prefixu);
	fprintf(Fo, "			-- Parallelism : number of cells to read at one time\n");
	fprintf(Fo, "			-- PAR_OUT / PAR_OZ must be 1, or a divisor of WINX, or a multiple of WINX and a divisor of WINX*WINY\n");
	fprintf(Fo, "			PAR_OUT => %s_PAR_OUT,\n", layer_prefixu);
	fprintf(Fo, "			-- Parallelism : Output side on Z dimension\n");
	fprintf(Fo, "			-- This must be a divisor of PAR_OUT, and a multiple of PAR_IN\n");
	fprintf(Fo, "			PAR_OZ => %s_PAR_OZ,\n", layer_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN,\n", layer_prefixu);
	fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
	fprintf(Fo, "			LOCKED => CONFIG_RDONLY\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk             => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset           => inst_%s_clear,\n", layer_prefixl);
	}
	else {
		fprintf(Fo, "			clear           => inst_%s_clear,\n", layer_prefixl);
	}
	fprintf(Fo, "			-- Run-time frame dimensions\n");
	fprintf(Fo, "			user_fsize_x    => inst_%s_fsize_x,\n", layer_prefixl);
	fprintf(Fo, "			user_fsize_y    => inst_%s_fsize_y,\n", layer_prefixl);
	fprintf(Fo, "			user_fsize_z    => inst_%s_fsize_z,\n", layer_prefixl);
	fprintf(Fo, "			-- Run-time window step on each dimension\n");
	fprintf(Fo, "			user_step_x     => inst_%s_step_x,\n", layer_prefixl);
	fprintf(Fo, "			user_step_y     => inst_%s_step_y,\n", layer_prefixl);
	fprintf(Fo, "			user_step_z     => inst_%s_step_z,\n", layer_prefixl);
	fprintf(Fo, "			-- Run-time number of times the window is used in each dimension\n");
	fprintf(Fo, "			user_nwin_x     => inst_%s_nwin_x,\n", layer_prefixl);
	fprintf(Fo, "			user_nwin_y     => inst_%s_nwin_y,\n", layer_prefixl);
	fprintf(Fo, "			user_nwin_z     => inst_%s_nwin_z,\n", layer_prefixl);
	fprintf(Fo, "			-- Run-time padding size at the beginning of each dimension\n");
	fprintf(Fo, "			user_begpad_x   => inst_%s_begpad_x,\n", layer_prefixl);
	fprintf(Fo, "			user_begpad_y   => inst_%s_begpad_y,\n", layer_prefixl);
	fprintf(Fo, "			-- Data input\n");
	fprintf(Fo, "			data_in         => inst_%s_data_in,\n", layer_prefixl);
	fprintf(Fo, "			data_in_rdy     => inst_%s_data_in_rdy,\n", layer_prefixl);
	fprintf(Fo, "			data_in_ack     => inst_%s_data_in_ack,\n", layer_prefixl);
	fprintf(Fo, "			-- Data output\n");
	fprintf(Fo, "			data_out        => inst_%s_data_out,\n", layer_prefixl);
	fprintf(Fo, "			data_out_rdy    => inst_%s_data_out_rdy,\n", layer_prefixl);
	fprintf(Fo, "			data_out_room   => inst_%s_data_out_room\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the prev and next layers are always FIFOs
	connObj.init_prev_next_layers(layer);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear    <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers for X dimension\n");
	fprintf(Fo, "	inst_%s_fsize_x  <= ", layer_prefixl);            print_cfg_reg(Fo, regs_idx, regfield_fx); fprintf(Fo, ";\n");
	fprintf(Fo, "	inst_%s_step_x   <= \"00\" & ", layer_prefixl);   print_cfg_reg(Fo, regs_idx, regfield_stepx); fprintf(Fo, ";\n");
	fprintf(Fo, "	inst_%s_begpad_x <= \"0000\" & ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_padx); fprintf(Fo, ";\n");
	fprintf(Fo, "	inst_%s_nwin_x   <= ", layer_prefixl);            print_cfg_reg(Fo, regs_idx, regfield_nwinx); fprintf(Fo, ";\n");
	fprintf(Fo, "	-- Get config registers for Z dimension\n");
	fprintf(Fo, "	inst_%s_fsize_z  <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_fz); fprintf(Fo, ";\n");
	fprintf(Fo, "	inst_%s_step_z   <= std_logic_vector(to_unsigned(1, 8));\n", layer_prefixl);
	fprintf(Fo, "	inst_%s_nwin_z   <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_nwinz); fprintf(Fo, ";\n");
	if(layer->win_sym_xy == true) {
		fprintf(Fo, "	-- Get config registers for Y dimension -> take values for X dimension because of symmetry\n");
		fprintf(Fo, "	inst_%s_fsize_y  <= inst_%s_fsize_x;\n",  layer_prefixl, layer_prefixl);
		fprintf(Fo, "	inst_%s_step_y   <= inst_%s_step_x;\n",   layer_prefixl, layer_prefixl);
		fprintf(Fo, "	inst_%s_begpad_y <= inst_%s_begpad_x;\n", layer_prefixl, layer_prefixl);
		fprintf(Fo, "	inst_%s_nwin_y   <= inst_%s_nwin_x;\n",   layer_prefixl, layer_prefixl);
	}
	else {
		fprintf(Fo, "	-- Get config registers for Y dimension\n");
		fprintf(Fo, "	inst_%s_fsize_y  <= ", layer_prefixl);            print_cfg_reg(Fo, regs_idx, regfield_fy); fprintf(Fo, ";\n");
		fprintf(Fo, "	inst_%s_step_y   <= \"00\" & ", layer_prefixl);   print_cfg_reg(Fo, regs_idx, regfield_stepy); fprintf(Fo, ";\n");
		fprintf(Fo, "	inst_%s_begpad_y <= \"0000\" & ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_pady); fprintf(Fo, ";\n");
		fprintf(Fo, "	inst_%s_nwin_y   <= ", layer_prefixl);            print_cfg_reg(Fo, regs_idx, regfield_nwiny); fprintf(Fo, ";\n");
	}
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(layer, Fo, true);

}
void LayerNeu::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	Layer* layer = this;
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	if(layer->neu_style==0 && layer->split_in*layer->split_out > 1024) {
		printf("Error: layer %s: Split into %u blocks is not possible\n", layer_prefixu, layer->split_in*layer->split_out);
		exit(EXIT_FAILURE);
	}

	char const * entity = nullptr;
	if     (layer->neu_style == 0) entity = "nnlayer_neurons_ter_postadd_xilinx";
	else if(layer->neu_style == 1) entity = "nnlayer_neurons";
	else if(layer->neu_style == 2) entity = "nnlayer_neurons";
	else {
		printf("Error: layer %s: Unhandled neuron style %u\n", layer_prefixu, layer->neu_style);
		exit(EXIT_FAILURE);
	}

	if(network->hwconfig_asicmode==true) entity = "neuron_layer";

	fprintf(Fo, "	i_%s : %s\n", layer_prefixl, entity);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			-- Parameters for the neurons\n");
	fprintf(Fo, "			WDATA      => %s_WDATA,\n", layer_prefixu);
	if(network->hwconfig_asicmode==false) {
		fprintf(Fo, "			SDATA      => %s_SDATA,\n", layer_prefixu);
		if(layer->neu_style==0) {
			fprintf(Fo, "			SFIXED     => %s_SFIXED,\n", layer_prefixu);
			fprintf(Fo, "			WACCU      => %s_WACCU,\n", layer_prefixu);
		}
		if(layer->neu_style==1 || layer->neu_style==2) {
			fprintf(Fo, "			WWEIGHT    => %s_WWEIGHT,\n", layer_prefixu);
			fprintf(Fo, "			SWEIGHT    => %s_SWEIGHT,\n", layer_prefixu);
		}
	}
	fprintf(Fo, "			WOUT       => %s_WOUT,\n", layer_prefixu);
	if(network->hwconfig_asicmode==false) {
		fprintf(Fo, "			-- Parameters for BRAM usage\n");
		fprintf(Fo, "			NPERBLK    => %s_NPERBLK,\n", layer_prefixu);
		fprintf(Fo, "			WRNB       => %s_WRNB,\n", layer_prefixu);
		if(layer->neu_style==1 || layer->neu_style==2) {
			fprintf(Fo, "			WWRITE     => %s_WWRITE,\n", layer_prefixu);
			fprintf(Fo, "			USE_REGS   => %s_REGS,\n", layer_prefixu);
			fprintf(Fo, "			USE_LUTRAM => %s_LUTRAM,\n", layer_prefixu);
			fprintf(Fo, "			USE_BRAM   => %s_BRAM,\n", layer_prefixu);
			fprintf(Fo, "			USE_URAM   => %s_URAM,\n", layer_prefixu);
			fprintf(Fo, "			USE_CONST  => %s_CONST,\n", layer_prefixu);
			fprintf(Fo, "			PACKED     => %s_PACKED,\n", layer_prefixu);
			fprintf(Fo, "			-- Identifier of neuron layer, mostly to be passed to custom internal components\n");
			fprintf(Fo, "			LAYER_ID   => %s_LAYER_ID,\n", layer_prefixu);
			fprintf(Fo, "			-- For compression of weights in memory\n");
			fprintf(Fo, "			COMP_STYLE => %s_COMP_STYLE,\n", layer_prefixu);
			fprintf(Fo, "	   	COMP_WRAW  => %s_COMP_WRAW,\n", layer_prefixu);
			fprintf(Fo, "	   	COMP_WENC  => %s_COMP_WENC,\n", layer_prefixu);
			fprintf(Fo, "			COMP_ENWR  => %s_COMP_ENWR,\n", layer_prefixu);
		}
	}
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			WWRITE     => %s_WWRITE,\n", layer_prefixu);
	}
	fprintf(Fo, "			-- Parameters for frame and number of neurons\n");
	fprintf(Fo, "			FSIZE      => %s_FSIZE_MAX,\n", layer_prefixu);
	fprintf(Fo, "			NBNEU      => %s_NBNEU_MAX,\n", layer_prefixu);
	fprintf(Fo, "			-- Level of time multiplexing of the neuron accumulators\n");
	fprintf(Fo, "			TIME_MUX   => %s_TIME_MUX,\n", layer_prefixu);
	fprintf(Fo, "			-- Depth-Wise convolution mode : each activation goes to only one physical neuron\n");
	fprintf(Fo, "			DWCONV     => %s_DWCONV,\n", layer_prefixu);
	fprintf(Fo, "			-- Parameters for input and output parallelism\n");
	fprintf(Fo, "			PAR_IN     => %s_PAR_IN,\n", layer_prefixu);
	fprintf(Fo, "			PAR_OUT    => %s_PAR_OUT", layer_prefixu);
	if(layer->neu_style == 1 || layer->neu_style == 2) {
		fprintf(Fo, ",\n");
		fprintf(Fo, "			-- Constant weights passed directly\n");
		fprintf(Fo, "			CSTWEIGHTS_NB  => %s_CSTWEIGHTS_NB,\n", layer_prefixu);
		fprintf(Fo, "			CSTWEIGHTS_VEC => %s_CSTWEIGHTS_VEC,\n", layer_prefixu);
		fprintf(Fo, "			-- Identifier of multiplication operation\n");
		fprintf(Fo, "			CUSTOM_MUL_ID => %s_CUSTOM_MUL_ID,\n", layer_prefixu);
		fprintf(Fo, "			CUSTOM_WMUL   => %s_CUSTOM_WMUL,\n", layer_prefixu);
		fprintf(Fo, "			CUSTOM_SMUL   => %s_CUSTOM_SMUL", layer_prefixu);
	}
	if(network->hwconfig_asicmode==false) {
		fprintf(Fo, ",\n");
		fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
		fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN", layer_prefixu);
		if(layer->neu_style == 1 || layer->neu_style == 2) {
			fprintf(Fo, ",\n");
			fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
			fprintf(Fo, "			LOCKED => CONFIG_RDONLY");
		}
	}
	fprintf(Fo, "\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_clear,\n", layer_prefixl);
	}
	else {
		fprintf(Fo, "			clear          => inst_%s_clear,\n", layer_prefixl);
	}
	fprintf(Fo, "			write_mode     => inst_%s_write_mode,\n", layer_prefixl);
	if(layer->neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "			write_idx      => inst_%s_write_idx,\n", layer_prefixl);
	}
	fprintf(Fo, "			write_data     => inst_%s_write_data,\n", layer_prefixl);
	fprintf(Fo, "			write_enable   => inst_%s_write_enable,\n", layer_prefixl);
	fprintf(Fo, "			write_end      => open,\n");
	fprintf(Fo, "			user_fsize     => inst_%s_user_fsize,\n", layer_prefixl);
	fprintf(Fo, "			user_nbneu     => inst_%s_user_nbneu,\n", layer_prefixl);
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", layer_prefixl);
	if(layer->neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "			data_in_signed => inst_%s_data_in_signed,\n", layer_prefixl);
	}
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", layer_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", layer_prefixl);
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", layer_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", layer_prefixl);
	fprintf(Fo, "			end_of_frame   => inst_%s_end_of_frame,\n", layer_prefixl);
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	fprintf(Fo, "	-- Need special stuff to generate the write mode\n");
	fprintf(Fo, "	gen_%s_wrmode: if true generate\n", layer_prefixl);
	fprintf(Fo, "		signal wrmode_layer : std_logic;\n");
	fprintf(Fo, "	begin\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "		process(clk)\n");
	fprintf(Fo, "		begin\n");
	fprintf(Fo, "			if rising_edge(clk) then\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "				wrmode_layer <= '0';\n");
	fprintf(Fo, "				if unsigned(cur_recv1) = CST_RECV_CFG_%s then\n", layer_prefixu);
	fprintf(Fo, "					wrmode_layer <= '1';\n");
	fprintf(Fo, "				end if;\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "			end if;\n");
	fprintf(Fo, "		end process;\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "		inst_%s_write_mode <= wrmode_layer;\n", layer_prefixl);
	if(layer->neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "		inst_%s_write_idx  <= cur_recv2;\n", layer_prefixl);
	}
	fprintf(Fo, "\n");
	fprintf(Fo, "	end generate;\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the prev layers is always FIFO
	connObj.init_prev_next_layers(layer);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear          <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers\n");
	fprintf(Fo, "	inst_%s_user_fsize     <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_fsize); fprintf(Fo, ";\n");
	fprintf(Fo, "	inst_%s_user_nbneu     <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_neu); fprintf(Fo, ";\n");
	if(layer->neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "	inst_%s_data_in_signed <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_sdata); fprintf(Fo, ";\n");
	}
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Set write channel\n");
	fprintf(Fo, "	inst_%s_write_data     <= inst_rxfifo_out_data(inst_%s_write_data'length - 1 downto 0);\n", layer_prefixl, layer_prefixl);
	fprintf(Fo, "	inst_%s_write_enable   <= inst_rxfifo_out_rdy;\n", layer_prefixl);
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(layer, Fo, true);

}

void LayerNeu_CM::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	Layer* layer = this;
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	if(layer->neu_style==0 && layer->split_in*layer->split_out > 1024) {
		printf("Error: layer %s: Split into %u blocks is not possible\n", layer_prefixu, layer->split_in*layer->split_out);
		exit(EXIT_FAILURE);
	}

	char const * entity = nullptr;
	if     (layer->neu_style == 0) entity = "nnlayer_neurons_ter_postadd_xilinx";
	else if(layer->neu_style == 1) entity = "nnlayer_neurons";
	else if(layer->neu_style == 2) entity = "nnlayer_neurons";
	else {
		printf("Error: layer %s: Unhandled neuron style %u\n", layer_prefixu, layer->neu_style);
		exit(EXIT_FAILURE);
	}

	if(network->hwconfig_asicmode==true) entity = "neuron_layer";

	fprintf(Fo, "	i_%s : %s\n", layer_prefixl, entity);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			-- Parameters for the neurons\n");
	fprintf(Fo, "			WDATA      => %s_WDATA,\n", layer_prefixu);
	if(network->hwconfig_asicmode==false) {
		fprintf(Fo, "			SDATA      => %s_SDATA,\n", layer_prefixu);
		if(layer->neu_style==0) {
			fprintf(Fo, "			SFIXED     => %s_SFIXED,\n", layer_prefixu);
			fprintf(Fo, "			WACCU      => %s_WACCU,\n", layer_prefixu);
		}
		if(layer->neu_style==1 || layer->neu_style==2) {
			fprintf(Fo, "			WWEIGHT    => %s_WWEIGHT,\n", layer_prefixu);
			fprintf(Fo, "			SWEIGHT    => %s_SWEIGHT,\n", layer_prefixu);
		}
	}
	fprintf(Fo, "			WOUT       => %s_WOUT,\n", layer_prefixu);
	if(network->hwconfig_asicmode==false) {
		fprintf(Fo, "			-- Parameters for BRAM usage\n");
		fprintf(Fo, "			NPERBLK    => %s_NPERBLK,\n", layer_prefixu);
		fprintf(Fo, "			WRNB       => %s_WRNB,\n", layer_prefixu);
		if(layer->neu_style==1 || layer->neu_style==2) {
			fprintf(Fo, "			WWRITE     => %s_WWRITE,\n", layer_prefixu);
			fprintf(Fo, "			USE_REGS   => %s_REGS,\n", layer_prefixu);
			fprintf(Fo, "			USE_LUTRAM => %s_LUTRAM,\n", layer_prefixu);
			fprintf(Fo, "			USE_BRAM   => %s_BRAM,\n", layer_prefixu);
			fprintf(Fo, "			USE_URAM   => %s_URAM,\n", layer_prefixu);
			fprintf(Fo, "			USE_CONST  => %s_CONST,\n", layer_prefixu);
			fprintf(Fo, "			PACKED     => %s_PACKED,\n", layer_prefixu);
			fprintf(Fo, "			-- Identifier of neuron layer, mostly to be passed to custom internal components\n");
			fprintf(Fo, "			LAYER_ID   => %s_LAYER_ID,\n", layer_prefixu);
			fprintf(Fo, "			-- For compression of weights in memory\n");
			fprintf(Fo, "			COMP_STYLE => %s_COMP_STYLE,\n", layer_prefixu);
			fprintf(Fo, "	   	COMP_WRAW  => %s_COMP_WRAW,\n", layer_prefixu);
			fprintf(Fo, "	   	COMP_WENC  => %s_COMP_WENC,\n", layer_prefixu);
			fprintf(Fo, "			COMP_ENWR  => %s_COMP_ENWR,\n", layer_prefixu);
		}
	}
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			WWRITE     => %s_WWRITE,\n", layer_prefixu);
	}
	fprintf(Fo, "			-- Parameters for frame and number of neurons\n");
	fprintf(Fo, "			FSIZE      => %s_FSIZE_MAX,\n", layer_prefixu);
	fprintf(Fo, "			NBNEU      => %s_NBNEU_MAX,\n", layer_prefixu);
	fprintf(Fo, "			-- Level of time multiplexing of the neuron accumulators\n");
	fprintf(Fo, "			TIME_MUX   => %s_TIME_MUX,\n", layer_prefixu);
	fprintf(Fo, "			-- Depth-Wise convolution mode : each activation goes to only one physical neuron\n");
	fprintf(Fo, "			DWCONV     => %s_DWCONV,\n", layer_prefixu);
	fprintf(Fo, "			-- Parameters for input and output parallelism\n");
	fprintf(Fo, "			PAR_IN     => %s_PAR_IN,\n", layer_prefixu);
	fprintf(Fo, "			PAR_OUT    => %s_PAR_OUT", layer_prefixu);
	if(layer->neu_style == 1 || layer->neu_style == 2) {
		fprintf(Fo, ",\n");
		fprintf(Fo, "			-- Constant weights passed directly\n");
		fprintf(Fo, "			CSTWEIGHTS_NB  => %s_CSTWEIGHTS_NB,\n", layer_prefixu);
		fprintf(Fo, "			CSTWEIGHTS_VEC => %s_CSTWEIGHTS_VEC,\n", layer_prefixu);
		fprintf(Fo, "			-- Identifier of multiplication operation\n");
		fprintf(Fo, "			CUSTOM_MUL_ID => %s_CUSTOM_MUL_ID,\n", layer_prefixu);
		fprintf(Fo, "			CUSTOM_WMUL   => %s_CUSTOM_WMUL,\n", layer_prefixu);
		fprintf(Fo, "			CUSTOM_SMUL   => %s_CUSTOM_SMUL", layer_prefixu);
	}
	if(network->hwconfig_asicmode==false) {
		fprintf(Fo, ",\n");
		fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
		fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN", layer_prefixu);
		if(layer->neu_style == 1 || layer->neu_style == 2) {
			fprintf(Fo, ",\n");
			fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
			fprintf(Fo, "			LOCKED => CONFIG_RDONLY");
		}
	}
	fprintf(Fo, "\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_clear,\n", layer_prefixl);
	}
	else {
		fprintf(Fo, "			clear          => inst_%s_clear,\n", layer_prefixl);
	}
	fprintf(Fo, "			write_mode     => inst_%s_write_mode,\n", layer_prefixl);
	if(layer->neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "			write_idx      => inst_%s_write_idx,\n", layer_prefixl);
	}
	fprintf(Fo, "			write_data     => inst_%s_write_data,\n", layer_prefixl);
	fprintf(Fo, "			write_enable   => inst_%s_write_enable,\n", layer_prefixl);
	fprintf(Fo, "			write_end      => open,\n");
	fprintf(Fo, "			user_fsize     => inst_%s_user_fsize,\n", layer_prefixl);
	fprintf(Fo, "			user_nbneu     => inst_%s_user_nbneu,\n", layer_prefixl);
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", layer_prefixl);
	if(layer->neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "			data_in_signed => inst_%s_data_in_signed,\n", layer_prefixl);
	}
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", layer_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", layer_prefixl);
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", layer_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", layer_prefixl);
	fprintf(Fo, "			end_of_frame   => inst_%s_end_of_frame,\n", layer_prefixl);
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	fprintf(Fo, "	-- Need special stuff to generate the write mode\n");
	fprintf(Fo, "	gen_%s_wrmode: if true generate\n", layer_prefixl);
	fprintf(Fo, "		signal wrmode_layer : std_logic;\n");
	fprintf(Fo, "	begin\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "		process(clk)\n");
	fprintf(Fo, "		begin\n");
	fprintf(Fo, "			if rising_edge(clk) then\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "				wrmode_layer <= '0';\n");
	fprintf(Fo, "				if unsigned(cur_recv1) = CST_RECV_CFG_%s then\n", layer_prefixu);
	fprintf(Fo, "					wrmode_layer <= '1';\n");
	fprintf(Fo, "				end if;\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "			end if;\n");
	fprintf(Fo, "		end process;\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "		inst_%s_write_mode <= wrmode_layer;\n", layer_prefixl);
	if(layer->neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "		inst_%s_write_idx  <= cur_recv2;\n", layer_prefixl);
	}
	fprintf(Fo, "\n");
	fprintf(Fo, "	end generate;\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the prev layers is always FIFO
	connObj.init_prev_next_layers(layer);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear          <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers\n");
	fprintf(Fo, "	inst_%s_user_fsize     <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_fsize); fprintf(Fo, ";\n");
	fprintf(Fo, "	inst_%s_user_nbneu     <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_neu); fprintf(Fo, ";\n");
	if(layer->neu_style==0 && network->hwconfig_asicmode==false) {
		fprintf(Fo, "	inst_%s_data_in_signed <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_sdata); fprintf(Fo, ";\n");
	}
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Set write channel\n");
	fprintf(Fo, "	inst_%s_write_data     <= inst_rxfifo_out_data(inst_%s_write_data'length - 1 downto 0);\n", layer_prefixl, layer_prefixl);
	fprintf(Fo, "	inst_%s_write_enable   <= inst_rxfifo_out_rdy;\n", layer_prefixl);
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(layer, Fo, true);

}
void LayerPool::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	// FIXME For Average pooling the FSIZE parameter should always be locked

	fprintf(Fo, "	i_%s : nnlayer_pooling\n", layer_prefixl);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			WDATA   => %s_WDATA,\n", layer_prefixu);
	fprintf(Fo, "			SDATA   => %s_SDATA,\n", layer_prefixu);
	fprintf(Fo, "			WOUT    => %s_WOUT,\n", layer_prefixu);
	fprintf(Fo, "			-- Frame size and number of units\n");
	fprintf(Fo, "			FSIZE   => %s_FSIZE,\n", layer_prefixu);
	fprintf(Fo, "			NPOOL   => %s_NPOOL,\n", layer_prefixu);
	fprintf(Fo, "			-- The type of pooling\n");
	fprintf(Fo, "			OPMAX   => %s_OPMAX,\n", layer_prefixu);
	fprintf(Fo, "			OPMIN   => %s_OPMIN,\n", layer_prefixu);
	fprintf(Fo, "			OPADD   => %s_OPADD,\n", layer_prefixu);
	fprintf(Fo, "			-- Parameters for Average pooling\n");
	fprintf(Fo, "			MULT    => %s_MULT,\n", layer_prefixu);
	fprintf(Fo, "			SHR     => %s_SHR,\n", layer_prefixu);
	fprintf(Fo, "			-- Activate rounding to nearest integer (default is rounding is towards zero)\n");
	fprintf(Fo, "			ROUND_NEAR => %s_ROUND_NEAR,\n", layer_prefixu);
	fprintf(Fo, "			-- Parameters for input and output parallelism\n");
	fprintf(Fo, "			PAR_IN  => %s_PAR_IN,\n", layer_prefixu);
	fprintf(Fo, "			PAR_OUT => %s_PAR_OUT,\n", layer_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN,\n", layer_prefixu);
	fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
	fprintf(Fo, "			LOCKED  => CONFIG_RDONLY\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk           => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset         => inst_%s_clear,\n", layer_prefixl);
	}
	else {
		fprintf(Fo, "			clear         => inst_%s_clear,\n", layer_prefixl);
	}
	fprintf(Fo, "			-- Frame size\n");
	fprintf(Fo, "			user_fsize    => inst_%s_user_fsize,\n", layer_prefixl);
	fprintf(Fo, "			-- Data input\n");
	fprintf(Fo, "			in_data       => inst_%s_in_data,\n", layer_prefixl);
	fprintf(Fo, "			in_rdy        => inst_%s_in_rdy,\n", layer_prefixl);
	fprintf(Fo, "			in_ack        => inst_%s_in_ack,\n", layer_prefixl);
	fprintf(Fo, "			-- Data output\n");
	fprintf(Fo, "			out_data      => inst_%s_out_data,\n", layer_prefixl);
	fprintf(Fo, "			out_rdy       => inst_%s_out_rdy,\n", layer_prefixl);
	fprintf(Fo, "			out_fifo_room => inst_%s_out_fifo_room\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the prev and next layers are always FIFOs
	connObj.init_prev_next_layers(this);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers\n");
	fprintf(Fo, "	inst_%s_user_fsize <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_fsize); fprintf(Fo, ";\n");
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(this, Fo, true);

}

void LayerNorm::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	i_%s : nnlayer_norm\n", layer_prefixl);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			WDATA => %s_WDATA,\n", layer_prefixu);
	fprintf(Fo, "			SDATA => %s_SDATA,\n", layer_prefixu);
	fprintf(Fo, "			WOUT  => %s_WOUT,\n", layer_prefixu);
	fprintf(Fo, "			FSIZE => %s_FSIZE_MAX,\n", layer_prefixu);
	fprintf(Fo, "			PAR   => %s_PAR,\n", layer_prefixu);
	fprintf(Fo, "			-- Identifier of neuron layer, for the constant memory component if any\n");
	fprintf(Fo, "			LAYER_ID => %s_LAYER_ID,\n", layer_prefixu);
	fprintf(Fo, "			-- Parameters for memory usage\n");
	fprintf(Fo, "			CONST_PARAMS => %s_CONST_PARAMS,\n", layer_prefixu);
	fprintf(Fo, "			-- Constant multiplication and shift parameters (zero means unused)\n");
	fprintf(Fo, "			MUL_CST => %s_MUL_CST,\n", layer_prefixu);
	fprintf(Fo, "			SHR_CST => %s_SHR_CST,\n", layer_prefixu);
	fprintf(Fo, "			-- The optional run-time bias parameter\n");
	fprintf(Fo, "			BIAS_EN => %s_BIAS_EN,\n", layer_prefixu);
	fprintf(Fo, "			WBIAS   => %s_WBIAS,\n", layer_prefixu);
	fprintf(Fo, "			-- The optional run-time multiplication parameter\n");
	fprintf(Fo, "			MUL_EN => %s_MUL_EN,\n", layer_prefixu);
	fprintf(Fo, "			WMUL   => %s_WMUL,\n", layer_prefixu);
	fprintf(Fo, "			-- The optional run-time shift right\n");
	fprintf(Fo, "			SHR_EN => %s_SHR_EN,\n", layer_prefixu);
	fprintf(Fo, "			WSHR   => %s_WSHR,\n", layer_prefixu);
	fprintf(Fo, "			-- Activate rounding to nearest integer (default is rounding is towards zero)\n");
	fprintf(Fo, "			ROUND_NEAR => %s_ROUND_NEAR,\n", layer_prefixu);
	fprintf(Fo, "			-- Width of the write port\n");
	fprintf(Fo, "			WWRITE => %s_WWRITE,\n", layer_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN,\n", layer_prefixu);
	fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
	fprintf(Fo, "			LOCKED => CONFIG_RDONLY\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_addr_clear,\n", layer_prefixl);
	}
	else {
		fprintf(Fo, "			addr_clear     => inst_%s_addr_clear,\n", layer_prefixl);
	}
	fprintf(Fo, "			write_mode     => inst_%s_write_mode,\n", layer_prefixl);
	fprintf(Fo, "			write_data     => inst_%s_write_data,\n", layer_prefixl);
	fprintf(Fo, "			write_enable   => inst_%s_write_enable,\n", layer_prefixl);
	fprintf(Fo, "			user_fsize     => inst_%s_user_fsize,\n", layer_prefixl);
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", layer_prefixl);
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", layer_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", layer_prefixl);
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", layer_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", layer_prefixl);
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "	inst_%s_write_mode <= '1' when unsigned(cur_recv1) = CST_RECV_CFG_%s else '0';\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the next layer is always FIFO
	connObj.init_prev_next_layers(this);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_addr_clear   <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers\n");
	fprintf(Fo, "	inst_%s_user_fsize   <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_fsize); fprintf(Fo, ";\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Set write channel\n");
	fprintf(Fo, "	inst_%s_write_data   <= inst_rxfifo_out_data(inst_%s_write_data'length - 1 downto 0);\n", layer_prefixl, layer_prefixl);
	fprintf(Fo, "	inst_%s_write_enable <= inst_rxfifo_out_rdy;\n", layer_prefixl);
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(this, Fo, true);

}
void LayerTernarize::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	char const * entity = "nnlayer_recode_to_ter";
	if(network->hwconfig_asicmode==true) entity = "recode_layer";

	fprintf(Fo, "	i_%s : %s\n", layer_prefixl, entity);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			WDATA => %s_WDATA,\n", layer_prefixu);
	fprintf(Fo, "			SDATA => %s_SDATA,\n", layer_prefixu);
	fprintf(Fo, "			WOUT  => %s_WOUT,\n", layer_prefixu);
	fprintf(Fo, "			FSIZE => %s_FSIZE_MAX,\n", layer_prefixu);
	fprintf(Fo, "			PAR   => %s_PAR,\n", layer_prefixu);
	fprintf(Fo, "			-- Identifier of neuron layer, for the constant memory component if any\n");
	fprintf(Fo, "			LAYER_ID => %s_LAYER_ID,\n", layer_prefixu);
	fprintf(Fo, "			-- Parameters for memory usage\n");
	fprintf(Fo, "			CONST_PARAMS => %s_CONST_PARAMS,\n", layer_prefixu);
	fprintf(Fo, "			-- To avoid storing outputs in memory\n");
	fprintf(Fo, "			OUT_STATIC => %s_OUT_STATIC,\n", layer_prefixu);
	fprintf(Fo, "			-- Width of the write port\n");
	fprintf(Fo, "			WWRITE => %s_WWRITE,\n", layer_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN,\n", layer_prefixu);
	fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
	fprintf(Fo, "			LOCKED => CONFIG_RDONLY\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_addr_clear,\n", layer_prefixl);
	}
	else {
		fprintf(Fo, "			addr_clear     => inst_%s_addr_clear,\n", layer_prefixl);
	}
	fprintf(Fo, "			write_mode     => inst_%s_write_mode,\n", layer_prefixl);
	fprintf(Fo, "			write_data     => inst_%s_write_data,\n", layer_prefixl);
	fprintf(Fo, "			write_enable   => inst_%s_write_enable,\n", layer_prefixl);
	fprintf(Fo, "			user_fsize     => inst_%s_user_fsize,\n", layer_prefixl);
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", layer_prefixl);
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", layer_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", layer_prefixl);
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", layer_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", layer_prefixl);
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "	inst_%s_write_mode <= '1' when unsigned(cur_recv1) = CST_RECV_CFG_%s else '0';\n", layer_prefixl, layer_prefixu);
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the next layer is always FIFO
	connObj.init_prev_next_layers(this);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_addr_clear    <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers\n");
	fprintf(Fo, "	inst_%s_user_fsize    <= ", layer_prefixl); print_cfg_reg(Fo, regs_idx, regfield_fsize); fprintf(Fo, ";\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Set write channel\n");
	fprintf(Fo, "	inst_%s_write_data    <= inst_rxfifo_out_data(inst_%s_write_data'length - 1 downto 0);\n", layer_prefixl, layer_prefixl);
	fprintf(Fo, "	inst_%s_write_enable  <= inst_rxfifo_out_rdy;\n", layer_prefixl);
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(this, Fo, true);

}
void LayerRelu::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	i_%s : nnlayer_relu\n", layer_prefixl);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			WDATA => %s_WDATA,\n", layer_prefixu);
	fprintf(Fo, "			SDATA => %s_SDATA,\n", layer_prefixu);
	fprintf(Fo, "			WOUT  => %s_WOUT,\n", layer_prefixu);
	fprintf(Fo, "			PAR   => %s_PAR,\n", layer_prefixu);
	fprintf(Fo, "			-- Enable/disable input buffering and flow control\n");
	fprintf(Fo, "			INBUF => %s_INBUF,\n", layer_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN,\n", layer_prefixu);
	fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
	fprintf(Fo, "			LOCKED => CONFIG_RDONLY\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_clear,\n", layer_prefixl);
	}
	else {
		fprintf(Fo, "			clear          => inst_%s_clear,\n", layer_prefixl);
	}
	fprintf(Fo, "			user_min       => inst_%s_user_min,\n", layer_prefixl);
	fprintf(Fo, "			user_max       => inst_%s_user_max,\n", layer_prefixl);
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", layer_prefixl);
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", layer_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", layer_prefixl);
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", layer_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", layer_prefixl);
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the next layer is always FIFO
	connObj.init_prev_next_layers(this);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers\n");
	fprintf(Fo, "	inst_%s_user_min <= ", layer_prefixl); print_cfg_reg_varwidth(Fo, regs_idx+regfield_thmin->reg_idx, 0, layer_prefixu, "_WOUT+1"); fprintf(Fo, ";\n");
	fprintf(Fo, "	inst_%s_user_max <= ", layer_prefixl); print_cfg_reg_varwidth(Fo, regs_idx+regfield_thmax->reg_idx, 0, layer_prefixu, "_WOUT+1"); fprintf(Fo, ";\n");
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(this, Fo, true);

}
void LayerLeaky::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	i_%s : nnlayer_leaky\n", layer_prefixl);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			WDATA => %s_WDATA,\n", layer_prefixu);
	fprintf(Fo, "			SDATA => %s_SDATA,\n", layer_prefixu);
	fprintf(Fo, "			WOUT  => %s_WOUT,\n", layer_prefixu);
	fprintf(Fo, "			PAR   => %s_PAR,\n", layer_prefixu);
	fprintf(Fo, "			-- Enable/disable input buffering and flow control\n");
	fprintf(Fo, "			INBUF => %s_INBUF,\n", layer_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN,\n", layer_prefixu);
	fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
	fprintf(Fo, "			LOCKED => CONFIG_RDONLY\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_clear,\n", layer_prefixl);
	}
	else {
		fprintf(Fo, "			clear          => inst_%s_clear,\n", layer_prefixl);
	}
	fprintf(Fo, "			user_min       => inst_%s_user_min,\n", layer_prefixl);
	fprintf(Fo, "			user_max       => inst_%s_user_max,\n", layer_prefixl);
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", layer_prefixl);
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", layer_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", layer_prefixl);
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", layer_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", layer_prefixl);
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the next layer is always FIFO
	connObj.init_prev_next_layers(this);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers\n");
	fprintf(Fo, "	inst_%s_user_min <= ", layer_prefixl); print_cfg_reg_varwidth(Fo, regs_idx+regfield_thmin->reg_idx, 0, layer_prefixu, "_WOUT+1"); fprintf(Fo, ";\n");
	fprintf(Fo, "	inst_%s_user_max <= ", layer_prefixl); print_cfg_reg_varwidth(Fo, regs_idx+regfield_thmax->reg_idx, 0, layer_prefixu, "_WOUT+1"); fprintf(Fo, ";\n");
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(this, Fo, true);

}
void LayerAdd::genvhdl_comp_inst(FILE* Fo) {

	fprintf(Fo, "	i_%s : nnlayer_add\n", vhdl_prefixl);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			WDATA  => %s_WDATA,\n", vhdl_prefixu);
	fprintf(Fo, "			SDATA  => %s_SDATA,\n", vhdl_prefixu);
	fprintf(Fo, "			WOUT   => %s_WOUT,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Parameters for input and output parallelism\n");
	fprintf(Fo, "			PAR_IN  => %s_PAR_IN,\n", vhdl_prefixu);
	fprintf(Fo, "			PAR_OUT => %s_PAR_OUT,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Enable/disable input buffering and flow control\n");
	fprintf(Fo, "			INBUF => %s_INBUF,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN\n", vhdl_prefixu);
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_clear,\n", vhdl_prefixl);
	}
	else {
		fprintf(Fo, "			clear          => inst_%s_clear,\n", vhdl_prefixl);
	}
	fprintf(Fo, "			-- Data input\n");
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", vhdl_prefixl);
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", vhdl_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", vhdl_prefixl);
	fprintf(Fo, "			-- Data output\n");
	fprintf(Fo, "			-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", vhdl_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", vhdl_prefixl);
	fprintf(Fo, "			-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", vhdl_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the next layer is always FIFO
	connObj.init_prev_next_layers(this);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear <= '1' when reset_reg = RSTVAL_GEN else '0';\n", vhdl_prefixl);
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(this, Fo, true);

}
void LayerCustom::genvhdl_comp_inst(FILE* Fo) {

	fprintf(Fo, "	i_%s : %s\n", vhdl_prefixl, custom_entity);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			WDATA => %s_WDATA,\n", vhdl_prefixu);
	fprintf(Fo, "			SDATA => %s_SDATA,\n", vhdl_prefixu);
	fprintf(Fo, "			WOUT  => %s_WOUT,\n", vhdl_prefixu);
	fprintf(Fo, "			PAR   => %s_PAR,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Identifier of layer (optional)\n");
	fprintf(Fo, "			LAYER_ID => %s_LAYER_ID,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Identifier of user function (optional)\n");
	fprintf(Fo, "			USER_ID => %s_USER_ID,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Enable/disable input buffering and flow control\n");
	fprintf(Fo, "			INBUF => %s_INBUF,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
	fprintf(Fo, "			LOCKED => CONFIG_RDONLY\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_clear,\n", vhdl_prefixl);
	}
	else {
		fprintf(Fo, "			clear          => inst_%s_clear,\n", vhdl_prefixl);
	}
	fprintf(Fo, "			-- Data input\n");
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", vhdl_prefixl);
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", vhdl_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", vhdl_prefixl);
	fprintf(Fo, "			-- Data output\n");
	fprintf(Fo, "			-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", vhdl_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", vhdl_prefixl);
	fprintf(Fo, "			-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", vhdl_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the next layer is always FIFO
	connObj.init_prev_next_layers(this);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear <= '1' when reset_reg = RSTVAL_GEN else '0';\n", vhdl_prefixl);
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(this, Fo, true);

}

void LayerFork::genvhdl_comp_inst(FILE* Fo) {

	// Note: the prev and next layers are always FIFOs
	Layer* layprev = this->prev;
	if(layprev == NULL || layprev->type != LAYER_FIFO) abort();
	const char* layprev_prefixl = layprev->vhdl_prefixl;

		// Pre-get the next FIFO names
	char const * nextfifos_prefixl[arr_layers.size()];
	for(unsigned i=0; i<arr_layers.size(); i++) {
		Layer* laynext = arr_layers[i];
		if(laynext == NULL || laynext->type != LAYER_FIFO) abort();
		nextfifos_prefixl[i] = laynext->vhdl_prefixl;
	}

	// Assignments to previous FIFO
	fprintf(Fo, "	-- Assignments to previous FIFO\n");
	fprintf(Fo, "	inst_%s_out_ack <=\n", layprev_prefixl);
	for(unsigned i=0; i<arr_layers.size(); i++) {
		if(i > 0) fprintf(Fo, " and\n");
		fprintf(Fo, "		inst_%s_in_rdy", nextfifos_prefixl[i]);
	}
	fprintf(Fo, ";\n");

	// Assignments to next FIFOs
	for(unsigned i=0; i<arr_layers.size(); i++) {
		fprintf(Fo, "\n");
		fprintf(Fo, "	-- Assignments to next FIFO index %u\n", i);
		fprintf(Fo, "	inst_%s_in_data <= inst_%s_out_data;\n", nextfifos_prefixl[i], layprev_prefixl);
		fprintf(Fo, "	inst_%s_in_ack  <=\n		inst_%s_out_rdy", nextfifos_prefixl[i], layprev_prefixl);
		for(unsigned i2=0; i2<arr_layers.size(); i2++) {
			if(i2 == i) continue;
			fprintf(Fo, " and\n		inst_%s_in_rdy", nextfifos_prefixl[i2]);
		}
		fprintf(Fo, ";\n");
	}

}
void LayerCat::genvhdl_comp_inst(FILE* Fo) {

	// Note: the prev and next layers are always FIFOs
	Layer* laynext = this->next;
	if(laynext == NULL || laynext->type != LAYER_FIFO) abort();
	const char* laynext_prefixl = laynext->vhdl_prefixl;

		// Pre-get the next FIFO names
	char const * prevfifos_prefixl[arr_layers.size()];
	for(unsigned i=0; i<arr_layers.size(); i++) {
		Layer* layprev = arr_layers[i];
		if(layprev == NULL || layprev->type != LAYER_FIFO) abort();
		prevfifos_prefixl[i] = layprev->vhdl_prefixl;
	}

	// Assignments to prev FIFOs
	for(unsigned i=0; i<arr_layers.size(); i++) {
		if(i > 0) fprintf(Fo, "\n");
		fprintf(Fo, "	-- Assignments to prev FIFO index %u\n", i);
		fprintf(Fo, "	inst_%s_out_ack <=\n		inst_%s_in_rdy", prevfifos_prefixl[i], laynext_prefixl);
		for(unsigned i2=0; i2<arr_layers.size(); i2++) {
			if(i2 == i) continue;
			fprintf(Fo, " and\n		inst_%s_out_rdy", prevfifos_prefixl[i2]);
		}
		fprintf(Fo, ";\n");
	}

	// Assignments to next FIFO
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Assignments to next FIFO\n");
	fprintf(Fo, "	inst_%s_in_data <=\n", laynext_prefixl);
	for(unsigned i=0; i<arr_layers.size(); i++) {
		if(i > 0) fprintf(Fo, " &\n");
		if(arr_layers[i]->out_wdata != out_wdata) {
			char const * castfunc = (arr_layers[i]->out_sdata == true) ? "signed" : "unsigned";
			fprintf(Fo, "		std_logic_vector(resize(%s(inst_%s_out_data), %u))", castfunc, prevfifos_prefixl[i], out_wdata);
		}
		else {
			fprintf(Fo, "		inst_%s_out_data", prevfifos_prefixl[i]);
		}
	}
	fprintf(Fo, ";\n");
	fprintf(Fo, "\n");
	fprintf(Fo, "	inst_%s_in_ack <=\n", laynext_prefixl);
	for(unsigned i=0; i<arr_layers.size(); i++) {
		if(i > 0) fprintf(Fo, " and\n");
		fprintf(Fo, "		inst_%s_out_rdy", prevfifos_prefixl[i]);
	}
	fprintf(Fo, ";\n");

}
void LayerScatter::genvhdl_comp_inst(FILE* Fo) {

	printf("Warning: Layer %s%u is missing VHDL generation implementation\n", typenamel, typeidx);
	fprintf(Fo, "-- FIXME Missing implemention\n");

}
void LayerGather::genvhdl_comp_inst(FILE* Fo) {

	printf("Warning: Layer %s%u is missing VHDL generation implementation\n", typenamel, typeidx);
	fprintf(Fo, "-- FIXME Missing implemention\n");

}

void LayerFlatten::genvhdl_comp_inst(FILE* Fo) {

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	connObj.init_prev_next_layers(this);

	if(connObj.layprev_out_ack != nullptr) {
		fprintf(Fo, "	-- Assignment for previous layer\n");
		fprintf(Fo, "	inst_%s_%s <= inst_%s_out_ack;\n", connObj.layprev->vhdl_prefixl, connObj.layprev_out_ack, this->vhdl_prefixl);
	}
	fprintf(Fo, "	-- Assignment of local layer signals\n");
	fprintf(Fo, "	inst_%s_out_data <= inst_%s_%s;\n", this->vhdl_prefixl, connObj.layprev->vhdl_prefixl, connObj.layprev_out_data);
	fprintf(Fo, "	inst_%s_out_rdy  <= inst_%s_%s;\n", this->vhdl_prefixl, connObj.layprev->vhdl_prefixl, connObj.layprev_out_rdy);
	if(connObj.laynext_in_rdy != nullptr) {
		fprintf(Fo, "	inst_%s_out_ack  <= inst_%s_%s;\n", this->vhdl_prefixl, connObj.laynext->vhdl_prefixl, connObj.laynext_in_rdy);
	}
	else {
		fprintf(Fo, "	-- Note : Next layer has no input-size RDY port\n");
		fprintf(Fo, "	inst_%s_out_ack  <= '1';\n", this->vhdl_prefixl);
	}

}
void LayerSoftMax::genvhdl_comp_inst(FILE* Fo) {

	fprintf(Fo, "	i_%s : nnlayer_softmax\n", vhdl_prefixl);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			WDATA => %s_WDATA,\n", vhdl_prefixu);
	fprintf(Fo, "			SDATA => %s_SDATA,\n", vhdl_prefixu);
	fprintf(Fo, "			WOUT  => %s_WOUT,\n", vhdl_prefixu);
	fprintf(Fo, "			FSIZE => %s_FSIZE,\n", vhdl_prefixu);
	fprintf(Fo, "			PAR_IN => %s_PAR_IN,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Take extra margin on the FIFO level, in case there is something outside\n");
	fprintf(Fo, "			FIFOMARGIN => %s_FIFOMARGIN,\n", vhdl_prefixu);
	fprintf(Fo, "			-- Lock the layer parameters to the generic parameter value\n");
	fprintf(Fo, "			LOCKED => CONFIG_RDONLY\n");
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk            => CLK,\n");
	if(network->hwconfig_asicmode==true) {
		fprintf(Fo, "			reset          => inst_%s_clear,\n", vhdl_prefixl);
	}
	else {
		fprintf(Fo, "			clear          => inst_%s_clear,\n", vhdl_prefixl);
	}
	fprintf(Fo, "			-- The user-specified frame size\n");
	fprintf(Fo, "			user_fsize     => inst_%s_user_fsize(%s_WOUT-1 downto 0),\n", vhdl_prefixl, vhdl_prefixu);
	fprintf(Fo, "			-- Data input\n");
	fprintf(Fo, "			data_in        => inst_%s_data_in,\n", vhdl_prefixl);
	fprintf(Fo, "			data_in_valid  => inst_%s_data_in_valid,\n", vhdl_prefixl);
	fprintf(Fo, "			data_in_ready  => inst_%s_data_in_ready,\n", vhdl_prefixl);
	fprintf(Fo, "			-- Data output\n");
	fprintf(Fo, "			-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "			data_out       => inst_%s_data_out,\n", vhdl_prefixl);
	fprintf(Fo, "			data_out_valid => inst_%s_data_out_valid,\n", vhdl_prefixl);
	fprintf(Fo, "			-- The output data enters a FIFO. This indicates the available room.\n");
	fprintf(Fo, "			out_fifo_room  => inst_%s_out_fifo_room\n", vhdl_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	// Object to help generating connections
	GenVhdl_ConnHelper connObj;

	// Note: the next layer is always FIFO
	connObj.init_prev_next_layers(this);

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear <= '1' when reset_reg = RSTVAL_GEN else '0';\n", vhdl_prefixl);
	fprintf(Fo, "\n");
	fprintf(Fo, "	-- Get config registers\n");
	fprintf(Fo, "	inst_%s_user_fsize <= ", vhdl_prefixl); print_cfg_reg(Fo, regs_idx, regfield_fsize); fprintf(Fo, ";\n");
	fprintf(Fo, "\n");

	// Generate connection from previous FIFO/layers, to next FIFO or layers, and to scatter-gather component
	connObj.gen_common_connect(this, Fo, true);

}
void LayerFifo::genvhdl_comp_inst(FILE* Fo) {

	// FIXME This is just to ease transition due to code refactoring
	Layer* layer = this;
	char* layer_prefixl = this->vhdl_prefixl;
	char* layer_prefixu = this->vhdl_prefixu;

	fprintf(Fo, "	i_%s : fifo_with_counters\n", layer_prefixl);
	fprintf(Fo, "		generic map (\n");
	fprintf(Fo, "			DATAW => %s_DATAW,\n", layer_prefixu);
	fprintf(Fo, "			DEPTH => %s_DEPTH,\n", layer_prefixu);
	fprintf(Fo, "			CNTW  => %s_CNTW\n", layer_prefixu);
	fprintf(Fo, "		)\n");
	fprintf(Fo, "		port map (\n");
	fprintf(Fo, "			clk           => CLK,\n");
	fprintf(Fo, "			reset         => inst_%s_clear,\n", layer_prefixl);
	fprintf(Fo, "			fifo_in_data  => inst_%s_in_data,\n", layer_prefixl);
	fprintf(Fo, "			fifo_in_rdy   => inst_%s_in_rdy,\n", layer_prefixl);
	fprintf(Fo, "			fifo_in_ack   => inst_%s_in_ack,\n", layer_prefixl);
	fprintf(Fo, "			fifo_in_cnt   => inst_%s_in_cnt,\n", layer_prefixl);
	fprintf(Fo, "			fifo_out_data => inst_%s_out_data,\n", layer_prefixl);
	fprintf(Fo, "			fifo_out_rdy  => inst_%s_out_rdy,\n", layer_prefixl);
	fprintf(Fo, "			fifo_out_ack  => inst_%s_out_ack,\n", layer_prefixl);
	fprintf(Fo, "			fifo_out_cnt  => inst_%s_out_cnt\n", layer_prefixl);
	fprintf(Fo, "		);\n");
	fprintf(Fo, "\n");

	fprintf(Fo, "	-- Set inputs\n");
	fprintf(Fo, "	inst_%s_clear <= '1' when reset_reg = RSTVAL_GEN else '0';\n", layer_prefixl);
	fprintf(Fo, "\n");

	fprintf(Fo, "	-- Connection to the mux to observe the state of FIFOs\n");
	fprintf(Fo, "	gen_fifomon_%s : if CONFIG_FIFOMON = true generate\n", layer_prefixl);
	fprintf(Fo, "		monitorfifo_in(%u*12 + 0) <= inst_%s_in_rdy;\n", layer->typeidx, layer_prefixl);
	fprintf(Fo, "		monitorfifo_in(%u*12 + 1) <= inst_%s_in_ack;\n", layer->typeidx, layer_prefixl);
	fprintf(Fo, "		monitorfifo_in(%u*12 + 2) <= inst_%s_out_rdy;\n", layer->typeidx, layer_prefixl);
	fprintf(Fo, "		monitorfifo_in(%u*12 + 3) <= inst_%s_out_ack;\n", layer->typeidx, layer_prefixl);
	fprintf(Fo, "		monitorfifo_in(%u*12 + 11 downto %u*12 + 4) <= inst_%s_out_cnt(7 downto 0);\n", layer->typeidx, layer->typeidx, layer_prefixl);
	fprintf(Fo, "	end generate;\n");

	if(this == network->layer_first) {
		fprintf(Fo, "\n");
		fprintf(Fo, "	-- Alias signals for first FIFO\n");
		fprintf(Fo, "	inst_%s_in_data     <= inst_firstfifo_in_data;\n", layer_prefixl);
		fprintf(Fo, "	inst_firstfifo_in_rdy <= inst_%s_in_rdy;\n", layer_prefixl);
		fprintf(Fo, "	inst_%s_in_ack      <= inst_firstfifo_in_ack;\n", layer_prefixl);
		fprintf(Fo, "	inst_firstfifo_in_cnt <= inst_%s_in_cnt;\n", layer_prefixl);
	}
	if(this == network->layer_last) {
		fprintf(Fo, "\n");
		fprintf(Fo, "	-- Alias signals for last FIFO\n");
		fprintf(Fo, "	inst_lastfifo_out_data <= inst_%s_out_data;\n", layer_prefixl);
		fprintf(Fo, "	inst_lastfifo_out_rdy  <= inst_%s_out_rdy;\n", layer_prefixl);
		fprintf(Fo, "	inst_%s_out_ack      <= inst_lastfifo_out_ack;\n", layer_prefixl);
		fprintf(Fo, "	inst_lastfifo_out_cnt  <= inst_%s_out_cnt;\n", layer_prefixl);
	}

}

void Network::genvhdl_comp_inst(FILE* Fo) {

	for(auto layer : layers) {

		// Print a clear comment
		if(layer != layers.front()) fprintf(Fo, "\n");
		fprintf(Fo, "	----------------------------------\n");
		fprintf(Fo, "	-- Instantiation of %s\n", layer->vhdl_prefixu);
		fprintf(Fo, "	----------------------------------\n");
		fprintf(Fo, "\n");

		// Actual per-layer instantiation code
		layer->genvhdl_comp_inst(Fo);

	}  // Scan all layers

}

//============================================
// Reusable object to fill sections in a file
//============================================

#include <unordered_map>

class TemplateFileFiller {

	private :

	// Dedicated hash function for C-style strings, stop at newline character
	// FIXME Use a better hashing function ? Fur current purpose it is not a problem
	class PatternHash {
		public :
		std::size_t operator()(char const * str) const noexcept {
			std::size_t h = 0;
			for( ; (*str) != 0 && (*str) != '\n'; str++) {
				h = 31*h + (*str);
			}
			return h;
		}
	};

	// Dedicated equal function for C-style strings, stop at newline character
	class PatternEqual {
		public :
		std::size_t operator()(char const * str1, char const * str2) const noexcept {
			do {
				if((*str1) == 0 || (*str2) == 0 || (*str1) == '\n' || (*str2) == '\n') return true;
				if((*str1) != (*str2)) return false;
				str1++;;
				str2++;;
			} while(1);
			return false;
		}
	};

	public :

	class FillerWorker {

		public :

		char const * line_begin = nullptr;
		char const * line_end   = nullptr;

		virtual ~FillerWorker(void);
		virtual void fill(TemplateFileFiller& filler);

	};

	// The first non-space characters that are beginning for all line patterns
	char const * fast_pattern_begin;
	// To clear the sections instead of filling them
	bool want_clear = false;

	char const * filename_in  = nullptr;
	char const * filename_out = nullptr;

	// Input template file
	FILE* Fi = nullptr;
	// Temp file to hold the input template file
	FILE* Ft = nullptr;
	// Output file
	FILE* Fo = nullptr;

	private :

	// The set of patterns
	// Note : Dedicated hash and comparison functions are provided to ensure the whole C string line is matched (but not newlines), not just the string pointer
	unordered_map<char const *, FillerWorker*, PatternHash, PatternEqual> patterns;

	// A buffer to read line by line
	size_t linebuf_size = 0;
	char*  linebuf = nullptr;

	public :

	// Constructor / Destructor
	~TemplateFileFiller(void);

	// Methods
	void open(void);
	void register_worker(FillerWorker* worker);
	void process(void);

};

TemplateFileFiller::~TemplateFileFiller(void) {

	if(Fi != nullptr) fclose(Fi);
	if(Ft != nullptr) fclose(Ft);
	if(Fo != nullptr) fclose(Fo);

	for(auto iter : patterns) delete iter.second;
	patterns.clear();

	if(linebuf != nullptr) free(linebuf);

}

void TemplateFileFiller::open(void) {

	Fi = fopen(filename_in, "rb");
	if(Fi==NULL) {
		printf("Error: Can't open file '%s' for reading\n", filename_in);
		exit(EXIT_FAILURE);
	}

	// Create temp file
	Ft = tmpfile();

	// A buffer to read line by line
	linebuf_size = 2048;
	linebuf = (char*)malloc(linebuf_size);

	// Copy the entire file in a temp file
	// That way we can edit a template file in-place
	do {
		ssize_t r = getline(&linebuf, &linebuf_size, Fi);
		if(r < 0) break;
		fprintf(Ft, "%s", linebuf);
	} while(1);

	rewind(Ft);
	fclose(Fi);
	Fi = nullptr;

	// Open the output file for writing
	Fo = fopen(filename_out, "wb");
	if(Fo==NULL) {
		printf("Error: Can't open file '%s' for writing\n", filename_out);
		exit(EXIT_FAILURE);
	}

}

void TemplateFileFiller::register_worker(TemplateFileFiller::FillerWorker* worker) {
	auto insert_res = patterns.insert(make_pair(worker->line_begin, worker));
	if(insert_res.second == false) {
		printf("Internal Error: TemplateFileFiller : Worker already registered with pattern '%s'\n", worker->line_begin);
		exit(EXIT_FAILURE);
	}
}

void TemplateFileFiller::process(void) {

	unsigned fast_pattern_len = 0;
	if(fast_pattern_begin != nullptr) fast_pattern_len = strlen(fast_pattern_begin);

	auto iter = patterns.end();

	// Scan the input file and insert the VHDL
	do {
		ssize_t r = getline(&linebuf, &linebuf_size, Ft);
		if(r < 0) break;

		// Get the first non-space character
		char* beg = linebuf;
		while((*beg)!=0 && isspace(*beg)!=0) beg++;

		// If the end of the line is not reached, check for registered patterns
		if((*beg) != 0) {
			// Fast check before actually searching in patterns
			if(fast_pattern_begin == nullptr || strncmp(beg, fast_pattern_begin, fast_pattern_len) == 0) {
				// Fast check before actually searching in patterns
				iter = patterns.find(beg);
			}
		}

		// Insert the desired contents in the file between patterns
		if(iter != patterns.end()) {
			FillerWorker* worker = iter->second;

			// Write the marker line
			fprintf(Fo, "%s", linebuf);
			if(want_clear==false) {
				worker->fill(*this);
			}

			// Find the corresponding end token, drop the temp file contents
			vhdl_gen_endline(Ft, &linebuf, &linebuf_size, worker->line_end);
			// Write the end line
			fprintf(Fo, "\n");
			fprintf(Fo, "%s", linebuf);

			// Clear the worker iterator for next line
			iter = patterns.end();
		}
		// Or, just copy the line as-is
		else {
			fprintf(Fo, "%s", linebuf);
		}

	} while(1);

}

// Default virtual worker methods

TemplateFileFiller::FillerWorker::~FillerWorker(void) {
	// Nothing to do, this default method is not supposed to be called
}

void TemplateFileFiller::FillerWorker::fill(TemplateFileFiller& filler) {
	// Nothing to do, this default method is not supposed to be called
}

//============================================
// Main VHDL generation function
//============================================

// Filler workers for VHDL generation of the network

// General config parameters such as number of config registers, etc
// FIXME Missing bits to indicate if sending data is locked to the first FIFO, and getting data is locked to the last FIFO
class FillerWorker_NNGen_Config : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_NNGen_Config(Network* n) {
		line_begin = "-- AUTOGEN CONFIG NB BEGIN";
		line_end   = "-- AUTOGEN CONFIG NB END";
		network = n;
	}

	void fill(TemplateFileFiller& filler) {
		FILE* Fo = filler.Fo;

		// Add one register at the beginning, to ease routing: this one will be read in slave registers
		unsigned total_regs_nb = 1;
		// Count the config registers
		for(auto layer : network->layers) {
			if(layer->type == LAYER_FIFO) continue;  // FIFOs are handled in other per-layer methods
			total_regs_nb += 1;  // The register between layers, or beginning of series of layers
			layer->regs_idx = total_regs_nb;
			total_regs_nb += layer->regs_nb;  // Layer-specific registers
			if(has_layer_after(layer) == false) {
				// The register at end of series of layers
				total_regs_nb += 1;
			}
		}

		printf("Total number of config registers: %u\n", total_regs_nb);
		if(HwAcc_Common::memreg_regs_nb->CheckCapacity(total_regs_nb) != 0) {
			printf("WARNING : The number of config registers does not fit in %u bits, the accelerator will only be usable in blind mode\n", HwAcc_Common::memreg_regs_nb->bits);
		}

		for(auto layer : network->layers) {
			// Skip FIFOs
			if(layer->type == LAYER_FIFO) continue;
			// Check and print warning
			if(
				HwAcc_Common::chanfield_par->CheckCapacity(layer->split_in) ||
				HwAcc_Common::chanfield_par->CheckCapacity(layer->split_out)
			) {
				unsigned num = warn_field_get_inc(HwAcc_Common::chanfield_par);
				if(num <= warn_max_number) {
					printf("WARNING %s%u: Values PAR=%u/%u exceed capacity of %u bits of fields '%s' in configuration registers, the accelerator will only be usable in blind mode\n",
						layer->typenameu, layer->typeidx,
						layer->split_in, layer->split_out,
						HwAcc_Common::chanfield_par->bits, HwAcc_Common::chanfield_par->name
					);
				}
				if(num == warn_max_number) {
					warn_print_max();
				}
			}
		}

		// Count the total number of FIFOs
		// FIXME The total number of layers should not be used as value for CONFIG_SELOUT_NB
		unsigned total_fifos_nb = 0;
		unsigned total_layers_nb = 0;
		for(auto layer : network->layers) {
			if(layer->type==LAYER_FIFO) total_fifos_nb++;
			else total_layers_nb++;
		}

		// Actually generate content
		fprintf(Fo, "\n");
		fprintf(Fo, "	-- Accelerator version\n");
		fprintf(Fo, "	constant VERSION_MAJ : natural := %u;\n", GENVHDL_VERSION_MAJ);
		fprintf(Fo, "	constant VERSION_MIN : natural := %u;\n", GENVHDL_VERSION_MIN);
		fprintf(Fo, "	-- Interface width\n");
		fprintf(Fo, "	constant CONFIG_IFW : natural := %u;\n", network->hwconfig_writewidth);
		fprintf(Fo, "	-- Values to set dimension to signals\n");
		fprintf(Fo, "	constant CONFIG_CHAIN_NB : natural := %u;\n", total_regs_nb);
		fprintf(Fo, "	constant CONFIG_FIFOS_NB : natural := %u;\n", total_fifos_nb);
		fprintf(Fo, "	-- For locking the pipeline parameters at synthesis time\n");
		fprintf(Fo, "	constant CONFIG_NOREGS : boolean := %s;\n", network->param_noregs==true ? "true" : "false");
		fprintf(Fo, "	-- For locking the pipeline parameters at synthesis time\n");
		fprintf(Fo, "	constant CONFIG_RDONLY : boolean := %s;\n", network->param_rdonly==true ? "true" : "false");
		fprintf(Fo, "	-- For monitoring FIFOs\n");
		fprintf(Fo, "	constant CONFIG_FIFOMON : boolean := %s;\n", network->param_fifomon==true ? "true" : "false");
		fprintf(Fo, "	-- For selection of the output layer\n");
		fprintf(Fo, "	constant CONFIG_SELOUT    : boolean := %s;\n", network->param_selout==true ? "true" : "false");
		fprintf(Fo, "	constant CONFIG_SELOUT_NB : natural := %u;\n", total_layers_nb);
		fprintf(Fo, "	-- For simulation purposes: number of input values (images * size of an image)\n");
		fprintf(Fo, "	constant SIMU_DATAIN_NB : natural := %u * %u;\n", param_fn, network->layers.front()->fsize);
		fprintf(Fo, "	constant SIMU_DEBUG : boolean := false;\n");
	}

};

// Declarations of constants (generics for components instantiations)
class FillerWorker_NNGen_CstDecl : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_NNGen_CstDecl(Network* n) {
		line_begin = "-- AUTOGEN CST DECL BEGIN";
		line_end   = "-- AUTOGEN CST DECL END";
		network = n;
	}

	void fill(TemplateFileFiller& filler) {
		FILE* Fo = filler.Fo;
		fprintf(Fo, "\n");
		network->genvhdl_cst_decl(Fo);
	}

};

// Declarations of constants weights when fully parallelized (one gigantic vector)
class FillerWorker_NNGen_CstWeightsVec : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_NNGen_CstWeightsVec(Network* n) {
		line_begin = "-- AUTOGEN CONST WEIGHTS VEC BEGIN";
		line_end   = "-- AUTOGEN CONST WEIGHTS VEC END";
		network = n;
	}

	void fill(TemplateFileFiller& filler) {
		FILE* Fo = filler.Fo;

		for(auto layer : network->layers) {
			if(layer->type != LAYER_NEU) continue;
			fprintf(Fo, "\n");

			if(layer->const_params == true && (layer->fsize / layer->split_in) * layer->neu_time_mux == 1) {
				fprintf(Fo, "	constant %s_CSTWEIGHTS_NB : natural := %u;\n", layer->vhdl_prefixu, layer->fsize * layer->neurons);
				fprintf(Fo, "	constant %s_CSTWEIGHTS_VEC : std_logic_vector(%s_CSTWEIGHTS_NB*%s_WWEIGHT-1 downto 0) := (\n", layer->vhdl_prefixu, layer->vhdl_prefixu, layer->vhdl_prefixu);
				layer->genvhdl_const_params_vec(Fo, "		");
				fprintf(Fo, "		);\n");
			}
			else {
				fprintf(Fo, "	constant %s_CSTWEIGHTS_NB : natural := 1;\n", layer->vhdl_prefixu);
				fprintf(Fo, "	constant %s_CSTWEIGHTS_VEC : std_logic_vector(%s_CSTWEIGHTS_NB*%s_WWEIGHT-1 downto 0) := (others => '0');\n", layer->vhdl_prefixu, layer->vhdl_prefixu, layer->vhdl_prefixu);
			}

		}
	}

};

// Declarations of components used in layers
class FillerWorker_NNGen_CompDecl : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_NNGen_CompDecl(Network* n) {
		line_begin = "-- AUTOGEN COMP DECL BEGIN";
		line_end   = "-- AUTOGEN COMP DECL END";
		network = n;
	}

	void fill(TemplateFileFiller& filler) {
		FILE* Fo = filler.Fo;
		fprintf(Fo, "\n");
		network->genvhdl_comp_decl(Fo);
	}

};

// Declarations of signals for instantiation of components
class FillerWorker_NNGen_SigDecl : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_NNGen_SigDecl(Network* n) {
		line_begin = "-- AUTOGEN SIG DECL BEGIN";
		line_end   = "-- AUTOGEN SIG DECL END";
		network = n;
	}

	void fill(TemplateFileFiller& filler) {
		FILE* Fo = filler.Fo;
		fprintf(Fo, "\n");
		network->genvhdl_sig_decl(Fo);
	}

};

// The constant values in config registers
class FillerWorker_NNGen_RegsSetConst : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_NNGen_RegsSetConst(Network* n) {
		line_begin = "-- AUTOGEN REGS SETCONST BEGIN";
		line_end   = "-- AUTOGEN REGS SETCONST END";
		network = n;
	}

	void fill(TemplateFileFiller& filler) {
		FILE* Fo = filler.Fo;
		fprintf(Fo, "\n");
		network->genvhdl_regs_setconst(Fo);
	}

};

// The constant values in config registers, locked mode only
class FillerWorker_NNGen_RegsSetConstLocked : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_NNGen_RegsSetConstLocked(Network* n) {
		line_begin = "-- AUTOGEN REGS SETCONST LOCKED BEGIN";
		line_end   = "-- AUTOGEN REGS SETCONST LOCKED END";
		network = n;
	}

	void fill(TemplateFileFiller& filler) {
		FILE* Fo = filler.Fo;
		fprintf(Fo, "\n");
		network->genvhdl_regs_setconst_locked(Fo);
	}

};

// Instantiation of components
class FillerWorker_NNGen_CompInst : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_NNGen_CompInst(Network* n) {
		line_begin = "-- AUTOGEN COMP INST BEGIN";
		line_end   = "-- AUTOGEN COMP INST END";
		network = n;
	}

	void fill(TemplateFileFiller& filler) {
		FILE* Fo = filler.Fo;
		fprintf(Fo, "\n");
		network->genvhdl_comp_inst(Fo);
	}

};

// Main VHDL generation function

int vhdl_gen(Network* network, const char* filename_in, const char* filename_out, const char* want_prefix, bool gen_clear) {
	auto& layers = network->layers;

	// Just to ensure that FIFO are inserted correctly
	network->insert_fifos();

	// Note : Better to call this on the entire network than on per-layer basis
	// To avoid network stats being counted twice
	network->hwconfig_finalize();

	// Set the number of configuration registers
	network->genvhdl_set_config_regs_numbers();

	// Initialize the prefix to generate all VHDL identifiers
	// This eases manually copying/pasting of networks or parts of networks
	if(want_prefix == NULL) want_prefix = "";
	for(auto layer : layers) {
		// Buffer to generate the names
		char namebuf[128];
		unsigned len = 0;
		// Generate the lowercase prefix
		sprintf(namebuf, "%s%s%u", want_prefix, layer->typenamel, layer->typeidx);
		len = strlen(namebuf);
		for(unsigned c=0; c<len; c++) namebuf[c] = tolower(namebuf[c]);
		layer->vhdl_prefixl = strdup(namebuf);
		// Generate the uppercase prefix
		sprintf(namebuf, "%s%s%u", want_prefix, layer->typenameu, layer->typeidx);
		len = strlen(namebuf);
		for(unsigned c=0; c<len; c++) namebuf[c] = toupper(namebuf[c]);
		layer->vhdl_prefixu = strdup(namebuf);
	}

	// Reset the warning counts
	map_warn_field_exceed.clear();

	// Create and initialize the template file filler object

	TemplateFileFiller filler;
	filler.filename_in  = filename_in;
	filler.filename_out = filename_out;

	// General behaviour for VHDL file content
	filler.fast_pattern_begin = "--";
	filler.want_clear = gen_clear;

	// Initialize
	filler.open();

	// Register filler workers
	filler.register_worker(new FillerWorker_NNGen_Config(network));
	filler.register_worker(new FillerWorker_NNGen_CstDecl(network));
	filler.register_worker(new FillerWorker_NNGen_CstWeightsVec(network));
	filler.register_worker(new FillerWorker_NNGen_CompDecl(network));
	filler.register_worker(new FillerWorker_NNGen_SigDecl(network));
	filler.register_worker(new FillerWorker_NNGen_RegsSetConst(network));
	filler.register_worker(new FillerWorker_NNGen_RegsSetConstLocked(network));
	filler.register_worker(new FillerWorker_NNGen_CompInst(network));

	// Scan the input file and insert the VHDL
	filler.process();

	return 0;
}

//============================================
// Generation of constant parameters
//============================================

// The vector generation methods :
// These functions generate a fully-unrolled vector initialization, this is a gigantic vector
// Assume : layer has const parameters indeed
// Assume : indent string is non-empty

// This memory generation methods :
// Assume : layer has const weights indeed
// Assume : indent string is non-empty

void Layer::genvhdl_const_params_vec(FILE* Fo, const char* indent) {
	printf("INTERNAL ERROR %s : Missing implementation for method genvhdl_const_params_vec()\n", vhdl_prefixu);
}

void Layer::genvhdl_const_params_mem(FILE* Fo, const char* indent) {
	printf("INTERNAL ERROR %s : Missing implementation for method genvhdl_const_params_mem()\n", vhdl_prefixu);
}

void LayerNeu::genvhdl_const_params_vec(FILE* Fo, const char* indent) {
	layer_t* layer = this;

	bool bin_sym = (layer->neu_wweight == 1) && ((layer->neu_sgnw & NEUSGN_SIGNED) != 0);
	int msb_mask = 1 << (layer->neu_wweight - 1);

	unsigned cat_num = 0;
	unsigned neu_per_po = layer->neurons / layer->split_out;
	for(int po = layer->split_out-1; po >= 0; po--) {
		for(int no = neu_per_po-1; no >= 0; no--) {

			unsigned n = no * layer->split_out + po;
			int* cfg_data_neu = layer->cfg_data[n];

			for(int f = layer->fsize-1; f >= 0; f--) {
				if(cat_num > 0) fprintf(Fo, " &\n");
				if(f == (int)layer->fsize-1) fprintf(Fo, "%s-- Neuron %u\n", indent, n);
				fprintf(Fo, "%s\"", indent);
				// scan bits one by one starting from MSB
				int val = cfg_data_neu[f];
				if(bin_sym == true) val = (val == -1);  // Stored 0 means +1, stored 1 means -1
				for(unsigned i=0; i<layer->neu_wweight; i++) {
					fprintf(Fo, "%c", ((val & msb_mask) != 0) ? '1' : '0');
					val <<= 1;
				}
				fprintf(Fo, "\"");
				cat_num ++;
			}

		}  // neurons
	}  // PAR_OUT

	if(cat_num > 0) fprintf(Fo, "\n");
}

void LayerNeu::genvhdl_const_params_mem(FILE* Fo, const char* indent) {
	layer_t* layer = this;

	bool bin_sym = (layer->neu_wweight == 1) && ((layer->neu_sgnw & NEUSGN_SIGNED) != 0);
	int msb_mask = 1 << (layer->neu_wweight - 1);

	unsigned cat_num = 0;
	unsigned neu_phy = (layer->neurons + layer->neu_time_mux - 1) / layer->neu_time_mux;

	for(unsigned t=0; t<layer->neu_time_mux; t++) {
		if(cat_num > 0) { fprintf(Fo, ",\n"); cat_num = 0; }
		if(layer->neu_time_mux > 1) {
			fprintf(Fo, "%s-- Time multiplexing : ", indent);
			if(neu_phy > 1) fprintf(Fo, "neurons %u to %u\n", t*neu_phy, (t+1)*neu_phy-1);
			else fprintf(Fo, "neuron %u\n", t);
		}

		for(unsigned fi=0; fi < layer->fsize; fi += layer->split_in) {
			if(cat_num > 0) { fprintf(Fo, ",\n"); cat_num = 0; }
			if(layer->fsize / layer->split_in > 1 && neu_phy * layer->split_in > 1) {
				fprintf(Fo, "%s-- Frame position %u\n", indent, fi);
			}

			// The memory of weights feeds all physical neurons
			// Order of neurons depends on PAR_OUT : first all neurons 0 mod PAR_OUT, then 1 mod PAR_OUT, etc

			// Here, generate NEU_PHY * PAR_IN concatenated values

			unsigned neu_per_po = neu_phy / layer->split_out;

			for(int po = layer->split_out-1; po >= 0; po--) {
				for(int no = neu_per_po-1; no >= 0; no--) {

					unsigned n = t * neu_phy + no * layer->split_out + po;
					int* cfg_data_neu = layer->cfg_data[n];

					for(int pi = layer->split_in-1; pi >= 0; pi--) {
						unsigned f = fi + pi;

						// Print necessary VHDL keywords and newline
						if(cat_num > 0) fprintf(Fo, " &\n");

						// Only after newline we can print comments
						if(pi == (int)layer->split_in-1) {
							if(layer->split_out > 1 && no == (int)neu_per_po-1) {
								fprintf(Fo, "%s-- Neurons %u mod PAR_OUT=%u\n", indent, po, layer->split_out);
							}
							if(layer->split_in > 1) {
								fprintf(Fo, "%s-- Neuron %u\n", indent, n);
							}
						}

						// Print the number
						fprintf(Fo, "			\"");
						int val = cfg_data_neu[f];
						if(bin_sym == true) val = (val == -1);  // Stored 0 means +1, stored 1 means -1
						for(unsigned i=0; i<layer->neu_wweight; i++) {
							fprintf(Fo, "%c", ((val & msb_mask) != 0) ? '1' : '0');
							val <<= 1;
						}
						fprintf(Fo, "\"");

						// Increment the number of concatenated values up to now
						cat_num++;

					}  // par_in
				}  // neuron
			}  // par_out

			// Note : End of a line in memory

		}  // fsize
	}  // tmux

	if(cat_num > 0) fprintf(Fo, "\n");
}

void LayerNeu_CM::genvhdl_const_params_vec(FILE* Fo, const char* indent) {
	layer_t* layer = this;

	bool bin_sym = (layer->neu_wweight == 1) && ((layer->neu_sgnw & NEUSGN_SIGNED) != 0);
	int msb_mask = 1 << (layer->neu_wweight - 1);

	unsigned cat_num = 0;
	unsigned neu_per_po = layer->neurons / layer->split_out;
	for(int po = layer->split_out-1; po >= 0; po--) {
		for(int no = neu_per_po-1; no >= 0; no--) {

			unsigned n = no * layer->split_out + po;
			int* cfg_data_neu = layer->cfg_data[n];

			for(int f = layer->fsize-1; f >= 0; f--) {
				if(cat_num > 0) fprintf(Fo, " &\n");
				if(f == (int)layer->fsize-1) fprintf(Fo, "%s-- Neuron %u\n", indent, n);
				fprintf(Fo, "%s\"", indent);
				// scan bits one by one starting from MSB
				int val = cfg_data_neu[f];
				if(bin_sym == true) val = (val == -1);  // Stored 0 means +1, stored 1 means -1
				for(unsigned i=0; i<layer->neu_wweight; i++) {
					fprintf(Fo, "%c", ((val & msb_mask) != 0) ? '1' : '0');
					val <<= 1;
				}
				fprintf(Fo, "\"");
				cat_num ++;
			}

		}  // neurons
	}  // PAR_OUT

	if(cat_num > 0) fprintf(Fo, "\n");
}

void LayerNeu_CM::genvhdl_const_params_mem(FILE* Fo, const char* indent) {
	layer_t* layer = this;

	bool bin_sym = (layer->neu_wweight == 1) && ((layer->neu_sgnw & NEUSGN_SIGNED) != 0);
	int msb_mask = 1 << (layer->neu_wweight - 1);

	unsigned cat_num = 0;
	unsigned neu_phy = (layer->neurons + layer->neu_time_mux - 1) / layer->neu_time_mux;

	for(unsigned t=0; t<layer->neu_time_mux; t++) {
		if(cat_num > 0) { fprintf(Fo, ",\n"); cat_num = 0; }
		if(layer->neu_time_mux > 1) {
			fprintf(Fo, "%s-- Time multiplexing : ", indent);
			if(neu_phy > 1) fprintf(Fo, "neurons %u to %u\n", t*neu_phy, (t+1)*neu_phy-1);
			else fprintf(Fo, "neuron %u\n", t);
		}

		for(unsigned fi=0; fi < layer->fsize; fi += layer->split_in) {
			if(cat_num > 0) { fprintf(Fo, ",\n"); cat_num = 0; }
			if(layer->fsize / layer->split_in > 1 && neu_phy * layer->split_in > 1) {
				fprintf(Fo, "%s-- Frame position %u\n", indent, fi);
			}

			// The memory of weights feeds all physical neurons
			// Order of neurons depends on PAR_OUT : first all neurons 0 mod PAR_OUT, then 1 mod PAR_OUT, etc

			// Here, generate NEU_PHY * PAR_IN concatenated values

			unsigned neu_per_po = neu_phy / layer->split_out;

			for(int po = layer->split_out-1; po >= 0; po--) {
				for(int no = neu_per_po-1; no >= 0; no--) {

					unsigned n = t * neu_phy + no * layer->split_out + po;
					int* cfg_data_neu = layer->cfg_data[n];

					for(int pi = layer->split_in-1; pi >= 0; pi--) {
						unsigned f = fi + pi;

						// Print necessary VHDL keywords and newline
						if(cat_num > 0) fprintf(Fo, " &\n");

						// Only after newline we can print comments
						if(pi == (int)layer->split_in-1) {
							if(layer->split_out > 1 && no == (int)neu_per_po-1) {
								fprintf(Fo, "%s-- Neurons %u mod PAR_OUT=%u\n", indent, po, layer->split_out);
							}
							if(layer->split_in > 1) {
								fprintf(Fo, "%s-- Neuron %u\n", indent, n);
							}
						}

						// Print the number
						fprintf(Fo, "			\"");
						int val = cfg_data_neu[f];
						if(bin_sym == true) val = (val == -1);  // Stored 0 means +1, stored 1 means -1
						for(unsigned i=0; i<layer->neu_wweight; i++) {
							fprintf(Fo, "%c", ((val & msb_mask) != 0) ? '1' : '0');
							val <<= 1;
						}
						fprintf(Fo, "\"");

						// Increment the number of concatenated values up to now
						cat_num++;

					}  // par_in
				}  // neuron
			}  // par_out

			// Note : End of a line in memory

		}  // fsize
	}  // tmux

	if(cat_num > 0) fprintf(Fo, "\n");
}

void LayerNorm::genvhdl_const_params_vec(FILE* Fo, const char* indent) {
	layer_t* layer = this;

	// Get the width of parameters
	unsigned norm_wbias = layer->norm_wbias;
	unsigned norm_wmul  = layer->norm_wmul;
	unsigned norm_wshr  = layer->norm_wshr;

	// Determine the position of bias and shr columns
	unsigned col_bias = 0;
	unsigned col_mul = 0;
	unsigned col_shr = 0;
	unsigned col_nb = 0;
	col_bias = col_nb; col_nb += (norm_wbias > 0) ? 1 : 0;
	col_mul  = col_nb; col_nb += (norm_wmul  > 0) ? 1 : 0;
	col_shr  = col_nb; col_nb += (norm_wshr  > 0) ? 1 : 0;

	unsigned cat_num = 0;

	// Helper lambda function to append a specified amount of bits to the config array
	auto func_print = [&](unsigned val, unsigned bits) {
		// Print the data
		fprintf(Fo, "\"");
		unsigned msb_mask = 1 << (bits-1);
		for(unsigned i=0; i<bits; i++) {
			fprintf(Fo, "%c", ((val & msb_mask) != 0) ? '1' : '0');
			val <<= 1;
		}
		fprintf(Fo, "\"");
		// Increment the number of concatenated values up to now
		cat_num++;
	};

	for(int n=layer->fsize-1; n >= 0; n--) {
		int* cfg_data = layer->cfg_data[n];

		// Print necessary VHDL keywords and newline
		if(cat_num > 0) fprintf(Fo, " &\n");

		// Only after newline we can print comments
		fprintf(Fo, "%s-- Neuron %u\n", indent, n);

		// Print the parameters
		fprintf(Fo, "			");
		if(norm_wshr  > 0) func_print(cfg_data[col_shr],  norm_wshr);
		if(cat_num > 0) fprintf(Fo, " & ");
		if(norm_wmul  > 0) func_print(cfg_data[col_mul],  norm_wmul);
		if(cat_num > 0) fprintf(Fo, " & ");
		if(norm_wbias > 0) func_print(cfg_data[col_bias], norm_wbias);

	}  // fsize

	if(cat_num > 0) fprintf(Fo, "\n");
}

void LayerNorm::genvhdl_const_params_mem(FILE* Fo, const char* indent) {
	layer_t* layer = this;

	// Get the width of parameters
	unsigned norm_wbias = layer->norm_wbias;
	unsigned norm_wmul  = layer->norm_wmul;
	unsigned norm_wshr  = layer->norm_wshr;

	// Determine the position of bias and shr columns
	unsigned col_bias = 0;
	unsigned col_mul = 0;
	unsigned col_shr = 0;
	unsigned col_nb = 0;
	col_bias = col_nb; col_nb += (norm_wbias > 0) ? 1 : 0;
	col_mul  = col_nb; col_nb += (norm_wmul  > 0) ? 1 : 0;
	col_shr  = col_nb; col_nb += (norm_wshr  > 0) ? 1 : 0;

	unsigned cat_num = 0;

	// Helper lambda function to append a specified amount of bits to the config array
	auto func_print = [&](unsigned val, unsigned bits) {
		// Print the data
		fprintf(Fo, "\"");
		unsigned msb_mask = 1 << (bits-1);
		for(unsigned i=0; i<bits; i++) {
			fprintf(Fo, "%c", ((val & msb_mask) != 0) ? '1' : '0');
			val <<= 1;
		}
		fprintf(Fo, "\"");
		// Increment the number of concatenated values up to now
		cat_num++;
	};

	for(unsigned fi=0; fi < layer->fsize; fi += layer->split_in) {
		if(cat_num > 0) { fprintf(Fo, ",\n"); cat_num = 0; }
		if(layer->fsize / layer->split_in > 1) {
			fprintf(Fo, "%s-- Frame position %u\n", indent, fi);
		}

		// Here, generate parameters for PAR_IN data paths in parallel

		for(int pi = layer->split_in-1; pi >= 0; pi--) {

			unsigned n = fi + pi;
			int* cfg_data = layer->cfg_data[n];

			// Print necessary VHDL keywords and newline
			if(cat_num > 0) fprintf(Fo, " &\n");

			// Only after newline we can print comments
			if(pi == (int)layer->split_in-1) {
				if(layer->split_in > 1) {
					fprintf(Fo, "%s-- Neuron %u\n", indent, n);
				}
			}

			// Print the parameters
			fprintf(Fo, "			");
			if(norm_wshr  > 0) func_print(cfg_data[col_shr],  norm_wshr);
			if(cat_num > 0) fprintf(Fo, " & ");
			if(norm_wmul  > 0) func_print(cfg_data[col_mul],  norm_wmul);
			if(cat_num > 0) fprintf(Fo, " & ");
			if(norm_wbias > 0) func_print(cfg_data[col_bias], norm_wbias);

		}  // PAR

		// Note : End of a line in memory

	}  // fsize

	if(cat_num > 0) fprintf(Fo, "\n");
}

void LayerTernarize::genvhdl_const_params_vec(FILE* Fo, const char* indent) {
	layer_t* layer = this;

	unsigned cat_num = 0;

	// Helper lambda function to append a specified amount of bits to the config array
	auto func_print = [&](unsigned val, unsigned bits) {
		// Print the data
		fprintf(Fo, "\"");
		unsigned msb_mask = 1 << (bits-1);
		for(unsigned i=0; i<bits; i++) {
			fprintf(Fo, "%c", ((val & msb_mask) != 0) ? '1' : '0');
			val <<= 1;
		}
		fprintf(Fo, "\"");
		// Increment the number of concatenated values up to now
		cat_num++;
	};

	for(int n=layer->fsize-1; n >= 0; n--) {
		int* cfg_data = layer->cfg_data[n];

		// Print necessary VHDL keywords and newline
		if(cat_num > 0) fprintf(Fo, " &\n");

		// Only after newline we can print comments
		fprintf(Fo, "%s-- Neuron %u\n", indent, n);

		// Print the parameters
		fprintf(Fo, "			");
		if(ter_out_static == false) {
			func_print(cfg_data[4], out_wdata);
			fprintf(Fo, " & ");
			func_print(cfg_data[3], out_wdata);
			fprintf(Fo, " & ");
			func_print(cfg_data[2], out_wdata);
			fprintf(Fo, " & ");
		}
		func_print(cfg_data[1], wdata);
		fprintf(Fo, " & ");
		func_print(cfg_data[0], wdata);

	}  // fsize

	if(cat_num > 0) fprintf(Fo, "\n");
}

void LayerTernarize::genvhdl_const_params_mem(FILE* Fo, const char* indent) {
	layer_t* layer = this;

	unsigned cat_num = 0;

	// Helper lambda function to append a specified amount of bits to the config array
	auto func_print = [&](unsigned val, unsigned bits) {
		// Print the data
		fprintf(Fo, "\"");
		unsigned msb_mask = 1 << (bits-1);
		for(unsigned i=0; i<bits; i++) {
			fprintf(Fo, "%c", ((val & msb_mask) != 0) ? '1' : '0');
			val <<= 1;
		}
		fprintf(Fo, "\"");
		// Increment the number of concatenated values up to now
		cat_num++;
	};

	for(unsigned fi=0; fi < layer->fsize; fi += layer->split_in) {
		if(cat_num > 0) { fprintf(Fo, ",\n"); cat_num = 0; }
		if(layer->fsize / layer->split_in > 1) {
			fprintf(Fo, "%s-- Frame position %u\n", indent, fi);
		}

		// Here, generate parameters for PAR_IN data paths in parallel

		for(int pi = layer->split_in-1; pi >= 0; pi--) {

			unsigned n = fi + pi;
			int* cfg_data = layer->cfg_data[n];

			// Print necessary VHDL keywords and newline
			if(cat_num > 0) fprintf(Fo, " &\n");

			// Only after newline we can print comments
			if(pi == (int)layer->split_in-1) {
				if(layer->split_in > 1) {
					fprintf(Fo, "%s-- Neuron %u\n", indent, n);
				}
			}

			// Print the parameters
			fprintf(Fo, "			");
			if(ter_out_static == false) {
				func_print(cfg_data[4], out_wdata);
				fprintf(Fo, " & ");
				func_print(cfg_data[3], out_wdata);
				fprintf(Fo, " & ");
				func_print(cfg_data[2], out_wdata);
				fprintf(Fo, " & ");
			}
			func_print(cfg_data[1], wdata);
			fprintf(Fo, " & ");
			func_print(cfg_data[0], wdata);

		}  // PAR

		// Note : End of a line in memory

	}  // fsize

	if(cat_num > 0) fprintf(Fo, "\n");
}

// Filler worker object to generate the constant weights, inside a component that contains a memory

class FillerWorker_ConstantParams : public TemplateFileFiller::FillerWorker {
	public :

	// The network to work on
	Network* network = nullptr;

	FillerWorker_ConstantParams(Network* n) {
		line_begin = "-- AUTOGEN CONST PARAMS BEGIN";
		line_end   = "-- AUTOGEN CONST PARAMS END";
		network = n;
	}

	void fill(TemplateFileFiller& filler);

};

void FillerWorker_ConstantParams::fill(TemplateFileFiller& filler) {
	FILE* Fo = filler.Fo;

	unsigned num_layers_cst = 0;

	for(auto layer : network->layers) {
		if(layer->const_params == false) continue;

		// Only handle some layers
		// FIXME This numlines should be obtained with a method
		unsigned numlines = 0;
		if(layer->type == LAYER_NEU) {
			numlines = (layer->fsize / layer->split_in) * layer->neu_time_mux;
		}
		else if(layer->type == LAYER_NORM || layer->type == LAYER_TER) {
			numlines = layer->fsize / layer->split_in;
		}
		if(numlines == 0) continue;

		fprintf(Fo, "\n\t");
		if(num_layers_cst == 0) fprintf(Fo, "gen : ");  // Add an identifier before the "if generate"
		else fprintf(Fo, "els");  // Convert "if ... generate" into "elsif ... generate"
		fprintf(Fo, "if LAYER_ID = %u generate\n", layer->cfg_id);
		fprintf(Fo, "\n\t\t");
		fprintf(Fo, "-- Layer %s\n", layer->vhdl_prefixu);
		fprintf(Fo, "\n");

		// Handle case where there is no underlying memory : just generate a gigantic vector
		// Note : Output of layer may still take several clock cycles, so order of neurons in the vector depends on PAR_OUT
		if(numlines <= 1) {

			fprintf(Fo, "		data_out <= (\n");
			layer->genvhdl_const_params_vec(Fo, "			");
			fprintf(Fo, "		);\n");

		}  // Just a constant signal

		// Handle case where there is an underlying memory, lutram or bram
		else {

			// Declaration of memory array
			fprintf(Fo, "		type mem_type is array (0 to %u-1) of std_logic_vector(WDATA*PAR_OUT-1 downto 0);\n", numlines);
			fprintf(Fo, "		signal mem : mem_type := (\n");
			layer->genvhdl_const_params_mem(Fo, "			");
			fprintf(Fo, "		);\n");

			// Generate the memory implementation type, lutram or bram
			fprintf(Fo, "\n");
			fprintf(Fo, "		attribute ram_style : string;\n");
			fprintf(Fo, "		attribute ram_style of mem : signal is \"");
			if(layer->mem.style == MemImplem::STYLE_LUTRAM) fprintf(Fo, "distributed");
			else fprintf(Fo, "block");
			fprintf(Fo, "\";\n");
			fprintf(Fo, "\n");
			fprintf(Fo, "	begin\n");
			fprintf(Fo, "\n");
			fprintf(Fo, "		data_out <= mem(to_integer(unsigned(addr_in)));\n");

		}

		num_layers_cst ++;
	}

	if(num_layers_cst > 0) {
		fprintf(Fo, "\n");
		fprintf(Fo, "	else generate\n");
		fprintf(Fo, "\n");
		fprintf(Fo, "		-- Default assignment just in case\n");
		fprintf(Fo, "		data_out <= (others => '0');\n");
		fprintf(Fo, "\n");
		fprintf(Fo, "	end generate;\n");
	}
	else {
		fprintf(Fo, "\n");
		fprintf(Fo, "	-- Default assignement for unused component\n");
		fprintf(Fo, "	data_out <= (others => '0');\n");
	}

}

// Main generation function

int vhdl_gen_const_params(Network* network, const char* filename_in, const char* filename_out, bool gen_clear) {
	auto& layers = network->layers;

	// Note : Better to call this on the entire network than on per-layer basis
	// To avoid network stats being counted twice
	network->hwconfig_finalize();

	// Check if there are any layers with constant weights

	int errors_nb = 0;

	for(auto layer : layers) {
		if(layer->const_params == false) continue;

		// Safety : Only consider a selection of layers that support const parameters
		if(
			(layer->type != LAYER_NEU) &&
			(layer->type != LAYER_NORM) &&
			(layer->type != LAYER_TER)
		) {
			continue;
		}

		// Load weights if not done already
		if(layer->cfg_data == nullptr) {
			int z = layer->load_config_files();
			if(z != 0) {
				errors_nb ++;
			}
		}

		if(layer->cfg_data == nullptr) {
			printf("Error: layer %s%u: Weights are marked constant but weights are not loaded\n", layer->typenameu, layer->typeidx);
			errors_nb++;
		}

	}  // layers

	if(errors_nb != 0) {
		return errors_nb;
	}

	// Create and initialize the template file filler object

	TemplateFileFiller filler;
	filler.filename_in  = filename_in;
	filler.filename_out = filename_out;

	// General behaviour for VHDL file content
	filler.fast_pattern_begin = "--";
	filler.want_clear = gen_clear;

	// Initialize
	filler.open();

	// Register filler worker
	filler.register_worker(new FillerWorker_ConstantParams(network));

	// Scan the input file and insert the VHDL
	filler.process();

	return 0;
}

