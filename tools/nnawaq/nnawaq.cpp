
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
#include <unistd.h>

#include "nnawaq_utils.h"

}

#include "nnawaq.h"
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

#ifdef HAVE_RIFFA
#include "hwacc_pcieriffa.h"
#endif

#ifdef HAVE_ZYNQ7
#include "hwacc_zynq7.h"
#endif


//============================================
// Main function
//============================================

void print_about(int argc, char** argv) {
	printf(
		"\n"
		"NNawaq: Neural Network Adequate hardWare Architectures for Quantization\n"
		"Copyright University Grenoble Alpes (Grenoble, France)\n"
		"Permanent contact: Adrien Prost-Boucle <adrien.prost-boucle@univ-grenoble-alpes.fr>\n"
		"Permanent contact: Frédéric Pétrot <frederic.petrot@univ-grenoble-alpes.fr>\n"
		"\n"
		"Disclaimer:\n"
		"This program is distributed in the hope that it will be useful.\n"
		"Despite a lot of testing, this program and other libraries that it uses may not be exempt of bugs.\n"
		"It is advised to run this program in a virtual machine and/or to backup your data before running it.\n"
		"\n"
	);
}

void print_usage(int argc, char** argv) {
	auto network = Network::GetSingleton();

	printf("\n");
	printf("Usage:\n");
	printf("  %s [options]\n", argv[0]);
	printf("\n");
	printf("Options:\n");
	printf("  -u -h --help      Display this help and exit\n");
	printf("  --about           Display copyright notice and disclaimer, and exit\n");
	#ifndef LIMITED
	printf("  -debug            Enable debug messages\n");
	printf("  -v                Enable more verbosity\n");
	#endif  // ifndef LIMITED
	printf("  -rand             Initialize the random generator to something really random\n");
	printf("  -seed <seed       Initialize the random generator\n");
	printf("\n");

	#ifndef NOTCL
	printf("Options for TCL scripting:\n");
	printf("  -tcl <file>       Execute the TCL script file <file>\n");
	printf("  -tcl-cmd <cmd>    Execute the TCL commands <cmd>\n");
	printf("  -tcl-clear        Terminate and clear the embedded TCL interpreter\n");
	printf("\n");
	#endif    // ifndef NOTCL

	printf("Options for NN topology:\n");
	printf("  -f <x> <y> <z>    Input frame size, 3 dimensions\n");
	printf("  -bin              Build binary neural networks\n");
	printf("  -ter              Build ternary neural networks\n");
	printf("  -dw <w>           Input data width\n");
	printf("  -in <w><u|s>      Input data width and signedness\n");
	printf("  -inpar <p>        Set parallelism <p> at input of first layer\n");
	printf("\n");

	printf("Options for input frames and config files:\n");
	printf("  -frames <file>    File name of frame data\n");
	printf("  -fn <n>           Number of frames to process\n");
	printf("  -floop            Scan input file several times if it does not contain enough frames\n");
	printf("  -ml               Frame data can span several lines in config files\n");
	printf("\n");

	printf("Options for outputs:\n");
	printf("  -ol <layer>       Select the output layer\n");
	printf("  -o <filename>     Write output data in <filename> instead of stdout\n");
	printf("  -noout            Disable output, useful for measuring time\n");
	printf("  -oraw             Output is raw output frame (inverse of -owin)\n");
	printf("  -owin             Output is the index of the highest value in output frame (inverse of -oraw)\n");
	printf("  -onl <n>          Raw output data has a newline character every <n> values\n");
	printf("  -sep <s>          Raw output data has separator <s> between values\n");
	printf("  -ofmt <f>         Raw output data follows printf-compatible format <f>\n");
	printf("  -ofmt-norm <f>    Raw output data follows printf-compatible format <f>\n");
	printf("                    This is with normalization data, output data type is double\n");
	printf("  -omask            Raw output data has a mask on raw data bits, useful for dump as hex (does not apply with normalization data)\n");
	printf("  -rand-range <min> <max> Range of random data to use in case of missing configuration data\n");
	printf("  -gencsv-rand <l> <c>  Generate CSV data, random, <l> lines and <c> columns\n");
	printf("  -gencsv-id <r> <c>\n");
	printf("                    Generate CSV data, <r> rows and <c> columns\n");
	printf("                    Data is the identity matrix\n");
	printf("  -gencsv-seq <r> <c>\n");
	printf("                    Generate CSV data, <r> rows and <c> columns\n");
	printf("                    Data is sequential, 0 to <c>-1, same on all rows\n");
	printf("\n");

	printf("Options about what to do:\n");
	printf("  -print            Print the NN details\n");
	printf("  -print-pretty     Print the NN details with a pretty table\n");
	printf("  -autopar <par>    Apply parallelism (experimental algorithm that iteratively adds 2x parallelism)\n");
	printf("  -swexec           Select software execution\n");
	printf("  -swexec-mod <m>   Software execution outputs only values 0 modulo <m>\n");
	printf("  -swexec-gen-in    Software execution outputs the input of the output layer\n");
	#ifndef LIMITED
	// FIXME Rename this to approximate hardware
	printf("  -swexec-tcam      Software execution : emulate approximations brought by approximate hardware\n");
	#endif  // ifndef LIMITED
	printf("\n");

	printf("Options for VHDL generation:\n");
	printf("  -noselout         Don't generate logic to select output layer\n");
	printf("  -nofifomon        Don't generate logic to monitor FIFOs\n");
	printf("  -noregs           Config registers are not implemented, always read zero, values are locked at synthesis time\n");
	printf("  -rdonly           Config registers are implemented but read-only\n");
	printf("\n");

	#ifndef LIMITED
	printf("Options for VHDL generation:\n");
	printf("  -vhdl-lut-ratio <ratio>\n");
	printf("                    Ratio of LUTs per BRAM to use (default %g%%)\n", network->hwconfig_luts_bram_ratio);
	printf("                    Set zero to disable this limit\n");
	printf("  -vhdl-ifw <bits>  Generate designs suited for the specified data interface width\n");
	printf("  -vhdl-neu-style <style>\n");
	printf("                    Select the neuron layer implementation (negative means auto)\n");
	printf("  -vhdl-asicmode    Generate design with components dedicated to ASIC implementation\n");
	printf("  -vhdl-comp-all <t> <b>\n");
	printf("  -vhdl-comp-bram <t> <b>\n");
	printf("  -vhdl-comp-fc <t> <b>\n");
	printf("                    Ternary compression: set the number of ternary digits and the number of bits\n");
	printf("                    Select all layers, only BRAM-based layers or only FC layers\n");
	printf("                    Set <t> to 0 or 1 for no compression\n");
	printf("\n");
	#endif  // ifndef LIMITED

	printf("Options for hardware accelerator usage:\n");
	printf("  -hw-fbufsz <sz>   Use a buffer size of max <sz> MB to send frames to hardware (default %u)\n", param_bufsz_mb);
	printf("  -hw-freerun       Disable sending hardware accelerator results back to computer (outputs are still counted in hardware side)\n");
	printf("  -hw-timeout <ms>  Timeout at receiving frame results, in seconds (0 means no timeout)\n");
	printf("  -hw-blind         Enable blind run on the hardware accelerator by assuming the current network is the one being implemented in HW:\n");
	printf("                    Don't try to get/set parameters, but still send config data and frames\n");
	printf("\n");

	#ifdef HAVE_RIFFA
	printf("Options for PCIe using the RIFFA PCIe framework:\n");
	printf("  -riffa-init        Detect PCIe accelerator\n");
	printf("  -riffa-reset       Send reset to PCIe accelerator\n");
	printf("\n");
	#endif  // ifdef HAVE_RIFFA

	#ifdef HAVE_ZYNQ7
	printf("Options for Zynq-7000 hardware accelerators:\n");
	printf("  -zynq7-init        Detect PCIe accelerator\n");
	printf("\n");
	#endif  // ifdef HAVE_ZYNQ7

	printf("Options for using the hardware accelerator:\n");
	printf("  -hwacc-init        Automatically find a hardware accelerator\n");
	printf("  -hwacc-clear       Send clear signal to hardware accelerator\n");
	printf("  -hwacc-regs        Print hardware accelerator registers\n");
	printf("  -hwacc-nnregs      Print neural network config registers\n");
	printf("  -hwacc-close       Close usage of hardware interface\n");
	printf("  -hwacc-build       Build the network based on what is implemented in the hardware accelerator (if network config registers are implemented)\n");
	printf("  -hwacc-fifos       Display the status of all FIFOs from the hardware accelerator (if implemented)\n");
	printf("  -hwacc-latency     Evaluate latency of the core HW pipeline and report the number of clock cycles\n");
	printf("\n");

	#ifndef LIMITED
	printf("Options for ASIC estimations:\n");
	printf("  -asic             Launch diginal ASIC estimations\n");
	printf("  -analog           Launch analog ASIC estimations\n");
	printf("  -sci              Print stats using scientific notation\n");
	printf("  -st-ll10          Use power calibration for ST LL 1.0V\n");
	printf("  -st-ll06          Use power calibration for ST LL 0.6V\n");
	printf("  -fifo-depth <d>   Set depth of FIFOs\n");
	printf("  -fifo-sh          Use shift-register FIFOs\n");
	printf("  -fifo-rf          Use register-file FIFOs\n");
	printf("  -tcam-prec <prec> Number of dummy lines is calculated to reach at most <prec> error ratio\n");
	printf("  -tcam-dl-max <l>  Max number of dummy lines to use per TCAM block\n");
	printf("  -tcam-dl-dist <l>  Min distance, in discharge rate, between dummy lines\n");
	printf("  -tcam-dl-dismin <v> Min discharge rate, in number of cells\n");
	printf("  -tcam-dl-disrate <min> <max>\n");
	printf("                    Min and max discharge rate, ratio of line size\n");
	printf("  -tcam-split <w> <l>  TCAMs are split with max frame size <w> and max <l> lines\n");
	printf("  -tcam-rec-rom     TCAMs are re-coded using ROM look-up tables\n");
	printf("  -tcam-rec-sram    TCAMs are re-coded using SRAM look-up tables\n");
	printf("\n");
	#endif  // ifndef LIMITED
}

static int read_params(int argc, char** argv) {
	auto network = Network::GetSingleton();

	if(argc==1) {
		print_about(argc, argv);
		exit(EXIT_SUCCESS);
	}

	// ASIC estimations
	#ifndef LIMITED
	// This techno sets most values
	select_techno_st_ll_10();
	// Default technology
	select_techno_st_ll_09();
	#endif

	// Read parameters

	unsigned argi = 1;
	do {
		if((int)argi >= argc) break;
		char* arg = argv[argi];
		if(arg[0]==0) break;

		// Local utility functions

		// Get parameters
		auto getparam_str = [&](void) {
			argi++;
			if((int)argi >= argc) {
				printf("Error: Missing parameters after '%s'\n", arg);
				exit(EXIT_FAILURE);
			}
			return argv[argi];
		};

		auto getparam_layer = [&](void) {
			char* strid = getparam_str();
			layer_t* layer = network->getlayer_from_string_id(strid);
			if(layer==NULL) {
				printf("Error command %s: Layer %s not found\n", arg, strid);
				exit(EXIT_FAILURE);
			}
			return layer;
		};

		auto getparam_width_sign = [&](bool* sgn_bool_p, unsigned* sgn_uint_p) {
			char* str = getparam_str();
			unsigned width = 0;
			int z = decodeparam_width_sign(str, &width, sgn_bool_p, sgn_uint_p);
			if(z != 0) {
				fprintf(stderr, "Error command %s : Wrong parameters specified\n", arg);
				exit(EXIT_FAILURE);
			}
			return width;
		};

		// Parse parameters

		if(strcmp(arg, "-u")==0 || strcmp(arg, "-h")==0 || strcmp(arg, "--help")==0) {
			print_usage(argc, argv);
			exit(EXIT_SUCCESS);
		}
		else if(strcmp(arg, "--about")==0) {
			print_about(argc, argv);
			exit(EXIT_SUCCESS);
		}

		else if(strcmp(arg, "-t") == 0) {
			param_print_time = true;
		}

		else if(strcmp(arg, "-r") == 0 || strcmp(arg, "-rand") == 0) {
			unsigned long seed = Time64_GetReal() + getpid();
			printf("Using random seed: 0x%lx\n", seed);
			srand48(seed);
			srand(seed);
			srandom(seed);
		}
		else if(strcmp(arg, "-seed") == 0) {
			unsigned long seed = strtoul(getparam_str(), NULL, 0);
			srand48(seed);
			srand(seed);
			srandom(seed);
		}

		#ifndef NOTCL

		else if(strcmp(arg, "-tcl")==0) {
			char* tcl_filename = getparam_str();
			tcl_init_interp(argv[0]);
			int ret = tcl_exec_file(tcl_filename);
			if(ret != 0) exit(EXIT_FAILURE);
		}
		else if(strcmp(arg, "-tcl-cmd")==0) {
			char* tcl_line = getparam_str();
			tcl_init_interp(argv[0]);
			int ret = tcl_exec_line(tcl_line);
			if(ret != 0) exit(EXIT_FAILURE);
		}
		else if(strcmp(arg, "-tcl-clear")==0) {
			tcl_clear();
		}

		#endif

		else if(strcmp(arg, "-debug")==0) {
			param_debug = true;
		}
		else if(strcmp(arg, "-sci")==0) {
			param_sci_notation = true;
		}

		else if(strcmp(arg, "-bin")==0) {
			network->default_neu_wd = 1;
			network->default_neu_ww = 1;
			network->default_neu_sd = false;
			network->default_neu_sw = false;
			network->default_neu_so = false;
		}
		else if(strcmp(arg, "-ter")==0) {
			network->default_neu_wd = 2;
			network->default_neu_ww = 2;
			network->default_neu_sd = true;
			network->default_neu_sw = true;
			network->default_neu_so = true;
		}

		else if(strcmp(arg, "-acts")==0) {
			network->default_neu_wd = getparam_width_sign(&network->default_neu_sd, NULL);
		}
		else if(strcmp(arg, "-weights")==0) {
			network->default_neu_ww = getparam_width_sign(&network->default_neu_sw, NULL);
		}
		else if(strcmp(arg, "-outs")==0) {
			network->default_neu_wo = getparam_width_sign(&network->default_neu_so, NULL);
		}
		else if(strcmp(arg, "-relu")==0) {
			network->default_relu_min = atoi(getparam_str());
			network->default_relu_max = atoi(getparam_str());
		}

		else if(strcmp(arg, "-frames")==0) {
			filename_frames = getparam_str();
		}
		else if(strcmp(arg, "-fn")==0) {
			char* param = getparam_str();
			param_fn = atoi(param);
		}
		else if(strcmp(arg, "-floop")==0) {
			param_floop = true;
		}

		// Options for outputs

		else if(strcmp(arg, "-ol")==0) {
			layer_t* layer = getparam_layer();
			param_out_layer = layer;
		}
		else if(strcmp(arg, "-o")==0) {
			filename_out = getparam_str();
			// In case an output file was already opened, close it
			// That way, several successive calls to chkoutfile() will only open the file once
			if(Fo != stdout) fclose(Fo);
			Fo = stdout;
		}
		else if(strcmp(arg, "-noout")==0) {
			param_noout = true;
		}
		else if(strcmp(arg, "-onl")==0) {
			param_out_nl = atoi(getparam_str());
		}
		else if(strcmp(arg, "-osep")==0) {
			param_out_sep = strdup(getparam_str());
		}
		else if(strcmp(arg, "-omask")==0) {
			param_out_mask = true;
		}
		else if(strcmp(arg, "-ofmt")==0) {
			param_out_format = strdup(getparam_str());
		}

		else if(strcmp(arg, "-print")==0) {
			nnprint_oneline(network->layers, "");
		}
		else if(strcmp(arg, "-print-pretty")==0) {
			nnprint(network->layers, NNPRINT_OPT_TABLE);
		}
		else if(strcmp(arg, "-swexec")==0) {
			chknonempty(network);
			chkoutfile();
			swexec(network, param_out_layer);
		}

		else if(strcmp(arg, "-swexec-mod")==0) {
			swexec_param_mod = atoi(getparam_str());
		}
		else if(strcmp(arg, "-swexec-tcam")==0) {
			swexec_mode_tcam = true;
		}
		else if(strcmp(arg, "-swexec-gen-in")==0) {
			swexec_gen_in = true;
		}

		else if(strcmp(arg, "-f") == 0) {
			network->param_fx = atoi(getparam_str());
			network->param_fy = atoi(getparam_str());
			network->param_fz = atoi(getparam_str());
		}
		else if(strcmp(arg, "-dw") == 0) {
			network->param_win = atoi(getparam_str());  // FIXME Obsolete ?
		}
		else if(strcmp(arg, "-in") == 0) {
			network->param_win = getparam_width_sign(&network->param_sin, NULL);
		}

		else if(strcmp(arg, "-inpar") == 0) {
			network->param_inpar = atoi(getparam_str());
		}
		else if(strcmp(arg, "-autopar") == 0) {
			unsigned par = atoi(getparam_str());
			apply_parallelism(network, par);
		}

		else if(strcmp(arg, "-ml")==0) {
			param_multiline = true;
		}

		// Generation of config files

		else if(strcmp(arg, "-rand-range") == 0) {
			param_rand_min = atoi(getparam_str());
			param_rand_max = atoi(getparam_str());
			param_rand_given = true;
		}
		else if(strcmp(arg, "-gencsv-rand") == 0) {
			unsigned nrow = atoi(getparam_str());
			unsigned ncol = atoi(getparam_str());
			chkoutfile();
			gencsv_rand(Fo, nrow, ncol, param_rand_min, param_rand_max, network->param_sin, network->param_win, NULL);
		}
		else if(strcmp(arg, "-gencsv-id") == 0) {
			unsigned nrow = atoi(getparam_str());
			unsigned ncol = atoi(getparam_str());
			chkoutfile();
			gencsv_id(Fo, nrow, ncol, NULL);
		}
		else if(strcmp(arg, "-gencsv-seq") == 0) {
			unsigned nrow = atoi(getparam_str());
			unsigned ncol = atoi(getparam_str());
			chkoutfile();
			gencsv_seq(Fo, nrow, ncol, NULL);
		}

		// VHDL generation

		else if(strcmp(arg, "-noselout")==0) {
			network->param_selout = false;
		}
		else if(strcmp(arg, "-nofifomon")==0) {
			network->param_fifomon = false;
		}
		else if(strcmp(arg, "-noregs")==0) {
			network->param_noregs = true;
		}
		else if(strcmp(arg, "-rdonly")==0) {
			network->param_rdonly = true;
		}

		// VHDL generation

		#ifndef LIMITED

		else if(strcmp(arg, "-vhdl-lut-ratio") == 0) {
			network->hwconfig_luts_bram_ratio = strtod_perc(getparam_str());
		}
		else if(strcmp(arg, "-vhdl-ifw") == 0) {
			network->hwconfig_writewidth = atoi(getparam_str());
		}
		else if(strcmp(arg, "-vhdl-neu-style") == 0) {
			network->hwconfig_neu_style = atoi(getparam_str());
		}
		else if(strcmp(arg, "-vhdl-asicmode") == 0) {
			network->hwconfig_asicmode = true;
		}

		#endif  // ifndef LIMITED

		// Hardware accelerator usage

		else if(strcmp(arg, "-hw-fbufsz")==0) {
			param_bufsz_mb = atoi(getparam_str());
		}
		else if(strcmp(arg, "-hw-freerun")==0) {
			param_freerun = true;
		}
		else if(strcmp(arg, "-hw-timeout")==0) {
			unsigned long us = 0;
			decodeparam_us(getparam_str(), &us);
			param_timeout_regs_us = us;
			param_timeout_send_us = us;
			param_timeout_recv_us = us;
		}
		else if(strcmp(arg, "-hw-blind")==0) {
			param_hw_blind = true;
		}

		#ifdef HAVE_RIFFA
		else if(strcmp(arg, "-riffa-init")==0) {
			HwAcc_Common* hwacc = HwAcc_PcieRiffa::GetSingleton();
			HwAcc_Common::CurrentHwAcc_Set(hwacc);
		}
		else if(strcmp(arg, "-riffa-reset")==0) {
			HwAcc_PcieRiffa* hwacc = HwAcc_PcieRiffa::GetSingleton();
			fpga_reset(hwacc->fpga);
			printf("Reset sent\n");
		}
		#endif  // ifdef HAVE_RIFFA

		#ifdef HAVE_ZYNQ7
		else if(strcmp(arg, "-zynq7-init")==0) {
			HwAcc_Common* hwacc = HwAcc_Zynq7::GetSingleton();
			HwAcc_Common::CurrentHwAcc_Set(hwacc);
		}
		#endif  // ifdef HAVE_ZYNQ7

		else if(strcmp(arg, "-hwacc-init")==0) {
			HwAcc_Common* hwacc = nullptr;

			#ifdef HAVE_RIFFA
			if(hwacc == nullptr) {
				hwacc = HwAcc_PcieRiffa::GetSingleton();
			}
			#endif

			#ifdef HAVE_ZYNQ7
			if(hwacc == nullptr) {
				hwacc = HwAcc_Zynq7::GetSingleton();
			}
			#endif

			if(hwacc == nullptr) {
				printf("Error: Failed to automatically find a hardware accelerator\n");
				exit(EXIT_FAILURE);
			}

			HwAcc_Common::CurrentHwAcc_Set(hwacc);
		}

		else if(strcmp(arg, "-hwacc-clear")==0) {
			HwAcc_Common* hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
			hwacc->accreg_clear();
		}
		else if(strcmp(arg, "-hwacc-regs")==0) {
			HwAcc_Common* hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
			hwacc->accreg_config_get();
			hwacc->accreg_print_regs();
			hwacc->accreg_config_print();
		}
		else if(strcmp(arg, "-hwacc-nnregs")==0) {
			HwAcc_Common* hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
			hwacc->accreg_config_get();
			hwacc->accreg_cfgnn_print();
		}
		else if(strcmp(arg, "-hwacc-close")==0) {
			HwAcc_Common* hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
			delete hwacc;
			HwAcc_Common::CurrentHwAcc_Set(nullptr);
		}
		else if(strcmp(arg, "-hwacc-build") == 0) {
			HwAcc_Common* hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
			hwacc->build_network(network);
		}
		else if(strcmp(arg, "-hwacc-fifos")==0) {
			HwAcc_Common* hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
			hwacc->print_fifos(network);
		}
		else if(strcmp(arg, "-hwacc-latency")==0) {
			unsigned num_out = atoi(getparam_str());
			HwAcc_Common* hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
			hwacc->eval_latency(network, num_out);
		}
		else if(strcmp(arg, "-hwacc-clkfreq")==0) {
			unsigned num_ms = atoi(getparam_str());
			HwAcc_Common* hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
			hwacc->eval_clkfreq(network, num_ms);
		}

		// ASIC estimations

		#ifndef LIMITED

		else if(strcmp(arg, "-v") == 0) {
			asic_verbose++;
		}

		else if(strcmp(arg, "-asic") == 0) {
			estimasic_digital(network);
		}
		else if(strcmp(arg, "-analog") == 0) {
			estimasic_analog(network);
		}
		else if(strcmp(arg, "-asic-mixed") == 0) {
			estimasic_mixed(network);
		}

		else if(strcmp(arg, "-fifo-depth") == 0) {
			fifo_depth = atoi(getparam_str());
		}
		else if(strcmp(arg, "-fifo-sh") == 0) {
			fifo_is_shift   = true;
			fifo_is_regfile = false;
		}
		else if(strcmp(arg, "-fifo-rf") == 0) {
			fifo_is_shift   = false;
			fifo_is_regfile = true;
		}
		else if(strcmp(arg, "-st-ll10") == 0) {
			select_techno_st_ll_10();
		}
		else if(strcmp(arg, "-st-ll09") == 0) {
			select_techno_st_ll_09();
		}
		else if(strcmp(arg, "-st-ll06") == 0) {
			select_techno_st_ll_06();
		}

		else if(strcmp(arg, "-tcam-prec") == 0) {
			tcam_prec_dbl = strtod_perc(getparam_str());
		}
		else if(strcmp(arg, "-tcam-dl-max") == 0) {
			tcam_dl_max = atoi(getparam_str());
		}
		else if(strcmp(arg, "-tcam-dl-dist") == 0) {
			tcam_dl_mindist = atoi(getparam_str());
		}
		else if(strcmp(arg, "-tcam-dl-dismin") == 0) {
			tcam_dl_mindisch = atoi(getparam_str());
		}
		else if(strcmp(arg, "-tcam-dl-disrate") == 0) {
			tcam_dl_minrate = strtod_perc(getparam_str());
			tcam_dl_maxrate = strtod_perc(getparam_str());
		}
		else if(strcmp(arg, "-tcam-split") == 0) {
			tcamneu_blkwidth  = strtod_perc(getparam_str());
			tcamneu_blkheight = strtod_perc(getparam_str());
		}

		else if(strcmp(arg, "-tcam-rec-rom") == 0) {
			tcam_rec_rom = true;
		}
		else if(strcmp(arg, "-tcam-rec-sram") == 0) {
			tcam_rec_sram = true;
		}

		#endif  // ifndef LIMITED

		else {
			printf("Error: Unknown parameter '%s'\n", arg);
			exit(EXIT_FAILURE);
		}

		// Here the parameter was known. Increment index.
		argi++;

	} while(1);

	return 0;
}

int main(int argc, char** argv) {
	int64_t oldtime;
	double diff;

	// Only to know the execution time
	oldtime = Time64_GetReal();

	// Default output is stdout
	Fo = stdout;

	// Populate the built-in list of layers
	int res = declare_builtin_layers();
	if(res != 0) {
		printf("Error : Failures occurred during initialization of built-in layer types, can't continue\n");
		exit(EXIT_FAILURE);
	}

	// Parse command-line parameters
	int z = read_params(argc, argv);
	if(z!=0) {
		exit(EXIT_FAILURE);
	}

	// Display the execution time
	if(param_print_time ==true) {
		diff = TimeDouble_DiffCurrReal_From64(oldtime);
		printf("Total execution time: %g seconds.\n", diff);
	}

	return 0;
}

