
#pragma once

extern "C" {

#include <stdint.h>
#include <stdbool.h>

}

#include <vector>

#include "hw_reg_fields.h"


// Object that represents one HW target
// This is a virtual class, has to be inherited to provide the implementation of methods

class HwAcc_Common {

	//============================================
	// Static methods
	//============================================

	private:

	// The current HwAcc object in use, unique at any given time
	static HwAcc_Common* current_hwacc;

	public:

	static inline HwAcc_Common* CurrentHwAcc_Get(void) { return current_hwacc; }
	static HwAcc_Common* CurrentHwAcc_GetCheck(void);

	static inline void CurrentHwAcc_Set(HwAcc_Common* hwacc) { current_hwacc = hwacc; }
	static inline HwAcc_Common* CurrentHwAcc_GetAndSet(HwAcc_Common* hwacc) {
		HwAcc_Common* prev = current_hwacc;
		current_hwacc = hwacc;
		return prev;
	}

	//============================================
	// Fields common to all HW targets
	//============================================

	public:

	unsigned  accreg_id       = 0;
	unsigned  accreg_id_maj   = 0;
	unsigned  accreg_id_min   = 0;

	unsigned  accreg_ifw      = 0;
	unsigned  accreg_ifw32    = 0;  // Number of 32b words for one interface data sample
	unsigned  accreg_wdi      = 0;
	unsigned  accreg_wdo      = 0;
	unsigned  accreg_pari     = 0;
	unsigned  accreg_paro     = 0;

	bool      accreg_noregs   = false;
	bool      accreg_rdonly   = false;
	unsigned  accreg_cfgnn_nb = 0;

	bool      accreg_selout   = false;
	bool      accreg_fifomon  = false;

	// The vector of config registers
	std::vector<uint32_t> accreg_cfgnn;

	//============================================
	// Fields in layer-specific config registers
	//============================================

	// Memory-mapped registers exposed to the software

	static std::vector< std::vector<LayerRegField> > memregs_fields;

	// Register 0
	static LayerRegField* memreg_acc_n0;
	static LayerRegField* memreg_acc_n1;
	static LayerRegField* memreg_ver_min;
	static LayerRegField* memreg_ver_maj;
	// Register 1
	static LayerRegField* memreg_layreg;
	// Register 2
	static LayerRegField* memreg_noregs;
	static LayerRegField* memreg_rdonly;
	static LayerRegField* memreg_shregs;
	static LayerRegField* memreg_getregs;
	static LayerRegField* memreg_setregs;
	static LayerRegField* memreg_regs_nb;
	// Register 3
	static LayerRegField* memreg_clear;
	static LayerRegField* memreg_selout;
	static LayerRegField* memreg_fifomon;
	static LayerRegField* memreg_busy_in;
	static LayerRegField* memreg_busy_out;
	static LayerRegField* memreg_mem_dma;
	static LayerRegField* memreg_addr64;
	static LayerRegField* memreg_ifwdi;
	static LayerRegField* memreg_ifwdo;
	static LayerRegField* memreg_ifpari;
	static LayerRegField* memreg_ifparo;
	static LayerRegField* memreg_ifw;
	static LayerRegField* memreg_freeruni;
	static LayerRegField* memreg_freeruno;
	// Register 4
	static LayerRegField* memreg_in_lay;
	static LayerRegField* memreg_in_par;
	// Register 5
	static LayerRegField* memreg_out_lay;
	static LayerRegField* memreg_out_par;
	// Register 6
	static LayerRegField* memreg_in_nb;
	// Register 7
	static LayerRegField* memreg_out_nb;
	// Register 8
	static LayerRegField* memreg_memaddr_in;
	// Register 9
	static LayerRegField* memreg_memaddr_out;
	// Register 10
	static LayerRegField* memreg_clkcnt;
	// Register 11
	// (unused)
	// Register 12
	// (unused)
	// Register 13
	static LayerRegField* memreg_rxfifo_in_rdy;
	static LayerRegField* memreg_rxfifo_in_ack;
	static LayerRegField* memreg_rxfifo_out_rdy;
	static LayerRegField* memreg_rxfifo_out_ack;
	static LayerRegField* memreg_rxfifo_cnt;
	static LayerRegField* memreg_txfifo_in_rdy;
	static LayerRegField* memreg_txfifo_in_ack;
	static LayerRegField* memreg_txfifo_out_rdy;
	static LayerRegField* memreg_txfifo_out_ack;
	static LayerRegField* memreg_txfifo_cnt;
	// Register 14
	static LayerRegField* memreg_fifo_idx;
	static LayerRegField* memreg_fifo_nb;
	// Register 15
	static LayerRegField* memreg_fifo_in_rdy;
	static LayerRegField* memreg_fifo_in_ack;
	static LayerRegField* memreg_fifo_out_rdy;
	static LayerRegField* memreg_fifo_out_ack;
	static LayerRegField* memreg_fifo_cnt;

	// Register between layers to document the channel

	static std::vector<LayerRegField> chanreg_fields;

	static LayerRegField* chanfield_enc;
	static LayerRegField* chanfield_wdata;
	static LayerRegField* chanfield_sdata;
	static LayerRegField* chanfield_layer;
	static LayerRegField* chanfield_enmon;
	static LayerRegField* chanfield_fifo;
	static LayerRegField* chanfield_par;

	// Common register fields to all layers

	static std::vector<LayerRegField> layerreg_fields;

	static LayerRegField* layerfield_type;
	static LayerRegField* layerfield_nbregs;

	// Method to initialize the register mappings
	static void DefineConfigRegs(void);

	//============================================
	// Constructor / destructor
	//============================================

	HwAcc_Common(void);

	// The destructor is virtual because this is a base class to be inherited
	virtual ~HwAcc_Common(void);

	//============================================
	// Virtual methods
	//============================================

	// Directly access memory-mapped registers
	virtual uint32_t accreg_rd(unsigned idx) { return 0; }
	virtual void     accreg_wr(unsigned idx, uint32_t val) {}

	// Just perform a dummy read operation for synchronization purposes, just target a read-only register
	virtual void     accreg_sync_read(void) { accreg_rd(memreg_acc_n0->reg_idx); }

	// Streams of data
	virtual unsigned fpga_send32(uint32_t* buf, unsigned buf_nb) { return 0; }
	virtual unsigned fpga_send32_wait(uint32_t* buf, unsigned buf_nb) { return 0; }  // There may be an additional wait to ensure data was processed indeed
	virtual unsigned fpga_recv32(uint32_t* buf, unsigned buf_nb) { return 0; }

	//============================================
	// Utility macros
	//============================================

	inline int accreg_ver_cmp(unsigned maj, unsigned min) const {
		if(accreg_id_maj != maj) return (int)accreg_id_maj - (int)(maj);
		return (int)accreg_id_min - (int)(min);
	}

	// FIXME Instead of accessing the HW accelerator for every read/write, we could operate on a local vector of registers and later commit, according to a 'dirty' register

	// These helper methods access the appropriate register and field
	inline bool     accreg_get_bool    (const LayerRegField& field) { return field.GetUnsigned(accreg_rd(field.reg_idx)) == 1 ? true : false; }
	inline unsigned accreg_get_unsigned(const LayerRegField& field) { return field.GetUnsigned(accreg_rd(field.reg_idx)); }
	inline int      accreg_get_signed  (const LayerRegField& field) { return field.GetSigned(accreg_rd(field.reg_idx)); }

	// These helper methods access the appropriate register and field
	inline void accreg_set(const LayerRegField& field, unsigned v)         { uint32_t r = accreg_rd(field.reg_idx); field.SetRef(r, v); accreg_wr(field.reg_idx, r); }
	inline int  accreg_set_verbose(const LayerRegField& field, unsigned v) { uint32_t r = accreg_rd(field.reg_idx); int z = field.SetRefVerbose(r, v); accreg_wr(field.reg_idx, r); return z; }

	inline void accreg_set_wmode1(unsigned i) { accreg_set_verbose(*memreg_in_lay, i); }
	inline void accreg_set_wmode2(unsigned i) { accreg_set_verbose(*memreg_in_par, i); }
	inline void accreg_set_wmode_frame(void)  { accreg_set(*memreg_in_lay, ~0); }

	inline void accreg_set_recv1(unsigned i) { accreg_set_verbose(*memreg_out_lay, i); }
	inline void accreg_set_recv2(unsigned i) { accreg_set_verbose(*memreg_out_par, i); }
	inline void accreg_set_recv_out(void)    { accreg_set(*memreg_out_lay, ~0); }

	inline void accreg_clear(void)             { accreg_set(*memreg_clear, true); }
	inline bool accreg_get_clear(void)         { return accreg_get_bool(*memreg_clear); }

	inline void accreg_freerun_in_set(void)    { accreg_set(*memreg_freeruni, true); }
	inline void accreg_freerun_in_clear(void)  { accreg_set(*memreg_freeruni, false); }
	inline void accreg_freerun_out_set(void)   { accreg_set(*memreg_freeruno, true); }
	inline void accreg_freerun_out_clear(void) { accreg_set(*memreg_freeruno, false); }

	inline void     accreg_cfgnn_sh_en(void)      { accreg_set(*memreg_shregs, true); }
	inline void     accreg_cfgnn_sh_dis(void)     { accreg_set(*memreg_shregs, false); }
	inline void     accreg_cfgnn_getregs(void)    { accreg_set(*memreg_getregs, true); }
	inline void     accreg_cfgnn_setregs(void)    { accreg_set(*memreg_setregs, true); }

	inline uint32_t accreg_cfgnn_pop(void)        { return accreg_rd(memreg_layreg->reg_idx); }
	inline void     accreg_cfgnn_push(uint32_t v) { accreg_wr(memreg_layreg->reg_idx, v); }

	inline void     accreg_set_nbinputs(unsigned nb) { accreg_wr(6, nb); }
	inline unsigned accreg_get_nbinputs(void)        { return accreg_rd(6); }

	inline void     accreg_set_nboutputs(unsigned nb) { accreg_wr(7, nb); }
	inline unsigned accreg_get_nboutputs(void)        { return accreg_rd(7); }

	inline uint32_t accreg_get_latency(void)       { return accreg_rd(10); }
	inline void     accreg_clear_latency(void)     { accreg_wr(10, 0); }

	inline unsigned accreg_get_rxfifo_cnt(void) { return accreg_get_unsigned(*memreg_rxfifo_cnt); }
	inline unsigned accreg_get_txfifo_cnt(void) { return accreg_get_unsigned(*memreg_txfifo_cnt); }

	inline unsigned accreg_get_fifosel(void)       { return accreg_get_unsigned(*memreg_fifo_idx); }
	inline void     accreg_set_fifosel(unsigned i) { accreg_set_verbose(*memreg_fifo_idx, i); }

	inline unsigned accreg_get_fifo_nb(void)       { return accreg_get_unsigned(*memreg_fifo_nb); }

	//============================================
	// Methods
	//============================================

	void accreg_print_regs(void);

	void accreg_cfgnn_read(void);
	void accreg_cfgnn_write(void);
	void accreg_cfgnn_print(void);

	void accreg_config_get(void);
	void accreg_config_print(void);

	void check_have_hwacc(Network* network);

	private :
	int  build_network_internal(Network* network);

	public :
	void build_network(Network* network);
	int  write_config_regs(Network* network);
	void print_fifos(Network* network);
	void eval_latency(Network* network, unsigned num_out);
	void eval_clkfreq(Network* network, unsigned num_ms);

	//============================================
	// Methods for using the HW accelerator
	//============================================

	// FIXME It may be convenient to have one Network instance owned by the HwAcc object
	// Or create the HwAcc instance with a reference to an external Network object

	int write_layer_config(layer_t* layer);
	int write_config(Network* network);

	// These methods should be private, but for now need to be public to be called from extrernal thread function
	void* getoutputs_thread(layer_t* layer, unsigned frames_nb);
	int write_frames_inout(const char* filename, layer_t* inlayer, layer_t* outlayer, layer_t* last_layer);

	int write_frames(Network* network, const char* filename);

	void run(Network* network);

};

