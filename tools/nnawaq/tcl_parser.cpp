
extern "C" {

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <regex.h>

#include <tcl.h>

#include "nnawaq_utils.h"

}  // extern "C"

#include <string>

#include "nn_layers_utils.h"
#include "hwacc_common.h"
#include "tcl_parser.h"

#ifdef HAVE_RIFFA
#include "hwacc_pcieriffa.h"
#endif

#ifdef HAVE_ZYNQ7
#include "hwacc_zynq7.h"
#endif

#include "nn_hw_config.h"
#include "nn_hwacc_config.h"
#include "swexec.h"

#ifndef LIMITED
#include "genvhdl.h"
#include "compress.h"
#endif


// Return values when processing parameters
#define PARAM_OK 0
#define PARAM_KO -1
#define PARAM_MALFORMED -2
#define PARAM_WRONG_NB -3

// An intermediate buffer to create error messages
static char errmsg[1024];

// This is a shared variable
static Tcl_Interp *interp = NULL;

// In case of output redirection, it is better to flush output files stdout/stderr after each invocation of a C/C++ callback
static bool fflush_after_callback = false;

// FIXME This code relies heavily on assumption that there exists one globally-defined and unique Network instance
// Need to clean this, keep a trustable link to a potentially externally-defined network
// Such as combinations of shared_ptr<Network*> and weak_ptr<Network*>


//============================================
// Utility functions to manipulate regexs
//============================================

#define REGEX_EQ1_REG  "^([^=]+)=([^=]*)$"
#define REGEX_EQ2S_REG "^([^=/]+)=([^=/]+)/([^=/]+)$"
#define REGEX_EQ2X_REG "^([^=]+)=([0-9]+)x([0-9]+)$"
#define REGEX_EQ3S_REG "^([^=/]+)=([^=/]+)/([^=/]+)/([^=/]+)$"

static regex_t regex_eq1;
static regex_t regex_eq2s;
static regex_t regex_eq2x;
static regex_t regex_eq3s;

static bool regcomp_done = false;

// Initialize regexs
static void regcomp_init(void) {
	if(regcomp_done == true) return;
	regcomp(&regex_eq1,  REGEX_EQ1_REG,  REG_EXTENDED);
	regcomp(&regex_eq2s, REGEX_EQ2S_REG, REG_EXTENDED);
	regcomp(&regex_eq2x, REGEX_EQ2X_REG, REG_EXTENDED);
	regcomp(&regex_eq3s, REGEX_EQ3S_REG, REG_EXTENDED);
	regcomp_done = true;
}
// Free regexs
static void regcomp_free(void) {
	if(regcomp_done == false) return;
	regfree(&regex_eq1);
	regfree(&regex_eq2s);
	regfree(&regex_eq2x);
	regfree(&regex_eq3s);
	regcomp_done = false;
}

// Extract the matching field from regex match results
// Return the size of the string, or negative if there was no match
static int regmatch_to_string(const regmatch_t* regmatch, const char* str, char* buf) {
	buf[0] = 0;
	int len = -1;
	if(regmatch->rm_so >= 0) {
		len = regmatch->rm_eo - regmatch->rm_so;
		strncpy(buf, str + regmatch->rm_so, len);
		buf[len] = 0;
		//printf("DEBUG regmatch %i %i\n", regmatch->rm_so, regmatch->rm_eo);
	}
	return len;
}

//============================================
// Utility functions
//============================================

// Utility functions
static unsigned nn_get_non_empty_val(const char* val1, const char* val2, const char* val3) {
	if(val1 == NULL || val1[0] == 0) return 0;
	if(val2 == NULL || val2[0] == 0) return 1;
	if(val3 == NULL || val3[0] == 0) return 2;
	return 3;
}
static int str2bool(const char* val) {
	if(strcasecmp(val, "true")==0 || strcasecmp(val, "yes")==0 || strcmp(val, "1")==0) {
		return true;
	}
	if(strcasecmp(val, "false")==0 || strcasecmp(val, "no")==0 || strcmp(val, "0")==0) {
		return false;
	}
	printf("Error: String '%s' is not recognized as boolean\n", val);
	return -1;
}

static unsigned nn_get_weights_order(const char* val) {
	if(strcasecmp(val, "keep") == 0) {
		return NEU_WORDER_KEEP;
	}
	else if(strcasecmp(val, "zfirst") == 0 || strcasecmp(val, "chan_first") == 0 || strcasecmp(val, "zxy") == 0) {
		return NEU_WORDER_ZFIRST;
	}
	else if(strcasecmp(val, "xfirst") == 0 || strcasecmp(val, "xyz") == 0) {
		return NEU_WORDER_XFIRST;
	}
	printf("Error: Weights order '%s' is not recognized\n", val);
	return NEU_WORDER_NONE;
}

// The strings command and option are optional
static layer_t* getlayer_verbose(Network* network, const char* strid, const char* command, const char* option) {
	layer_t* layer = network->getlayer_from_string_id(strid);
	if(layer==NULL) {
		printf(errmsg, "Error");
		if(command !=NULL) printf(" command %s", command);
		if(option !=NULL) printf(" option %s", option);
		printf(" : Layer '%s' is not found\n", strid);
		return NULL;
	}
	return layer;
}

//============================================
// TCL callbacks to get parameters
//============================================

static int cb_nn_get(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	auto network = Network::GetSingleton();
	auto& layers = network->layers;

	if (objc < 2) {
		sprintf(errmsg, "%s - Wrong number of arguments, need at lead the layer ID", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	char* name = Tcl_GetString(objv[1]);
	char buf[128];

	if(strcmp(name, "layers_nb") == 0) {
		sprintf(buf, "%u", (unsigned)layers.size());
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "layers") == 0) {
		unsigned layer_type = LAYER_NONE;
		MemImplem::style_type mem_implem = MemImplem::STYLE_NONE;
		// Process additional parameters, if any
		for(int i=2; i<objc; i++) {
			char* str = Tcl_GetString(objv[i]);
			if(strncmp(str, "-type-", 6) == 0) {
				unsigned type = Layer::get_type_name2id_verbose(str + 6);
				if(type == LAYER_NONE) {
					sprintf(errmsg, "%s - Error, unknown argument '%s'", Tcl_GetString(objv[0]), str);
					Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
					return TCL_ERROR;
				}
				layer_type = type;
			}
			else if(strncmp(str, "-mem-", 5) == 0) {
				MemImplem::style_type implem = MemImplem::GetStyle(str + 5);
				if(implem == MemImplem::STYLE_NONE) {
					sprintf(errmsg, "%s - Error, unknown argument '%s'", Tcl_GetString(objv[0]), str);
					Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
					return TCL_ERROR;
				}
				mem_implem = implem;
			}
			else {
				sprintf(errmsg, "%s - Error, unknown argument '%s'", Tcl_GetString(objv[0]), str);
				Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
				return TCL_ERROR;
			}
		}
		// Scan layers
		for(auto layer : layers) {
			if(layer_type != LAYER_NONE && (unsigned)layer->type != layer_type) continue;
			if(mem_implem != MemImplem::STYLE_NONE && layer->mem.style != mem_implem) continue;
			sprintf(buf, "%s%u ", layer->typenamel, layer->typeidx);  // Spaces between values makes TCL parse the result at a list
			Tcl_AppendResult(interp, buf, nullptr);
		}
	}

	else if(strcmp(name, "first") == 0) {
		if(network->layer_first == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		sprintf(buf, "%s%u", network->layer_first->typenamel, network->layer_first->typeidx);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "last") == 0) {
		if(network->layer_last == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		sprintf(buf, "%s%u", network->layer_last->typenamel, network->layer_last->typeidx);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "bottleneck") == 0) {
		if(layers.size() == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		layer_t* bot_layer = NULL;
		unsigned bot_cycles = 0;
		for(auto layer : layers) {
			if(layer->cycles > bot_cycles)     { bot_layer = layer; bot_cycles = layer->cycles; }
			if(layer->out_cycles > bot_cycles) { bot_layer = layer; bot_cycles = layer->out_cycles; }
		}
		sprintf(buf, "%s%u", bot_layer->typenamel, bot_layer->typeidx);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "bottleneck_cycles") == 0) {
		if(layers.size() == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		unsigned maxcy = 0;
		for(auto layer : layers) {
			if(layer->cycles > maxcy)     maxcy = layer->cycles;
			if(layer->out_cycles > maxcy) maxcy = layer->out_cycles;
		}
		sprintf(buf, "%u", maxcy);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "wdata") == 0) {
		if(network->layer_last == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		sprintf(buf, "%u", network->layer_last->out_wdata);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "sdata") == 0) {
		if(network->layer_last == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		sprintf(buf, "%u", network->layer_last->out_sdata);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "fsize_x") == 0) {
		if(network->layer_last == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		sprintf(buf, "%u", network->layer_last->out_fx);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "fsize_y") == 0) {
		if(network->layer_last == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		sprintf(buf, "%u", network->layer_last->out_fy);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "fsize_z") == 0) {
		if(network->layer_last == 0) {
			printf("%s %s - Error: No network\n", Tcl_GetString(objv[0]), name);
			return TCL_ERROR;
		}
		sprintf(buf, "%u", network->layer_last->out_fz);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "use_uram") == 0) {
		sprintf(buf, "%u", network->hwconfig_use_uram);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else {
		printf("%s - Error: Unknown name '%s'\n", Tcl_GetString(objv[0]), name);
		return TCL_ERROR;
	}

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

// Callback function to get parameters from a layer
static int cb_nn_layer_get(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	if (objc != 3) {
		sprintf(errmsg, "%s - Wrong number of arguments, only layer and id is required ", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	auto network = Network::GetSingleton();

	// Retrieve layer id
	char* strid = Tcl_GetString(objv[1]);
	layer_t* layer = getlayer_verbose(network, strid, Tcl_GetString(objv[0]), NULL);
	if(layer==NULL) return PARAM_KO;

	char* name = Tcl_GetString(objv[2]);
	char buf[128];

	if(strcmp(name, "type") == 0) {
		Tcl_SetResult(interp, (char*)layer->typenamel, TCL_VOLATILE);
	}

	else if(strcmp(name, "prev") == 0) {
		if(layer->prev == nullptr) {
			printf("%s - Error: Layer %s%u has no previous layer\n", Tcl_GetString(objv[0]), layer->typenamel, layer->typeidx);
			return TCL_ERROR;
		}
		sprintf(buf, "%s%u", layer->prev->typenamel, layer->prev->typeidx);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "next") == 0) {
		if(layer->next == nullptr) {
			printf("%s - Error: Layer %s%u has no next layer\n", Tcl_GetString(objv[0]), layer->typenamel, layer->typeidx);
			return TCL_ERROR;
		}
		sprintf(buf, "%s%u", layer->next->typenamel, layer->next->typeidx);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "fsize_in") == 0) {
		sprintf(buf, "%u", layer->fsize);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "fsize_out") == 0) {
		sprintf(buf, "%u", layer->out_fsize);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "wdata_in") == 0) {
		sprintf(buf, "%u", layer->wdata);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "wdata_out") == 0) {
		sprintf(buf, "%u", layer->out_wdata);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "sdata_in") == 0) {
		sprintf(buf, "%u", layer->sdata);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "sdata_out") == 0) {
		sprintf(buf, "%u", layer->out_sdata);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "par_in") == 0) {
		sprintf(buf, "%u", layer->split_in);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "par_out") == 0) {
		sprintf(buf, "%u", layer->split_out);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else if(strcmp(name, "cycles_in") == 0) {
		sprintf(buf, "%u", layer->cycles);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "cycles_out") == 0) {
		sprintf(buf, "%u", layer->out_cycles);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "cycles_max") == 0) {
		unsigned cy = GetMax(layer->cycles, layer->out_cycles);
		sprintf(buf, "%u", cy);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	// Window parameters
	else if(strcmp(name, "win_x") == 0) {
		sprintf(buf, "%u", layer->winx);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "win_y") == 0) {
		sprintf(buf, "%u", layer->winy);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "win_xy") == 0) {
		sprintf(buf, "%u", layer->winx * layer->winy);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	// Neuron parameters
	else if(strcmp(name, "tmux") == 0) {
		sprintf(buf, "%u", layer->neu_time_mux);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	// Memory implementation
	else if(strcmp(name, "mem")==0) {
		const char * implem = layer->mem.GetStyleName();
		Tcl_SetResult(interp, (char*)implem, TCL_VOLATILE);
	}
	else if(strcmp(name, "mem_width")==0) {
		sprintf(buf, "%u", layer->mem.width);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "mem_lines")==0) {
		sprintf(buf, "%u", layer->mem.lines);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "mem_num")==0) {
		sprintf(buf, "%u", layer->mem.num);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}
	else if(strcmp(name, "mem_size")==0) {
		sprintf(buf, "%lu", layer->mem.EvalSizeTotal());
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	}

	else {
		printf("%s - Error: Unknown name '%s'\n", Tcl_GetString(objv[0]), name);
		return TCL_ERROR;
	}

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

//============================================
// Utility functions to set layer parameters
//============================================

// Set global network parameters
static int nn_param_eq(const char* name, const char* val1, const char* val2, const char* val3){
	auto network = Network::GetSingleton();
	auto& layers = network->layers;

	unsigned non_empty_nb = nn_get_non_empty_val(val1, val2, val3);

	// Global options

	if(strcasecmp(name, "debug")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		param_debug = b;
	}
	else if(strcasecmp(name, "tcl_cb_flush")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		fflush_after_callback = b;
	}

	// Options for NN topology

	else if(strcasecmp(name, "f")==0 || strcasecmp(name, "frame_size")==0) {
		if(non_empty_nb != 3) return PARAM_WRONG_NB;
		network->param_fx = atoi(val1);
		network->param_fy = atoi(val2);
		network->param_fz = atoi(val3);
		if(network->param_fx <= 0 || network->param_fy <= 0 || network->param_fz <= 0) {
			printf("Error param %s: Values must be >0\n", name);
			return PARAM_KO;
		}
	}
	else if(strcasecmp(name, "bin")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		if(strcmp(val1, "sym") == 0 || strcmp(val1, "symmetric") == 0) {
			network->param_win = 1;
			network->param_sin = true;
			network->default_neu_wd = 1;
			network->default_neu_ww = 1;
			network->default_neu_sd = true;
			network->default_neu_sw = true;
			network->default_neu_so = true;
		}
		else {
			network->param_win = 1;
			network->param_sin = false;
			network->default_neu_wd = 1;
			network->default_neu_ww = 1;
			network->default_neu_sd = false;
			network->default_neu_sw = false;
			network->default_neu_so = false;
		}
	}
	else if(strcasecmp(name, "ter")==0) {
		network->param_win = 2;
		network->param_sin = true;
		network->default_neu_wd = 2;
		network->default_neu_ww = 2;
		network->default_neu_sd = true;
		network->default_neu_sw = true;
		network->default_neu_so = true;
	}
	else if(strcasecmp(name, "in")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int z = decodeparam_width_sign(val1, &network->param_win, &network->param_sin, NULL);
		if(z != 0) return PARAM_KO;
	}
	else if(strcasecmp(name, "inpar")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->param_inpar = atoi(val1);
		// Propagate the parallelism
		if(network->param_inpar > 0 && layers.size() > 0) {
			network->layer_first->split_in = network->param_inpar;
			network->propag_params();
		}
	}

	else if(strcasecmp(name, "acts")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int z = decodeparam_width_sign(val1, &network->default_neu_wd, &network->default_neu_sd, NULL);
		if(z != 0) return PARAM_KO;
	}
	else if(strcasecmp(name, "weights")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int z = decodeparam_width_sign(val1, &network->default_neu_ww, &network->default_neu_sw, NULL);
		if(z != 0) return PARAM_KO;
	}
	else if(strcasecmp(name, "outs")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int z = decodeparam_width_sign(val1, &network->default_neu_wo, &network->default_neu_so, NULL);
		if(z != 0) return PARAM_KO;
	}

	else if(strcasecmp(name, "norm_mulcst")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->default_norm_mul_cst = atoi(val1);
	}
	else if(strcasecmp(name, "norm_shrcst")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->default_norm_shr_cst = atoi(val1);
	}
	else if(strcasecmp(name, "norm_wmul")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->default_norm_wmul = atoi(val1);
	}
	else if(strcasecmp(name, "norm_wbias")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->default_norm_wbias = atoi(val1);
	}
	else if(strcasecmp(name, "norm_wshr")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->default_norm_wshr = atoi(val1);
	}

	else if(strcasecmp(name, "relu")==0) {
		if(non_empty_nb != 2) return PARAM_WRONG_NB;
		network->default_relu_min = atoi(val1);
		network->default_relu_max = atoi(val2);
	}

	else if(strcasecmp(name, "leaky")==0) {
		if(non_empty_nb != 2) return PARAM_WRONG_NB;
		network->default_leaky_min = atoi(val1);
		network->default_leaky_max = atoi(val2);
	}

	// Options for input frames and config files

	else if(strcasecmp(name, "if")==0 || strcasecmp(name, "frames")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		filename_frames = strdup(val1);
	}
	else if(strcasecmp(name, "fn")==0 || strcasecmp(name, "frames_nb")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		param_fn = atoi(val1);
	}
	else if(strcasecmp(name, "floop")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		param_floop = b;
	}
	else if(strcasecmp(name, "ml")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		param_multiline = b;
	}
	else if(strcasecmp(name, "worder")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		unsigned worder = nn_get_weights_order(val1);
		if(worder == NEU_WORDER_NONE) return PARAM_KO;
		network->default_neu_worder = worder;
	}

	// Options for outputs

	else if(strcasecmp(name, "ol")==0 || strcasecmp(name, "outlayer")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		const char* strid = val1;
		layer_t* layer = getlayer_verbose(network, strid, NULL, name);
		if(layer==nullptr) return PARAM_KO;
		param_out_layer = layer;
		// Note : In case the out layer is "last", param_out_layer points to the last FIFO instead of the last non-FIFO layer
		//   Which leads to an error about the HW only supporting output at last layer
		if(strcmp(strid, "last") == 0) {
			param_out_layer = nullptr;
		}
	}
	else if(strcasecmp(name, "noout")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		param_noout = b;
	}
	else if(strcasecmp(name, "no")==0 || strcasecmp(name, "nout")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		unsigned nout = atoi(val1);
		network->cnn_outneu = nout;
		// FIXME Using the locked field of the Hwacc object should be more appropriate
		if(network->param_cnn_origin == Network::CNN_ORIGIN_HARDWARE && network->param_rdonly == true) { }
		else {
			apply_outneu(network, nout);
		}
	}
	else if(strcasecmp(name, "o")==0 || strcasecmp(name, "output")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		filename_out = strdup(val1);
		chkoutfile();
	}
	else if(strcasecmp(name, "omask")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		param_out_mask = b;
	}
	else if(strcasecmp(name, "ofmt")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		param_out_format = strdup(val1);
	}
	else if(strcasecmp(name, "onl")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		param_out_nl = atoi(val1);
	}

	// Options for VHDL generation

	else if(strcasecmp(name, "fifomon")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		network->param_fifomon = b;
	}
	else if(strcasecmp(name, "selout")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		network->param_selout = b;
	}
	else if(strcasecmp(name, "noregs")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		network->param_noregs = b;
	}
	else if(strcasecmp(name, "rdonly")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		network->param_rdonly = b;
	}

	// Options for VHDL generation

	else if(strcasecmp(name, "neu_style")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->hwconfig_neu_style = atoi(val1);
	}
	else if(strcasecmp(name, "win_mem")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		MemImplem::style_type implem = MemImplem::GetStyleVerbose(val1);
		if(implem == MemImplem::STYLE_NONE) return PARAM_KO;
		network->default_mem_implem_win = implem;
	}
	else if(strcasecmp(name, "neu_mem")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		MemImplem::style_type implem = MemImplem::GetStyleVerbose(val1);
		if(implem == MemImplem::STYLE_NONE) return PARAM_KO;
		network->default_mem_implem_neu = implem;
	}
	else if(strcasecmp(name, "ifw")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->hwconfig_writewidth = atoi(val1);
	}
	else if(strcasecmp(name, "lut_threshold")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->hwconfig_lut_threshold = atoi(val1);
	}
	else if(strcasecmp(name, "use_uram")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		network->hwconfig_use_uram = b;
	}
	else if(strcasecmp(name, "bram_opt_speed")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		network->hwconfig_bram_opt_speed = b;
	}
	else if(strcasecmp(name, "round_near")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		network->default_round_nearest = b;
	}

	else if(strcmp(name, "comp_all") == 0) {
		if(non_empty_nb != 3) return PARAM_WRONG_NB;
		network->default_comp_all_style = atoi(val1);
		network->default_comp_all_nraw  = atoi(val2);
		network->default_comp_all_nbin  = atoi(val3);
	}
	else if(strcmp(name, "comp_bram") == 0) {
		if(non_empty_nb != 3) return PARAM_WRONG_NB;
		network->default_comp_bram_style = atoi(val1);
		network->default_comp_bram_nraw  = atoi(val2);
		network->default_comp_bram_nbin  = atoi(val3);
	}
	else if(strcmp(name, "comp_fc") == 0) {
		if(non_empty_nb != 3) return PARAM_WRONG_NB;
		network->default_comp_fc_style = atoi(val1);
		network->default_comp_fc_nraw  = atoi(val2);
		network->default_comp_fc_nbin  = atoi(val3);
	}

	else if(strcasecmp(name, "no_fifo_win_neu_th")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->nofifo_win_neu_th = atoi(val1);
	}
	else if(strcasecmp(name, "no_fifo_win_pool")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->nofifo_win_pool = str2bool(val1);
	}
	else if(strcasecmp(name, "no_fifo_neu_relu")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->nofifo_neu_relu = str2bool(val1);
	}
	else if(strcasecmp(name, "no_fifo_norm_relu")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->nofifo_norm_relu = str2bool(val1);
	}
	else if(strcasecmp(name, "no_fifo_neu_leaky")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->nofifo_neu_leaky = str2bool(val1);
	}
	else if(strcasecmp(name, "no_fifo_norm_leaky")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		network->nofifo_norm_leaky = str2bool(val1);
	}

	#ifndef LIMITED
	else if(strcasecmp(name, "vhdl_prefix")==0) {
		if(non_empty_nb > 1) return PARAM_WRONG_NB;
		if(vhdl_gen_prefix != NULL) free((void*)vhdl_gen_prefix);  // Free old C style
		vhdl_gen_prefix = (non_empty_nb == 1) ? strdup(val1) : NULL;
	}
	#endif

	// Hardware accelerator usage

	else if(strcmp(name, "hw_fbufsz")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		param_bufsz_mb = atoi(val1);
	}
	else if(strcmp(name, "hw_freerun")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		param_freerun = b;
	}
	else if(strcmp(name, "hw_timeout")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		unsigned long us = 0;
		int z = decodeparam_us(val1, &us);
		if(z != 0) return PARAM_KO;
		param_timeout_regs_us = us;
		param_timeout_send_us = us;
		param_timeout_recv_us = us;
	}
	else if(strcmp(name, "hw_timeout_regs")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int z = decodeparam_us(val1, &param_timeout_regs_us);
		if(z != 0) return PARAM_KO;
	}
	else if(strcmp(name, "hw_timeout_send")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int z = decodeparam_us(val1, &param_timeout_send_us);
		if(z != 0) return PARAM_KO;
	}
	else if(strcmp(name, "hw_timeout_recv")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int z = decodeparam_us(val1, &param_timeout_recv_us);
		if(z != 0) return PARAM_KO;
	}
	else if(strcmp(name, "hw_blind")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		param_hw_blind = b;
	}

	else if(strcasecmp(name, "swexec_err_lin")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		swexec_emulate_error_lin = strtod_perc(val1);
	}

	#ifndef LIMITED
	else if(strcasecmp(name, "vd")==0 || strcasecmp(name, "vhdl_dumpcfg")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		genvhdl_dump_dir = strdup(val1);
	}
	#endif

	else {
		printf("Error: Unknown parameter name '%s'\n", name);
		return PARAM_KO;
	}

	return PARAM_OK;
}

// Set parameters on a layer, or in global parameters
static int nn_layer_param_eq(layer_t* layer, const char* name, const char* val1, const char* val2, const char* val3){

	// If no layer is specified, apply to global parameters
	if(layer == NULL) {
		return nn_param_eq(name, val1, val2, val3);
	}

	auto network = Network::GetSingleton();

	unsigned non_empty_nb = nn_get_non_empty_val(val1, val2, val3);

	// This is mostly for layers after SCATTER, for which the fsize of successors can be lower
	// Also for layer GATHER, for which the fsize of successor can be higher than predecessors
	if(strcasecmp(name, "fz")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->fz = atoi(val1);
	}
	else if(strcasecmp(name, "fsize")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->fsize = atoi(val1);
	}

	else if(strcasecmp(name, "par")==0) {
		if(non_empty_nb < 1 || non_empty_nb > 2) return PARAM_WRONG_NB;
		int par_in = atoi(val1);
		int par_out = par_in;
		if(non_empty_nb == 2) par_out = atoi(val2);
		layer->split_in = par_in;
		layer->split_out = par_out;
	}
	else if(strcasecmp(name, "par_in")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->split_in = atoi(val1);
	}
	else if(strcasecmp(name, "par_out")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->split_out = atoi(val1);
	}

	else if(strcasecmp(name, "prev")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer_t* layer_prev = getlayer_verbose(network, val1, NULL, name);
		if(layer_prev == NULL) return PARAM_KO;
		if(layer_prev->next_is_arr == true) {
			layer_link(layer_prev, layer);
		}
		else if(layer->prev_is_arr == true) {
			layer_link(layer_prev, layer);
		}
		else {
			printf("Error: Layer %s%u is not a valid previous layer for layer %s%u\n", layer_prev->typenamel, layer_prev->typeidx, layer->typenamel, layer->typeidx);
			return PARAM_KO;
		}
	}

	// FIXME This will probably not be taken into account for most layer types
	else if(strcasecmp(name, "outs")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int z = decodeparam_width_sign(val1, &layer->out_wdata, NULL, &layer->out_sdata);
		if(z != 0) return PARAM_KO;
	}

	else if(strcasecmp(name, "neu")==0) {
		if(non_empty_nb < 1 || non_empty_nb > 2) return PARAM_WRONG_NB;
		int neurons = atoi(val1);
		int neurons_max = neurons;
		if(non_empty_nb == 2) neurons_max = atoi(val2);
		layer->neurons = neurons;
		layer->neurons_max = neurons_max;
	}
	else if(strcasecmp(name, "const_params")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		layer->const_params = b;
	}
	else if(strcasecmp(name, "ww")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->neu_wweight = atoi(val1);
	}
	else if(strcasecmp(name, "weights")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		bool sign = false;
		int z = decodeparam_width_sign(val1, &layer->neu_wweight, &sign, NULL);
		if(z != 0) return PARAM_MALFORMED;
		char want_sgnw = (sign == true) ? NEUSGN_SIGNED : NEUSGN_UNSIGNED;
		// FIXME If this is still used, need a way to set (or not) the parameter LOCKED, to generate run-time configurable signedness
		//want_sgnw |= NEUSGN_LOCKED;
		want_sgnw |= NEUSGN_VALID;
		z = neu_sign_check_set(layer, "weight", want_sgnw, &layer->neu_sgnw);
		if(z != 0) return PARAM_KO;
	}
	else if(strcasecmp(name, "custom_mul")==0) {
		if(non_empty_nb != 1 && non_empty_nb != 2) return PARAM_WRONG_NB;
		layer->neu_custom_mul = 1;
		layer->neu_custom_mul_id = atoi(val1);
		layer->neu_custom_wmul = 0;  // Default
		if(non_empty_nb == 2) {
			int z = decodeparam_width_sign(val2, &layer->neu_custom_wmul, NULL, &layer->neu_custom_smul);
			if(z != 0) return PARAM_MALFORMED;
		}
	}
	else if(strcasecmp(name, "tmux")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->neu_time_mux = atoi(val1);
	}
	else if(strcasecmp(name, "worder")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		unsigned worder = nn_get_weights_order(val1);
		if(worder == NEU_WORDER_NONE) return PARAM_KO;
		layer->neu_worder = worder;
	}

	else if(strcasecmp(name, "comp")==0) {
		if(non_empty_nb != 3) return PARAM_WRONG_NB;
		layer->neu_comp_style = atoi(val1);
		layer->neu_comp_nraw = atoi(val2);
		layer->neu_comp_nbin = atoi(val3);
	}

	else if(strcasecmp(name, "win")==0) {
		if(non_empty_nb < 1 || non_empty_nb > 2) return PARAM_WRONG_NB;
		int winx = atoi(val1);
		int winy = winx;
		if(non_empty_nb == 2) winy = atoi(val2);
		layer->winx = winx;
		layer->winy = winy;
	}
	else if(strcasecmp(name, "winx")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->winx = atoi(val1);
	}
	else if(strcasecmp(name, "winy")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->winy = atoi(val1);
	}
	else if(strcasecmp(name, "step")==0) {
		if(non_empty_nb < 1 || non_empty_nb > 2) return PARAM_WRONG_NB;
		int stepx = atoi(val1);
		int stepy = stepx;
		if(non_empty_nb == 2) stepy = atoi(val2);
		layer->stepx = stepx;
		layer->stepy = stepy;
	}
	else if(strcasecmp(name, "stepx")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->stepx = atoi(val1);
	}
	else if(strcasecmp(name, "stepy")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->stepy = atoi(val1);
	}
	else if(strcasecmp(name, "pad")==0) {
		if(non_empty_nb < 1 || non_empty_nb > 2) return PARAM_WRONG_NB;
		int begpadx = atoi(val1);
		int begpady = begpadx;
		if(non_empty_nb == 2) begpady = atoi(val2);
		layer->begpadx = begpadx;
		layer->begpady = begpady;
	}
	else if(strcasecmp(name, "padx")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->begpadx = atoi(val1);
	}
	else if(strcasecmp(name, "pady")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->begpady = atoi(val1);
	}
	else if(strcasecmp(name, "nwin")==0) {
		if(non_empty_nb < 1 || non_empty_nb > 2) return PARAM_WRONG_NB;
		int nwinx = atoi(val1);
		int nwiny = nwinx;
		if(non_empty_nb == 2) nwiny = atoi(val2);
		layer->nwinx = nwinx;
		layer->nwiny = nwiny;
	}
	else if(strcasecmp(name, "nwinx")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->nwinx = atoi(val1);
	}
	else if(strcasecmp(name, "nwiny")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->nwiny = atoi(val1);
	}
	else if(strcasecmp(name, "bufy")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->bufy = atoi(val1);
	}
	else if(strcasecmp(name, "par_oz")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->user_par_oz = atoi(val1);
		layer->win_par_oz = atoi(val1);
	}
	else if(strcasecmp(name, "repeat")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->win_repeat = atoi(val1);
	}
	else if(strcasecmp(name, "dwconv")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		layer->win_dwconv = b;
	}
	else if(strcasecmp(name, "symxy")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		int b = str2bool(val1);
		if(b < 0) return PARAM_KO;
		layer->win_sym_xy = b;
	}

	else if(strcasecmp(name, "norm_mulcst")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->norm_mul_cst = atoi(val1);
	}
	else if(strcasecmp(name, "norm_shrcst")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->norm_shr_cst = atoi(val1);
	}
	else if(strcasecmp(name, "norm_wmul")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->norm_wmul = atoi(val1);
	}
	else if(strcasecmp(name, "norm_wbias")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->norm_wbias = atoi(val1);
	}
	else if(strcasecmp(name, "norm_wshr")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->norm_wshr = atoi(val1);
	}

	else if(strcasecmp(name, "relu")==0) {
		if(non_empty_nb != 2) return PARAM_WRONG_NB;
		layer->relu_min = atoi(val1);
		layer->relu_max = atoi(val2);
	}
	else if(strcasecmp(name, "leaky")==0) {
		if(non_empty_nb != 2) return PARAM_WRONG_NB;
		layer->leaky_min = atoi(val1);
		layer->leaky_max = atoi(val2);
	}
	else if(strcasecmp(name, "user_id")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->custom_user_id = atoi(val1);
	}

	else if(strcasecmp(name, "cfg")==0 || strcasecmp(name, "config")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->cfg_filename = strdup(val1);
	}
	else if(strcasecmp(name, "mem")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		MemImplem::style_type implem = MemImplem::GetStyleVerbose(val1);
		if(implem == MemImplem::STYLE_NONE) return PARAM_KO;
		layer->mem.style = implem;
	}
	else if(strcasecmp(name, "wout")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->user_wout = atoi(val1);
	}
	else if(strcasecmp(name, "round_near")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->round_nearest = atoi(val1);
	}

	// ASIC-specific parameters
	else if(strcasecmp(name, "zd")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->stat_zd = strtod_perc(val1);
	}
	else if(strcasecmp(name, "nzd_zw")==0) {
		if(non_empty_nb != 1) return PARAM_WRONG_NB;
		layer->stat_nzd_zw = strtod_perc(val1);
	}

	else {
		printf("Error: Unknown parameter name '%s'\n", name);
		return PARAM_KO;
	}

	return PARAM_OK;
}

// Set parameters : Try the different regexs
static int nn_set_param(layer_t* layer, const char* str){

	// Zero length means no action
	if(str[0] == 0) return PARAM_OK;

	// Ensure regexs are initialized
	regcomp_init();

	regmatch_t regmatch_arr[10];
	int retcode = 0;
	unsigned len = strlen(str) + 1;

	bool found_malformed = false;
	bool found_wrong_nb = false;

	// Regexs are intentionally tested with decreasing number of "result" values
	// This way, the parameters that need 2 values but can also tolerate just one, receive the 2-values form in priority

	// Detect name=val1/val2/val3
	retcode = regexec(&regex_eq3s, str, 5, regmatch_arr, 0);
	if(retcode == 0) {
		char buf_name[len];
		char buf_val1[len];
		char buf_val2[len];
		char buf_val3[len];
		regmatch_to_string(regmatch_arr + 1, str, buf_name);
		regmatch_to_string(regmatch_arr + 2, str, buf_val1);
		regmatch_to_string(regmatch_arr + 3, str, buf_val2);
		regmatch_to_string(regmatch_arr + 4, str, buf_val3);
		int retcode = nn_layer_param_eq(layer, buf_name, buf_val1, buf_val2, buf_val3);
		if(retcode == PARAM_OK) return 0;
		if(retcode == PARAM_KO) return 1;
		if(retcode == PARAM_MALFORMED) found_malformed = true;
		if(retcode == PARAM_WRONG_NB)  found_wrong_nb = true;
	}

	// Detect name=val1/val2
	retcode = regexec(&regex_eq2s, str, 4, regmatch_arr, 0);
	if(retcode == 0) {
		char buf_name[len];
		char buf_val1[len];
		char buf_val2[len];
		regmatch_to_string(regmatch_arr + 1, str, buf_name);
		regmatch_to_string(regmatch_arr + 2, str, buf_val1);
		regmatch_to_string(regmatch_arr + 3, str, buf_val2);
		int retcode = nn_layer_param_eq(layer, buf_name, buf_val1, buf_val2, NULL);
		if(retcode == PARAM_OK) return 0;
		if(retcode == PARAM_KO) return 1;
		if(retcode == PARAM_MALFORMED) found_malformed = true;
		if(retcode == PARAM_WRONG_NB)  found_wrong_nb = true;
	}

	// Detect name=val1xval2
	retcode = regexec(&regex_eq2x, str, 4, regmatch_arr, 0);
	if(retcode == 0) {
		char buf_name[len];
		char buf_val1[len];
		char buf_val2[len];
		regmatch_to_string(regmatch_arr + 1, str, buf_name);
		regmatch_to_string(regmatch_arr + 2, str, buf_val1);
		regmatch_to_string(regmatch_arr + 3, str, buf_val2);
		int retcode = nn_layer_param_eq(layer, buf_name, buf_val1, buf_val2, NULL);
		if(retcode == PARAM_OK) return 0;
		if(retcode == PARAM_KO) return 1;
		if(retcode == PARAM_MALFORMED) found_malformed = true;
		if(retcode == PARAM_WRONG_NB)  found_wrong_nb = true;
	}

	// Detect name=[val]
	retcode = regexec(&regex_eq1, str, 3, regmatch_arr, 0);
	if(retcode == 0) {
		char buf_name[len];
		char buf_val[len];
		regmatch_to_string(regmatch_arr + 1, str, buf_name);
		regmatch_to_string(regmatch_arr + 2, str, buf_val);
		int retcode = nn_layer_param_eq(layer, buf_name, buf_val, NULL, NULL);
		if(retcode == PARAM_OK) return 0;
		if(retcode == PARAM_KO) return 1;
		if(retcode == PARAM_MALFORMED) found_malformed = true;
		if(retcode == PARAM_WRONG_NB)  found_wrong_nb= true;
	}

	// Here the pattern was not recognized
	if(found_malformed == true) {
		printf("Error: Malformed parameter expression '%s'\n", str);
	}
	else if(found_wrong_nb == true) {
		printf("Error: Wrong number of values in parameter expression '%s'\n", str);
	}
	else {
		printf("Error: Unrecognized parameter in expression '%s'\n", str);
	}

	return PARAM_KO;
}

// Set parameters : Decode TCL objects and list of objects
static int nn_set_param_list(Tcl_Interp *interp, layer_t* layer, Tcl_Obj* obj) {
	int objc = 0;
	Tcl_Obj** objv = NULL;

	int res = Tcl_ListObjGetElements(interp, obj, &objc, &objv);
	if(res == TCL_ERROR) return TCL_ERROR;

	for(unsigned i=0; (int)i<objc; i++) {
		char* str = Tcl_GetString(objv[i]);
		if(nn_set_param(layer, str) != PARAM_OK) {
			printf("Error while processing argument '%s'\n", str);
			return TCL_ERROR;
		}
	}

	return TCL_OK;
}

//============================================
// TCL callbacks for network
//============================================

// Print help on available network-specific commands
// FIXME This is very incomplete
static int cb_nn_help(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	printf("Parameters applicable to layers are the following.\n");
	printf("  Any layer:\n");
	printf("  - par: PAR_IN or PAR_IN/PAR_OUT. Parallelism levels\n");
	printf("  Neuron layer:\n");
	printf("  - neu: nb_neu/max_neu or nb_neu (max_neu=nb_neu in this case)\n");
	printf("  Window layer:\n");
	printf("  - win_style: zfirst or poolpar\n");
	printf("  - win: arg1xarg2. arg1 is winx and arg2 is winy\n");
	printf("  - winx\n");
	printf("  - winy\n");
	printf("  - step: arg1xarg2. arg1 is stepx and arg2 is stepy\n");
	printf("  - stepx\n");
	printf("  - stepy\n");
	printf("  - pad: arg1xarg2. arg1 is padx and arg2 is pady\n");
	printf("  - padx\n");
	printf("  - pady\n");
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

// Setting global parameters
static int cb_nn_set(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	if (objc < 2) {
		sprintf(errmsg, "%s - Too few arguments. At least one PARAM in the format param=N.", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	// Process all parameters
	for(int i=1; i < objc; i++) {
		int res = nn_set_param_list(interp, NULL, objv[i]);
		if(res == TCL_ERROR) return TCL_ERROR;
	}

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

// Clear any existing network
static int cb_nn_clear(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
  Network::GetSingleton()->clear();
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

// Helper function to decode options to print the network or a layer
static int helper_params_print(Tcl_Interp *interp, int start, int objc, Tcl_Obj *const objv[], unsigned* options, unsigned* layer_type){

	// Process additional parameters, if any
	for(int i=1; i<objc; i++) {
		char* str = Tcl_GetString(objv[i]);
		if(strcmp(str, "-table") == 0) {
			*options |= NNPRINT_OPT_TABLE;
		}
		else if(strcmp(str, "-notable") == 0) {
			*options |= NNPRINT_OPT_TABLE;
		}
		else if(strcmp(str, "-hwid") == 0) {
			*options |= NNPRINT_OPT_HWID;
		}
		else if(strcmp(str, "-cycles") == 0) {
			*options |= NNPRINT_OPT_CYCLES;
		}
		else if(strcmp(str, "-total") == 0) {
			*options |= NNPRINT_OPT_TOTAL;
		}
		else if(strncmp(str, "-type-", 6) == 0) {
			unsigned type = Layer::get_type_name2id_verbose(str + 6);
			if(type == LAYER_NONE) {
				sprintf(errmsg, "%s - Error, unknown argument '%s'", Tcl_GetString(objv[0]), str);
				Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
				return TCL_ERROR;
			}
			*layer_type = type;
		}
		else {
			sprintf(errmsg, "%s - Error, unknown argument '%s'", Tcl_GetString(objv[0]), str);
			Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
			return TCL_ERROR;
		}
	}

	return TCL_OK;
}

// Callback function to print the network
static int cb_nn_print(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	unsigned options = NNPRINT_OPT_TABLE;
	unsigned layer_type = LAYER_NONE;

	int z = helper_params_print(interp, 1, objc, objv, &options, &layer_type);
	if(z == TCL_ERROR) return TCL_ERROR;

	auto network = Network::GetSingleton();
	nnprint(network->layers, options, layer_type);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

// Callback function to print the network memory usage
static int cb_nn_print_mem(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	unsigned options = NNPRINT_OPT_TABLE;
	unsigned layer_type = LAYER_NONE;

	int z = helper_params_print(interp, 1, objc, objv, &options, &layer_type);
	if(z == TCL_ERROR) return TCL_ERROR;

	auto network = Network::GetSingleton();
	nnprint_mem(network->layers, options, layer_type);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

//============================================
// TCL callbacks for layers
//============================================

// Callback function to create a new custom later type
static int cb_nn_custom_layer_type(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	char* layer_type = nullptr;
	char* entity = nullptr;
	unsigned latency = 1;

	// FIXME Use same look and feel than other commands, with <name>=<val>
	for(int i=1; i < objc; i++) {
		char* param = Tcl_GetString(objv[i]);
		if(strcmp(param, "-name") == 0) {
			i++;
			if(i >= objc) break;
			layer_type = Tcl_GetString(objv[i]);
		}
		else if(strcmp(param, "-entity") == 0) {
			i++;
			if(i >= objc) break;
			entity = Tcl_GetString(objv[i]);
		}
		else if(strcmp(param, "-latency") == 0) {
			i++;
			if(i >= objc) break;
			latency = atoi(Tcl_GetString(objv[i]));
		}
		else {
			sprintf(errmsg, "%s - Unknown argument : %s\n", Tcl_GetString(objv[0]), param);
			Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
			return TCL_ERROR;
		}
	}

	if(layer_type ==nullptr) {
		printf("%s - Missing short name for custom layer\n", Tcl_GetString(objv[0]));
		return TCL_ERROR;
	}
	if(entity ==nullptr) {
		printf("%s - Missing entity name for custom layer\n", Tcl_GetString(objv[0]));
		return TCL_ERROR;
	}

	char buf[128];

	// Layer creation
	layer_t* layer = new LayerCustom();
	// Set the type
	layer->type = LAYER_TYPE_MAX + 1 + Layer::map_layer_entity2id_size();
	// Set the type name
	strcpy(buf, layer_type);
	for(unsigned i=0; buf[i]!=0; i++) buf[i] = tolower(buf[i]);
	layer->typenamel = strdup(buf);
	for(unsigned i=0; buf[i]!=0; i++) buf[i] = toupper(buf[i]);
	layer->typenameu = strdup(buf);
	// Set entity
	strcpy(buf, entity);
	for(unsigned i=0; buf[i]!=0; i++) buf[i] = tolower(buf[i]);
	layer->custom_entity = strdup(buf);

	// Register the layer
	int z = Layer::register_type(layer, layer->typenamel);
	if(z != 0) {
		return TCL_ERROR;
	}

	// Other fields
	layer->custom_latency = latency;

	if(fflush_after_callback == true) fflush(nullptr);

	// FIXME For now, no return value
	return TCL_OK;
}

// Callback function to create a layer
static int cb_nn_layer_create(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	auto network = Network::GetSingleton();

	if(network->param_cnn_origin == Network::CNN_ORIGIN_HARDWARE) {
		printf("%s - Can't add layers to a network that represents a hardware accelerator\n", Tcl_GetString(objv[0]));
		return TCL_ERROR;
	}

	if (objc < 2) {
		sprintf(errmsg, "%s - Too few arguments", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	// For debug
	#if 0
	for(int i=0; i < objc; i++){
		printf("arg %d = %s\n", i, Tcl_GetString(objv[i]));
	}
	#endif

	// Type of the layer to create
	char* layer_type = Tcl_GetString(objv[1]);

	// Layer creation
	layer_t* layer = network->layer_new_fromtypename(layer_type);
	if(layer==NULL) {
		printf("%s - Layer type '%s' is not recognized\n", Tcl_GetString(objv[0]), layer_type);
		return TCL_ERROR;
	}

	// Process additional parameters, if any
	for(int i=2; i<objc; i++) {
		int res = nn_set_param_list(interp, layer, objv[i]);
		if(res == TCL_ERROR) {
			sprintf(errmsg, "%s - Error while processing arguments", Tcl_GetString(objv[0]));
			Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
			return TCL_ERROR;
		}
	}

	// Connect
	if(layer->prev_is_arr == true) {
		if(layer->arr_layers.size() == 0) {
			printf("%s - Error : Layer of type %s has no prev layer\n", Tcl_GetString(objv[0]), layer->typenameu);
			return TCL_ERROR;
		}
	}
	else if(layer->prev == nullptr) {
		network->layer_enqueue(layer);
	}

	// Update first and last layer pointers
	if(network->layer_first == nullptr) network->layer_first = layer;
	network->layer_last = layer;

	// In case input parallelism has changed
	propag_backward_par(network, layer);

	// In case this is a NEURON or MAXPOOL layer, propagate from a previous window layer, if any
	layer_t* prev_win = NULL;
	if(layer->type == LAYER_NEU || layer->type == LAYER_POOL) {
		for(layer_t* scanlayer=layer->prev; scanlayer != NULL; scanlayer = scanlayer->prev) {
			if(scanlayer->type == LAYER_FIFO) continue;
			if(scanlayer->type == LAYER_WIN) prev_win = scanlayer;
			break;
		}
	}
	if(prev_win != NULL) {
		propag_params_layer(network, prev_win);
	}
	else {
		propag_params_layer(network, layer);
	}

	// Set return value
	Tcl_Obj* return_index = Tcl_NewObj();
	Tcl_SetIntObj(return_index, layer->id);
	Tcl_SetObjResult(interp, return_index);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

// Callback function to set parameters to a layer
static int cb_nn_layer_set(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	if (objc < 2) {
		sprintf(errmsg, "%s - Too few arguments. At least ID and PARAM in the format param=N. Help: nn_params", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	auto network = Network::GetSingleton();

	// Retrieve layer id
	char* strid = Tcl_GetString(objv[1]);
	layer_t* layer = getlayer_verbose(network, strid, Tcl_GetString(objv[0]), NULL);
	if(layer==NULL) return PARAM_KO;

	// Process all parameters
	for(int i=2; i<objc; i++) {
		int res = nn_set_param_list(interp, layer, objv[i]);
		if(res == TCL_ERROR) {
			sprintf(errmsg, "%s - Error while processing arguments", Tcl_GetString(objv[0]));
			Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
			return TCL_ERROR;
		}
	}

	// Propagate other layer parameters from previous layer
	propag_params_layer(network, layer);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

// Callback to print a layer
static int cb_nn_layer_print(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	if (objc < 2) {
		sprintf(errmsg, "%s - Wrong number of arguments, at least layer name is required ", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	auto network = Network::GetSingleton();

	char* strid = Tcl_GetString(objv[1]);
	layer_t* layer = getlayer_verbose(network, strid, Tcl_GetString(objv[0]), NULL);
	if(layer==NULL) return PARAM_KO;

	unsigned options = NNPRINT_OPT_TABLE;
	unsigned layer_type = LAYER_NONE;

	int z = helper_params_print(interp, 2, objc, objv, &options, &layer_type);
	if(z == TCL_ERROR) return TCL_ERROR;

	nnprint_layer(layer, options);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

#ifndef LIMITED

static int cb_nn_layer_finalize_hw_config(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	if (objc < 2) {
		sprintf(errmsg, "%s - Too few arguments", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	auto network = Network::GetSingleton();

	// Retrieve layer id
	char* strid = Tcl_GetString(objv[1]);
	layer_t* layer = getlayer_verbose(network, strid, Tcl_GetString(objv[0]), NULL);
	if(layer==NULL) return PARAM_KO;

	layer->hwconfig_finalize();

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_finalize_hw_config(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	auto network = Network::GetSingleton();

	network->hwconfig_finalize();
	network->insert_fifos();

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_layer_loadcfg(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	if (objc < 2) {
		sprintf(errmsg, "%s - Too few arguments", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	auto network = Network::GetSingleton();

	// Retrieve layer id
	char* strid = Tcl_GetString(objv[1]);
	layer_t* layer = getlayer_verbose(network, strid, Tcl_GetString(objv[0]), NULL);
	if(layer==NULL) return PARAM_KO;

	network->load_config_files();

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_loadcfg(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	auto network = Network::GetSingleton();
	network->load_config_files();

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

// Callback function to generate the VHDL wrapper
static int cb_nn_genvhdl(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	char* fi = NULL;
	char* fo = NULL;

	if (objc == 2) {
		fi = Tcl_GetString(objv[1]);
		fo = Tcl_GetString(objv[1]);
	}
	else if (objc == 3) {
		fi = Tcl_GetString(objv[1]);
		fo = Tcl_GetString(objv[2]);
	}
	else {
		sprintf(errmsg, "%s - Expected arguments are : <template-file> [dest-file]", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	auto network = Network::GetSingleton();
	network->check_integrity();  // Paranoia

	int z = vhdl_gen(network, fi, fo, vhdl_gen_prefix, false);
	if(z != 0) {
		return TCL_ERROR;
	}

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}
// Callback function to generate the VHDL wrapper
static int cb_nn_genvhdl_const_weights(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	char* fi = NULL;
	char* fo = NULL;

	if (objc == 2) {
		fi = Tcl_GetString(objv[1]);
		fo = Tcl_GetString(objv[1]);
	}
	else if (objc == 3) {
		fi = Tcl_GetString(objv[1]);
		fo = Tcl_GetString(objv[2]);
	}
	else {
		sprintf(errmsg, "%s - Expected arguments are : <template-file> [dest-file]", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	auto network = Network::GetSingleton();
	network->check_integrity();  // Paranoia

	int z = vhdl_gen_const_params(network, fi, fo, false);
	if(z != 0) {
		return TCL_ERROR;
	}

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

// Callback function to dump layers configuration
static int cb_nn_dump_config(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	auto network = Network::GetSingleton();
	vhdl_dumpconfig(network);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_comp_2t3b_test(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	auto network = Network::GetSingleton();
	compress_2t3b_test(network);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

#endif  // ifndef LIMITED

static int cb_nn_autopar(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	if (objc != 2) {
		sprintf(errmsg, "%s - Wrong number of arguments, only desired parallelism level is required ", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	auto network = Network::GetSingleton();

	unsigned par = atoi(Tcl_GetString(objv[1]));
	apply_parallelism(network, par);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_autotmux(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	auto network = Network::GetSingleton();
	apply_time_mux(network);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_maxtmux(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	auto network = Network::GetSingleton();
	apply_time_mux_max(network);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_parin_with_tmux(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	auto network = Network::GetSingleton();
	apply_parin_with_time_mux(network);

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_swexec(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	auto network = Network::GetSingleton();
	int z = swexec(network, param_out_layer);
	if(z != 0) return TCL_ERROR;

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

#ifdef HAVE_RIFFA

static int cb_nn_riffa_init(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	HwAcc_Common* hwacc = HwAcc_PcieRiffa::GetSingleton();
	HwAcc_Common::CurrentHwAcc_Set(hwacc);
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

static int cb_nn_riffa_reset(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	auto hwacc_riffa = HwAcc_PcieRiffa::GetSingleton();
	fpga_reset(hwacc_riffa->fpga);
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

#endif  // ifdef HAVE_RIFFA

#ifdef HAVE_ZYNQ7

static int cb_nn_zynq7_init(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	HwAcc_Common* hwacc = HwAcc_Zynq7::GetSingleton();
	HwAcc_Common::CurrentHwAcc_Set(hwacc);
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

#endif  // ifdef HAVE_ZYNQ7

static int cb_nn_hwacc_init(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
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
		sprintf(errmsg, "%s - Error failed to automatically find a hardware accelerator", Tcl_GetString(objv[0]));
		Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
		return TCL_ERROR;
	}

	HwAcc_Common::CurrentHwAcc_Set(hwacc);
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

static int cb_nn_hwacc_clear(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	auto hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
	hwacc->accreg_clear();
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

static int cb_nn_hwacc_build(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	auto hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
	auto network = Network::GetSingleton();
	hwacc->build_network(network);
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

static int cb_nn_hwacc_fifoscan(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	auto hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
	auto network = Network::GetSingleton();
	hwacc->print_fifos(network);
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

static int cb_nn_hwacc_run(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){

	// Save global parameter
	bool save_blind = param_hw_blind;

	// Parse extra options
	for(int j=1; j < objc; j++) {
		char* str = Tcl_GetString(objv[j]);
		if(strcmp(str, "-blind") == 0) {
			param_hw_blind = true;
		}
		else {
			sprintf(errmsg, "%s - Error unknown argument '%s'", Tcl_GetString(objv[0]), str);
			Tcl_SetResult(interp, errmsg, TCL_VOLATILE);
			return TCL_ERROR;
		}
	}

	// Actual run
	auto hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
	auto network = Network::GetSingleton();
	hwacc->run(network);

	// Restore global parameter
	param_hw_blind = save_blind;

	if(fflush_after_callback == true) fflush(nullptr);

	return TCL_OK;
}

static int cb_nn_hwacc_close(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
	auto hwacc = HwAcc_Common::CurrentHwAcc_GetCheck();
	delete hwacc;
	HwAcc_Common::CurrentHwAcc_Set(nullptr);
	if(fflush_after_callback == true) fflush(nullptr);
	return TCL_OK;
}

// Declare our own commands to the TCL interpreter
static int tcl_add_commands(Tcl_Interp *interp) {

	Tcl_CreateObjCommand(interp, "nn_help",         cb_nn_help, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_get",          cb_nn_get, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_layer_get",    cb_nn_layer_get, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_set",          cb_nn_set, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_clear",        cb_nn_clear, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_print",        cb_nn_print, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_print_mem",    cb_nn_print_mem, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_custom_layer_type", cb_nn_custom_layer_type, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_layer_create", cb_nn_layer_create, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_layer_set",    cb_nn_layer_set, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_layer_print",  cb_nn_layer_print, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_autopar",      cb_nn_autopar, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_autotmux",     cb_nn_autotmux, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_maxtmux",      cb_nn_maxtmux, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_maxparin",     cb_nn_parin_with_tmux, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_swexec",       cb_nn_swexec, (ClientData) NULL, NULL);

	#ifndef LIMITED
	Tcl_CreateObjCommand(interp, "nn_layer_finalize_hw_config", cb_nn_layer_finalize_hw_config, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_finalize_hw_config", cb_nn_finalize_hw_config, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_layer_load_config", cb_nn_layer_loadcfg, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_load_config",  cb_nn_loadcfg, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_genvhdl",      cb_nn_genvhdl, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_genvhdl_const_weights", cb_nn_genvhdl_const_weights, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_dump_config",  cb_nn_dump_config, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_comp_2t3b_test", cb_nn_comp_2t3b_test, (ClientData) NULL, NULL);
	#endif

	#ifdef HAVE_RIFFA
	Tcl_CreateObjCommand(interp, "nn_riffa_init",    cb_nn_riffa_init, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_riffa_reset",   cb_nn_riffa_reset, (ClientData) NULL, NULL);
	#endif

	#ifdef HAVE_ZYNQ7
	Tcl_CreateObjCommand(interp, "nn_zynq7_init",    cb_nn_zynq7_init, (ClientData) NULL, NULL);
	#endif

	Tcl_CreateObjCommand(interp, "nn_hwacc_init",    cb_nn_hwacc_init, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_hwacc_clear",   cb_nn_hwacc_clear, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_hwacc_build",   cb_nn_hwacc_build, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_hwacc_fifoscan", cb_nn_hwacc_fifoscan, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_hwacc_run",     cb_nn_hwacc_run, (ClientData) NULL, NULL);
	Tcl_CreateObjCommand(interp, "nn_hwacc_close",   cb_nn_hwacc_close, (ClientData) NULL, NULL);

	return TCL_OK;
}

//============================================
// Main interface
//============================================

int tcl_init_interp(const char* argv0) {
	if(interp != nullptr) return 0;

	// Get the process name of the file from which the app was invoked, and save it for Tcl's internal use
	Tcl_FindExecutable(argv0);

	// Initialize regexs
	regcomp_init();

	interp = Tcl_CreateInterp();

	// Initialazing the TCL interpreter
	if(Tcl_Init(interp) != TCL_OK) {
		fprintf(stderr, "Tcl_Init error: %s\n", Tcl_GetStringResult(interp));
		Tcl_Exit(EXIT_FAILURE);
	}

	// Extend the TCL interpreter with our own commands
	if(tcl_add_commands(interp) != TCL_OK) {
		fprintf(stderr, "ExtendTcl error: %s\n", Tcl_GetStringResult(interp));
		Tcl_Exit(EXIT_FAILURE);
	}

	return 0;
}

int tcl_exec_line(const char* tcl_line) {

	if(interp == nullptr) {
		fprintf(stderr, "Internal Error: TCL interpreter is not initialized\n");
		exit(EXIT_FAILURE);
	}

	// Parse the TCL string
	int retcode = Tcl_Eval(interp, tcl_line);
	if(retcode == TCL_ERROR) {
		fprintf(stderr, "Tcl_EvalFile error: %s\n", Tcl_GetStringResult(interp));
		Tcl_Exit(EXIT_FAILURE);
	}

	return 0;
}

int tcl_exec_file(const char* tcl_filename) {

	if(interp == nullptr) {
		fprintf(stderr, "Internal Error: TCL interpreter is not initialized\n");
		exit(EXIT_FAILURE);
	}

	// Check access to the script file
	int ret = access(tcl_filename, F_OK);
	if(ret == -1 ) {
		fprintf(stderr, "Error: TCL script file %s not found\n", tcl_filename);
		return 1;
	}

	printf("TCL : Executing file %s\n", tcl_filename);

	// Parse the TCL command file
	int retcode = Tcl_EvalFile(interp, tcl_filename);
	if(retcode == TCL_ERROR) {
		fprintf(stderr, "Tcl_EvalFile error: %s\n", Tcl_GetStringResult(interp));
		Tcl_Exit(EXIT_FAILURE);
	}

	printf("TCL : Successfully executed file %s\n", tcl_filename);

	return 0;
}

int tcl_clear(void) {

	// Free regexs if necessary
	regcomp_free();

	// Clear the TCL context
	Tcl_Finalize();

	// Mark the interpreter as deleted
	interp = nullptr;

	return 0;
}

