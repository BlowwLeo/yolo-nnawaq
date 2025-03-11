
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
#include "estimasic.h"

using namespace std;



// Global verbosity

unsigned asic_verbose = 0;
unsigned asic_verbose_printmem = 0;

// ASIC technology parameters

char const * techno_desc = NULL;

// Parameters to estimate area with the formula : F * (h + H) * (w + W)
// Where F, H and W are constants and the result is in um2
double spram_area_F = 0;
double spram_area_H = 0;
double spram_area_W = 0;
// Ditto but for read power, in uW/MHz
double spram_read_F = 0;
double spram_read_H = 0;
double spram_read_W = 0;
// Ditto but for write power
double spram_write_F = 0;
double spram_write_H = 0;
double spram_write_W = 0;

double ff_area     = 0;  // mm2 per FF
double ffwe_area   = 0;  // mm2 per FF with write enable
double ff_ener     = 0;  // J per switch

double inv_area    = 0;  // mm2
double inv_ener    = 0;  // J per switch

double and2_area   = 0;  // mm2
double and2_ener   = 0;  // J per switch

double xor2_area   = 0;  // mm2
double xor2_ener   = 0;  // J per switch

// Half adder
double hadd_area   = 0;  // mm2
double hadd_ener   = 0;  // J per switch
// Full adder
double fadd_area   = 0;  // mm2
double fadd_ener   = 0;  // J per switch

// TCAM-inspired neuron
double   tcamneu_area_mb   = 0;    // mm2 per Mb
double   tcamneu_ener      = 0;    // J per search  FIXME This includes 25% toggle rate
double   tcamneu_scale     = 1.1;  // Scaling factor to take control + periphery into account
unsigned tcamneu_blkheight = 500;  // Maximum number of lines in one block (zero means no limit)
unsigned tcamneu_blkwidth  = 0;    // Maximum frame size of one block (zero means no limit)
double   tcam_disch_time   = 0.10e-9;  // Time for one cell to discharge its own ML section

static double spram_estim_area_mm2(unsigned words, unsigned width) {
	double um2 = spram_area_F * (words + spram_area_H) * (width + spram_area_W);
	return um2 / 1e6;  // Conversion um2 -> mm2
}
static double spram_estim_ener_read(unsigned words, unsigned width) {
	double power = spram_read_F * (words + spram_read_H) * (width + spram_read_W);
	return power / 1e6 / 1e6;  // Conversion uW/MHz -> J
}
static double spram_estim_ener_write(unsigned words, unsigned width) {
	double power = spram_write_F * (words + spram_write_H) * (width + spram_write_W);
	return power / 1e6 / 1e6;  // Conversion uW/MHz -> J
}

void select_techno_st_ll_10() {
	techno_desc = "ST LL 1.0V";

	// FIXME This is SRAM data from techno 0.9V

	// The memory cuts are compiled for high density with no internal mux
	// Area in um2, add shrink of 0.81 to convert 32nm to 28nm (according to ST docs)
	spram_area_F = 0.16 * 0.81;
	spram_area_H = 335;
	spram_area_W = 10.7;
	// Read power in uW/MHz, for 100% read
	spram_read_F = 4.4e-5;
	spram_read_H = 1650;
	spram_read_W = 8.2;
	// Write power in uW/MHz, for 100% write
	spram_write_F = 3.8e-5;
	spram_write_H = 1350;
	spram_write_W = 23;

	// FIXME
	ff_area     = 2.6 * 1e-6;  // mm2 per FF
	ffwe_area   = 3.6 * 1e-6;  // mm2 per FF with write enable
	ff_ener     = 17e-9 / 1e6;  // J per switch

	inv_area    = 0.33 * 1e-6;  // mm2
	inv_ener    = 790e-12 / 1e6;  // J per switch

	// FIXME This is for 0.9V
	and2_area   = 0.65 * 1e-6;  // mm2
	and2_ener   = 1.98e-9 / 1e6;  // J per switch

	xor2_area   = 1.5 * 1e-6;  // mm2
	xor2_ener   = 3.54e-9 / 1e6;  // J per switch

	// Half adder
	hadd_area   = 1.45 * 1e-6;  // mm2
	hadd_ener   = 4.0e-9 / 1e6;  // J per switch
	// Full adder
	fadd_area   = 2.6 * 1e-6;  // mm2
	fadd_ener   = 5.4e-9 / 1e6;  // J per switch FIXME

	// FIXME TCAM data is just phony value just to run

	// TCAM-inspired neuron
	tcamneu_area_mb = 0.6;  // mm2 per Mb
	//tcamneu_area_mb = 0.02;  // mm2 per Mb - for PCM, phase-change memory, non volatile
	tcamneu_ener = 1e-15;  // J per search
}

void select_techno_st_ll_09() {
	techno_desc = "ST LL 0.9V";

	// The memory cuts are compiled for high density with no internal mux
	// Area in um2, add shrink of 0.81 to convert 32nm to 28nm (according to ST docs)
	spram_area_F = 0.16 * 0.81;
	spram_area_H = 335;
	spram_area_W = 10.7;
	// Read power in uW/MHz, for 100% read
	spram_read_F = 4.4e-5;
	spram_read_H = 1650;
	spram_read_W = 8.2;
	// Write power in uW/MHz, for 100% write
	spram_write_F = 3.8e-5;
	spram_write_H = 1350;
	spram_write_W = 23;

	ff_area     = 4.08 * 1e-6;  // mm2 per FF
	ffwe_area   = 5.71 * 1e-6;  // mm2 per FF with write enable
	ff_ener     = 6.64e-9 / 1e6;  // J per switch

	inv_area    = 0.32 * 1e-6;  // mm2
	inv_ener    = 0.406e-9 / 1e6;  // J per switch

	and2_area   = 0.65 * 1e-6;  // mm2
	and2_ener   = 1.98e-9 / 1e6;  // J per switch

	xor2_area   = 1.45 * 1e-6;  // mm2
	xor2_ener   = 3.14e-9 / 1e6;  // J per switch

	// Half adder
	hadd_area   = 1.47 * 1e-6;  // mm2
	hadd_ener   = 2.44e-9 / 1e6;  // J per switch
	// Full adder
	fadd_area   = 2.61 * 1e-6;  // mm2
	fadd_ener   = 3.27e-9 / 1e6;  // J per switch

	// FIXME Missing TCAM power
}

void select_techno_st_ll_06() {
	// FIXME Missing SRAM power + all area + TCAM
	techno_desc = "ST LL 0.6V FIXME";
	ff_ener   = 3.600e-9 / 1e6;  // J per switch
	inv_ener  = 0.121e-9 / 1e6;  // J per switch
	xor2_ener = 0.900e-9 / 1e6;  // J per switch
	fadd_ener = 0.930e-9 / 1e6;  // J per switch
}

// Some macro-parameters
double logic_freq   = 500e6;
double toggle_rate  = 0.5;

// Select fifo as SRAM+counters or shift register
unsigned fifo_depth = 8;
bool fifo_is_shift = false;
bool fifo_is_regfile = false;

bool tcam_rec_rom  = false;
bool tcam_rec_sram = false;



double estimasic_total_area(asic_estim_t* estim) {
	return estim->sram_area + estim->gate_area + estim->tcamneu_area;
}
double estimasic_total_ener(asic_estim_t* estim) {
	return estim->sram_ener + estim->gate_ener + estim->tcamneu_ener;
}

asic_estim_t* estimasic_getdata(layer_t* layer) {
	asic_estim_t* estim = (asic_estim_t*)layer->ptrdata;
	if(estim==NULL) {
		estim = (asic_estim_t*)calloc(1, sizeof(*estim));
		layer->ptrdata = estim;
	}
	return estim;
}

void estimasic_addhw(asic_estim_t* estim_dst, asic_estim_t* estim_src) {
	estim_dst->sram_bits    += estim_src->sram_bits;
	estim_dst->sram_area    += estim_src->sram_area;
	estim_dst->sram_ener    += estim_src->sram_ener;
	estim_dst->gate_area    += estim_src->gate_area;
	estim_dst->gate_ener    += estim_src->gate_ener;
	estim_dst->tcamneu_bits += estim_src->tcamneu_bits;
	estim_dst->tcamneu_area += estim_src->tcamneu_area;
	estim_dst->tcamneu_ener += estim_src->tcamneu_ener;
}

// Print results

#define PRINTESTIM_WITHTCAM 0x01

void print_estim(char const * title, asic_estim_t* estim, int flags) {
	if(estimasic_total_area(estim)==0 && estimasic_total_ener(estim)==0) return;
	printf("| %-12s |", title);
	if(flags & PRINTESTIM_WITHTCAM) {
		printf(" %12u | %12.6f | %12.6f |", estim->tcamneu_bits, estim->tcamneu_area, estim->tcamneu_ener * 1e6);
	}
	printf(" %12u | %12.6f | %12.6f |", estim->sram_bits, estim->sram_area, estim->sram_ener * 1e6);
	printf(" %12.6f | %12.6f |", estim->gate_area, estim->gate_ener * 1e6);
	printf(" %12.6f | %12.6f |",
		estim->tcamneu_area + estim->sram_area + estim->gate_area,
		(estim->tcamneu_ener + estim->sram_ener + estim->gate_ener) * 1e6
	);
	printf("\n");
}
void print_hline(unsigned firstwidth, unsigned width, unsigned nbcol) {
	printf("+");
	for(unsigned i=0; i<firstwidth; i++) printf("-");
	printf("+");
	for(unsigned k=1; k<nbcol; k++) {
		for(unsigned i=0; i<width; i++) printf("-");
		printf("+");
	}
	printf("\n");
}

void print_table_layers(vector<layer_t*>& layers, asic_estim_t* hwestim_types) {

	// Auto-detect if print of TCAM is needed
	int flags = 0;
	bool tcam_not_zero = false;
	for(auto layer : layers) {
		asic_estim_t* estim = estimasic_getdata(layer);
		if(estim->tcamneu_area > 0 || estim->tcamneu_ener > 0) { tcam_not_zero = true; break; }
	}
	if(tcam_not_zero==true) flags |= PRINTESTIM_WITHTCAM;

	// Build the list of columns
	char const * arrtitle[16];
	unsigned nbcol = 0;
	arrtitle[nbcol++] = " ";
	if(flags & PRINTESTIM_WITHTCAM) {
		arrtitle[nbcol++] = "TCAM bits";
		arrtitle[nbcol++] = "TCAM mm2";
		arrtitle[nbcol++] = "TCAM uJ";
	}
	arrtitle[nbcol++] = "SRAM bits";
	arrtitle[nbcol++] = "SRAM mm2";
	arrtitle[nbcol++] = "SRAM uJ";
	arrtitle[nbcol++] = "Gates mm2";
	arrtitle[nbcol++] = "Gates uJ";
	arrtitle[nbcol++] = "Total mm2";
	arrtitle[nbcol++] = "Total uJ";

	// Print the table
	print_hline(14, 14, nbcol);
	printf("| %12s |", arrtitle[0]);
	for(unsigned i=1; i<nbcol; i++) printf(" %12s |", arrtitle[i]);
	printf("\n");
	print_hline(14, 14, nbcol);
	for(auto layer : layers) {
		asic_estim_t* estim = estimasic_getdata(layer);
		char buf[100];
		sprintf(buf, "%s%u", layer->typenamel, layer->typeidx);
		print_estim(buf, estim, flags);
	}
	print_hline(14, 14, nbcol);
	print_estim("Total", hwestim_types + 0, flags);
	print_hline(14, 14, nbcol);
}
void print_table(asic_estim_t* hwestim_types) {
	// Auto-detect if print of TCAM is needed
	int flags = 0;
	bool tcam_not_zero = false;
	for(unsigned i=1; i<=LAYER_TYPE_MAX; i++) {
		asic_estim_t* estim_type = hwestim_types + i;
		if(estim_type->tcamneu_area > 0 || estim_type->tcamneu_ener > 0) { tcam_not_zero = true; break; }
	}
	if(tcam_not_zero==true) flags |= PRINTESTIM_WITHTCAM;

	// Build the list of columns
	char const * arrtitle[16];
	unsigned nbcol = 0;
	arrtitle[nbcol++] = " ";
	if(flags & PRINTESTIM_WITHTCAM) {
		arrtitle[nbcol++] = "TCAM bits";
		arrtitle[nbcol++] = "TCAM mm2";
		arrtitle[nbcol++] = "TCAM uJ";
	}
	arrtitle[nbcol++] = "SRAM bits";
	arrtitle[nbcol++] = "SRAM mm2";
	arrtitle[nbcol++] = "SRAM uJ";
	arrtitle[nbcol++] = "Gates mm2";
	arrtitle[nbcol++] = "Gates uJ";
	arrtitle[nbcol++] = "Total mm2";
	arrtitle[nbcol++] = "Total uJ";

	// Print the table
	print_hline(14, 14, nbcol);
	printf("| %12s |", arrtitle[0]);
	for(unsigned i=1; i<nbcol; i++) printf(" %12s |", arrtitle[i]);
	printf("\n");
	print_hline(14, 14, nbcol);
	print_estim("Neurons", hwestim_types + LAYER_NEU, flags);
	print_estim("Windows", hwestim_types + LAYER_WIN, flags);
	print_estim("Ternarize", hwestim_types + LAYER_TER, flags);
	print_estim("MaxPool", hwestim_types + LAYER_POOL, flags);
	print_estim("Fifo",    hwestim_types + LAYER_FIFO, flags);
	print_hline(14, 14, nbcol);
	print_estim("Total", hwestim_types + 0, flags);
	print_hline(14, 14, nbcol);
}

void print_one_val(char const * msg, double d, char const * unit) {
	if(msg!=NULL) printf("%s", msg);
	if(param_sci_notation==true) printf("%g %s", d, unit);
	else print_unit_double("%10.3f", d, unit);
}
void print_total_stats(double latency, double frames_per_sec, double ener_per_frame, long mac_per_frame, double area) {
	double mac_per_sec = frames_per_sec * mac_per_frame;
	double power = frames_per_sec * ener_per_frame;
	printf("Global stats:\n");
	print_one_val("  Latency   ", latency, "s\n");
	print_one_val("  Speed     ", frames_per_sec, "fps\n");
	print_one_val("            ", mac_per_sec, "MAC/s\n");
	print_one_val("            ", 2 * mac_per_sec, "OP/s\n");
	print_one_val("  Power     ", ener_per_frame * frames_per_sec, "W\n");
	print_one_val("  Ener      ", ener_per_frame, "J/frame\n");
	print_one_val("  Ener eff  ", 1. / ener_per_frame, "fps/W\n");
	print_one_val("            ", mac_per_sec / power, "MAC/s/W\n");
	print_one_val("            ", 2 * mac_per_sec / power, "OP/s/W\n");
	print_one_val("  Area      ", area, "mm2\n");
	print_one_val("  Area eff  ", frames_per_sec / area, "fps/mm2\n");
}

// Digital estimations

void estimasic_digital_layer_win(layer_t* layer, asic_estim_t* estim) {
	if(asic_verbose > 0) {
		printf("  win %ux%u step %u %u pad %u %u nwin %u %u %u\n",
			layer->winx, layer->winy,
			layer->stepx, layer->stepy,
			layer->begpadx, layer->begpady,
			layer->nwinx, layer->nwiny, layer->nwinz
		);
	}

	// FIXME This code needs to be revised

	// Assume there are 2 memory banks of width PAR_OUT * WDATA
	// They are used in ping-pong style, so only one port is needed
	// Each has size = X * WinY * (DimZ / PAR)
	unsigned rcyclesz = (layer->fz + layer->split_out - 1) / layer->split_out;

	unsigned mem_width = layer->split_out * layer->wdata;
	unsigned mem_lines = layer->fx * layer->winy * rcyclesz;

	if(asic_verbose_printmem > 0) {
		printf("MEM %s%u ports 1rw, size %u words of %u bits, 2 instances\n", layer->typenameu, layer->typeidx, mem_lines, mem_width);
	}

	// SRAM
	estim->sram_bits += 2 * mem_lines * mem_width;
	estim->sram_area += 2 * spram_estim_area_mm2(mem_lines, mem_width);
	// SRAM, write operation
	// FIXME Missing reg to have full width before write operation
	estim->sram_ener += rcyclesz * layer->fx * layer->fy * spram_estim_ener_write(mem_lines, mem_width);
	// SRAM, read operation
	estim->sram_ener += rcyclesz * layer->nwinx * layer->nwiny * layer->winx * layer->winy * spram_estim_ener_read(mem_lines, mem_width);
	// The SRAM output is buffered (in addition to the SRAM power)
	estim->gate_area += layer->wdata * layer->split_out * ff_area;
	estim->gate_ener += layer->wdata * layer->out_fsize * layer->out_nbframes * ff_ener * toggle_rate;

	// FIXME Missing the address generation pipelines

}

void estimasic_digital_layer_neu(layer_t* layer, asic_estim_t* estim) {
	unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;
	unsigned nbneu_split = (layer->neurons + layer->split_out - 1) / layer->split_out;
	unsigned cycles      = fsize_split * layer->nbframes;
	unsigned mux_depth   = uint_bitsnb(layer->neurons - 1);

	unsigned mem_lines = fsize_split;
	unsigned mem_width = nbneu_split * layer->split_out * layer->split_in * layer->neu_wweight;

	if(asic_verbose > 0) {
		printf("  neurons %u (%u blocks of %u)\n", layer->neurons, layer->split_out, nbneu_split);
		printf("  fsize   %u (%u blocks of %u)\n", layer->fsize, layer->split_in, fsize_split);
		printf("  cycles  %u\n", cycles);
	}

	if(asic_verbose_printmem > 0) {
		printf("MEM %s%u ports 1rw, size %u words of %u bits\n", layer->typenameu, layer->typeidx, mem_lines, mem_width);
	}

	estim->neurons += layer->neurons;
	estim->params  += layer->fsize * layer->neurons;
	estim->macs    += layer->neurons * layer->fsize * layer->nbframes;

	estim->layer_freq   = logic_freq;
	estim->layer_cycles = cycles;
	estim->layer_time   = cycles / logic_freq;

	// SRAM, weights
	estim->sram_bits += mem_lines * mem_width;
	estim->sram_area += spram_estim_area_mm2(mem_lines, mem_width);
	estim->sram_ener += cycles * (1 - layer->stat_zd) * spram_estim_ener_read(mem_lines, mem_width);
	// The 2-bit weight is buffered (in addition to the SRAM power)
	estim->gate_area += mem_width * ff_area;
	estim->gate_ener += mem_width * cycles * ff_ener * toggle_rate;

	// The multipliers
	if(layer->wdata == 1 || layer->neu_wweight == 1) {
		unsigned other_width = GetMax(layer->wdata, layer->neu_wweight);
		// Binary multiplier: one AND gate per bit
		estim->gate_area += layer->neurons * layer->split_in * (other_width * and2_area);
		estim->gate_ener += layer->neurons * layer->split_in * cycles * (other_width * and2_ener) * toggle_rate;
	}
	else if(layer->wdata == 2 && layer->neu_wweight == 2) {
		// Ternary multiplier: one XOR and one AND gate
		estim->gate_area += layer->neurons * layer->split_in * (and2_area + xor2_area);
		estim->gate_ener += layer->neurons * layer->split_in * cycles * (and2_ener + xor2_ener) * toggle_rate;
	}
	else {
		// The conditional subtraction is partly done inside the adder tree
		// Here we need only one AND (for weight 0) + XOR (to prepare negation)
		estim->gate_area += layer->wdata * layer->neurons * layer->split_in * (and2_area + xor2_area);
		estim->gate_ener += layer->wdata * layer->neurons * layer->split_in * cycles * (1 - layer->stat_zd) * (and2_ener + xor2_ener) * toggle_rate;
	}

	unsigned waccu = GetMax(layer->wdata, layer->neu_wweight);

	// The adder tree
	if(layer->split_in > 1) {
		// FIXME Missing intermediate buffering
		for(unsigned nbadd=layer->split_in; nbadd>1; ) {
			nbadd = (nbadd + 1) / 2;
			waccu++;
			if(waccu > layer->out_wdata) waccu = layer->out_wdata;
			// Adders + buffer after adders
			estim->gate_area += waccu * nbadd * layer->neurons * fadd_area;
			estim->gate_ener += waccu * nbadd * layer->neurons * cycles * (1 - layer->stat_zd) * fadd_ener * toggle_rate;
			// If there was multi-bit inputs, add two gates (AND + OR) to handle the missing +1 of negated inputs
			if(layer->wdata > 2) {
				estim->gate_area += nbadd * layer->neurons * 2*and2_area;
				estim->gate_ener += nbadd * layer->neurons * cycles * (1 - layer->stat_zd) * 2*and2_ener * toggle_rate;
			}
		}
	}

	// For ternary input, on average 2 bits toggle in the accumulator, that's wdata/2 + 1
	// So estimate the number of bits that toggle in the accumulator as wdata/2 + 1
	// Or, just use wdata * toggle_rate

	// Accumulator: ALU + register
	if(layer->split_in <= 1) {
		// The neuron Write Enable: Two gates, count it as 2 NAND
		estim->gate_area += layer->neurons * layer->split_in * (4 * inv_area);
		estim->gate_ener += layer->neurons * layer->split_in * cycles * (4 * inv_ener) * toggle_rate;
		// The AND + adder + register: on average, 2 FFs toggle in the accumulator
		// The AND is just 2 pass transistors per accu bit -> estimate it as one inverter
		estim->gate_area += layer->out_wdata * layer->neurons * layer->split_in * (inv_area + fadd_area + ffwe_area);
		estim->gate_ener += waccu * layer->neurons * cycles * (1 - layer->stat_zd - layer->stat_nzd_zw) * (inv_ener + fadd_ener + ff_ener);
	}
	else {
		// The AND + adder + register
		// The AND is just 2 pass transistors per accu bit -> estimate it as one inverter
		estim->gate_area += layer->out_wdata * layer->neurons * layer->split_in * (inv_area + fadd_area + ffwe_area);
		estim->gate_ener += waccu * layer->neurons * cycles * (1 - layer->stat_zd) * (inv_ener + fadd_ener + ff_ener);
	}

	// The mirror register
	estim->gate_area += layer->out_wdata * layer->neurons * ffwe_area;
	estim->gate_ener += layer->out_wdata * layer->neurons * layer->nbframes * ff_ener * toggle_rate;

	// The mux: selection: 1-bit scan chain, one-hot
	estim->gate_area += 1 * layer->neurons * ff_area;
	estim->gate_ener += 2 * layer->split_out * layer->nbframes * ff_ener;
	// The mux: selection: There are neurons / 2 OR gates. Assume 1 OR = 1.5 inv for custom implementation.
	// The mux: selection: Assume 1 OR gate toggles per clock cycle
	estim->gate_area += 1 * nbneu_split / 2 * layer->split_out * (1.5 * inv_area);
	estim->gate_ener += 1 * nbneu_split * layer->split_out * layer->nbframes * (1 * inv_ener);
	// The mux: data path: pass transistors: there are 2 * neurons
	estim->gate_area += layer->out_wdata * nbneu_split * 2 * layer->split_out * (0.5 * inv_area);
	// The mux: data path: repeaters: assume that there is one inv every 2 mux stages
	estim->gate_area += layer->out_wdata * nbneu_split * layer->split_out * (1./4 + 1./16) * inv_area;
	estim->gate_ener += layer->out_wdata * nbneu_split * layer->split_out * layer->nbframes * mux_depth / 2 * inv_ener * toggle_rate;

	// Output buffer, after mux
	estim->gate_area += layer->out_wdata * layer->split_out * ffwe_area;
	estim->gate_ener += layer->out_wdata * layer->split_out * layer->nbframes * ff_ener * toggle_rate;
}

void estimasic_digital_layer_pool(layer_t* layer, asic_estim_t* estim) {
	if(asic_verbose > 0) {
		printf("  win %ux%u step %u %u pad %u %u nwin %u %u %u\n",
			layer->winx, layer->winy,
			layer->stepx, layer->stepy,
			layer->begpadx, layer->begpady,
			layer->nwinx, layer->nwiny, layer->nwinz
		);
	}

	// Logic gates: Assume as many pairs sub/reg than there are input values
	estim->gate_area += layer->split_in * layer->wdata * (fadd_area + ffwe_area);
	estim->gate_ener += layer->split_in * layer->wdata * (layer->fsize / layer->split_in) * layer->nbframes * (fadd_ener + ff_ener) * toggle_rate;

	// One counter for frame size: 3 bits wide, average 2 bits toggle per clock cycle
	estim->gate_area += 3 * (fadd_area + ffwe_area);
	estim->gate_ener += 2 * layer->fsize * layer->nbframes * (fadd_ener + ff_ener);
}

void estimasic_digital_layer_ter(layer_t* layer, asic_estim_t* estim) {
	if(asic_verbose > 0) {
		printf("  wdata %u->%u\n", layer->wdata, layer->out_wdata);
	}

	unsigned fsize_split = (layer->fsize + layer->split_in - 1) / layer->split_in;

	unsigned thres_nb = 2;
	if(layer->out_wdata == 1) thres_nb = 1;

	//unsigned mem_width = (thres_nb * layer->wdata + (thres_nb+1) * layer->out_wdata) * layer->split_in;
	unsigned mem_width = thres_nb * layer->wdata * layer->split_in;  // No need to store the outputs
	unsigned mem_lines = fsize_split;

	// SRAM
	estim->sram_bits += mem_lines * mem_width;
	estim->sram_area += spram_estim_area_mm2(mem_lines, mem_width);
	estim->sram_ener += fsize_split * layer->nbframes * spram_estim_ener_read(mem_lines, mem_width);
	// The SRAM output is buffered (in addition to the SRAM power)
	estim->gate_area += mem_width * ff_area;
	estim->gate_ener += mem_width * fsize_split * layer->nbframes * ff_ener * toggle_rate;

	// Comparators, implemented as subtracters
	estim->gate_area += thres_nb * layer->split_in * layer->wdata * fadd_area;
	estim->gate_ener += thres_nb * layer->split_in * layer->wdata * fsize_split * layer->nbframes * fadd_ener * toggle_rate;
	// Logic gates: 3-input MUX, approx. 3 inverters per bit
	estim->gate_area += (thres_nb+1) * layer->split_in * layer->wdata * 3 * inv_area;
	estim->gate_ener += (thres_nb+1) * layer->split_in * layer->wdata * fsize_split * layer->nbframes * 3 * inv_ener * toggle_rate;
	// Logic gates: one output register
	estim->gate_area += layer->split_in * layer->wdata * ffwe_area;
	estim->gate_ener += layer->split_in * layer->wdata * fsize_split * layer->nbframes * ff_ener * toggle_rate;

	// One address counter for frame size, on average 2 bits toggle per clock cycle
	unsigned waddr = uint_bitsnb(layer->fsize - 1);
	estim->gate_area += waddr * (fadd_area + ffwe_area);
	estim->gate_ener += 2 * layer->fsize * layer->nbframes * (fadd_ener + ff_ener);
}

void estimasic_digital_layer_fifo(layer_t* layer, asic_estim_t* estim) {

	if(fifo_is_shift==true) {
		// The shift registers
		estim->gate_area += layer->wdata * layer->split_in * fifo_depth * ffwe_area;
		estim->gate_ener += layer->wdata * layer->split_in * fifo_depth * (layer->fsize / layer->split_in) * layer->nbframes * ff_ener * toggle_rate;
		// Additional 1-bit shift register for data valid
		estim->gate_area += fifo_depth * ffwe_area;
		estim->gate_ener += fifo_depth * (layer->fsize / layer->split_in) * layer->nbframes * ff_ener;
		// One buffer for number of empty cells, assume it's one full adder + FF
		// It does only +1 or -1 at each cycle -> 2 bits toggle
		unsigned wcnt = uint_bitsnb(fifo_depth);
		estim->gate_area += wcnt * (fadd_area + ffwe_area);
		estim->gate_ener += 2 * (layer->fsize / layer->split_in) * layer->nbframes * (fadd_ener + ff_ener);
	}

	else if(fifo_is_regfile==true) {
		// The shift registers
		estim->gate_area += layer->wdata * layer->split_in * fifo_depth * ffwe_area;
		estim->gate_ener += layer->wdata * layer->split_in * (layer->fsize / layer->split_in) * layer->nbframes * ff_ener * toggle_rate;
		// Additional 1-bit shift registers for write and read position
		estim->gate_area += 2 * fifo_depth * ffwe_area;
		estim->gate_ener += 4 * (layer->fsize / layer->split_in) * layer->nbframes * ff_ener;
		// One buffer for number of empty cells, assume it's one full adder + FF
		// It does only +1 or -1 at each cycle -> 2 bits toggle on average
		unsigned wcnt = uint_bitsnb(fifo_depth);
		estim->gate_area += wcnt * (fadd_area + ffwe_area);
		estim->gate_ener += 2 * (layer->fsize / layer->split_in) * layer->nbframes * (fadd_ener + ff_ener);
	}

	else {

		// FIXME Need estimation of double-port SRAM: one Write, one Read
		// FIXME Currently, power is same for read and for write

		// SRAM
		estim->sram_bits += layer->wdata * layer->split_in * fifo_depth;
		estim->sram_area += spram_estim_area_mm2(fifo_depth, layer->wdata * layer->split_in);
		// SRAM, write operation
		estim->sram_ener += (layer->fsize / layer->split_in) * layer->nbframes * spram_estim_ener_write(fifo_depth, layer->wdata * layer->split_in);
		// SRAM, read operation
		estim->sram_ener += (layer->fsize / layer->split_in) * layer->nbframes * spram_estim_ener_read(fifo_depth, layer->wdata * layer->split_in);
		// The SRAM output is buffered (in addition to the SRAM power)
		estim->gate_area += layer->wdata * layer->split_in * ff_area;
		estim->gate_ener += layer->wdata * layer->split_in * (layer->fsize / layer->split_in) * layer->nbframes * ff_ener * toggle_rate;

		// Gates: 2 indexes + one counter
		unsigned waddr = uint_bitsnb(fifo_depth-1);
		unsigned wcnt  = uint_bitsnb(fifo_depth);
		estim->gate_area += waddr * 2 * (fadd_area + ffwe_area);
		estim->gate_area += wcnt * (fadd_area + ffwe_area);
		estim->gate_ener += 2 * 3 * (layer->fsize / layer->split_in) * layer->nbframes * (fadd_ener + ff_ener);
	}

}

void estimasic_digital_layer(layer_t* layer, asic_estim_t* estim) {
	if(asic_verbose > 0) printf("%s%u\n", layer->typenameu, layer->typeidx);

	if(layer->type == LAYER_WIN) {
		estimasic_digital_layer_win(layer, estim);
	}

	else if(layer->type == LAYER_NEU) {
		estimasic_digital_layer_neu(layer, estim);
	}

	else if(layer->type == LAYER_POOL) {
		estimasic_digital_layer_pool(layer, estim);
	}

	else if(layer->type == LAYER_TER) {
		estimasic_digital_layer_ter(layer, estim);
	}

	else if(layer->type == LAYER_FIFO) {
		estimasic_digital_layer_fifo(layer, estim);
	}

	else {
		printf("Error line %u: Layer type %u not handled\n", __LINE__, layer->type);
		exit(EXIT_FAILURE);
	}

	if(asic_verbose > 0) {
		printf("  image    %ux%ux%u %ux%ux%u\n", layer->fx, layer->fy, layer->fz, layer->out_fx, layer->out_fy, layer->out_fz);
		printf("  fsize    %u %u\n", layer->fsize, layer->out_fsize);
		printf("  nbframes %u %u\n", layer->nbframes, layer->out_nbframes);
		printf("  split    %u %u\n", layer->split_in, layer->split_out);
	}
}

void estimasic_digital(Network* network) {

	// Ensure we have a CNN
	chknonempty(network);
	network->insert_fifos();

	auto& layers = network->layers;

	// Scan all layers

	for(auto layer : layers) {
		asic_estim_t* estim = estimasic_getdata(layer);
		estimasic_digital_layer(layer, estim);
	}

	// Get total/max stats

	unsigned total_neurons = 0;
	unsigned total_params = 0;
	unsigned total_cycles = 0;
	unsigned total_macs = 0;
	unsigned max_cycles = 0;

	for(auto layer : layers) {
		asic_estim_t* estim = estimasic_getdata(layer);
		total_neurons += estim->neurons;
		total_params  += estim->params;
		total_macs    += estim->macs;
		total_cycles  += estim->layer_cycles;
		if(estim->layer_cycles > max_cycles) max_cycles = estim->layer_cycles;
	}

	// Get all resource stats for each layer type

	asic_estim_t hwestim_types[LAYER_TYPE_MAX+1];
	memset(hwestim_types, 0, (LAYER_TYPE_MAX+1) * sizeof(*hwestim_types));

	for(auto layer : layers) {
		asic_estim_t* estim_layer = estimasic_getdata(layer);
		asic_estim_t* estim_type  = hwestim_types + layer->type;
		estimasic_addhw(estim_type, estim_layer);
	}

	// Print results

	double freq    = logic_freq;
	double fps     = freq / max_cycles;
	double latency = (double)total_cycles / freq;

	asic_estim_t* estim_all = hwestim_types + 0;
	for(unsigned i=1; i<=LAYER_TYPE_MAX; i++) {
		asic_estim_t* estim = hwestim_types + i;
		estimasic_addhw(estim_all, estim);
	}

	double total_area = estimasic_total_area(estim_all);
	double total_ener = estimasic_total_ener(estim_all);

	if(asic_verbose > 0) {
		printf("\n");
		printf("RESULTS\n");
		printf("\n");
		print_table_layers(layers, hwestim_types);
		printf("\n");
	}

	printf("Techno %s\n", techno_desc);
	printf("Fequency %g MHz\n", freq / 1e6);
	printf("  Total neurons %u\n", total_neurons);
	printf("  Total params  %u\n", total_params);
	printf("  Total cycles  %u\n", total_cycles);
	printf("  Total MACs    %u\n", total_macs);

	print_table(hwestim_types);

	print_total_stats(latency, fps, total_ener, total_macs, total_area);
}

// Analog estimations

void estimasic_analog_layer_win(layer_t* layer, asic_estim_t* estim) {
	// The memory is ping-pong, 2 cuts of winy planes XZ of frame data
	// Data width = Fz * data width
	// Lines = winy * fx
	unsigned mem_width = layer->wdata * layer->fz;
	unsigned mem_lines = layer->winy * layer->fx;
	unsigned mem_nread = layer->winx * layer->winy * layer->nwinx * layer->nwiny;

	if(asic_verbose > 0) {
		printf("  win %ux%u\n", layer->winx, layer->winy);
		printf("  mem width %u lines %u nread %u\n", mem_width, mem_lines, mem_nread);
	}

	// SRAM
	estim->sram_bits += 2 * mem_width * mem_lines;
	estim->sram_area += 2 * spram_estim_area_mm2(mem_lines, mem_width);
	// SRAM, write operation
	estim->sram_ener += layer->fx * layer->fy * spram_estim_ener_write(mem_lines, mem_width);
	// SRAM, read operation
	estim->sram_ener += mem_nread * spram_estim_ener_read(mem_lines, mem_width);
	// The SRAM output is buffered (in addition to the SRAM power)
	estim->gate_area += mem_width * ff_area;
	estim->gate_ener += mem_width * mem_nread * ff_ener * toggle_rate;

	// FIXME Missing the address generation
	// FIXME This needs to be revised

	// Registers that store the entire, larger output frame
	estim->gate_area += mem_width * layer->winx * layer->winy * ff_area;
	estim->gate_ener += mem_width * mem_nread * ff_ener * toggle_rate;

	// Another of this register, for pipelining: hold data while TCAM neurons "compute"
	estim->gate_area += mem_width * layer->winx * layer->winy * ff_area;
	estim->gate_ener += mem_width * mem_nread * ff_ener * toggle_rate;

	// FIXME Missing energy to send SRAM data to all these registers -> high fanout, long lines
}

void estimasic_analog_layer_neu(layer_t* layer, asic_estim_t* estim) {

	// Remove the sign, there is no weight for that
	unsigned inwidth = layer->wdata * layer->fsize;
	if(layer->wdata > 1) inwidth = (layer->wdata - 1) * layer->fsize;

	unsigned tcam_lines = 2 * layer->neurons;
	if(layer->neu_wweight==1) tcam_lines = layer->neurons;

	// Compute the number of blocks in width and height
	unsigned nbblocks_width = 1;
	unsigned lastblk_width  = inwidth;
	unsigned maxblkwidth    = inwidth;
	if(tcamneu_blkwidth > 0) {
		maxblkwidth = tcamneu_blkwidth;
		nbblocks_width = inwidth / tcamneu_blkwidth;
		lastblk_width  = inwidth - nbblocks_width * tcamneu_blkwidth;
		if(lastblk_width==0) lastblk_width = tcamneu_blkwidth;
		else nbblocks_width++;
	}

	unsigned nbblocks_height = 1;
	unsigned lastblk_height  = tcam_lines;
	if(tcamneu_blkheight > 0) {
		nbblocks_height = tcam_lines / tcamneu_blkheight;
		lastblk_height  = tcam_lines - nbblocks_height * tcamneu_blkheight;
		if(lastblk_height==0) lastblk_height = tcamneu_blkheight;
		else nbblocks_height++;
	}

	unsigned blocks_nb = nbblocks_width * nbblocks_height;

	// The number of dummy lines needed for this layer
	unsigned dummy_lines_nb = 0;
	int dlarr[1000];
	// The width of the re-coded value for each TCAM line
	unsigned rec_width = 0;

	// Count dummy lines
	if(nbblocks_width == 1) {
		dummy_lines_nb = tcam_dl_recode_array(maxblkwidth, NULL, dlarr, false);
		rec_width = uint_bitsnb(maxblkwidth-1);
	}
	else {
		dummy_lines_nb = tcam_dl_recode_array(maxblkwidth, NULL, dlarr, true);
		rec_width = uint_bitsnb(tcamneu_blkwidth-1);
	}
	// Get min & max dicharge rates
	unsigned dl_mincell = dlarr[0];
	unsigned dl_maxcell = dlarr[dummy_lines_nb-1];
	if(tcam_dl_max > 0 && dummy_lines_nb > tcam_dl_max) dummy_lines_nb = tcam_dl_max;

	unsigned tcam_lines_total = tcam_lines + blocks_nb * dummy_lines_nb;

	estim->neurons += layer->neurons;
	estim->params  += layer->fsize * layer->neurons;
	estim->macs    += layer->neurons * layer->fsize * layer->nbframes;

	double layer_period = tcam_disch_time * maxblkwidth / dl_mincell;
	estim->layer_freq   = 1 / layer_period;
	estim->layer_cycles = layer->nbframes;
	estim->layer_time   = layer->nbframes * layer_period;

	if(asic_verbose > 0) {
		printf("  neurons %u\n", layer->neurons);
		printf("  fsize   %u\n", layer->fsize);
		printf("  time    %g ns = %u x %g ns\n", estim->layer_time * 1e9, layer->nbframes, layer_period * 1e9);
		printf("  inwidth %u\n", inwidth);
		printf("  blocks  width %u height %u total %u\n", nbblocks_width, nbblocks_height, blocks_nb);
		printf("  dummy lines %u (min %u max %u, width %u)\n", dummy_lines_nb, dl_mincell, dl_maxcell, maxblkwidth);
	}

	// The TCAM blocks themselves
	estim->tcamneu_bits += inwidth * tcam_lines_total * tcamneu_scale;
	// FIXME TCAM toggle rate is included in tcamneu_ener
	estim->tcamneu_ener += inwidth * tcam_lines_total * tcamneu_scale * layer->nbframes * tcamneu_ener;

	// The width of the output of each TCAM dummy line (Gray or binary) + the dummy line zero
	unsigned dlidx_width = uint_bitsnb(dummy_lines_nb);

	if(layer->neu_wweight > 1 || nbblocks_width > 1) {

		if(tcam_rec_rom==true || tcam_rec_sram==true) {
			// Re-code using ROM or SRAM

			// Sense amps (2 NOR gates) + arbiter + Gray priority encoders = 2+1 gates + 2 gates = 5 gates = 10 inv (per SA+enc)
			estim->gate_area += 10 * dummy_lines_nb * nbblocks_width * tcam_lines * inv_area;
			estim->gate_ener += 10 * dummy_lines_nb * nbblocks_width * tcam_lines * layer->nbframes * inv_ener * toggle_rate;

			// Bufferize the output of all blocks (Gray code)
			estim->gate_area += dlidx_width * nbblocks_width * tcam_lines * ffwe_area;
			estim->gate_ener += dlidx_width * nbblocks_width * tcam_lines * layer->nbframes * ff_ener * toggle_rate;

			// The output of the TCAM lines must be converted to real discharge rate => use ROM or SRAM blocks
			// Each mem block is used to re-code the output of several TCAM lines
			// Count the number of re-coding mem blocks using the ratio of discharge period and digital frequency
			unsigned tcam_rec_lines = layer_period * logic_freq;
			if(tcam_rec_lines < 20) tcam_rec_lines = 20;
			unsigned nbblocks_rec = (tcam_lines + tcam_rec_lines - 1) / tcam_rec_lines;
			unsigned blocks_rec_nb = nbblocks_width * nbblocks_rec;

			if(asic_verbose > 0) {
				printf("  recode  lines %u blocks %u\n", tcam_rec_lines, blocks_rec_nb);
			}

			#if 1
			double rec_time = layer->nbframes * tcam_rec_lines / logic_freq;
			if(rec_time > estim->layer_time) {
				estim->layer_freq   = logic_freq;
				estim->layer_cycles = tcam_rec_lines * layer->nbframes;
				estim->layer_time   = rec_time;
				if(asic_verbose > 0) {
					printf("  time bounded by recoding  %g ns = %u x %g ns\n", rec_time * 1e9, tcam_rec_lines * layer->nbframes, 1e9 / logic_freq);
				}
			}
			#endif

			if(tcam_rec_rom==true) {
				// Recoding using ROM
				// Assume A + W inverters, with A = address width and W = data width
				// FIXME These ROMs are estimated way too coarse
				estim->gate_area += dlidx_width * rec_width * blocks_rec_nb * inv_area;
				estim->gate_ener += (dlidx_width + rec_width) * nbblocks_width * tcam_lines * layer->nbframes * inv_ener * toggle_rate;
			}
			else if(tcam_rec_sram==true) {
				// Recoding using SRAM
				// Use one SRAM line per TCAM dummy line + one line for the value zero
				estim->sram_bits += blocks_rec_nb * (dummy_lines_nb + 1) * rec_width;
				estim->sram_area += blocks_rec_nb * spram_estim_area_mm2(dummy_lines_nb + 1, rec_width);
				estim->sram_ener += nbblocks_width * tcam_lines * layer->nbframes * spram_estim_ener_read(dummy_lines_nb + 1, rec_width);
			}

			// The SRAM output is buffered (in addition to the SRAM power)
			estim->gate_area += rec_width * nbblocks_width * tcam_lines * ffwe_area;
			estim->gate_ener += rec_width * nbblocks_width * tcam_lines * layer->nbframes * ff_ener * toggle_rate;
		}
		else {
			// Re-code using hard-coded values, inside the sense amps

			// Sense amp + recode = 2+1 gates (arbiter) + 2 gates + width gates = 10 + 2*width inverters
			estim->gate_area += (10 + 2*rec_width) * dummy_lines_nb * nbblocks_width * tcam_lines * inv_area;
			estim->gate_ener += (10 + 2*rec_width) * dummy_lines_nb * nbblocks_width * tcam_lines * layer->nbframes * inv_ener * toggle_rate;

			// Bufferize the output of all TCAM lines
			estim->gate_area += rec_width * nbblocks_width * tcam_lines * ffwe_area;
			estim->gate_ener += rec_width * nbblocks_width * tcam_lines * layer->nbframes * ff_ener * toggle_rate;
		}

	}  // End of re-coding

	// The adders
	if(nbblocks_width > 1) {
		estim->gate_area += layer->out_wdata * (nbblocks_width - 1) * tcam_lines * fadd_area;
		estim->gate_ener += layer->out_wdata * (nbblocks_width - 1) * tcam_lines * layer->nbframes * fadd_ener * toggle_rate;
	}

	// The subtracters, because neurons are signed
	estim->gate_area += layer->out_wdata * layer->neurons * fadd_area;
	estim->gate_ener += layer->out_wdata * layer->neurons * layer->nbframes * fadd_ener * toggle_rate;

	// Bufferize the output
	estim->gate_area += layer->out_wdata * layer->neurons * ffwe_area;
	estim->gate_ener += layer->out_wdata * layer->neurons * layer->nbframes * ff_ener * toggle_rate;
}

void estimasic_analog_layer_pool(layer_t* layer, asic_estim_t* estim) {
	unsigned acc_nb = layer->fz;
	unsigned cycles = layer->fsize * layer->out_fx * layer->out_fx;

	if(asic_verbose > 0) {
		printf("  win    %ux%u\n", layer->winx, layer->winy);
		printf("  par    %u\n", acc_nb);
		printf("  cycles %u\n", cycles);
	}

	// The comparators = subtracters
	estim->gate_area += layer->wdata * acc_nb * fadd_area;
	estim->gate_ener += layer->wdata * acc_nb * fadd_ener * toggle_rate;

	// The registers
	estim->gate_area += layer->wdata * acc_nb * ffwe_area;
	estim->gate_ener += layer->wdata * acc_nb * cycles * ff_ener * toggle_rate;
}

void estimasic_analog_layer_ter(layer_t* layer, asic_estim_t* estim) {
	unsigned par    = layer->fsize;
	unsigned cycles = layer->nbframes;

	if(asic_verbose > 0) {
		printf("  wdata  %u->%u\n", layer->wdata, layer->out_wdata);
		printf("  par    %u\n", par);
		printf("  cycles %u\n", cycles);
	}

	// SRAM, only for storage (actually it's part of the neurons)
	estim->sram_bits += 2 * layer->wdata * par;
	estim->sram_area += spram_estim_area_mm2(par, 2 * layer->wdata);

	// The comparators = subtracters
	estim->gate_area += 2 * layer->wdata * par * fadd_area;
	estim->gate_ener += 2 * layer->wdata * par * cycles * fadd_ener * toggle_rate;

	// The MUX, assume it's one gate
	estim->gate_area += par * 2 * inv_area;
	estim->gate_ener += par * cycles * 2 * inv_ener * toggle_rate;

	// The registers
	estim->gate_area += layer->wdata * par * ffwe_area;
	estim->gate_ener += layer->wdata * par * cycles * ff_ener * toggle_rate;
}

void estimasic_analog_layer(layer_t* layer, asic_estim_t* estim) {
	if(asic_verbose > 0) printf("%s%u\n", layer->typenameu, layer->typeidx);

	if(layer->type == LAYER_WIN) {
		estimasic_analog_layer_win(layer, estim);
	}

	else if(layer->type == LAYER_NEU) {
		estimasic_analog_layer_neu(layer, estim);
	}

	else if(layer->type == LAYER_POOL) {
		estimasic_analog_layer_pool(layer, estim);
	}

	else if(layer->type == LAYER_TER) {
		estimasic_analog_layer_ter(layer, estim);
	}

	else if(layer->type == LAYER_FIFO) {
		// Note: there are no FIFOs in analog design
	}

	else {
		printf("Error line %u: Layer type %u not handled\n", __LINE__, layer->type);
		exit(EXIT_FAILURE);
	}

	if(asic_verbose > 0) {
		printf("  image    %ux%ux%u %ux%ux%u\n", layer->fx, layer->fy, layer->fz, layer->out_fx, layer->out_fy, layer->out_fz);
		printf("  fsize    %u %u\n", layer->fsize, layer->out_fsize);
		printf("  nbframes %u %u\n", layer->nbframes, layer->out_nbframes);
	}
}

void estimasic_analog(Network* network) {

	// Ensure we have a CNN
	chknonempty(network);
	network->insert_fifos();

	auto& layers = network->layers;

	// Scan all layers

	for(auto layer : layers) {
		asic_estim_t* estim = estimasic_getdata(layer);
		estimasic_analog_layer(layer, estim);
	}

	// Get total/max stats

	unsigned total_neurons = 0;
	unsigned total_params = 0;
	unsigned total_macs = 0;
	double   total_time = 0;
	double   max_layer_time = 0;

	for(auto layer : layers) {
		asic_estim_t* estim = estimasic_getdata(layer);
		total_neurons += estim->neurons;
		total_params  += estim->params;
		total_macs    += estim->macs;
		total_time    += estim->layer_time;
		if(estim->layer_time > max_layer_time) max_layer_time = estim->layer_time;
	}

	// Get all resource stats for each layer type

	asic_estim_t hwestim_types[LAYER_TYPE_MAX+1];
	memset(hwestim_types, 0, (LAYER_TYPE_MAX+1) * sizeof(*hwestim_types));

	for(auto layer : layers) {
		asic_estim_t* estim_layer = estimasic_getdata(layer);
		asic_estim_t* estim_type  = hwestim_types + layer->type;
		estimasic_addhw(estim_type, estim_layer);
	}

	// Print results

	double fps = 1 / max_layer_time;

	for(unsigned i=1; i<=LAYER_TYPE_MAX; i++) {
		asic_estim_t* estim = hwestim_types + i;
		estim->tcamneu_area = estim->tcamneu_bits / 1e6 * tcamneu_area_mb;
	}

	asic_estim_t* estim_all = hwestim_types + 0;
	for(unsigned i=1; i<=LAYER_TYPE_MAX; i++) {
		asic_estim_t* estim = hwestim_types + i;
		estimasic_addhw(estim_all, estim);
	}

	double total_area = estimasic_total_area(estim_all);
	double total_ener = estimasic_total_ener(estim_all);

	if(asic_verbose > 0) {
		printf("\n");
		printf("RESULTS\n");
		printf("\n");
		print_table_layers(layers, hwestim_types);
		printf("\n");
	}

	printf("Techno %s\n", techno_desc);
	printf("  Total neurons %u\n", total_neurons);
	printf("  Total params  %u\n", total_params);
	printf("  Total MACs    %u\n", total_macs);

	print_table(hwestim_types);

	print_total_stats(total_time, fps, total_ener, total_macs, total_area);
}

// Mixed digital-analog estimations

void estimasic_mixed(Network* network) {

	// Ensure we have a CNN
	chknonempty(network);
	network->insert_fifos();

	auto& layers = network->layers;

	// Scan all layers

	bool prev_is_analog = false;

	for(auto layer : layers) {
		asic_estim_t* estim = estimasic_getdata(layer);

		if(asic_verbose > 0) printf("%s%u\n", layer->typenameu, layer->typeidx);

		if(layer->type == LAYER_WIN) {
			estimasic_analog_layer_win(layer, estim);
			prev_is_analog = true;
		}

		else if(layer->type == LAYER_NEU) {
			if(layer->nbframes == 1 || layer->wdata > 2) {
				estimasic_digital_layer_neu(layer, estim);
				prev_is_analog = false;
			}
			else {
				estimasic_analog_layer_neu(layer, estim);
				prev_is_analog = true;
			}
		}

		else if(layer->type == LAYER_POOL) {
			estimasic_analog_layer_pool(layer, estim);
			prev_is_analog = true;
		}

		else if(layer->type == LAYER_TER) {
			if(prev_is_analog==false) {
				estimasic_analog_layer_ter(layer, estim);
			}
			else {
				estimasic_analog_layer_ter(layer, estim);
			}
		}

		else if(layer->type == LAYER_FIFO) {
			// Note: there are no FIFOs in analog design
			if(prev_is_analog==false) {
				estimasic_digital_layer_fifo(layer, estim);
			}
		}

		else {
			printf("Error line %u: Layer type %u not handled\n", __LINE__, layer->type);
			exit(EXIT_FAILURE);
		}

		if(asic_verbose > 0) {
			printf("%s%u is %s\n", layer->typenameu, layer->typeidx, prev_is_analog==false ? "digital" : "analog");
		}

	}

	// Get total/max stats

	unsigned total_neurons = 0;
	unsigned total_params = 0;
	unsigned total_macs = 0;
	double   total_time = 0;
	double   max_layer_time = 0;

	for(auto layer : layers) {
		asic_estim_t* estim = estimasic_getdata(layer);
		total_neurons += estim->neurons;
		total_params  += estim->params;
		total_macs    += estim->macs;
		total_time    += estim->layer_time;
		if(estim->layer_time > max_layer_time) max_layer_time = estim->layer_time;
	}

	// Get all resource stats for each layer type

	asic_estim_t hwestim_types[LAYER_TYPE_MAX+1];
	memset(hwestim_types, 0, (LAYER_TYPE_MAX+1) * sizeof(*hwestim_types));

	for(auto layer : layers) {
		asic_estim_t* estim_layer = estimasic_getdata(layer);
		asic_estim_t* estim_type  = hwestim_types + layer->type;
		estimasic_addhw(estim_type, estim_layer);
	}

	// Print results

	double fps = 1 / max_layer_time;

	for(unsigned i=1; i<=LAYER_TYPE_MAX; i++) {
		asic_estim_t* estim = hwestim_types + i;
		estim->tcamneu_area = estim->tcamneu_bits / 1e6 * tcamneu_area_mb;
	}

	asic_estim_t* estim_all = hwestim_types + 0;
	for(unsigned i=1; i<=LAYER_TYPE_MAX; i++) {
		asic_estim_t* estim = hwestim_types + i;
		estimasic_addhw(estim_all, estim);
	}

	double total_area = estimasic_total_area(estim_all);
	double total_ener = estimasic_total_ener(estim_all);

	if(asic_verbose > 0) {
		printf("\n");
		printf("RESULTS\n");
		printf("\n");
		print_table_layers(layers, hwestim_types);
		printf("\n");
	}

	printf("Techno %s\n", techno_desc);
	printf("  Total neurons %u\n", total_neurons);
	printf("  Total params  %u\n", total_params);
	printf("  Total MACs    %u\n", total_macs);

	print_table(hwestim_types);

	print_total_stats(total_time, fps, total_ener, total_macs, total_area);
}

