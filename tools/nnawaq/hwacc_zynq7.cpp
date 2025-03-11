
// This file contains utility functions to talk to the FPGA peripheral from AXI port GP0

extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>  // For usleep()
#include <errno.h>  // For errno

// For access to memory
#include <sys/mman.h>
#include <fcntl.h>

#include "nnawaq_utils.h"

}  // extern "C"

#include "nn_layers_utils.h"
#include "hwacc_zynq7.h"

using namespace std;


//============================================
// Class fields
//============================================

bool HwAcc_Zynq7::atexit_registered = false;

HwAcc_Zynq7* HwAcc_Zynq7::singleton = nullptr;

// The only way of obtaining an HwAcc object for Zynq7
HwAcc_Zynq7* HwAcc_Zynq7::GetSingleton(intptr_t addr) {
	if(singleton == nullptr) {
		singleton = new HwAcc_Zynq7(addr);
	}
	return singleton;
}
void HwAcc_Zynq7::CloseSingleton(void) {
	if(singleton != nullptr) {
		delete singleton;
	}
	singleton = nullptr;
}

void HwAcc_Zynq7::zynq7_atexit(void) {
	if(singleton == nullptr) return;
	delete singleton;  // FIXME nontrivial destructor may have undefined behaviour, says g++
	singleton = nullptr;  // FIXME future uses of several FPGAs at the same time will conflict with widespread singleton
}


//============================================
// Constructor / Destructor
//============================================

HwAcc_Zynq7::HwAcc_Zynq7(intptr_t addr) {
	zynq7_init(addr);
}

HwAcc_Zynq7::~HwAcc_Zynq7(void) {
	zynq7_close();
	if(this == singleton) singleton = nullptr;
}


//============================================
// Methods
//============================================

void HwAcc_Zynq7::zynq7_init(intptr_t addr) {
	if(mmap_base != nullptr) return;

	// Check address validity
	if(addr == 0) {
		printf("Zynq7 Error: Unsupported address 0x%" PRIxPTR "\n", addr);
		exit(EXIT_FAILURE);
	}
	hwacc_addr = addr;

	// Open memory
	fd_mem = open("/dev/mem", O_RDWR);
	if(fd_mem < 1) {
		printf("Zynq7 Error: Could not open /dev/mem\n");
		perror(nullptr);
		exit(EXIT_FAILURE);
	}

	// Memory mapping of the device into virtual memory
	page_size = sysconf(_SC_PAGESIZE);
	intptr_t page_addr = (addr & (~(page_size-1)));
	unsigned page_offset = addr - page_addr;
	mmap_base = mmap(nullptr, page_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd_mem, page_addr);
	if(mmap_base == MAP_FAILED) {
		int errno_saved = errno;
		printf("System error %d : %s\n", errno_saved, strerror(errno_saved));
		printf("Zynq7 Error: Could not perform memory map at address 0x%" PRIxPTR "\n", addr);
		exit(EXIT_FAILURE);
	}
	hwacc_ptr_regs32 = (uint32_t*)((char*)(mmap_base) + page_offset);

	printf("Successful memory mapping to access hardware at address 0x%" PRIxPTR "\n", addr);
	fflush(stdout);

	// Register the close function
	singleton = this;
	if(atexit_registered == false) {
		atexit(zynq7_atexit);
		atexit_registered = true;
	}
}

void HwAcc_Zynq7::zynq7_close() {
	munmap(mmap_base, page_size);
	close(fd_mem);
	mmap_base = nullptr;
	hwacc_ptr_regs32 = nullptr;
}

// These methods override the virtual methods

// Access configuration registers
uint32_t HwAcc_Zynq7::accreg_rd(unsigned reg) {
	return hwacc_ptr_regs32[reg];
}
void HwAcc_Zynq7::accreg_wr(unsigned reg, uint32_t v) {
	hwacc_ptr_regs32[reg] = v;
}

// Just perform a dummy read operation for synchronization purposes
void HwAcc_Zynq7::accreg_sync_read(void) {
	// Just read a read-only register
	accreg_rd(memreg_acc_n0->reg_idx);
}

// Don't use bursts transfers because the hardware does not support it
// FIXME Maybe a config bit would be useful for that
#define BURSTS_ENABLE false
// Apply max burst size of 16 values, for AXI3 - FIXME a config field would be useful for that
#define BURSTS_MAX 16u

// Streams of data
// Read/Write anywhere in the slave registers 64 to 127 result in push/pop to/from RX and TX fifos
unsigned HwAcc_Zynq7::fpga_send32(uint32_t* buf, unsigned buf_nb) {
	unsigned rem_nb = buf_nb;
	unsigned res_nb = 0;
	unsigned total_sleep_us = 0;
	while(rem_nb > 0) {
		unsigned len = std::min(rem_nb, accreg_get_rxfifo_cnt());
		if(len == 0) {
			if(param_timeout_send_us > 0 && total_sleep_us > param_timeout_send_us) {
				break;
			}
			usleep(100);  // FIXME Arbitrary sleep
			total_sleep_us += 1000;
			continue;
		}
		total_sleep_us = 0;
		if(BURSTS_ENABLE == true) {
			len = std::min(len, BURSTS_MAX);
			memcpy(hwacc_ptr_regs32+64, buf, len*sizeof(*buf));
		}
		else {
			// Don't use burst transfers
			for(unsigned i=0; i<len; i++) ((volatile uint32_t*)hwacc_ptr_regs32)[64] = buf[i];
		}
		buf += len;
		rem_nb -= len;
		res_nb += len;
	}
	return res_nb;
}

unsigned HwAcc_Zynq7::fpga_send32_wait(uint32_t* buf, unsigned buf_nb) {
	// No need to wait after transfers because interaction with the slave is with the same interface and is in-order
	return fpga_send32(buf, buf_nb);
}

unsigned HwAcc_Zynq7::fpga_recv32(uint32_t* buf, unsigned buf_nb) {
	unsigned rem_nb = buf_nb;
	unsigned res_nb = 0;
	unsigned total_sleep_us = 0;
	while(rem_nb > 0) {
		unsigned len = std::min(rem_nb, accreg_get_txfifo_cnt());
		if(len == 0) {
			if(param_timeout_send_us > 0 && total_sleep_us > param_timeout_send_us) {
				break;
			}
			usleep(100);  // FIXME Arbitrary sleep
			total_sleep_us += 1000;
			continue;
		}
		total_sleep_us = 0;
		if(BURSTS_ENABLE == true) {
			len = std::min(len, BURSTS_MAX);
			memcpy(buf, hwacc_ptr_regs32+64, len*sizeof(*buf));
		}
		else {
			// Don't use burst transfers
			for(unsigned i=0; i<len; i++) buf[i] = ((volatile uint32_t*)hwacc_ptr_regs32)[64];
		}
		buf += len;
		rem_nb -= len;
		res_nb += len;
	}
	return res_nb;
}

