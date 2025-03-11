
#pragma once

extern "C" {
#include <riffa.h>
}

#include "hwacc_common.h"


class HwAcc_PcieRiffa : public HwAcc_Common {

	//============================================
	// Class fields
	//============================================

	private :

	// Arbitrary snooze to let all data be consumed by the FPGA before affected registers can be queried
	static const unsigned USLEEP_RIFFA_SEND = 15;

	// The Riffa channel to access config registers
	static const unsigned riffa_regs_chan = 0;
	// The Riffa channel for large data transfers
	static const unsigned riffa_data_chan = 1;

	static bool atexit_registered;
	static fpga_info_list fpgalist;

	// Only one instance is allowed for now
	static HwAcc_PcieRiffa* singleton;

	//============================================
	// Class methods
	//============================================

	// The only way of obtaining an HwAcc object for PCIe RIFFA
	// FIXME Future uses will require to use several FPGAs in parallel
	public :
	static HwAcc_PcieRiffa* GetSingleton(void);
	static void CloseSingleton(void);

	private :
	static void riffa_atexit(void);

	//============================================
	// Object fields
	//============================================

	// The FPGA that this object instance controls
	public :
	fpga_t* fpga = nullptr;

	// This tiny buffer is only used for small read/write commands on HW registers
	private :
	uint32_t accregbuf[128];

	//============================================
	// Constructor / Destructor
	//============================================

	private :
	HwAcc_PcieRiffa();

	public :
	~HwAcc_PcieRiffa();

	//============================================
	// Override of virtual methods
	//============================================

	// Access configuration registers
	uint32_t accreg_rd(unsigned idx);
	void     accreg_wr(unsigned idx, uint32_t val);

	// Just perform a dummy read operation for synchronization purposes
	void     accreg_sync_read(void);

	// Streams of data
	unsigned fpga_send32(uint32_t* buf, unsigned buf_nb);
	unsigned fpga_send32_wait(uint32_t* buf, unsigned buf_nb);  // There may be an additional wait to ensure data was processed indeed
	unsigned fpga_recv32(uint32_t* buf, unsigned buf_nb);

	//============================================
	// Methods
	//============================================

	private :
	void riffa_init(void);
	void riffa_close(void);

};

