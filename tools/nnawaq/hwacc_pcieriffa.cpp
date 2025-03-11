
// This file contains utility functions to talk to the FPGA board connected with PCI-Express
// Data transfers are handled with the RIFFA driver and library

extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>  // For usleep()

#include <riffa.h>

#include "nnawaq_utils.h"

}  // extern "C"

#include "nn_layers_utils.h"
#include "hwacc_pcieriffa.h"

using namespace std;


//============================================
// Class fields
//============================================

bool HwAcc_PcieRiffa::atexit_registered = false;

fpga_info_list HwAcc_PcieRiffa::fpgalist;  // Warning : This is a C struct, no init provided

HwAcc_PcieRiffa* HwAcc_PcieRiffa::singleton = nullptr;

// The only way of obtaining an HwAcc object for PCIe RIFFA
HwAcc_PcieRiffa* HwAcc_PcieRiffa::GetSingleton(void) {
	if(singleton == nullptr) {
		singleton = new HwAcc_PcieRiffa();
	}
	return singleton;
}
void HwAcc_PcieRiffa::CloseSingleton(void) {
	if(singleton != nullptr) {
		delete singleton;
	}
	singleton = nullptr;
}

void HwAcc_PcieRiffa::riffa_atexit(void) {
	if(singleton == nullptr) return;
	delete singleton;  // FIXME nontrivial destructor may have undefined behaviour, says g++
	singleton = nullptr;  // FIXME future uses of several FPGAs at the same time will conflict with widespread singleton
}


//============================================
// Constructor / Destructor
//============================================

HwAcc_PcieRiffa::HwAcc_PcieRiffa(void) {
	riffa_init();
}

HwAcc_PcieRiffa::~HwAcc_PcieRiffa(void) {
	riffa_close();
	if(this == singleton) singleton = nullptr;
}


//============================================
// Methods
//============================================

void HwAcc_PcieRiffa::riffa_init() {
	if(fpga != NULL) return;
	// Populate the fpga_info_list struct
	if (fpga_list(&fpgalist) != 0) {
		printf("RIFFA Error: Can't detect FPGAs. Is the RIFFA driver running?\n");
		exit(EXIT_FAILURE);
	}
	if(fpgalist.num_fpgas!=1) {
		printf("RIFFA Error: %u FPGA(s) detected, need exactly 1.\n", fpgalist.num_fpgas);
		exit(EXIT_FAILURE);
	}
	if(fpgalist.num_chnls[0] < 2) {
		printf("RIFFA Error: The FPGA has %u channels, need exactly 2.\n", fpgalist.num_chnls[0]);
		exit(EXIT_FAILURE);
	}
	// Get the device with id
	fpga = fpga_open(0);
	if(fpga == nullptr) {
		printf("RIFFA Error: Could not open the FPGA\n");
		exit(EXIT_FAILURE);
	}
	printf("RIFFA : FPGA found\n");
	// Register the Riffa close function
	singleton = this;
	if(atexit_registered == false) {
		atexit(riffa_atexit);
		atexit_registered = true;
	}
}

void HwAcc_PcieRiffa::riffa_close() {
	if(fpga != NULL) fpga_close(fpga);
	fpga = nullptr;
}

// These methods override the virtual methods

// FIXME Config regs : Change reg usage for chan 0 to read/write config regs :
//   receive 1 word = read operation, 2 words = write operation
//   and drop the limitation of missing support for 32b interface

// Access configuration registers
uint32_t HwAcc_PcieRiffa::accreg_rd(unsigned reg) {
	accregbuf[0] = reg | ((uint32_t)1 << 30);
	fpga_send(fpga, riffa_regs_chan, accregbuf, 1, 0, 1, param_timeout_regs_us / 1000);
	fpga_recv(fpga, riffa_regs_chan, accregbuf, 1, param_timeout_regs_us / 1000);
	return accregbuf[0];
}
void HwAcc_PcieRiffa::accreg_wr(unsigned reg, uint32_t v) {
	accregbuf[0] = reg | ((uint32_t)1 << 31);
	accregbuf[1] = v;
	fpga_send(fpga, riffa_regs_chan, accregbuf, 2, 0, 1, param_timeout_regs_us / 1000);
}

// Just perform a dummy read operation for synchronization purposes
void HwAcc_PcieRiffa::accreg_sync_read(void) {
	// FIXME This seems needed to have wmode1 correctly taken into account before the large data transfer arrives through chan1
	// Perform this wait before anything else in case the issue comes from getting out of reset
	// This issue is not understood
	usleep(50);
	// Just read a read-only register
	accreg_rd(memreg_acc_n0->reg_idx);
}

// The max size for each send() transfer
// The parameter len is signed so the max must be representable on 31b
// The max is also a multiple of the interface width, for extra safety
#define TRANSFER_MAX_LEN 0x7FFFFFF0u
// To use an artificially small transfer size (debug purpose only)
//#define TRANSFER_MAX_LEN 1000u

// To set the bit "last" to 1 for each transfer (debug purpose only)
#define SEND_SPLIT_LAST_SET 1

// Streams of data
unsigned HwAcc_PcieRiffa::fpga_send32(uint32_t* buf, unsigned buf_nb) {

	// Split super-large transfers into smaller transfers that the driver can handle
	unsigned rem_nb = buf_nb;
	unsigned res_nb = 0;

	while(rem_nb > 0) {
		unsigned len = GetMin(rem_nb, TRANSFER_MAX_LEN);
		int last = (len == rem_nb) || SEND_SPLIT_LAST_SET;
		// Send the smaller buffer to the FPGA
		int z = fpga_send(fpga, riffa_data_chan, buf, len, 0, last, param_timeout_send_us / 1000);
		if(z < (int)len) break;
		buf += len;
		rem_nb -= len;
		res_nb += len;
	}

	return res_nb;
}

unsigned HwAcc_PcieRiffa::fpga_send32_wait(uint32_t* buf, unsigned buf_nb) {
	// Send the big buffer to the FPGA, through the Riffa channel for large data transfers
	unsigned z = fpga_send32(buf, buf_nb);
	// Wait loop
	usleep(USLEEP_RIFFA_SEND);
	return z;
}

unsigned HwAcc_PcieRiffa::fpga_recv32(uint32_t* buf, unsigned buf_nb) {

	// Split super-large transfers into smaller transfers that the driver can handle
	unsigned rem_nb = buf_nb;
	unsigned res_nb = 0;

	while(rem_nb > 0) {
		unsigned len = GetMin(rem_nb, TRANSFER_MAX_LEN);
		// Send the smaller buffer to the FPGA
		int z = fpga_recv(fpga, riffa_data_chan, buf, len, param_timeout_recv_us / 1000);
		if(z < (int)len) break;
		buf += len;
		rem_nb -= len;
		res_nb += len;
	}

	return res_nb;
}

