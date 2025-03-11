
// This file contains utility functions to talk to a hardware accelerator
// Implementation is not specific to a particular HW target

extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>  // For usleep()

#include "nnawaq_utils.h"

}

#include "nn_layers_utils.h"
#include "hwacc_common.h"

using namespace std;


//============================================
// Register mapping
//============================================

// Memory-mapped registers exposed to the software
// Note : Results are implicitly 32b

std::vector< std::vector<LayerRegField> > HwAcc_Common::memregs_fields;

// Register 0
LayerRegField* HwAcc_Common::memreg_acc_n0  = nullptr;
LayerRegField* HwAcc_Common::memreg_acc_n1  = nullptr;
LayerRegField* HwAcc_Common::memreg_ver_min = nullptr;
LayerRegField* HwAcc_Common::memreg_ver_maj = nullptr;
// Register 1
LayerRegField* HwAcc_Common::memreg_layreg  = nullptr;
// Register 2
LayerRegField* HwAcc_Common::memreg_noregs  = nullptr;
LayerRegField* HwAcc_Common::memreg_rdonly  = nullptr;
LayerRegField* HwAcc_Common::memreg_shregs  = nullptr;
LayerRegField* HwAcc_Common::memreg_getregs = nullptr;
LayerRegField* HwAcc_Common::memreg_setregs = nullptr;
LayerRegField* HwAcc_Common::memreg_regs_nb = nullptr;
// Register 3
LayerRegField* HwAcc_Common::memreg_clear   = nullptr;
LayerRegField* HwAcc_Common::memreg_selout  = nullptr;
LayerRegField* HwAcc_Common::memreg_fifomon = nullptr;
LayerRegField* HwAcc_Common::memreg_busy_in = nullptr;
LayerRegField* HwAcc_Common::memreg_busy_out = nullptr;
LayerRegField* HwAcc_Common::memreg_mem_dma = nullptr;
LayerRegField* HwAcc_Common::memreg_addr64  = nullptr;
LayerRegField* HwAcc_Common::memreg_ifwdi   = nullptr;
LayerRegField* HwAcc_Common::memreg_ifwdo   = nullptr;
LayerRegField* HwAcc_Common::memreg_ifpari  = nullptr;
LayerRegField* HwAcc_Common::memreg_ifparo  = nullptr;
LayerRegField* HwAcc_Common::memreg_ifw     = nullptr;
LayerRegField* HwAcc_Common::memreg_freeruni = nullptr;
LayerRegField* HwAcc_Common::memreg_freeruno = nullptr;
// Register 4
LayerRegField* HwAcc_Common::memreg_in_lay  = nullptr;
LayerRegField* HwAcc_Common::memreg_in_par  = nullptr;
// Register 5
LayerRegField* HwAcc_Common::memreg_out_lay = nullptr;
LayerRegField* HwAcc_Common::memreg_out_par = nullptr;
// Register 6
LayerRegField* HwAcc_Common::memreg_in_nb   = nullptr;
// Register 7
LayerRegField* HwAcc_Common::memreg_out_nb  = nullptr;
// Register 8
LayerRegField* HwAcc_Common::memreg_memaddr_in = nullptr;
// Register 9
LayerRegField* HwAcc_Common::memreg_memaddr_out = nullptr;
// Register 10
LayerRegField* HwAcc_Common::memreg_clkcnt = nullptr;
// Register 11
// (unused)
// Register 12
// (unused)
// Register 13
LayerRegField* HwAcc_Common::memreg_rxfifo_in_rdy  = nullptr;
LayerRegField* HwAcc_Common::memreg_rxfifo_in_ack  = nullptr;
LayerRegField* HwAcc_Common::memreg_rxfifo_out_rdy = nullptr;
LayerRegField* HwAcc_Common::memreg_rxfifo_out_ack = nullptr;
LayerRegField* HwAcc_Common::memreg_rxfifo_cnt     = nullptr;
LayerRegField* HwAcc_Common::memreg_txfifo_in_rdy  = nullptr;
LayerRegField* HwAcc_Common::memreg_txfifo_in_ack  = nullptr;
LayerRegField* HwAcc_Common::memreg_txfifo_out_rdy = nullptr;
LayerRegField* HwAcc_Common::memreg_txfifo_out_ack = nullptr;
LayerRegField* HwAcc_Common::memreg_txfifo_cnt     = nullptr;
// Register 14
LayerRegField* HwAcc_Common::memreg_fifo_idx     = nullptr;
LayerRegField* HwAcc_Common::memreg_fifo_nb      = nullptr;
// Register 15
LayerRegField* HwAcc_Common::memreg_fifo_in_rdy  = nullptr;
LayerRegField* HwAcc_Common::memreg_fifo_in_ack  = nullptr;
LayerRegField* HwAcc_Common::memreg_fifo_out_rdy = nullptr;
LayerRegField* HwAcc_Common::memreg_fifo_out_ack = nullptr;
LayerRegField* HwAcc_Common::memreg_fifo_cnt     = nullptr;

// There is one config register between layers, and ensuring there is one at the beginning and one at the end of series of layers
// This register documents the data format of the channel for both previous and next layers (if any)

std::vector<LayerRegField> HwAcc_Common::chanreg_fields;

LayerRegField* HwAcc_Common::chanfield_enc   = nullptr;
LayerRegField* HwAcc_Common::chanfield_wdata = nullptr;
LayerRegField* HwAcc_Common::chanfield_sdata = nullptr;
LayerRegField* HwAcc_Common::chanfield_layer = nullptr;
LayerRegField* HwAcc_Common::chanfield_enmon = nullptr;
LayerRegField* HwAcc_Common::chanfield_fifo  = nullptr;
LayerRegField* HwAcc_Common::chanfield_par   = nullptr;

// Register fields common to all layers

std::vector<LayerRegField> HwAcc_Common::layerreg_fields;

LayerRegField* HwAcc_Common::layerfield_type   = nullptr;
LayerRegField* HwAcc_Common::layerfield_nbregs = nullptr;

void HwAcc_Common::DefineConfigRegs(void) {
	// Call only once
	if(memregs_fields.empty() == false) return;

	// Fields for memory-mapped registers

	memregs_fields.reserve(16);
	std::vector<LayerRegField>* cur_reg = nullptr;

	// Register 0 (read only)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(4);
	memreg_acc_n0  = &LayerRegField::AppendRange(cur_reg, "acc_n0",   7,  0);  // ASCII for character 'N'
	memreg_acc_n1  = &LayerRegField::AppendRange(cur_reg, "acc_n1",  15,  8);  // ASCII for character 'N'
	memreg_ver_min = &LayerRegField::AppendRange(cur_reg, "ver_min", 23, 16);  // Minor version
	memreg_ver_maj = &LayerRegField::AppendRange(cur_reg, "ver_maj", 31, 24);  // Major version
	// Register 1 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(1);
	// Write a register value, read a register value.
	// The chain is shifted if the flag is set in reg 3
	memreg_layreg = &LayerRegField::AppendRange(cur_reg, "layreg", 31,  0);
	// Register 2 (partial read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(6);
	memreg_noregs  = &LayerRegField::AppendBit  (cur_reg, "noregs",       0);  // Flag that indicates if the NN config register are present
	memreg_rdonly  = &LayerRegField::AppendBit  (cur_reg, "rdonly",       1);  // Flag that indicates if the NN config register are read-only
	memreg_shregs  = &LayerRegField::AppendBit  (cur_reg, "shregs",       2);  // Shift enable when reading/writing config registers
	memreg_getregs = &LayerRegField::AppendBit  (cur_reg, "getregs",      3);  // Get config registers (copy regs -> scan chain) - this bit is not written
	memreg_setregs = &LayerRegField::AppendBit  (cur_reg, "setregs",      4);  // Set config registers (copy scan chain -> regs) - this bit is not written
	memreg_regs_nb = &LayerRegField::AppendRange(cur_reg, "regs_nb", 31, 16);  // Number of registers in the scan chain
	// Register 3 (partial read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(14);
	memreg_clear   = &LayerRegField::AppendBit  (cur_reg, "clear",        0);  // Clear all (not written)
	memreg_selout  = &LayerRegField::AppendBit  (cur_reg, "selout",       2);  // Indicate if it is possible to select the output layer
	memreg_fifomon = &LayerRegField::AppendBit  (cur_reg, "fifomon",      3);  // Indicate if it is possible to monitor FIFOs
	memreg_busy_in = &LayerRegField::AppendBit  (cur_reg, "busy_in",      4);  // Busy status for input transfers
	memreg_busy_out = &LayerRegField::AppendBit (cur_reg, "busy_out",     5);  // Busy status for output transfers
	memreg_mem_dma = &LayerRegField::AppendBit  (cur_reg, "mem_dma",      6);  // Indicate if the accelerator can access memory
	memreg_addr64  = &LayerRegField::AppendBit  (cur_reg, "addr64",       7);  // Indicate if the memory DMA supports 64b addresses
	memreg_ifwdi   = &LayerRegField::AppendRange(cur_reg, "ifwdi",   10,  8);  // Width of one input data item in the interface, log base 2 (3b) : 0->7 means 1->128 bits
	memreg_ifwdo   = &LayerRegField::AppendRange(cur_reg, "ifwdo",   13, 11);  // Width of one output data item in the interface, log base 2 (3b) : 0->7 means 1->128 bits
	memreg_ifpari  = &LayerRegField::AppendRange(cur_reg, "ifpari",  19, 14);  // Data parallelism at input of the interface, minus 1 (6b) : 0->63 means 1->64
	memreg_ifparo  = &LayerRegField::AppendRange(cur_reg, "ifparo",  22, 20);  // Data parallelism at output of the interface, minus 1 (3b) : 0->7 means 1->8
	memreg_ifw     = &LayerRegField::AppendRange(cur_reg, "ifw",     27, 23);  // Width of the data interface minus 1, in bytes (5b) : 0->31 means 1->256 bytes
	memreg_freeruni = &LayerRegField::AppendBit (cur_reg, "freerun_in",  30);  // Free run mode, input: send arbitrary data on network input
	memreg_freeruno = &LayerRegField::AppendBit (cur_reg, "freerun_out", 31);  // Free run mode, output: network output is counted and dropped
	// Register 4 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(2);
	memreg_in_lay  = &LayerRegField::AppendRange(cur_reg, "in_lay",  15,  0);  // Target layer for PC->acc transfers, all 1s means frame data
	memreg_in_par  = &LayerRegField::AppendRange(cur_reg, "in_par",  31, 16);  // Parallelism index for PC->acc transfers, when applicable
	// Register 5 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(2);
	memreg_out_lay = &LayerRegField::AppendRange(cur_reg, "out_lay", 15,  0);  // Source layer for acc->PC transfers, all 1s means last layer
	memreg_out_par = &LayerRegField::AppendRange(cur_reg, "out_par", 31, 16);  // Parallelism index for acc->PC transfers, when applicable
	// Register 6 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(1);
	memreg_in_nb = &LayerRegField::AppendRange(cur_reg, "in_nb", 31,  0);  // Number of network inputs to receive/read, start on write event
	// Register 7 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(1);
	memreg_out_nb = &LayerRegField::AppendRange(cur_reg, "out_nb", 31,  0);  // Number of network output values to send/write, start on write event
	// Register 8 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(1);
	memreg_memaddr_in = &LayerRegField::AppendRange(cur_reg, "memaddr_in", 31,  0);  // Memory address to read the inputs from
	// Register 9 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(1);
	memreg_memaddr_out = &LayerRegField::AppendRange(cur_reg, "memaddr_out", 31,  0);  // Memory address to write the outputs to
	// Register 10 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(1);
	memreg_clkcnt = &LayerRegField::AppendRange(cur_reg, "clkcnt", 31,  0);  // Counter of clock cycles, each read produces 32 additional MSB bits
	// Register 11 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(1);
	LayerRegField::AppendRange(cur_reg, "unused", 31,  0);
	// Register 12 (read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(1);
	LayerRegField::AppendRange(cur_reg, "unused", 31,  0);
	// Register 13 (read only)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(10);
	memreg_rxfifo_in_rdy  = &LayerRegField::AppendBit  (cur_reg, "rxfifo_in_rdy",       0);  // RX FIFO : Signal RDY, input side
	memreg_rxfifo_in_ack  = &LayerRegField::AppendBit  (cur_reg, "rxfifo_in_ack",       1);  // RX FIFO : Signal ACK, input side
	memreg_rxfifo_out_rdy = &LayerRegField::AppendBit  (cur_reg, "rxfifo_out_rdy",      2);  // RX FIFO : Signal RDY, output side
	memreg_rxfifo_out_ack = &LayerRegField::AppendBit  (cur_reg, "rxfifo_out_ack",      3);  // RX FIFO : Signal ACK, output side
	memreg_rxfifo_cnt     = &LayerRegField::AppendRange(cur_reg, "rxfifo_cnt",     11,  4);  // RX FIFO : Number of empty slots
	memreg_txfifo_in_rdy  = &LayerRegField::AppendBit  (cur_reg, "txfifo_in_rdy",      16);  // TX FIFO : Signal RDY, input side
	memreg_txfifo_in_ack  = &LayerRegField::AppendBit  (cur_reg, "txfifo_in_ack",      17);  // TX FIFO : Signal ACK, input side
	memreg_txfifo_out_rdy = &LayerRegField::AppendBit  (cur_reg, "txfifo_out_rdy",     18);  // TX FIFO : Signal RDY, output side
	memreg_txfifo_out_ack = &LayerRegField::AppendBit  (cur_reg, "txfifo_out_ack",     19);  // TX FIFO : Signal ACK, output side
	memreg_txfifo_cnt     = &LayerRegField::AppendRange(cur_reg, "txfifo_cnt",     27, 20);  // TX FIFO : Number of occupied slots
	// Register 14 (partial read/write)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(2);
	memreg_fifo_idx = &LayerRegField::AppendRange(cur_reg, "fifo_idx", 15,  0);  // Index of FIFO to monitor
	memreg_fifo_nb  = &LayerRegField::AppendRange(cur_reg, "fifo_nb",  31, 16);  // Number of FIFOs that can be monitored
	// Register 15 (read only)
	memregs_fields.emplace_back();
	cur_reg = &memregs_fields.back();
	cur_reg->reserve(5);
	memreg_fifo_in_rdy  = &LayerRegField::AppendBit  (cur_reg, "fifo_in_rdy",       0);  // Monitored FIFO : Signal RDY, input side
	memreg_fifo_in_ack  = &LayerRegField::AppendBit  (cur_reg, "fifo_in_ack",       1);  // Monitored FIFO : Signal ACK, input side
	memreg_fifo_out_rdy = &LayerRegField::AppendBit  (cur_reg, "fifo_out_rdy",      2);  // Monitored FIFO : Signal RDY, output side
	memreg_fifo_out_ack = &LayerRegField::AppendBit  (cur_reg, "fifo_out_ack",      3);  // Monitored FIFO : Signal ACK, output side
	memreg_fifo_cnt     = &LayerRegField::AppendRange(cur_reg, "fifo_cnt",     11,  4);  // Monitored FIFO : Number of values inside
	// Assign register index into fields
	LayerRegField::AssignRegIdx(memregs_fields);
	LayerRegField::EnsureNoOverlaps(memregs_fields);

	// Fields for inter-layer register

	chanreg_fields.reserve(7);
	chanfield_enc   = &LayerRegField::AppendRange(chanreg_fields, "enc"  ,  5,  0);  // data encoding (6b, default is zero for standard integer)
	chanfield_wdata = &LayerRegField::AppendRange(chanreg_fields, "wdata", 11,  6);  // data width, minus 1 (6b, to represent 1-63)
	chanfield_sdata = &LayerRegField::AppendBit  (chanreg_fields, "sdata",     12);  // flag to indicate data signedness
	chanfield_layer = &LayerRegField::AppendBit  (chanreg_fields, "layer",     13);  // flag to indicate if there is an immediately following layer
	chanfield_enmon = &LayerRegField::AppendBit  (chanreg_fields, "enmon",     14);  // flag to indicate if this channel can be observed (functionality "selout")
	chanfield_fifo  = &LayerRegField::AppendBit  (chanreg_fields, "fifo",      15);  // flag to indicate if this channel is a physical fifo (linked to functionality "fifomon")
	chanfield_par   = &LayerRegField::AppendRange(chanreg_fields, "par",   31, 16);  // parallelism level, minus 1 (16b, to represent 1-65k)
	// Assign register index into fields
	LayerRegField::AssignRegIdx(chanreg_fields, 0);
	LayerRegField::EnsureNoOverlaps(chanreg_fields);

	// Common fields for each layer

	layerreg_fields.reserve(2);
	layerfield_type   = &LayerRegField::AppendRange(layerreg_fields, "type"  ,  7,  0);  // layer type (8b)
	layerfield_nbregs = &LayerRegField::AppendRange(layerreg_fields, "nbregs", 15,  8);  // number of config regs used by the layer (8b)
	// Assign register index into fields
	LayerRegField::AssignRegIdx(layerreg_fields, 0);
	LayerRegField::EnsureNoOverlaps(layerreg_fields);

}


//============================================
// Static members
//============================================

HwAcc_Common* HwAcc_Common::current_hwacc = nullptr;

HwAcc_Common* HwAcc_Common::CurrentHwAcc_GetCheck(void) {
	if(current_hwacc == nullptr) {
		printf("ERROR HwAcc : No HwAcc object to return\n");
		exit(EXIT_FAILURE);
	}
	return current_hwacc;
}


//============================================
// Constructor / destructor
//============================================

HwAcc_Common::HwAcc_Common(void) {
	// All initialization is the init values of fields, already specified

	// Initialize the fields for the config register between layers
	DefineConfigRegs();
}

HwAcc_Common::~HwAcc_Common(void) {
	// Nothing specific to destruct : all object fields have their own destructor
}


//============================================
// Methods
//============================================

// Utility function to print the state of relevant IP registers
void HwAcc_Common::accreg_print_regs(void) {
	printf("Registers of the hardware accelerator:\n");
	printf(" ");
	for(unsigned i=0; i<16; i++) {
		if(i > 0) printf(" ");
		printf("%08x", accreg_rd(i));
	}
	printf("\n");
}

void HwAcc_Common::accreg_cfgnn_read(void) {
	if(accreg_cfgnn_nb == 0) return;
	// Get registers in the scan chain
	accreg_cfgnn_getregs();
	// Enable register shift
	accreg_cfgnn_sh_en();
	if(accreg_get_bool(*memreg_shregs) == false) {
		printf("WARNING HwAcc : Config bit '%s' has not been written by the HW\n", memreg_shregs->name);
	}
	// Read registers
	accreg_cfgnn.resize(accreg_cfgnn_nb);
	for(unsigned i=0; i<accreg_cfgnn_nb; i++) accreg_cfgnn[i] = accreg_cfgnn_pop();
	// Disable register shift
	accreg_cfgnn_sh_dis();
}
void HwAcc_Common::accreg_cfgnn_write(void) {
	if(accreg_cfgnn_nb==0) return;
	// Enable register shift
	accreg_cfgnn_sh_en();
	// Read registers
	for(unsigned i=0; i<accreg_cfgnn_nb; i++) accreg_cfgnn_push(accreg_cfgnn[i]);
	// Disable register shift
	accreg_cfgnn_sh_dis();
	// Set registers from the scan chain
	accreg_cfgnn_setregs();
}
void HwAcc_Common::accreg_cfgnn_print(void) {
	printf("Registers of the network pipeline (number %u):\n", accreg_cfgnn_nb);
	for(unsigned i=0; i<accreg_cfgnn_nb; i++) {
		printf("  0x%08x\n", accreg_cfgnn[i]);
	}
}

// Get IP configuration
void HwAcc_Common::accreg_config_get(void) {
	if(accreg_cfgnn_nb > 0) return;

	// Get the accelerator ID
	// FIXME This assumes fields are indeed in same register
	accreg_id = accreg_rd(memreg_acc_n0->reg_idx);
	// Parse the ID
	char id_n1    = memreg_acc_n0->Get(accreg_id);
	char id_n2    = memreg_acc_n1->Get(accreg_id);
	accreg_id_min = memreg_ver_min->Get(accreg_id);
	accreg_id_maj = memreg_ver_maj->Get(accreg_id);

	if(id_n1!='N' || id_n2!='N') {
		printf("Error HwAcc : Unknown accelerator ID 0x%08x\n", accreg_id);
		exit(EXIT_FAILURE);
	}

	// Check
	if(accreg_ver_cmp(2, 0) < 0) {
		printf("Error HwAcc : Accelerator version %u.%u not handled, too low\n", accreg_id_maj, accreg_id_min);
		exit(EXIT_FAILURE);
	}
	if(accreg_id_maj > 3) {
		printf("Error HwAcc : Accelerator version %u.%u not handled, too high\n", accreg_id_maj, accreg_id_min);
		exit(EXIT_FAILURE);
	}

	// Read the flags
	accreg_wdi     = 1 << accreg_get_unsigned(*memreg_ifwdi);
	accreg_wdo     = 1 << accreg_get_unsigned(*memreg_ifwdo);
	accreg_pari    = 1 + accreg_get_unsigned(*memreg_ifpari);
	accreg_paro    = 1 + accreg_get_unsigned(*memreg_ifparo);
	accreg_selout  = accreg_get_bool(*memreg_selout);
	accreg_fifomon = accreg_get_bool(*memreg_fifomon);

	// Read the status of network config registers
	accreg_noregs   = accreg_get_bool(*memreg_noregs);
	accreg_rdonly   = accreg_get_bool(*memreg_rdonly);
	accreg_cfgnn_nb = accreg_get_unsigned(*memreg_regs_nb);

	accreg_ifw   = 8 * (accreg_get_unsigned(*memreg_ifw) + 1);
	accreg_ifw32 = (accreg_ifw + 31) / 32;

	// Read the NN config registers
	accreg_cfgnn_read();
}

// Print IP configuration
void HwAcc_Common::accreg_config_print(void) {
	printf("HwAcc : Summary of IP configuration:\n");
	printf("  Accelerator ID ........... 0x%08x\n", accreg_id);
	printf("  Accelerator version ...... %u.%u\n", accreg_id_maj, accreg_id_min);
	printf("  Input data width ......... %u\n", accreg_wdi);
	printf("  Input parallelism ........ %u\n", accreg_pari);
	printf("  Output data width ........ %u\n", accreg_wdo);
	printf("  Output parallelism ....... %u\n", accreg_paro);
	printf("  Can select output layer .. %s\n", accreg_selout==true ? "yes" : "no");
	printf("  Can monitor FIFOs ........ %s\n", accreg_fifomon==true ? "yes" : "no");
	printf("  Network regs status ...... %s\n", (accreg_noregs==true) ? "noregs" : (accreg_rdonly==true) ? "read-only" : "writable");
	printf("  Network regs number ...... %u\n", accreg_cfgnn_nb);
}

// Ensure the current network was built from HW registers
// FIXME The variable cnn_origin should also be checked if blind mode is not active
void HwAcc_Common::check_have_hwacc(Network* network) {
	if(accreg_cfgnn_nb == 0 || network->layers.size() == 0) {
		printf("Error HwAcc : Trying to perform an operation without a network from a Hardware Accelerator\n");
		exit(EXIT_FAILURE);
	}
}


//============================================
// Building network from HW registers
//============================================

void Layer::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	// Nothing to configure
}
void Layer::config_from_regs_id2layer(const std::vector<uint32_t>& accreg_cfgnn, map<unsigned, Layer*>& map_hwid_to_prev_layer) {
	// Nothing to configure
}

void LayerWin::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb >= regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	layer->win_dwconv = regfield_dwconv->Get(r);
	layer->win_sym_xy = regfield_symxy->Get(r);
	layer->win_repeat = regfield_repeat->Get(r);
	// Reg 1
	r = accreg_cfgnn[i++];
	layer->fx      = regfield_fx->Get(r);
	layer->fx_max  = regfield_fx_max->Get(r);
	// Reg 3
	r = accreg_cfgnn[i++];
	layer->fz      = regfield_fz->Get(r);
	layer->fz_max  = regfield_fz_max->Get(r);
	// Reg 4
	r = accreg_cfgnn[i++];
	layer->stepx   = regfield_stepx->Get(r);
	layer->winx    = regfield_winx->Get(r);
	layer->begpadx = regfield_padx->Get(r);
	layer->nwinx   = regfield_nwinx->Get(r);
	// Reg 6
	r = accreg_cfgnn[i++];
	layer->nwinz   = layer->fz;  // FIXME
	layer->win_par_oz = regfield_par_oz->Get(r);

	// Get the Y fields
	if(layer->win_sym_xy == false) {
		// Reg 5
		r = accreg_cfgnn[i++];
		layer->fy      = regfield_fy->Get(r);
		layer->fy_max  = regfield_fy_max->Get(r);
		// Reg 6
		r = accreg_cfgnn[i++];
		layer->stepy   = regfield_stepy->Get(r);
		layer->winy    = regfield_winy->Get(r);
		layer->begpady = regfield_pady->Get(r);
		layer->nwiny   = regfield_nwiny->Get(r);
	}
	else {
		// Set Y fields from X fields
		layer->fy      = layer->fx;
		layer->fy_max  = layer->fx_max;
		layer->stepy   = layer->stepx;
		layer->winy    = layer->winx;
		layer->begpady = layer->begpadx;
		layer->nwiny   = layer->nwinx;
	}

	// Apply parallelism multiplier
	layer->fz     *= layer->win_par_oz;
	layer->fz_max *= layer->win_par_oz;
	layer->nwinz  *= layer->win_par_oz;

}

void LayerNeu::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb != regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	layer->win_dwconv   = regfield_dwconv->Get(r);
	layer->neu_time_mux = regfield_tmux->Get(r);

	// Reg 1
	r = accreg_cfgnn[i++];
	layer->fsize       = regfield_fsize->Get(r);
	layer->fsize_max   = regfield_fsize_max->Get(r);

	// Reg 2
	r = accreg_cfgnn[i++];
	layer->neurons     = regfield_neu->Get(r);
	layer->neurons_max = regfield_neu_max->Get(r);

	// Reg 3
	r = accreg_cfgnn[i++];
	layer->neu_per_bram = regfield_nperblk->Get(r) + 1;
	layer->neu_wrnb     = regfield_wrnb->Get(r) + 1;
	layer->neu_wweight  = regfield_wweight->Get(r) + 1;
	layer->neu_sgnd = 0;
	if(regfield_sdlock->Get(r)) layer->neu_sgnd |= NEUSGN_LOCKED;
	if(regfield_sdata->Get(r))  layer->neu_sgnd |= NEUSGN_SIGNED;
	else layer->neu_sgnd |= NEUSGN_UNSIGNED;
	layer->neu_sgnd |= NEUSGN_VALID;
	layer->neu_style    = regfield_style->Get(r);
	// Fields for custom multiplication operations
	layer->neu_custom_mul_id = regfield_mul_id->Get(r);
	layer->neu_custom_mul    = (layer->neu_custom_mul_id != 0);
	// Weight signedness
	layer->neu_sgnd = 0;
	if(regfield_sweight->Get(r)) layer->neu_sgnw |= NEUSGN_SIGNED;
	layer->neu_sgnw |= NEUSGN_LOCKED;
	layer->neu_sgnw |= NEUSGN_VALID;

	// Checks
	if(layer->neu_per_bram == 0) {
		printf("Error layer %u, neuron type : neu_per_bram = 0\n", (unsigned)network->layers.size());
		exit(EXIT_FAILURE);
	}

	// Apply time multiplexing
	layer->neurons     *= layer->neu_time_mux;
	layer->neurons_max *= layer->neu_time_mux;

	// Apply parallelism
	layer->fsize       *= layer->split_in;
	layer->fsize_max   *= layer->split_in;
	layer->neurons     *= layer->split_out;
	layer->neurons_max *= layer->split_out;
	// Protection against uninitialized registers
	if(layer->fsize==0 || layer->fsize > layer->fsize_max) layer->fsize = layer->fsize_max;
	if(layer->neurons==0 || layer->neurons > layer->neurons_max) layer->neurons = layer->neurons_max;

}

void LayerNeu_CM::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb != regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	layer->win_dwconv   = regfield_dwconv->Get(r);
	layer->neu_time_mux = regfield_tmux->Get(r);

	// Reg 1
	r = accreg_cfgnn[i++];
	layer->fsize       = regfield_fsize->Get(r);
	layer->fsize_max   = regfield_fsize_max->Get(r);

	// Reg 2
	r = accreg_cfgnn[i++];
	layer->neurons     = regfield_neu->Get(r);
	layer->neurons_max = regfield_neu_max->Get(r);

	// Reg 3
	r = accreg_cfgnn[i++];
	layer->neu_per_bram = regfield_nperblk->Get(r) + 1;
	layer->neu_wrnb     = regfield_wrnb->Get(r) + 1;
	layer->neu_wweight  = regfield_wweight->Get(r) + 1;
	layer->neu_sgnd = 0;
	if(regfield_sdlock->Get(r)) layer->neu_sgnd |= NEUSGN_LOCKED;
	if(regfield_sdata->Get(r))  layer->neu_sgnd |= NEUSGN_SIGNED;
	else layer->neu_sgnd |= NEUSGN_UNSIGNED;
	layer->neu_sgnd |= NEUSGN_VALID;
	layer->neu_style    = regfield_style->Get(r);
	// Fields for custom multiplication operations
	layer->neu_custom_mul_id = regfield_mul_id->Get(r);
	layer->neu_custom_mul    = (layer->neu_custom_mul_id != 0);
	// Weight signedness
	layer->neu_sgnd = 0;
	if(regfield_sweight->Get(r)) layer->neu_sgnw |= NEUSGN_SIGNED;
	layer->neu_sgnw |= NEUSGN_LOCKED;
	layer->neu_sgnw |= NEUSGN_VALID;

	// Checks
	if(layer->neu_per_bram == 0) {
		printf("Error layer %u, neuron type : neu_per_bram = 0\n", (unsigned)network->layers.size());
		exit(EXIT_FAILURE);
	}

	// Apply time multiplexing
	layer->neurons     *= layer->neu_time_mux;
	layer->neurons_max *= layer->neu_time_mux;

	// Apply parallelism
	layer->fsize       *= layer->split_in;
	layer->fsize_max   *= layer->split_in;
	layer->neurons     *= layer->split_out;
	layer->neurons_max *= layer->split_out;
	// Protection against uninitialized registers
	if(layer->fsize==0 || layer->fsize > layer->fsize_max) layer->fsize = layer->fsize_max;
	if(layer->neurons==0 || layer->neurons > layer->neurons_max) layer->neurons = layer->neurons_max;

}

void LayerPool::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb > regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	layer->pool_type     = regfield_type->Get(r);
	layer->round_nearest = regfield_rndnear->Get(r);
	bool have_avg_reg    = regfield_avgreg->Get(r);

	// Reg 1
	r = accreg_cfgnn[i++];
	layer->fsize = regfield_fsize->Get(r);

	// Reg 2
	if(have_avg_reg) {
		r = accreg_cfgnn[i++];
		layer->pool_avg_mult = regfield_mul->Get(r);
		layer->pool_avg_shr  = regfield_shr->Get(r);
	}

	// Checks
	if(
		layer->pool_type != POOL_TYPE_MAX &&
		layer->pool_type != POOL_TYPE_MIN &&
		layer->pool_type != POOL_TYPE_ADD &&
		layer->pool_type != POOL_TYPE_AVG
	) {
		printf("Error HwAcc : Layer of type %s created from register %u has invalid pooling type %u\n", layer->get_type_id2nameu(layer->pool_type), i, layer->pool_type);
		exit(EXIT_FAILURE);
	}

}

void LayerNorm::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb != regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	int bias_en       = regfield_enbias->Get(r);
	int mul_en        = regfield_enmul->Get(r);
	// Width of run-time bias
	layer->norm_wbias = (bias_en == 0) ? 0 : (regfield_wbias->Get(r) + 1);
	// Width of run-time multiplier
	layer->norm_wmul = (mul_en == 0) ? 0 : (regfield_wmul->Get(r) + 1);
	// Width of run-time shift
	layer->norm_wshr = regfield_wshr->Get(r);

	// Reg 1
	r = accreg_cfgnn[i++];
	layer->fsize     = regfield_fsize->Get(r);
	layer->fsize_max = regfield_fsize_max->Get(r);
	// Apply parallelism
	layer->fsize     *= layer->split_in;
	layer->fsize_max *= layer->split_in;
	// Protection against uninitialized registers
	if(layer->fsize==0 || layer->fsize > layer->fsize_max) layer->fsize = layer->fsize_max;

	// Reg 3
	r = accreg_cfgnn[i++];
	layer->norm_mul_cst = regfield_cstmul->Get(r);
	layer->norm_shr_cst = regfield_cstshr->Get(r);
	layer->round_nearest = regfield_rndtype->Get(r);  // 0 : truncate or unspecified, 1 : nearest

}

void LayerTernarize::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb != regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	layer->ter_out_static = regfield_out_static->Get(r);

	// Reg 1
	r = accreg_cfgnn[i++];
	layer->fsize     = regfield_fsize->Get(r);
	layer->fsize_max = regfield_fsize_max->Get(r);
	// Apply parallelism
	layer->fsize     *= layer->split_in;
	layer->fsize_max *= layer->split_in;
	// Protection against uninitialized registers
	if(layer->fsize==0 || layer->fsize > layer->fsize_max) layer->fsize = layer->fsize_max;

}

void LayerRelu::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb != regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	// (no extra fields)

	// Reg 1
	r = accreg_cfgnn[i++];
	layer->relu_min = regfield_thmin->Get(r);

	// Reg 2
	r = accreg_cfgnn[i++];
	layer->relu_max = regfield_thmax->Get(r);

}

void LayerLeaky::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb != regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	// (no extra fields)

	// Reg 1
	r = accreg_cfgnn[i++];
	layer->leaky_min = regfield_thmin->Get(r);

	// Reg 2
	r = accreg_cfgnn[i++];
	layer->leaky_max = regfield_thmax->Get(r);

}

void LayerCustom::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb != regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	layer->custom_user_id = regfield_func_id->Get(r);

}

void LayerFork::config_from_regs_id2layer(const std::vector<uint32_t>& accreg_cfgnn, map<unsigned, Layer*>& map_hwid_to_prev_layer) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb <= regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	unsigned layers_nb = regfield_layers_nb->Get(r);

	// Next layers
	unsigned cur_inreg_nb = 0;
	for(unsigned i = 0; i < layers_nb; i++) {
		if(cur_inreg_nb == 0) r = accreg_cfgnn[i++];
		unsigned next_id = (r >> (cur_inreg_nb * 16)) & 0xFFFF;
		map_hwid_to_prev_layer[next_id] = layer;
		cur_inreg_nb = (cur_inreg_nb + 1) % 2;
	}
}

void LayerCat::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb <= regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	unsigned layers_nb = regfield_layers_nb->Get(r);

	// Source layers
	unsigned cur_inreg_nb = 0;
	for(unsigned i = 0; i < layers_nb; i++) {
		if(cur_inreg_nb == 0) r = accreg_cfgnn[i++];
		unsigned source_id = (r >> (cur_inreg_nb * 16)) & 0xFFFF;
		Layer* layer_prev = network->getlayer_from_hwid(source_id);
		if(layer_prev == nullptr) abort();
		if(layer_prev->next_is_arr == true || layer->prev_is_arr == true) {
			layer_link(layer_prev, layer);
		}
		else {
			printf("Error: Layer %s%u is not a valid previous layer for layer %s%u\n", layer_prev->typenamel, layer_prev->typeidx, layer->typenamel, layer->typeidx);
			abort();
		}
		cur_inreg_nb = (cur_inreg_nb + 1) % 2;
	}
}

void LayerSoftMax::config_from_regs(const std::vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned i = layer->regs_idx;

	// Number of registers
	if(layer->regs_nb != regs_fields.size()) abort();

	// Reg 0
	r = accreg_cfgnn[i++];
	layer->fsize     = regfield_fsize->Get(r);
	layer->fsize_max = layer->fsize;  // No max is really necessary for this component

	// Apply parallelism
	layer->fsize     *= layer->split_in;
	layer->fsize_max *= layer->split_in;

}

int HwAcc_Common::build_network_internal(Network* network) {

	// Clear any existing NN
	network->clear();

	if(accreg_cfgnn_nb==0) {
		printf("Error HwAcc : No configuration registers to build a network from\n");
		exit(EXIT_FAILURE);
	}

	// This map stored IDs of next layers, needed because these have not yet been created
	map<unsigned, Layer*> map_hwid_to_prev_layer;

	// Scan all config registers
	for(unsigned i=1; i<accreg_cfgnn_nb; ) {

		// Get the register that documents the data channel
		uint32_t reg_in = accreg_cfgnn[i++];

		// Create a FIFO layer to represent physical FIFOs
		if(chanfield_fifo->Get(reg_in) != 0) {
			Layer* layer_fifo = network->layer_new_enqueue_fromtype(LAYER_FIFO);
			propag_params_layer(network, layer_fifo);
		}

		// If this is the end of the series of layers, the next register also describes a channel
		if(chanfield_layer->Get(reg_in) == 0) continue;

		// Create the actual layer
		uint32_t reg_layer = accreg_cfgnn[i];
		unsigned type = layerfield_type->Get(reg_layer);
		if(type==LAYER_NONE) {
			printf("Error HwAcc : Invalid layer type %u from config register %u value 0x%08x\n", type, i, reg_layer);
			exit(EXIT_FAILURE);
		}
		Layer* layer = network->layer_new_fromtype(type);
		if(layer==nullptr) {
			printf("Error HwAcc : Could not create a layer of type %u from config register %u value 0x%08x\n", type, i, reg_layer);
			exit(EXIT_FAILURE);
		}

		// Apply input channel parameters
		layer->wdata    = chanfield_wdata->Get(reg_in);
		layer->sdata    = chanfield_sdata->Get(reg_in);
		layer->split_in = chanfield_par->Get(reg_in);

		// Get the number of layer-specific fields
		layer->regs_idx = i;
		layer->regs_nb = layerfield_nbregs->Get(reg_layer);

		// The number of registers actually used by this layer is specified in the first register
		// This must not be overwritten by the per-layer config method
		i += layer->regs_nb;

		// Apply output channel parameters
		uint32_t reg_out = accreg_cfgnn[i];
		layer->out_sdata = chanfield_sdata->Get(reg_out);
		layer->out_wdata = chanfield_wdata->Get(reg_out);
		layer->split_out = chanfield_par->Get(reg_out);

		// Parse layer-specific fields
		// This must be done after setting in/out details
		layer->config_from_regs(accreg_cfgnn);

		// In case there is an array of next layers, index them in the map
		// FIXME This would not work for target layers CAT where there are more than one FORK layer in prev layers
		if(layer->next_is_arr == true) {
			layer->config_from_regs_id2layer(accreg_cfgnn, map_hwid_to_prev_layer);
		}

		// Connect
		if(layer->prev_is_arr == true) {
			if(layer->arr_layers.size() == 0) {
				printf("Error HwAcc : Layer %s%u has no prev layer\n", layer->typenameu, layer->typeidx);
				exit(EXIT_FAILURE);
			}
		}
		else if(layer->prev == nullptr) {
			auto find_res = map_hwid_to_prev_layer.find(layer->id);
			if(find_res != map_hwid_to_prev_layer.end()) {
				Layer* layer_prev = find_res->second;
				if(layer_prev->next_is_arr == true || layer->prev_is_arr == true) {
					layer_link(layer_prev, layer);
				}
				else {
					printf("Error: Layer %s%u is not a valid previous layer for layer %s%u\n", layer_prev->typenamel, layer_prev->typeidx, layer->typenamel, layer->typeidx);
					abort();
				}
			}
			else {
				network->layer_enqueue(layer);
			}
		}
		network->layer_last = layer;

		// Propagate parameters
		propag_params_layer(network, layer);

	}  // Scan all config registers

	// Get last parameters
	network->param_win = network->layer_first->wdata;
	network->param_sin = network->layer_first->sdata;
	network->param_inpar = network->layer_first->split_in;

	// Enforce parameters if network has read-only config
	if(accreg_rdonly == true) {

		// For network with read-only parameters : get the image size from first WIN layer
		for(auto layer : network->layers) {
			if(layer->type == LAYER_FIFO) continue;
			if(layer->type == LAYER_WIN) {
				printf("HwAcc : Setting network frame size from layer %s%u to %u %u %u\n",
					layer->typenamel, layer->typeidx,
					layer->fx, layer->fy, layer->fz
				);
				network->param_fx = layer->fx;
				network->param_fy = layer->fy;
				network->param_fz = layer->fz;
			}
			break;
		}

	}

	// Set the number of neurons of the output layer
	if(accreg_rdonly == false) {
		if(network->cnn_outneu > 0) {
			apply_outneu(network, network->cnn_outneu);
		}
	}

	return 0;
}

void HwAcc_Common::build_network(Network* network) {

	// Ensure hardware is initialized
	accreg_config_get();

	// Ensure there is no network
	if(network->layers.size() != 0) {
		printf("Error HwAcc : A network is already built\n");
		exit(EXIT_FAILURE);
	}

	build_network_internal(network);
	network->param_cnn_origin = Network::CNN_ORIGIN_HARDWARE;

	network->param_selout  = accreg_selout;
	network->param_fifomon = accreg_fifomon;
	network->param_noregs  = accreg_noregs;
	network->param_rdonly  = accreg_rdonly;

	network->hwconfig_writewidth = accreg_ifw;

	// FIXME Check integrity of the network connectivity

}


//============================================
// Write config registers to HW accelerator
//============================================

void Layer::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	// Nothing to write
}

void LayerWin::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;

	// Reg 0
	r = accreg_cfgnn[layer->regs_idx + 0];
	regfield_dwconv->SetRef(r, layer->win_dwconv);
	regfield_symxy->SetRef(r, layer->win_sym_xy);
	regfield_repeat->SetRefVerbose(r, layer->win_repeat);
	accreg_cfgnn[layer->regs_idx + 0] = r;

	// Reg 1
	r = accreg_cfgnn[layer->regs_idx + 1];
	regfield_fx->SetRefVerbose(r, layer->fx);
	accreg_cfgnn[layer->regs_idx + 1] = r;

	// Reg 2
	r = accreg_cfgnn[layer->regs_idx + 2];
	regfield_fz->SetRefVerbose(r, layer->fz / layer->win_par_oz);
	accreg_cfgnn[layer->regs_idx + 2] = r;

	// Reg 3
	r = accreg_cfgnn[layer->regs_idx + 3];
	regfield_stepx->SetRefVerbose(r, layer->stepx);
	regfield_stepx->SetRefVerbose(r, layer->winx);
	regfield_padx->SetRefVerbose (r, layer->begpadx);
	regfield_nwinx->SetRefVerbose(r, layer->nwinx);
	accreg_cfgnn[layer->regs_idx + 3] = r;

	// Reg 4
	r = accreg_cfgnn[layer->regs_idx + 4];
	regfield_nwinz->SetRefVerbose(r, layer->fz / layer->win_par_oz);
	regfield_par_oz->SetRefVerbose(r, layer->win_par_oz);
	accreg_cfgnn[layer->regs_idx + 4] = r;

	// Reg 5
	r = accreg_cfgnn[layer->regs_idx + 5];
	regfield_fy->SetRefVerbose(r, layer->fy);
	accreg_cfgnn[layer->regs_idx + 5] = r;

	// Reg 6
	r = accreg_cfgnn[layer->regs_idx + 6];
	regfield_stepy->SetRefVerbose(r, layer->stepy);
	regfield_stepy->SetRefVerbose(r, layer->winy);
	regfield_pady->SetRefVerbose (r, layer->begpady);
	regfield_nwiny->SetRefVerbose(r, layer->nwiny);
	accreg_cfgnn[layer->regs_idx + 6] = r;

}

void LayerNeu::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned fsize = (layer->fsize + layer->split_in - 1) / layer->split_in;
	unsigned nbneu = (layer->neurons + layer->split_out - 1) / layer->split_out / neu_time_mux;

	// Reg 0
	r = accreg_cfgnn[layer->regs_idx + 0];
	regfield_dwconv->SetRef(r, win_dwconv);
	regfield_tmux->SetRefVerbose(r, neu_time_mux);
	accreg_cfgnn[layer->regs_idx + 0] = r;

	// Reg 1
	r = accreg_cfgnn[layer->regs_idx + 1];
	regfield_fsize->SetRefVerbose(r, fsize);
	accreg_cfgnn[layer->regs_idx + 1] = r;

	// Reg 2
	r = accreg_cfgnn[layer->regs_idx + 2];
	regfield_neu->SetRefVerbose(r, nbneu);
	accreg_cfgnn[layer->regs_idx + 2] = r;

	// Reg 3
	if((layer->neu_sgnd & NEUSGN_LOCKED) == 0) {
		r = accreg_cfgnn[layer->regs_idx + 3];
		regfield_sdata->SetRef(r, layer->neu_sgnd & NEUSGN_SIGNED ? 1 : 0);
		accreg_cfgnn[layer->regs_idx + 3] = r;
	}

}

void LayerNeu_CM::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned fsize = (layer->fsize + layer->split_in - 1) / layer->split_in;
	unsigned nbneu = (layer->neurons + layer->split_out - 1) / layer->split_out / neu_time_mux;

	// Reg 0
	r = accreg_cfgnn[layer->regs_idx + 0];
	regfield_dwconv->SetRef(r, win_dwconv);
	regfield_tmux->SetRefVerbose(r, neu_time_mux);
	accreg_cfgnn[layer->regs_idx + 0] = r;

	// Reg 1
	r = accreg_cfgnn[layer->regs_idx + 1];
	regfield_fsize->SetRefVerbose(r, fsize);
	accreg_cfgnn[layer->regs_idx + 1] = r;

	// Reg 2
	r = accreg_cfgnn[layer->regs_idx + 2];
	regfield_neu->SetRefVerbose(r, nbneu);
	accreg_cfgnn[layer->regs_idx + 2] = r;

	// Reg 3
	if((layer->neu_sgnd & NEUSGN_LOCKED) == 0) {
		r = accreg_cfgnn[layer->regs_idx + 3];
		regfield_sdata->SetRef(r, layer->neu_sgnd & NEUSGN_SIGNED ? 1 : 0);
		accreg_cfgnn[layer->regs_idx + 3] = r;
	}

}

void LayerPool::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;

	// Reg 1
	r = accreg_cfgnn[layer->regs_idx + 1];
	regfield_fsize->SetRefVerbose(r, layer->fsize);
	accreg_cfgnn[layer->regs_idx + 1] = r;

}

void LayerNorm::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned fsize = (layer->fsize + layer->split_in - 1) / layer->split_in;

	// Reg 1
	r = accreg_cfgnn[layer->regs_idx + 1];
	regfield_fsize->SetRefVerbose(r, fsize);
	accreg_cfgnn[layer->regs_idx + 1] = r;

}

void LayerTernarize::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned fsize = (layer->fsize + layer->split_in - 1) / layer->split_in;

	// Reg 1
	r = accreg_cfgnn[layer->regs_idx + 1];
	regfield_fsize->SetRefVerbose(r, fsize);
	accreg_cfgnn[layer->regs_idx + 1] = r;

}

void LayerRelu::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;

	// Reg 1
	r = accreg_cfgnn[layer->regs_idx + 1];
	regfield_thmin->SetRefVerbose(r, layer->relu_min);
	accreg_cfgnn[layer->regs_idx + 1] = r;

	// Reg 2
	r = accreg_cfgnn[layer->regs_idx + 2];
	regfield_thmax->SetRefVerbose(r, layer->relu_max);
	accreg_cfgnn[layer->regs_idx + 2] = r;

}

void LayerLeaky::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;

	// Reg 1
	r = accreg_cfgnn[layer->regs_idx + 1];
	regfield_thmin->SetRefVerbose(r, layer->leaky_min);
	accreg_cfgnn[layer->regs_idx + 1] = r;

	// Reg 2
	r = accreg_cfgnn[layer->regs_idx + 2];
	regfield_thmax->SetRefVerbose(r, layer->leaky_max);
	accreg_cfgnn[layer->regs_idx + 2] = r;

}

void LayerSoftMax::write_config_regs(vector<uint32_t>& accreg_cfgnn) {
	Layer* layer = this;
	uint32_t r = 0;
	unsigned fsize = (layer->fsize + layer->split_in - 1) / layer->split_in;

	// Reg 0
	r = accreg_cfgnn[layer->regs_idx + 0];
	regfield_fsize->SetRefVerbose(r, fsize);
	accreg_cfgnn[layer->regs_idx + 0] = r;

}

int HwAcc_Common::write_config_regs(Network* network) {
	auto& layers = network->layers;

	// Ensure the design is based on Harwdare Accelerator design
	check_have_hwacc(network);

	// Scan all layers
	for(auto layer : layers) {
		layer->write_config_regs(accreg_cfgnn);
	}

	// Commit these changes into the accelerator
	accreg_cfgnn_write();

	return 0;
}


//============================================
// Print the status of all FIFOs
//============================================

void HwAcc_Common::print_fifos(Network* network) {
	auto& layers = network->layers;

	// Ensure hardware is initialized
	accreg_config_get();

	// Check that FIFO monitoring is enabled
	if(accreg_fifomon==false) {
		printf("Error: The accelerator does not have the FIFO monitoring option\n");
		exit(EXIT_FAILURE);
	}

	// No need to build the network in order to know the number of FIFOs
	// FIXME the number of fifos may not be equal to layers+1 in the near future
	if(param_hw_blind == false) {
		// If no network is built, try to build it from HW registers
		if(layers.size() == 0) {
			build_network(network);
		}
	}
	// Ensure the network is built
	check_have_hwacc(network);

	// Scan all FIFOs
	unsigned fifos_nb = accreg_get_fifo_nb();
	for(unsigned i=0; i<fifos_nb; i++) {
		accreg_set_fifosel(i);
		usleep(15);  // FIXME This should be target-dependent
		unsigned r = accreg_rd(memreg_fifo_cnt->reg_idx);  // FIXME Assume that all fields of interest are in the same register
		printf("Fifo %02u in rdy/ack %u/%u out rdy/ack %u/%u cnt %u\n", i,
			HwAcc_Common::memreg_fifo_in_rdy->Get(r),
			HwAcc_Common::memreg_fifo_in_ack->Get(r),
			HwAcc_Common::memreg_fifo_out_rdy->Get(r),
			HwAcc_Common::memreg_fifo_out_ack->Get(r),
			HwAcc_Common::memreg_fifo_cnt->Get(r)
		);
	}
}


//============================================
// Print the core HW latency in clock cycles
//============================================

void HwAcc_Common::eval_latency(Network* network, unsigned num_out) {
	auto& layers = network->layers;

	// Ensure hardware is initialized
	accreg_config_get();

	// No need to build the network in order to know the number of FIFOs
	// FIXME the number of fifos may not be equal to layers+1 in the near future
	if(param_hw_blind == false) {
		// If no network is built, try to build it from HW registers
		if(layers.size() == 0) {
			build_network(network);
		}
	}
	// Ensure the network is built
	check_have_hwacc(network);

	// Clear any previous freerun flag
	accreg_freerun_in_clear();
	accreg_freerun_out_clear();
	accreg_clear_latency();
	// Clear the pipeline contents
	accreg_clear();
	while(accreg_get_clear()) ;
	// Set the number of desired outputs
	accreg_set_nboutputs(num_out);
	// Launch latency measurement
	accreg_freerun_out_set();
	accreg_freerun_in_set();

	// FIXME Wait until busy flag is set (need proper HW support)
	usleep(100*1000);
	uint64_t cnt64 = accreg_get_latency();
	cnt64 += ((uint64_t)accreg_get_latency()) << 32;
	accreg_freerun_out_clear();
	accreg_freerun_in_clear();

	// Report
	printf("Latency : %" PRIu64 " clock cycles\n", cnt64);

	// Report when the desired number of outputs was not reached
	unsigned num_out_get = accreg_get_nboutputs();
	if(num_out_get < num_out) {
		printf("Warning : Expected %u outputs but only got %u (hardcoded timeout of 100 ms)\n", num_out, num_out_get);
	}

	// Clear the hardware pipeline
	accreg_clear();
}


//============================================
// Print the core HW latency in clock cycles
//============================================

void HwAcc_Common::eval_clkfreq(Network* network, unsigned num_ms) {

	// Ensure hardware is initialized
	accreg_config_get();

	// No need to have a network built in order to evaluate the frequency

	// Clear any previous freerun flag
	accreg_freerun_in_clear();
	accreg_freerun_out_clear();
	accreg_clear_latency();
	// Clear the pipeline contents
	accreg_clear();
	while(accreg_get_clear()) ;
	// Set the number of desired outputs
	accreg_set_nboutputs(~0);
	// Launch latency measurement
	accreg_freerun_out_set();
	accreg_freerun_in_set();

	// Wait the required amount of time and measure just one word of clock counter
	usleep(num_ms*1000);
	uint64_t cnt64 = accreg_get_latency();
	cnt64 += ((uint64_t)accreg_get_latency()) << 32;
	accreg_freerun_out_clear();
	accreg_freerun_in_clear();

	// Report
	printf("Got %" PRIu64 " clock cycles in %u milliseconds\n", cnt64, num_ms);
	unsigned freqHz = (cnt64 * 1000) / num_ms;
	printf("Frequency : %u Hz (%u MHz)\n", freqHz, freqHz / 1000000);

	// Clear the hardware pipeline
	accreg_clear();
}

