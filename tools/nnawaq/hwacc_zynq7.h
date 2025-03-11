
#pragma once

extern "C" {
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
}

#include "hwacc_common.h"


class HwAcc_Zynq7 : public HwAcc_Common {

	//============================================
	// Class fields
	//============================================

	public :

	// Default is base address of AXI GP0 port
	static const unsigned ADDR_AXI_GP0 = 0x40000000;

	private :

	// FIXME Arbitrary snooze to let all data be consumed by the FPGA before affected registers can be queried
	static const unsigned USLEEP_RIFFA_SEND = 15;


	// The file descriptor to access memory
	int fd_mem = -1;

	// The bare-metal address where the accelerator is expected to be found
	intptr_t hwacc_addr = 0;
	// The virtual address of the page that maps to the desired hardware
	unsigned page_size = 0;
	void*    mmap_base = nullptr;
	// The virtual address the maps to the desired hardware
	uint32_t* hwacc_ptr_regs32 = nullptr;

	static bool atexit_registered;

	// Only one instance is allowed for now
	// FIXME Actually we want only one instance of HwAcc_Common*
	static HwAcc_Zynq7* singleton;

	//============================================
	// Class methods
	//============================================

	// The only way of obtaining an HwAcc object for Zynq7
	public :
	static HwAcc_Zynq7* GetSingleton(intptr_t addr = ADDR_AXI_GP0);
	static void CloseSingleton(void);

	private :
	static void zynq7_atexit(void);

	//============================================
	// Constructor / Destructor
	//============================================

	private :
	HwAcc_Zynq7(intptr_t addr);

	public :
	~HwAcc_Zynq7();

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
	void zynq7_init(intptr_t addr);
	void zynq7_close(void);

};

