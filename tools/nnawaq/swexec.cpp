// Software execution/simulation of image inference task
// Useful to create reference output data and to compare to hardware behaviour

extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <time.h>

#include "nnawaq_utils.h"
#include "load_config.h"

}

#include "nnawaq.h"
#include "nn_load_config.h"
#include "swexec.h"


// Shared variables

unsigned swexec_param_mod = 0;
bool     swexec_mode_tcam = false;
bool     swexec_gen_in = false;

// To emulate computing errors with a linear distribution
// This is the maximum error ratio
double swexec_emulate_error_lin;

//============================================
// Software execution
//============================================

// Perform shift right with rounding reverse-engineered from tflite sources (thanks Frédéric Pétrot)
int64_t shround64_tf1(int64_t val, int shr) {
	if(shr == 0) return val;
	int64_t m = ((int64_t)1) << (shr-1);
	int64_t rnd = (val >= 0) ? m : (1 - m);
	return (val + rnd) / (((int64_t)1) << shr);
}
int64_t shround64_tf2(int64_t val, int shr) {
	int64_t m = (((int64_t)1) << shr) - 1;
	int64_t u = val & m;
	int64_t t = (m >> 1) + (val < 0);
	return (val >> shr) + (u > t);
}

static int swexec_print(FILE* Fo, layer_t* layer, int* bufin, int* bufout, unsigned f) {
	if(Fo==stdout) printf("RESULT: Frame %u: ", f);

	unsigned mask = ~0;
	if(param_out_mask==true) mask = ((unsigned)~0) >> (32 - layer->out_wdata);

	// Special case: Emulate the hardware padding with zeros at end of Fz dimension
	// FIXME This code was previously here for Z-first scan order, need to be adapted
	#if 0
	if(
		layer->type == LAYER_WIN &&
		swexec_param_mod > 1 && layer->fz > swexec_param_mod && layer->fz % swexec_param_mod != 0
	) {
		unsigned oidx = 0;
		for(unsigned i=0; i<layer->out_nbframes*layer->out_fsize; i+=layer->fz) {
			for(unsigned z=0; z<layer->out_fz; z+=swexec_param_mod) {
				if(param_out_nl > 0 && oidx > 0 && oidx % param_out_nl == 0) fprintf(Fo, "\n");
				else if(oidx > 0 && param_out_sep != NULL) fprintf(Fo, "%s", param_out_sep);
				fprintf(Fo, param_out_format, bufout[i+z] & mask);
				oidx++;
			}
		}
		fprintf(Fo, "\n");
		if(param_out_nl > 0) fprintf(Fo, "\n");
		return 0;
	}
	#endif

	// Select output side by default
	unsigned print_fsize = layer->out_nbframes*layer->out_fsize;
	int*     print_pdata = bufout;

	// Select input side
	if(swexec_gen_in==true) {
		print_fsize = layer->nbframes*layer->fsize;
		print_pdata = bufin;
		if(param_out_mask==true) mask = ((unsigned)~0) >> (32 - layer->wdata);
	}

	// Print outputs
	unsigned oidx = 0;
	for(unsigned i=0; i<print_fsize; i++) {
		if(swexec_param_mod > 1 && i % swexec_param_mod != 0) continue;
		if(param_out_nl > 0 && oidx > 0 && oidx % param_out_nl == 0) fprintf(Fo, "\n");
		else if(oidx > 0 && param_out_sep != NULL) fprintf(Fo, "%s", param_out_sep);
		fprintf(Fo, param_out_format, print_pdata[i] & mask);
		oidx++;
	}
	fprintf(Fo, "\n");
	if(param_out_nl > 0) fprintf(Fo, "\n");

	return 0;
}

static int** recode_tcam_style = NULL;

int Layer::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	printf("Error: Layer type %s is not handled yet in swexec\n", typenameu);
	exit(EXIT_FAILURE);
	return 0;
}

int LayerWin::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	// Variable to ease code refactoring
	Layer* layer = this;
	printf("Layer Index WIN: %u\n", layer->index);
	int* loc_bufout = bufout;
	unsigned buf_xz = layer->fx * layer->fz;

	// Slide the window along X, then Y. Each time, copy the full Z depth
	int winy = -layer->begpady;
	for(unsigned ny=0; ny<layer->nwiny; ny++) {
		int winx = -layer->begpadx;
		for(unsigned nx=0; nx<layer->nwinx; nx++) {

			int winz = 0;
			for(unsigned nz=0; nz<layer->fz; nz+=layer->win_par_oz) {

				// Scan inside the window, X and Y
				int posy = winy;
				for(unsigned wy=0; wy<layer->winy; wy++) {
					int posx = winx;
					for(unsigned wx=0; wx<layer->winx; wx++) {

						// Scan inside the window, PAR_OZ
						for(unsigned pz=0; pz<layer->win_par_oz; pz++) {

							// Copy one value
							if(posx<0 || posy<0 || posx>=(int)layer->fx || posy>=(int)layer->fy) {
								// This is padding
								loc_bufout[0] = 0;
							}
							else {
								unsigned inidx = posy * buf_xz + posx * layer->fz + winz + pz;
								loc_bufout[0] = bufin[inidx];
							}
							loc_bufout ++;

						}  // Scan along Z (PAR_OZ)

						posx++;
					}  // Scan inside the window along X
					posy++;
				}  // Scan inside the window along Y

				winz += layer->win_par_oz;
			}  // Move the window along Z

			winx += layer->stepx;
		}  // Move the window along X
		winy += layer->stepy;
	}  // Move the window along Y

	return 0;
}

int LayerWin_CM::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
    // Variable pour faciliter la refactorisation
    Layer* layer = this;
    printf("Layer Index WIN_CM (YOLOv2 channel-major ordering): %u\n", layer->index);

    // Taille d'une image (pour un canal) en entrée
    unsigned buf_xy = layer->fx * layer->fy;
    int* loc_bufout = bufout;

    // Réorganisation : pour chaque canal, extraire tous les patchs
    for (unsigned nz = 0; nz < layer->fz; nz += layer->win_par_oz) {
        for (unsigned pz = 0; pz < layer->win_par_oz; pz++) {
            int channel = nz + pz;
            // Pour ce canal, on parcourt toutes les positions de la fenêtre (patch)
            for (unsigned ny = 0; ny < layer->nwiny; ny++) {
                int win_y = ny * layer->stepy - layer->begpady;
                for (unsigned nx = 0; nx < layer->nwinx; nx++) {
                    int win_x = nx * layer->stepx - layer->begpadx;
                    // Extraction du patch dans la région spatiale
                    int posy = win_y;
                    for (unsigned wy = 0; wy < layer->winy; wy++) {
                        int posx = win_x;
                        for (unsigned wx = 0; wx < layer->winx; wx++) {
                            if (posx < 0 || posy < 0 || posx >= (int)layer->fx || posy >= (int)layer->fy)
                                *loc_bufout = 0;
                            else {
                                unsigned inidx = channel * buf_xy + posy * layer->fx + posx;
                                *loc_bufout = bufin[inidx];
                            }
                            loc_bufout++;
                            posx++;
                        }
                        posy++;
                    }
                }
            }
        }
    }

    return 0;
}



int LayerNeu::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {

	// Variable to ease code refactoring
	Layer* layer = this;

	if(layer->neu_custom_mul != 0) {
		printf("Warning: Layer %s%u has custom multiplication operation ID %u, this is not handled in SW execution\n", layer->typenameu, layer->typeidx, layer->neu_custom_mul_id);
	}

	int* loc_bufin  = bufin;
	int* loc_bufout = bufout;
	for(unsigned k=0; k<layer->nbframes; k++) {
		for(unsigned n=0; n<layer->neurons; n++) {
			int* weights = layer->cfg_data[n];

			int* arr_recode_tcam = NULL;
			if(swexec_mode_tcam==true) {
				arr_recode_tcam = recode_tcam_style[layer->typeidx];
			}

			// Normal, digital neuron
			if(arr_recode_tcam==NULL) {
				int sum = 0;
				int64_t sum2 = 0;
				for(unsigned i=0; i<layer->fsize; i++) sum += weights[i] * loc_bufin[i];
				for(unsigned i=0; i<layer->fsize; i++) sum2 += weights[i] * loc_bufin[i];

				if (sum2!=sum){
					printf("########## Overflow !##########\n\n\n");
				}


				loc_bufout[n] = sum;
			}
			// Neuron with errors
			else {
				int sum_p = 0;
				int sum_n = 0;
				for(unsigned i=0; i<layer->fsize; i++) {
					int p = weights[i] * loc_bufin[i];
					if(p > 0) sum_p += p;
					if(p < 0) sum_n -= p;
				}
				sum_p = arr_recode_tcam[sum_p];
				sum_n = arr_recode_tcam[sum_n];
				loc_bufout[n] = sum_p - sum_n;
			}

			// Emulate errors to mimic approximate computing
			// This is linear distribution
			if(swexec_emulate_error_lin != 0) {
				for(unsigned i=0; i<layer->fsize; i++) {
					// Compute an accuracy error
					double p = drand48() * swexec_emulate_error_lin;
					// Apply the error
					bufout[i] *= (p + 1);
				}
			}

		}
		loc_bufin  += layer->fsize;
		loc_bufout += layer->out_fsize;
	}

	return 0;
}

int LayerNeu_CM::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
    // Variable pour faciliter la refactorisation
    Layer* layer = this;

    if(layer->neu_custom_mul != 0) {
        printf("Warning: Layer %s%u has custom multiplication operation ID %u, this is not handled in SW execution\n", 
               layer->typenameu, layer->typeidx, layer->neu_custom_mul_id);
    }

    // Boucle sur les neurones (channels)
    for(unsigned n = 0; n < layer->neurons; n++) {
        int* weights = layer->cfg_data[n];
        int* arr_recode_tcam = NULL;
        if(swexec_mode_tcam == true) {
            arr_recode_tcam = recode_tcam_style[layer->typeidx];
        }

        // Pour chaque neurone, on parcourt toutes les frames
        for(unsigned k = 0; k < layer->nbframes; k++) {
            if(arr_recode_tcam == NULL) {
                int sum = 0;
                int64_t sum2 = 0;
                // Calcul du dot produit en channel-major
                for(unsigned i = 0; i < layer->fsize; i++) {
                    // Accès à la valeur de la feature i pour la frame k
                    int val = bufin[i * layer->nbframes + k];
                    sum += weights[i] * val;
                }
                // Vérification d'overflow avec une seconde accumulation sur 64 bits
                for(unsigned i = 0; i < layer->fsize; i++) {
                    int val = bufin[i * layer->nbframes + k];
                    sum2 += weights[i] * val;
                }
                if(sum2 != sum) {
                    printf("########## Overflow !##########\n\n\n");
                }
                // Stockage du résultat dans le buffer de sortie en channel-major
                bufout[n * layer->nbframes + k] = sum;
            } else {
                int sum_p = 0;
                int sum_n = 0;
                for(unsigned i = 0; i < layer->fsize; i++) {
                    int val = bufin[i * layer->nbframes + k];
                    int p = weights[i] * val;
                    if(p > 0)
                        sum_p += p;
                    if(p < 0)
                        sum_n -= p;
                }
                sum_p = arr_recode_tcam[sum_p];
                sum_n = arr_recode_tcam[sum_n];
                bufout[n * layer->nbframes + k] = sum_p - sum_n;
            }
        }

        // Emulation d'erreurs pour simuler l'approximate computing :
        // Pour le neurone courant, on applique l'erreur sur toutes les frames
        if(swexec_emulate_error_lin != 0) {
            for(unsigned k = 0; k < layer->nbframes; k++) {
                double p = drand48() * swexec_emulate_error_lin;
                unsigned outidx = n * layer->nbframes + k;
                bufout[outidx] = bufout[outidx] * (p + 1);
            }
        }
    }

    return 0;
}



int LayerPool::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	int* loc_bufin  = bufin;
	int* loc_bufout = bufout;
	for(unsigned k=0; k<nbframes; k++) {
		int res = 0;
		if(pool_type == POOL_TYPE_MAX) {
			res = loc_bufin[0];
			for(unsigned i=1; i<fsize; i++) res = GetMax(res, loc_bufin[i]);
		}
		else if(pool_type == POOL_TYPE_MIN) {
			res = loc_bufin[0];
			for(unsigned i=1; i<fsize; i++) res = GetMin(res, loc_bufin[i]);
		}
		else if(pool_type == POOL_TYPE_AVG) {
			for(unsigned i=0; i<fsize; i++) res += loc_bufin[i];
			if(round_nearest == true) {
				res = round(ldexp(double(res) * pool_avg_mult, -pool_avg_shr));
			}
			else {
				res = (labs(int64_t(res) * pool_avg_mult) >> pool_avg_shr) * (res < 0 ? -1 : 1);
			}
		}
		else if(pool_type == POOL_TYPE_ADD) {
			for(unsigned i=0; i<fsize; i++) res += loc_bufin[i];
		}
		loc_bufout[0] = res;
		loc_bufin  += fsize;
		loc_bufout ++;
	}
	return 0;
}
int LayerNorm::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	// Variable to ease code refactoring
	Layer* layer = this;

	// Note : The bias is currently added before mul/shr
	// But it may be interesting to allow rescaling or shl before addition, for potentially better rounding possibilities

	int** cfg = layer->cfg_data;
	unsigned col_bias = 0;
	unsigned col_mul = 0;
	unsigned col_shr = 0;
	unsigned col_nb = 0;
	col_bias = col_nb; col_nb += (layer->norm_wbias > 0) ? 1 : 0;
	col_mul  = col_nb; col_nb += (layer->norm_wmul  > 0) ? 1 : 0;
	col_shr  = col_nb; col_nb += (layer->norm_wshr  > 0) ? 1 : 0;

	int* loc_bufin  = bufin;
	int* loc_bufout = bufout;
	for(unsigned k=0; k<layer->nbframes; k++) {
		for(unsigned i=0; i<layer->fsize; i++) {
			// Note : Due to optional multiplication, intermediate values can exceed 32b, so 64b is used
			int64_t v = loc_bufin[i];
			// Bias
			if(layer->norm_wbias > 0) v += cfg[i][col_bias];
			// Multiplication
			if(layer->norm_wmul > 0) v *= cfg[i][col_mul];
			if(layer->norm_mul_cst != 0) v *= layer->norm_mul_cst;
			// Descale
			unsigned sh = 0;
			if(layer->norm_shr_cst > 0) sh += layer->norm_shr_cst;
			if(layer->norm_wshr > 0)    sh += cfg[i][col_shr];
			if(sh > 0) {
				if(round_nearest == true) {
					v = round(ldexp(v, -sh));
				}
				else {
					v = (labs(v) >> sh) * (v < 0 ? -1 : 1);
				}
			}
			// Save result
			loc_bufout[i] = v;
		}
		loc_bufin  += layer->fsize;
		loc_bufout += layer->out_fsize;
	}

	return 0;
}
int LayerNorm_CM::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
    // Récupération de la structure de couche
    Layer* layer = this;

    // Récupération de la configuration
    int** cfg = layer->cfg_data;
    unsigned col_bias = 0;
    unsigned col_mul = 0;
    unsigned col_shr = 0;
    unsigned col_nb = 0;
    col_bias = col_nb; col_nb += (layer->norm_wbias > 0) ? 1 : 0;
    col_mul  = col_nb; col_nb += (layer->norm_wmul  > 0) ? 1 : 0;
    col_shr  = col_nb; col_nb += (layer->norm_wshr  > 0) ? 1 : 0;

    // Ordre channel-major : chaque canal (feature) est traité sur toutes les frames
    for (unsigned i = 0; i < layer->fsize; i++) {
        // Paramètres spécifiques au canal i
        int bias    = (layer->norm_wbias > 0) ? cfg[i][col_bias] : 0;
        int mul     = (layer->norm_wmul > 0)  ? cfg[i][col_mul]  : 1;
        int shr_cfg = (layer->norm_wshr > 0)  ? cfg[i][col_shr]  : 0;
        
        for (unsigned k = 0; k < layer->nbframes; k++) {
            // Calcul de l'indice dans un layout channel-major :
            // chaque canal contient layer->nbframes éléments consécutifs
            unsigned index = i * layer->nbframes + k;
            
            // Traitement de la normalisation
            int64_t v = bufin[index];
            if(layer->norm_wbias > 0) v += bias;
            if(layer->norm_wmul  > 0) v *= mul;
            if(layer->norm_mul_cst != 0) v *= layer->norm_mul_cst;
            
            // Calcul du décalage (shr)
            unsigned sh = 0;
            if(layer->norm_shr_cst > 0) sh += layer->norm_shr_cst;
            if(layer->norm_wshr  > 0) sh += shr_cfg;
            if(sh > 0) {
                if(round_nearest == true) {
                    v = round(ldexp(v, -sh));
                }
                else {
                    v = (labs(v) >> sh) * (v < 0 ? -1 : 1);
                }
            }
            bufout[index] = v;
        }
    }
    return 0;
}
int LayerTernarize::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	int** thresholds = cfg_data;
	int* loc_bufin  = bufin;
	int* loc_bufout = bufout;
	for(unsigned k=0; k<nbframes; k++) {
		for(unsigned i=0; i<fsize; i++) {
			if     (loc_bufin[i] < thresholds[i][0]) loc_bufout[i] = -1;
			else if(loc_bufin[i] > thresholds[i][1]) loc_bufout[i] = 1;
			else loc_bufout[i] = 0;
		}
		loc_bufin  += fsize;
		loc_bufout += out_fsize;
	}
	return 0;
}

int LayerRelu::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	// Variable to ease code refactoring
	Layer* layer = this;

	int* loc_bufin  = bufin;
	int* loc_bufout = bufout;
	for(unsigned k=0; k<layer->nbframes; k++) {
		for(unsigned i=0; i<layer->fsize; i++) {
			// Note : Due to optional multiplication, intermediate values can exceed 32b, so 64b is used
			int64_t v = loc_bufin[i];
			if     (v < layer->relu_min) loc_bufout[i] = layer->relu_min;
			else if(v > layer->relu_max) loc_bufout[i] = layer->relu_max;
			else loc_bufout[i] = v;
		}
		loc_bufin  += layer->fsize;
		loc_bufout += layer->out_fsize;
	}

	return 0;
}

int LayerLeaky::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	// Variable to ease code refactoring
	Layer* layer = this;

	int* loc_bufin  = bufin;
	int* loc_bufout = bufout;
	for(unsigned k=0; k<layer->nbframes; k++) {
		for(unsigned i=0; i<layer->fsize; i++) {
			// Note : Due to optional multiplication, intermediate values can exceed 32b, so 64b is used
			int64_t v = loc_bufin[i];
			if (v<0) v = static_cast<int>(v/8);
			if     (v < layer->leaky_min) loc_bufout[i] = layer->leaky_min;
			else if(v > layer->leaky_max) loc_bufout[i] = layer->leaky_max;
			else loc_bufout[i] = v;
		}
		loc_bufin  += layer->fsize;
		loc_bufout += layer->out_fsize;
	}

	return 0;
}

int LayerAdd::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {

	for(unsigned k=0; k<nbframes; k++) {
		for(unsigned o=0; o<out_fsize; o++) {
			int sum = 0;
			for(unsigned s=0; s<split_in; s++) sum += bufin[s];
			bufout[o] = sum;
			bufin += split_in;
		}
	}

	return 0;
}

int LayerCustom::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	printf("Error: Layer %s%u unknown behaviour and can't be executed\n", typenameu, typeidx);
	exit(EXIT_FAILURE);
	return 0;
}

int LayerFork::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {

	// Save a copy of all input data
	memcpy(swexec_output, bufin, out_nbframes * out_fsize * sizeof(*bufin));

	// Recursively launch in all successor branches
	for(unsigned i=0; i<arr_layers.size(); i++) {
		// Import the FORK output again
		if(i > 0) {
			memcpy(bufin, swexec_output, out_nbframes * out_fsize * sizeof(*bufin));
		}
		// Launch recursion
		int z = swexec_series_of_layers(arr_layers[i], outlayer, bufin, bufout, f);
		if(z != 0) return 1;
	}

	return 0;
}

int LayerCat::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {

	// Write the output of predecessors directly at the expected place in output vector

	// Concatenate full depth of each prev layer
	// This is the behaviour of training frameworks
	#if 0

	unsigned offset_out = 0;

	for(unsigned p=0; p<arr_layers.size(); p++) {
		layer_t* layer_prev = arr_layers[p];

		// Convenient pointers for input and output
		int* ptr_in = arr_layers[p]->swexec_output;
		int* ptr_out = bufout + offset_out;

		// Copy the data
		for(unsigned k=0; k<nbframes; k++) {
			for(unsigned i=0; i<layer_prev->out_fsize; i+=layer_prev->out_fz) {
				memcpy(ptr_out, ptr_in, layer_prev->out_fz * sizeof(*ptr_in));
				ptr_in += layer_prev->out_fz;
				ptr_out += out_fz;
			}
		}

		// Update start position for next layer
		offset_out += layer_prev->out_fz;

	}  // Predecessors

	#endif

	// Concatenate parallelism-wise
	// This is the behaviour of the hardware accelerator
	#if 1

	unsigned offset_out = 0;

	for(unsigned p=0; p<arr_layers.size(); p++) {
		layer_t* layer_prev = arr_layers[p];

		// Convenient pointers for input and output
		int* ptr_in = arr_layers[p]->swexec_output;
		int* ptr_out = bufout + offset_out;

		// Copy the data
		for(unsigned k=0; k<nbframes; k++) {
			for(unsigned i=0; i<layer_prev->out_fsize; i+=layer_prev->split_out) {
				memcpy(ptr_out, ptr_in, layer_prev->split_out * sizeof(*ptr_in));
				ptr_in += layer_prev->split_out;
				ptr_out += split_out;
			}
		}

		// Update start position for next layer
		offset_out += layer_prev->split_out;

	}  // Predecessors

	#endif

	return 0;
}

int LayerScatter::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {

	// Save a copy of all input data
	memcpy(swexec_output, bufin, out_nbframes * out_fsize * sizeof(*bufin));

	// Parallelism is same in all layers
	unsigned par = split_in;

	// Recursively launch in all successor branches
	for(unsigned i=0; i<arr_layers.size(); i++) {
		Layer* layer_next = arr_layers[i];

		// FIXME Prepare an array of indexes to reduce the control flow and the scanning of cells that we have to skip

		// Clear results
		memset(bufin, 0, nbframes * fsize * sizeof(*bufin));

		// Convenient pointers for input and output buffers
		int* ptr_in  = swexec_output;
		int* ptr_out = bufin;

		// Copy the data
		for(unsigned k=0; k<nbframes; k++) {
			for(unsigned f=0; f<fsize; f+=par) {
				if(cfg_data[f][i] != 0) {
					memcpy(ptr_out, ptr_in, par * sizeof(*ptr_in));
					ptr_out += par;
				}
				ptr_in += par;
			}
		}

		// Launch recursion
		int z = swexec_series_of_layers(layer_next, outlayer, bufin, bufout, f);
		if(z != 0) return 1;

	}  // Successor layers

	return 0;
}

int LayerGather::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	// Parallelism is same in all layers
	unsigned par = split_out;

	// Clear results
	memset(bufout, 0, nbframes * out_fsize * sizeof(*bufout));

	// Write the output of predecessors directly at the expected place in output vector
	for(unsigned p=0; p<arr_layers.size(); p++) {
		layer_t* layer_prev = arr_layers[p];

		// Convenient pointers for input and output buffers
		int* ptr_in = layer_prev->swexec_output;
		int* ptr_out = bufout;

		// Copy the data
		for(unsigned k=0; k<nbframes; k++) {
			for(unsigned f=0; f<out_fsize; f+=par) {
				if(cfg_data[f][p] != 0) {
					memcpy(ptr_out, ptr_in, par * sizeof(*ptr_in));
					ptr_in += par;
				}
				ptr_out += par;
			}
		}

	}  // Predecessor layers

	return 0;
}

int LayerFlatten::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	// Propagate data as-is
	memcpy(bufout, bufin, nbframes * fsize * sizeof(*bufin));
	return 0;
}

int LayerSoftMax::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {

	int* loc_bufin  = bufin;
	int* loc_bufout = bufout;

	for(unsigned k=0; k<nbframes; k++) {
		int max_val = loc_bufin[0];
		unsigned max_idx = 0;
		for(unsigned i=1; i<fsize; i++) {
			if(loc_bufin[i] > max_val) {
				max_val = loc_bufin[i];
				max_idx = i;
			}
		}
		loc_bufout[0] = max_idx;
		loc_bufin  += fsize;
		loc_bufout += out_fsize;
	}

	return 0;
}

int LayerFifo::swexec(int* bufin, int* bufout, unsigned f, layer_t* outlayer) {
	// Propagate data as-is
	memcpy(bufout, bufin, nbframes * fsize * sizeof(*bufin));
	return 0;
}

// Return zero if outlayer has not been reached yet
int swexec_series_of_layers(layer_t* inlayer, layer_t* outlayer, int* bufin, int* bufout, unsigned f) {

	// Process all layers in series
	for(layer_t* layer = inlayer; layer != NULL; layer = layer->next) {

		// For CAT layers, only continue if all predecessors have been reached
		// FIXME Should this be within the member function of LayerCat and similar ?
		if(layer->prev_is_arr == true) {
			layer->cat_cnt_fwd_propag ++;
			if(layer->cat_cnt_fwd_propag < layer->arr_layers.size()) break;
		}

		// Swap arrays if needed
		if(layer != inlayer) {
			int* tmp = bufin;
			bufin = bufout;
			bufout = tmp;
		}

		// Print input data
		if(param_noout==false && layer==outlayer && swexec_gen_in==true) {
			swexec_print(Fo, layer, bufin, bufout, f);
			// Output layer is reached, stop calculation for this frame
			return 1;
		}

		if(layer->out_wdata > 32) {
			printf("Warning : Layer %s%u has output width %u, this is not handled in SW execution\n", layer->typenameu, layer->typeidx, layer->out_wdata);
		}

		// Layer-specific processing
		int res = layer->swexec(bufin, bufout, f, outlayer);
		if(res != 0) return res;

		// Optionally apply the constraint on output width
		#if 1
		unsigned num_resized = 0;
		__attribute((unused)) unsigned sh = 32 - layer->out_wdata;
		__attribute((unused)) unsigned mask = uint_genmask(layer->out_wdata);
		for(unsigned i = 0; i < layer->out_nbframes * layer->out_fsize; i++) {
			int v = bufout[i];
			int v2 = v;

			// Version that directly sets sign bits
			v2 = (v < 0) ? (v | (~mask)) : (v & mask);

			// Version with arithmetic shift left/right
			#if 0
			if(layer->out_sdata == true) {
				v2 = (v << sh) >> sh;
			}
			else {
				v2 = (unsigned(v) << sh) >> sh;
			}
			#endif

			bufout[i] = v2;
			num_resized += (v2 != v);
		}
		if(num_resized > 0) {
			printf("Info : Layer %s%u : Resizing output to %u bits did affect %u values\n", layer->typenameu, layer->typeidx, layer->out_wdata, num_resized);
		}
		#endif

		// Print results
		// FIXME If the layer to print is in predecessors of a CAT, some branches will be executed even if not used
		//   Potential solution ? Create an array of layers with only the necessary layers in it
		if(param_noout==false && layer==outlayer) {
			swexec_print(Fo, layer, bufin, bufout, f);
			// Output layer is reached, stop calculation for this frame
			return 1;
		}

		// Save results if the layer has an array for this purpose
		if(layer->swexec_output != NULL) {
			memcpy(layer->swexec_output, bufout, layer->out_nbframes * layer->out_fsize * sizeof(*bufout));
		}

	}  // Loop on layers

	return 0;
}

int swexec(Network* network, layer_t* outlayer) {
	auto& layers = network->layers;

	unsigned frames = param_fn;
	if(frames==0) {
		printf("Error: frames = %u\n", frames);
		exit(EXIT_FAILURE);
	}

	if(outlayer==NULL) outlayer = network->layer_last;

	// Load configuration data
	int z = network->load_config_files();
	if(z != 0) return 1;

	// Load frame data
	layer_t* firstlayer = network->layer_first;
	int **dataframes = array_create_dim2(frames, firstlayer->fsize);
	if(filename_frames!=NULL) {
		int z = loadfile(dataframes, filename_frames, frames, firstlayer->fsize, param_multiline);
		if(z != 0) return 1;
		unsigned num_exceed = array_check_data_width(dataframes, frames, 0, firstlayer->fsize, firstlayer->wdata, firstlayer->sdata);
		if(num_exceed > 0) {
			printf("Warning: Some values from frame inputs exceed the hardware capacity (%u values)\n", num_exceed);
		}
	}
	else {
		if(param_rand_given==false) {
			printf("Error: No file is specified for input frames\n");
			return 1;
		}
		array_fillrand_dim2(dataframes, frames, firstlayer->fsize, firstlayer->wdata, param_rand_min, param_rand_max);
	}
	#if 0
	// If needed, reorder image data
	if(firstlayer->fx > 1 || firstlayer->fy > 1) {
		if(param_debug==true) {
			printf("INFO: Reordering data inside input frames...\n");
		}
		unsigned fx = firstlayer->fx;
		unsigned fy = firstlayer->fy;
		unsigned fz = firstlayer->fz;
		// Reorder
		reorder_to_zfirst_dim2(dataframes, frames, firstlayer->fsize, fx, fy, fz, 0);
	}
	#endif
	#if 0  // For debug: print frame data
	for(unsigned f=0; f<frames; f++) {
		if(Fo==stdout) printf("FRAME %u: ", f);
		for(unsigned i=0; i<firstlayer->fsize; i++) {
			if(i > 0) fprintf(Fo, ",");
			fprintf(Fo, "%i", dataframes[f][i]);
		}
		fprintf(Fo, "\n");
	}
	fprintf(Fo, "\n");
	#endif

	// Allocate per-layer storage of output data
	unsigned max_fsize = 0;
	unsigned layers_cat_nb = 0;
	for(auto layer : layers) {
		// Count stats
		max_fsize = GetMax(max_fsize, layer->nbframes * layer->fsize);
		max_fsize = GetMax(max_fsize, layer->out_nbframes * layer->out_fsize);
		if(layer->prev_is_arr == true) layers_cat_nb++;
		// Layers FORK need to store their output
		if(layer->next_is_arr == true) {
			layer->swexec_output = (int*)malloc(layer->out_nbframes * layer->out_fsize * sizeof(*layer->swexec_output));
		}
		// For predecessors of CAT layers, allocate an array to store intermediate results
		if(layer->next != NULL && layer->next->prev_is_arr == true) {
			layer->swexec_output = (int*)malloc(layer->out_nbframes * layer->out_fsize * sizeof(*layer->swexec_output));
		}
	}

	// List the CAT layers to ease reset of counters between frames
	layer_t* layers_cat[layers_cat_nb];
	if(layers_cat_nb > 0) {
		layers_cat_nb = 0;
		for(auto layer : layers) {
			if(layer->prev_is_arr == true) layers_cat[layers_cat_nb++] = layer;
		}
	}

	// Allocate shared data buffers to be used ping-pong way
	int* bufin  = (int*)malloc(max_fsize * sizeof(*bufin));
	int* bufout = (int*)malloc(max_fsize * sizeof(*bufout));

	// Under TCAM-approximations, pre-compute recoding arrays
	recode_tcam_style = NULL;
	if(swexec_mode_tcam==true) {
		recode_tcam_style = (int**)calloc(100, sizeof(*recode_tcam_style));
		// Scan all neuron layers
		for(auto layer : layers) {
			if(layer->type!=LAYER_NEU) continue;
			if(layer->wdata > 2) continue;

			// Create the recoding array
			unsigned arrsize = layer->fsize + 1;
			int* recarr = (int*)calloc(arrsize, sizeof(*recarr));
			recode_tcam_style[layer->typeidx] = recarr;

			// Fill the recoding array
			int dlarr[1000];
			unsigned dl = tcam_dl_recode_array(layer->fsize, recarr, dlarr, false);

			#if 1  // Print for debug
			printf("%s%u: %u DL: ", layer->typenameu, layer->typeidx, dl);
			for(unsigned k=0; k<dl; k++) printf("%u ", dlarr[k]);
			printf("\n");
			#endif

			#if 0  // Print for debug
			printf("%s%u: recode: ", layer->typenameu, layer->typeidx);
			for(unsigned k=0; k<arrsize; k++) printf("%u ", recarr[k]);
			printf("\n");
			#endif

		}  // Scan all neuron layers
	}

	printf("INFO: Processing.......\n");

	for(unsigned f=0; f<frames; f++) {

		// Get the frame data

		memcpy(bufin, dataframes[f], firstlayer->fsize * sizeof(*bufin));

		// Reset counters of CAT layers
		for(unsigned i=0; i<layers_cat_nb; i++) {
			layer_t* layer = layers_cat[i];
			layer->cat_cnt_fwd_propag = 0;
		}

		// Process all layers from the first one
		swexec_series_of_layers(network->layer_first, outlayer, bufin, bufout, f);

	}  // Loop on frames

	// Clean per-layer buffers
	for(auto layer : layers) {
		if(layer->swexec_output != NULL) {
			free(layer->swexec_output);
			layer->swexec_output = NULL;
		}
	}

	// Clean shared buffers
	free(bufin);
	free(bufout);

	return 0;
}


