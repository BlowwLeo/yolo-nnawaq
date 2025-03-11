
#pragma once

extern "C" {

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

}

#include <vector>
#include <map>
#include <string>

#include "hw_reg_fields.h"
#include "mem_implem.h"


//============================================
// Definitions common to all NN layer types
//============================================

#define LAYER_NONE     0
// Main computation layers
#define LAYER_WIN      1
#define LAYER_NEU      2
#define LAYER_POOL     3
// Layers with simple pipeline operation
#define LAYER_NORM     4
#define LAYER_TER      5
#define LAYER_RELU     6 //same for leaky?
#define LAYER_ADD      7
#define LAYER_CUSTOM   8
// Layers for parallel branches
#define LAYER_FORK     9
#define LAYER_CAT      10
#define LAYER_SCATTER  11
#define LAYER_GATHER   12
// Other special layers
#define LAYER_FLATTEN  13
#define LAYER_SOFTMAX  14
#define LAYER_FIFO     15
#define LAYER_TYPE_MAX 15
#define LAYER_LEAKY    16

// Layers for Channel Major
#define LAYER_WIN_CM 17
#define LAYER_NEU_CM 18
#define LAYER_NORM_CM 19

// The pooling type is a special field in the layer structure
// In HW, the style is given by the layer ID itself
#define POOL_TYPE_NONE 0
#define POOL_TYPE_MAX  1
#define POOL_TYPE_MIN  2
#define POOL_TYPE_AVG  3
#define POOL_TYPE_ADD  4  // This is same than AvgPool, except there is no division after

// Flags for neuron signedness
#define NEUSGN_UNSIGNED  0x01
#define NEUSGN_SIGNED    0x02
#define NEUSGN_LOCKED    0x04
#define NEUSGN_VALID     0x08

// The order of neuron weights when loaded from disk
// NONE is the init/default or unspecified per-layer order, the behaviour is KEEP
#define NEU_WORDER_NONE    0
#define NEU_WORDER_KEEP    1
#define NEU_WORDER_ZFIRST  2
#define NEU_WORDER_XFIRST  3

// Declaration of type from other source files
class HwAcc_Common;

// Forward declarations
class Network;


//============================================
// Utility struct to compute latency
//============================================

typedef struct {
	unsigned nbin_before_begin;
	unsigned cycles_to_first_out;  // after the first input necessary to emit the first output
	unsigned cycles_to_last_out;  // after the last input
} Latency;


//============================================
// Definition of the main NN layer type (virtual class)
//============================================

// FIXME This is only for transition to more C++ coding style and rename layer_t -> Layer
typedef class Layer layer_t;

class Layer {

	public :

	// General fields

	int      type    = LAYER_NONE;
	unsigned typeidx = 0;
	char const * typenamel = nullptr;  // Shared pointer
	char const * typenameu = nullptr;  // Shared pointer
	int      id = 0;  // To identify layers except FIFOs, in HW. Invalid/unset if negative.
	// Name prefix for VHDL generation
	char*    vhdl_prefixl = nullptr;
	char*    vhdl_prefixu = nullptr;

	// Relationship with the parent Network class
	unsigned index   = 0;  // Position in the vector of layers of the parent Network class
	Network* network = nullptr;

	// Layer-specific configuration registers
	unsigned regs_idx = 0;  // Index of first mandatory register in the chain of registers
	unsigned regs_nb  = 0;  // Numbers of configuration registers, including the first mandatory register

	// Configuration
	unsigned cfg_id       = 0;
	char*    cfg_filename = nullptr;
	int **   cfg_data     = nullptr;  // For execution in software

	// Fields specific to layer types

	// Parameter for user-specified output data width
	unsigned user_wout = 0;
	unsigned user_par_oz = 0;

	// Parameters for parallelism
	unsigned split_in  = 0;
	unsigned split_out = 0;

	// Input 3D image
	unsigned fx = 0;
	unsigned fy = 0;
	unsigned fz = 0;
	unsigned fx_max = 0;
	unsigned fy_max = 0;
	unsigned fz_max = 0;
	unsigned wdata = 0;
	unsigned sdata = 0;
	unsigned fsize = 0;
	unsigned fsize_max = 0;
	unsigned nbframes = 0;
	unsigned cycles = 0;

	// Parameter for implementation of internal memory
	MemImplem mem;

	// Parameter for rounding result of internal computations
	bool     round_nearest = false;
	// Parameter for constant weights/params
	bool     const_params = false;

	// Parameters for sliding window
	unsigned winx = 0;
	unsigned winy = 0;
	unsigned win_par_oz = 0;  // Output parallelism on Z dimension (style NORMAL only)
	unsigned win_repeat = 0;  // To repeat window content before going to the next window position
	bool     win_dwconv = false;  // To indicate DepthWiseConv
	unsigned stepx = 0;
	unsigned stepy = 0;
	unsigned nwinx = 0;
	unsigned nwiny = 0;
	unsigned nwinz = 0;
	unsigned begpadx = 0;
	unsigned begpady = 0;
	unsigned bufy = 0;
	bool     win_sym_xy = true;

	// Parameters for neurons
	unsigned neurons = 0;
	unsigned neurons_max = 0;
	unsigned neu_wweight = 0;
	unsigned neu_per_bram = 0;
	unsigned neu_wrnb = 0;  // Number of values per neuron and per config word
	char     neu_sgnd = 0;
	char     neu_sgnw = 0;
	unsigned neu_custom_mul    = 0;  // To be used as zero / non-zero
	unsigned neu_custom_mul_id = 0;  // ID of custom multiplication operation
	unsigned neu_custom_wmul   = 0;  // Bit width of result
	unsigned neu_custom_smul   = 0;  // Signedness of result, to be used as zero / non-zero
	unsigned neu_style = 0;  // 0 = old implementation with per-BRAM implem, 1 = new implem non-packed, 2 = new implem packed
	unsigned neu_comp_style = 0;  // Custom string identifier for compression, (0 = no compression)
	unsigned neu_comp_nraw = 0;  // Number of weights per compressed group (0 = no compression)
	unsigned neu_comp_nbin = 0;  // Number of bits per group of compressed weights
	unsigned neu_waccu = 0;  // Only for an old neuron style
	unsigned neu_time_mux = 0;  // To implement time multiplexing
	unsigned neu_worder = NEU_WORDER_NONE;  // How weights are ordered on disk

	// Parameters for MaxPool / MinPool / AvgPool
	unsigned pool_type = 0;
	// Number of parallel pooling units
	unsigned pool_units_nb = 0;
	// Parameters for AvgPool
	unsigned pool_avg_mult = 1;
	unsigned pool_avg_shr = 0;

	// Parameters for BatchNorm : constants
	unsigned norm_mul_cst = 0;
	unsigned norm_shr_cst = 0;
	// Parameters for BatchNorm
	unsigned norm_wbias = 0;
	unsigned norm_wmul = 0;
	unsigned norm_wshr = 0;

	// Parameters for Ternarize
	bool     ter_out_static = true;

	// Parameters for ReLU
	int      relu_min  = 0;
	int      relu_max  = 0;

	// Parameters for Leaky
	int      leaky_min  = 0;
	int      leaky_max  = 0;

	// Parameters for custom layer
	char*    custom_entity = nullptr;
	unsigned custom_user_id = 0;
	unsigned custom_latency = 0;

	// For layers that support it : skip creation of input buffers and flow control from output FIFO
	bool     flow_skip_inbuf = false;
	// For layers that support it : the additional FIFO margin on output side to allow computation
	unsigned out_extra_fifo_room = 0;

	// For fork / concat layers and similar
	bool     prev_is_arr = false;
	bool     next_is_arr = false;
	std::vector<Layer*> arr_layers;

	// Stats, activity ratios for neurons
	double   stat_zd     = 0;  // Ratio of data=0
	double   stat_nzd_zw = 0;  // Ratio of data!=0 and weight=0

	// Layer CAT : a temp marker for forward propag
	// FIXME This should be a private field of the traversal object
	unsigned cat_cnt_fwd_propag = 0;

	// Output 3D image
	unsigned out_fx = 0;
	unsigned out_fy = 0;
	unsigned out_fz = 0;
	unsigned out_wdata = 0;
	unsigned out_sdata = 0;
	unsigned out_fsize = 0;
	unsigned out_nbframes = 0;
	unsigned out_cycles = 0;
	unsigned out_cycles_real = 0;  // Account for inefficiency of available HW implementation (not critical)

	// In case other modules want to play with stuff
	void*    ptrdata = nullptr;
	// For software execution, predecessors of a CAT layer need to store their results
	int*     swexec_output = nullptr;

	// Previous and next layers
	Layer* prev = nullptr;
	Layer* next = nullptr;

	// Constructor
	Layer(void);

	// The destructor is virtual because this is a base class to be inherited
	virtual ~Layer(void);

	// Static members

	private :

	static std::map<std::string, unsigned> map_layer_name2id;
	static std::vector<Layer*> vec_layer_id2type;

	static std::map<std::string, unsigned> map_layer_entity2id;

	public :

	static inline unsigned map_layer_entity2id_size() { return map_layer_entity2id.size(); }

	static int register_type(Layer* layer, const char* name);

	static int get_type_name2id(const char* type_name);
	static int get_type_name2id_verbose(const char* type_name);
	static char const * get_type_id2namel(int type_id);
	static char const * get_type_id2nameu(int type_id);

	static Layer* create_new_from_id(unsigned type_id, const char* type_name = nullptr);
	static Layer* create_new_from_id_verbose(unsigned type_id, const char* type_name = nullptr);

	// Methods

	public :

	virtual void DefineConfigRegs(void);

	virtual void   apply_network_defaults(Network* network);
	virtual Layer* create_new(void);
	Layer*         create_new(Network* network);
	virtual Layer* clone(void);

	virtual int params_from_type_name(char const * type_name = nullptr);

	virtual bool requires_idxhw(void) { return false; }
	virtual bool requires_idxcfg(void) { return false; }

	virtual int propag_params_forward(void);

	virtual unsigned long eval_mem_size(void);
	virtual void eval_latency(Latency& lat) const;

	virtual void print_extra_details(void);

	virtual void hwconfig_finalize(void);
	virtual int  load_config_files(void);
	virtual int  dump_config_vhdl(void);  // Does nothing, silently
	int          dump_config_vhdl_generic(void);  // Generic layer handling, verbose

	virtual void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	virtual void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);
	virtual void config_from_regs_id2layer(const std::vector<uint32_t>& accreg_cfgnn, std::map<unsigned, Layer*>& map_hwid_to_prev_layer);

	virtual void genvhdl_set_config_regs_numbers(void);
	virtual void genvhdl_cst_decl(FILE* Fo);
	virtual unsigned genvhdl_comp_decl_register(void);
	virtual void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	virtual void genvhdl_sig_decl(FILE* Fo);

	virtual void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	virtual void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	virtual void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	virtual void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	virtual void genvhdl_comp_get_out_room_port(char const*& name_port);
	virtual void genvhdl_comp_get_wout_param(char const*& name_param);
	virtual void genvhdl_comp_inst(FILE* Fo);

	// Generate the constant parameters in a VHDL file
	virtual void genvhdl_const_params_vec(FILE* Fo, const char* indent);
	virtual void genvhdl_const_params_mem(FILE* Fo, const char* indent);

	virtual int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

	// Generate the raw configuration stream to be programmed inyo the HW accelerator
	// The configuration may be sent in several parts, hence the arguments idx_part and num_parts
	virtual int hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part);

};


//============================================
// Definition of the NN layer types
//============================================

class LayerWin : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_dwconv;
	static LayerRegField* regfield_symxy;
	static LayerRegField* regfield_repeat;
	// Reg 1
	static LayerRegField* regfield_fx;
	static LayerRegField* regfield_fx_max;
	// Reg 2
	static LayerRegField* regfield_fz;
	static LayerRegField* regfield_fz_max;
	// Reg 3
	static LayerRegField* regfield_stepx;
	static LayerRegField* regfield_winx;
	static LayerRegField* regfield_padx;
	static LayerRegField* regfield_nwinx;
	// Reg 4
	static LayerRegField* regfield_nwinz;
	static LayerRegField* regfield_par_oz;
	// Reg 5
	static LayerRegField* regfield_fy;
	static LayerRegField* regfield_fy_max;
	// Reg 6
	static LayerRegField* regfield_stepy;
	static LayerRegField* regfield_winy;
	static LayerRegField* regfield_pady;
	static LayerRegField* regfield_nwiny;

	public :

	// Constructor / Destructor

	LayerWin(void);
	//~LayerWin(void) {}

	// Methods

	void DefineConfigRegs(void);

	void   apply_network_defaults(Network* network);
	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }

	int propag_params_forward(void);

	unsigned long eval_mem_size(void);
	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	void hwconfig_finalize(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerWin_CM : public LayerWin {
	public:

	LayerWin_CM(void);

	Layer* create_new(void);
	Layer* clone(void);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerNeu : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_dwconv;
	static LayerRegField* regfield_tmux;
	// Reg 1
	static LayerRegField* regfield_fsize;
	static LayerRegField* regfield_fsize_max;
	// Reg 2
	static LayerRegField* regfield_neu;
	static LayerRegField* regfield_neu_max;
	// Reg 3
	static LayerRegField* regfield_nperblk;
	static LayerRegField* regfield_wrnb;
	static LayerRegField* regfield_wweight;
	static LayerRegField* regfield_sdlock;
	static LayerRegField* regfield_sdata;
	static LayerRegField* regfield_swlock;
	static LayerRegField* regfield_sweight;
	static LayerRegField* regfield_style;
	static LayerRegField* regfield_mul_id;

	public :

	// Constructor / Destructor

	LayerNeu(void);
	//~LayerNeu(void) {}

	// Methods

	void DefineConfigRegs(void);

	void   apply_network_defaults(Network* network);
	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }
	bool requires_idxcfg(void) { return true; }

	int propag_params_forward(void);

	unsigned long eval_mem_size(void);
	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	void hwconfig_finalize(void);
	int  load_config_files(void);
	int  dump_config_vhdl(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	// Generate the constant parameters in a VHDL file
	void genvhdl_const_params_vec(FILE* Fo, const char* indent);
	void genvhdl_const_params_mem(FILE* Fo, const char* indent);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

	int hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part);

};

class LayerNeu_CM : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_dwconv;
	static LayerRegField* regfield_tmux;
	// Reg 1
	static LayerRegField* regfield_fsize;
	static LayerRegField* regfield_fsize_max;
	// Reg 2
	static LayerRegField* regfield_neu;
	static LayerRegField* regfield_neu_max;
	// Reg 3
	static LayerRegField* regfield_nperblk;
	static LayerRegField* regfield_wrnb;
	static LayerRegField* regfield_wweight;
	static LayerRegField* regfield_sdlock;
	static LayerRegField* regfield_sdata;
	static LayerRegField* regfield_swlock;
	static LayerRegField* regfield_sweight;
	static LayerRegField* regfield_style;
	static LayerRegField* regfield_mul_id;

	public :

	// Constructor / Destructor

	LayerNeu_CM(void);
	//~LayerNeu(void) {}

	// Methods

	void DefineConfigRegs(void);

	void   apply_network_defaults(Network* network);
	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }
	bool requires_idxcfg(void) { return true; }

	int propag_params_forward(void);

	unsigned long eval_mem_size(void);
	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	void hwconfig_finalize(void);
	int  load_config_files(void);
	int  dump_config_vhdl(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	// Generate the constant parameters in a VHDL file
	void genvhdl_const_params_vec(FILE* Fo, const char* indent);
	void genvhdl_const_params_mem(FILE* Fo, const char* indent);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

	int hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part);

};

class LayerPool : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_type;
	static LayerRegField* regfield_rndnear;
	static LayerRegField* regfield_avgreg;
	// Reg 1
	static LayerRegField* regfield_fsize;
	// Reg 2
	static LayerRegField* regfield_mul;
	static LayerRegField* regfield_shr;

	public :

	// Constructor / Destructor

	LayerPool(void);
	//~LayerPool(void) {}

	// Methods

	void DefineConfigRegs(void);

	void   apply_network_defaults(Network* network);
	Layer* create_new(void);
	Layer* clone(void);
	int params_from_type_name(char const * type_name = nullptr);

	bool requires_idxhw(void) { return true; }

	int propag_params_forward(void);

	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerNorm : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_enbias;
	static LayerRegField* regfield_enmul;
	static LayerRegField* regfield_wbias;
	static LayerRegField* regfield_wmul;
	static LayerRegField* regfield_wshr;
	// Reg 1
	static LayerRegField* regfield_fsize;
	static LayerRegField* regfield_fsize_max;
	// Reg 2
	static LayerRegField* regfield_cstmul;
	static LayerRegField* regfield_cstshr;
	static LayerRegField* regfield_rndtype;

	public :

	// Constructor / Destructor

	LayerNorm(void);
	//~LayerNorm(void) {}

	// Methods

	void DefineConfigRegs(void);

	void   apply_network_defaults(Network* network);
	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }
	bool requires_idxcfg(void) { return true; }

	int propag_params_forward(void);

	unsigned long eval_mem_size(void);
	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	int  load_config_files(void);
	int  dump_config_vhdl(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	// Generate the constant parameters in a VHDL file
	void genvhdl_const_params_vec(FILE* Fo, const char* indent);
	void genvhdl_const_params_mem(FILE* Fo, const char* indent);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

	int hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part);

};

class LayerNorm_CM : public LayerNorm {
	public:

	LayerNorm_CM(void);

	Layer* create_new(void);
	Layer* clone(void);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);
};

class LayerTernarize : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_out_static;
	static LayerRegField* regfield_out_low;
	static LayerRegField* regfield_out_med;
	static LayerRegField* regfield_out_up;
	// Reg 1
	static LayerRegField* regfield_fsize;
	static LayerRegField* regfield_fsize_max;

	public :

	// Constructor / Destructor

	LayerTernarize(void);
	//~LayerTernarize(void) {}

	// Methods

	void DefineConfigRegs(void);

	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }
	bool requires_idxcfg(void) { return true; }

	int propag_params_forward(void);

	unsigned long eval_mem_size(void);
	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	int  load_config_files(void);
	int  dump_config_vhdl(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	// Generate the constant parameters in a VHDL file
	void genvhdl_const_params_vec(FILE* Fo, const char* indent);
	void genvhdl_const_params_mem(FILE* Fo, const char* indent);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

	int hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part);

};

class LayerRelu : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	// (no extra fields)
	// Reg 1
	static LayerRegField* regfield_thmin;
	// Reg 2
	static LayerRegField* regfield_thmax;

	public :

	// Constructor / Destructor

	LayerRelu(void);
	//~LayerRelu(void) {}

	// Methods

	void DefineConfigRegs(void);

	void   apply_network_defaults(Network* network);
	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }

	int propag_params_forward(void);

	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};
class LayerLeaky : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	// (no extra fields)
	// Reg 1
	static LayerRegField* regfield_thmin;
	// Reg 2
	static LayerRegField* regfield_thmax;

	public :

	// Constructor / Destructor

	LayerLeaky(void);
	//~LayerLeaky(void) {}

	// Methods

	void DefineConfigRegs(void);

	void   apply_network_defaults(Network* network);
	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }

	int propag_params_forward(void);

	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerAdd : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	public :

	// Constructor / Destructor

	LayerAdd(void);
	//~LayerAdd(void) {}

	// Methods

	void DefineConfigRegs(void);

	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }

	int propag_params_forward(void);

	virtual void eval_latency(Latency& lat) const;

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerCustom : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_func_id;

	public :

	// Constructor / Destructor

	LayerCustom(void);
	//~LayerCustom(void) {}

	// Methods

	void DefineConfigRegs(void);

	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }

	int propag_params_forward(void);

	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerFork : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_layers_nb;

	public :

	// Constructor / Destructor

	LayerFork(void);
	//~LayerFork(void) {}

	// Methods

	void DefineConfigRegs(void);

	Layer* create_new(void);
	Layer* clone(void);

	int propag_params_forward(void);

	void print_extra_details(void);

	void config_from_regs_id2layer(const std::vector<uint32_t>& accreg_cfgnn, std::map<unsigned, Layer*>& map_hwid_to_prev_layer);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);

	// Note : No component declaration because functionality is written inline
	// Note : No signal declarations are needed
	// Note : No config registers to set
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerCat : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_layers_nb;

	public :

	// Constructor / Destructor

	LayerCat(void);
	//~LayerCat(void) {}

	// Methods

	void DefineConfigRegs(void);

	Layer* create_new(void);
	Layer* clone(void);

	int propag_params_forward(void);

	void print_extra_details(void);

	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);

	// Note : No component declaration because functionality is written inline
	// Note : No signal declarations are needed
	// Note : No config registers to set
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerScatter : public Layer {

	public :

	// Constructor / Destructor

	LayerScatter(void);
	//~LayerScatter(void) {}

	// Methods

	Layer* create_new(void);
	Layer* clone(void);

	int propag_params_forward(void);

	unsigned long eval_mem_size(void);

	void print_extra_details(void);

	int  load_config_files(void);

	// FIXME Missing VHDL generation

	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

	int hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part);

};

class LayerGather : public Layer {

	public :

	// Constructor / Destructor

	LayerGather(void);
	//~LayerGather(void) {}

	// Methods

	Layer* create_new(void);
	Layer* clone(void);

	int propag_params_forward(void);

	unsigned long eval_mem_size(void);

	void print_extra_details(void);

	int  load_config_files(void);

	// FIXME Missing VHDL generation

	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

	int hwacc_genconfig(const HwAcc_Common* hwacc, std::vector<uint32_t>& arr, unsigned& code_part, unsigned& num_parts, unsigned idx_part);

};

class LayerFlatten : public Layer {

	public :

	// Constructor / Destructor

	LayerFlatten(void);
	//~LayerFlatten(void) {}

	// Methods

	Layer* create_new(void);
	Layer* clone(void);

	int propag_params_forward(void);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	// Note : No component declaration
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerSoftMax : public Layer {

	public :

	static std::vector< std::vector<LayerRegField> > regs_fields;

	// Reg 0
	static LayerRegField* regfield_fsize;

	public :

	// Constructor / Destructor

	LayerSoftMax(void);
	//~LayerSoftMax(void) {}

	// Methods

	void DefineConfigRegs(void);

	Layer* create_new(void);
	Layer* clone(void);

	bool requires_idxhw(void) { return true; }

	int propag_params_forward(void);

	virtual void eval_latency(Latency& lat) const;

	void print_extra_details(void);

	void write_config_regs(std::vector<uint32_t>& accreg_cfgnn);
	void config_from_regs(const std::vector<uint32_t>& accreg_cfgnn);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	unsigned genvhdl_comp_decl_register(void);
	void genvhdl_comp_decl(FILE* Fo, unsigned flags);
	void genvhdl_sig_decl(FILE* Fo);

	void genvhdl_regs_setconst(FILE* Fo, const char* tabs, const char* tabs1);
	void genvhdl_regs_setconst_locked(FILE* Fo, const char* tabs, const char* tabs1);

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_room_port(char const*& name_port);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};

class LayerFifo : public Layer {

	public :

	// Constructor / Destructor

	LayerFifo(void);
	//~LayerFifo(void) {}

	// Methods

	Layer* create_new(void);
	Layer* clone(void);

	int propag_params_forward(void);

	unsigned long eval_mem_size(void);
	virtual void eval_latency(Latency& lat) const;

	// Note : No config registers for this component
	void genvhdl_cst_decl(FILE* Fo);
	// Note : No component declaration because this is supposed to be present in the VHDL wrapper already
	void genvhdl_sig_decl(FILE* Fo);

	// Note : No config registers to set

	void genvhdl_comp_get_in_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_out_ports(char const*& name_data, char const*& name_rdy, char const*& name_ack);
	void genvhdl_comp_get_wout_param(char const*& name_param);
	void genvhdl_comp_inst(FILE* Fo);

	int swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer);

};


//============================================
// Definition of the neural network
//============================================

class Network {

	// Only one network is allowed for now

	private :
	static Network* singleton;

	public :
	static Network* GetSingleton(void);
	static void DeleteSingleton(void);

	// Constructor / Destructor

	private :
	Network(void) {}

	public :
	~Network(void) {}

	// Default parameters

	// Parameters for compression
	unsigned default_comp_all_style = 0;
	unsigned default_comp_all_nraw  = 0;
	unsigned default_comp_all_nbin  = 0;
	unsigned default_comp_bram_style = 0;
	unsigned default_comp_bram_nraw = 0;
	unsigned default_comp_bram_nbin = 0;
	unsigned default_comp_fc_style  = 0;
	unsigned default_comp_fc_nraw   = 0;
	unsigned default_comp_fc_nbin   = 0;

	// These variables MUST NOT be set to STYLE_NONE
	MemImplem::style_type default_mem_implem_win = MemImplem::STYLE_AUTO;
	MemImplem::style_type default_mem_implem_neu = MemImplem::STYLE_AUTO;

	// Parameter for rounding result of internal computations
	bool     default_round_nearest = false;

	// Default bit widths for neuron layers
	unsigned default_neu_wd = 2;
	bool     default_neu_sd = true;
	unsigned default_neu_ww = 2;
	bool     default_neu_sw = true;
	unsigned default_neu_wo = 0;
	bool     default_neu_so = true;
  // How weights are ordered on disk
	unsigned default_neu_worder = NEU_WORDER_NONE;

	// Default features to enable in BatchNorm layers : Constant multiplication and shift
	unsigned default_norm_mul_cst = 0;
	unsigned default_norm_shr_cst = 0;
	// Default features to enable in BatchNorm layers (zero bits means disabled)
	unsigned default_norm_wbias = 0;
	unsigned default_norm_wmul = 0;
	unsigned default_norm_wshr = 0;

	// Default parameters for ReLU
	int      default_relu_min = 0;
	int      default_relu_max = 1;
	
	// Default parameters for ReLU 
	int      default_leaky_min = -1; //Quantification => valeurs > 0?
	int      default_leaky_max = 1;

	// Network build parameters

	// Data at input of the network
	unsigned param_win = 8;
	bool     param_sin = false;
	// Dimension of input frame
	unsigned param_fx = 32;
	unsigned param_fy = 32;
	unsigned param_fz = 3;
	// Parallelism at input of first layer
	unsigned param_inpar = 1;

	// This indicates the number of output neurons to use
	// It may be lower than the number of neurons physically present in the network
	unsigned cnn_outneu = 0;

	// To inhibit generation of FIFO between WIN and NEU layers, under threshold of output frame size
	unsigned nofifo_win_neu_th = 0;
	// To inhibit generation of FIFO between WIN and POOL layers
	bool     nofifo_win_pool = false;
	// To inhibit generation of FIFO between NEU and ReLU layers
	bool     nofifo_neu_relu = false;
	// To inhibit generation of FIFO between NORM and ReLU layers
	bool     nofifo_norm_relu = false;
	
	// To inhibit generation of FIFO between NEU and ReLU layers
	bool     nofifo_neu_leaky = false;
	// To inhibit generation of FIFO between NORM and ReLU layers
	bool     nofifo_norm_leaky = false;

	bool param_selout  = true;
	bool param_fifomon = true;
	bool param_noregs  = false;
	bool param_rdonly  = false;

	// Hardware config parameters

	// Note: Value for board VC709
	double hwconfig_luts_per_bram18 = 433000 / 1470 / 2;
	// Note: Arbitrary value: authorize 50% more LUT per BRAM than available
	double hwconfig_luts_bram_ratio = 1.5;

	// By default, assume 128-bit PCIe interface
	unsigned hwconfig_writewidth = 128;

	// Negative value means auto
	int      hwconfig_neu_style = -1;
	unsigned hwconfig_lut_threshold = 64;
	bool     hwconfig_use_uram = false;
	// FIXME This parameter is not propagated to HW, need to manually edit VHDL
	// FIXME This parameter could/should also be set on a per-layer basis ?
	// FIXME This parameter is not applied to layers NORM, TER, etc
	bool     hwconfig_bram_opt_speed = false;

	// To perform some evaluations and generation tasks in a more ASIC-friendly mode
	bool hwconfig_asicmode = false;

	// Counters to generate layer identifiers

	private :

	std::vector<unsigned> layers_idx;

	unsigned layers_idxhw  = 0;
	unsigned layers_idxcfg = 0;

	// Layers

	public :

	std::vector<layer_t*> layers;

	// The first and last layers are saved in separate fields because layer insertion may not respect order in vector (temporarily)
	layer_t* layer_first = nullptr;
	layer_t* layer_last  = nullptr;

	// More fields

	static const unsigned CNN_ORIGIN_SCRIPT = 0;
	static const unsigned CNN_ORIGIN_HARDWARE = 1;

	// To indicate if the network is built by script, or if it represents a hardware accelerator
	unsigned param_cnn_origin = CNN_ORIGIN_SCRIPT;

	// Fields for global statistics

	unsigned total_neurons     = 0;
	unsigned total_neurons_phy = 0;
	unsigned total_multipliers = 0;
	unsigned long total_weights = 0;
	unsigned long total_weight_bits = 0;
	unsigned long total_macs = 0;

	// Memory size
	unsigned total_bram18 = 0;
	unsigned total_lutram = 0;
	unsigned total_regs   = 0;

	// Methods

	Layer* layer_new_fromtype(int type_id, char const * type_name = nullptr);
	Layer* layer_new_fromtypename(const char * type_name = nullptr);

	void layer_enqueue(layer_t* layer);
	layer_t* layer_new_enqueue_fromtype(int type_id, const char * type_name = nullptr);

	void clear(void);

	layer_t* getlayer_from_string_id(const char* strid);
	layer_t* getlayer_from_nameidx(const char* type_name, unsigned typeidx);
	layer_t* getlayer_from_hwid(unsigned hwid);

	int propag_params(void);
	int insert_fifos(void);
	void layers_reorder(void);
	int check_integrity(void);

	unsigned long eval_latency(void) const;

	void hwconfig_finalize(void);
	int  load_config_files(void);

	void genvhdl_set_config_regs_numbers(void);
	void genvhdl_cst_decl(FILE* Fo);
	void genvhdl_comp_decl(FILE* Fo);
	void genvhdl_sig_decl(FILE* Fo);
	void genvhdl_regs_setconst(FILE* Fo);
	void genvhdl_regs_setconst_locked(FILE* Fo);
	void genvhdl_comp_inst(FILE* Fo);

};


//============================================
// Functions
//============================================

int declare_builtin_layers(void);

void layer_link(layer_t* layer_prev, layer_t* layer_next);
void layer_insert(layer_t* layer, layer_t* layer_prev, layer_t* layer_next);
void layer_insert_replace(layer_t* layer, layer_t* layer_prev, layer_t* layer_next);

