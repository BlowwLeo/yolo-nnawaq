// Actual usage of the FPGA accelerator
// Write configuration data, send frames, receive classification results

extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>  // For usleep()

#include "nnawaq_utils.h"
#include "load_config.h"

}

#include "nn_layers_utils.h"
#include "nn_hwacc_config.h"
#include "nn_hw_config.h"
#include "nn_load_config.h"

#include "hwacc_common.h"

using namespace std;


//============================================
// Write config for NN layers
//============================================

int HwAcc_Common::write_layer_config(layer_t* layer) {
	// Initial number of loop bounds
	unsigned num_parts = 1;

	vector<uint32_t> arr;

	for(unsigned idx_part=0; idx_part < num_parts; idx_part++) {
		unsigned code_part = 0;

		// Get the configuration stream
		int z = layer->hwacc_genconfig(this, arr, code_part, num_parts, idx_part);
		if(param_debug==true) {
			printf("DEBUG HwAcc %s%u : genconfig returns : code=%u, num=%u, idx=%u, size=%zu\n",
				layer->typenameu, layer->typeidx,
				code_part, num_parts, idx_part, arr.size()
			);
		}
		if(z != 0) return z;

		if(num_parts == 0 || arr.size() == 0) break;

		// FIXME Reset the entire HW accelerator
		#if 1
		accreg_clear();
		accreg_sync_read();
		// Send the NN config to the FPGA so the frame size is known by the neurons
		//if(param_hw_blind==false) {
		//	build_network();
		//	accreg_sync_read();
		//}
		#endif

		// Set primary write mode
		accreg_set_wmode1(layer->cfg_id);
		// Set the secondary write mode
		accreg_set_wmode2(code_part);

		// Tiny read to make sure the mode is correctly taken into account
		accreg_sync_read();

		if(param_debug==true) {
			printf("DEBUG HwAcc %s%u : Sending data buffer with %zu 32b words\n", layer->typenameu, layer->typeidx, arr.size());
		}

		#if 0
		for(unsigned i=0; i<arr.size(); i+=accreg_ifw32) {
			printf("DEBUG HwAcc %s%u : addr %u :", layer->typenameu, layer->typeidx, i/accreg_ifw32);
			unsigned local_nb32 = GetMin(accreg_ifw32, arr.size() - i);  // End of the buffer when not a multiple of the interface width
			for(unsigned v=0; v<local_nb32; v++) printf(" 0x%08x", arr[i+v]);
			printf("\n");
		}
		#endif

		#if 0
		std::string dbg_filename = std::string("debug-cfg-") + layer->typenamel + to_string(layer->typeidx) + ".hex";
		FILE* dbg_file_out = fopen(dbg_filename.c_str(), "wb");
		if(dbg_file_out != nullptr) {
			for(unsigned i=0; i<arr.size(); i+=accreg_ifw32) {
				unsigned local_nb32 = GetMin(accreg_ifw32, arr.size() - i);  // End of the buffer when not a multiple of the interface width
				for(unsigned v=0; v<local_nb32; v++) fprintf(dbg_file_out, "%s%08x", v==0 ? "" : " ", arr[i+v]);
				fprintf(dbg_file_out, "\n");
			}
			fclose(dbg_file_out);
		}
		#endif

		// Send the big buffer to the FPGA
		int sent_nb32 = fpga_send32_wait(arr.data(), arr.size());
		if(param_debug == true) {
			printf("DEBUG HwAcc : Send() returned %i\n", sent_nb32);
			unsigned hwr_got = accreg_get_nbinputs();
			unsigned hwr_exp = (arr.size() + accreg_ifw32 - 1) / accreg_ifw32;
			printf("DEBUG HwAcc : Hardware counters indicate the network received %u transfers (%+i)\n", hwr_got, hwr_got - hwr_exp);
		}
		if(sent_nb32 != (int)arr.size()) {
			printf("Warning HwAcc : Sent %i 32b data words to the accelerator, instead of %u\n", sent_nb32, unsigned(arr.size()));
		}

		// Clean
		arr.resize(0);

	}  // Loop on parts

	return 0;
}

int HwAcc_Common::write_config(Network* network) {
	int64_t oldtime, newtime;

	// Only to know the execution time
	oldtime = Time64_GetReal();

	auto& layers = network->layers;
	for(auto layer : layers) {
		// Read the config files
		layer->load_config_files();
		// Convert to raw configuration data and send to the accelerator
		write_layer_config(layer);
	}  // Scan all layers

	newtime = Time64_GetReal();
	int64_t totime_config = newtime - oldtime;
	printf("Config time .. %g s\n", TimeDouble_From64(totime_config));

	return 0;
}



//============================================
// Write frames
//============================================

// Mutex to prevent send and recv threads to conflict when using the control channel
pthread_mutex_t ctrl_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct frame_thread_data_t {
	HwAcc_Common* hwacc;
	layer_t* layer;
	unsigned frames_nb;
} frame_thread_data_t;

// Wrapper thread routine to call pthread_create on a non-member function
static void* getoutputs_thread_wrapper(void* arg) {
	frame_thread_data_t* thdata = (frame_thread_data_t*)arg;
	HwAcc_Common* hwacc = thdata->hwacc;
	return hwacc->getoutputs_thread(thdata->layer, thdata->frames_nb);
}

// Thread routine to receive NN results while the frames are being sent
void* HwAcc_Common::getoutputs_thread(layer_t* layer, unsigned frames_nb) {
	unsigned frame_size = layer->out_nbframes * ((layer->out_fsize + layer->split_out - 1) / layer->split_out);
	unsigned frame_size_user = frame_size;
	Network* network = layer->network;

	if(layer->type == LAYER_NEU) {
		if(accreg_rdonly==true || param_hw_blind==true) {
			unsigned neurons = layer->neurons;
			if(network->cnn_outneu > 0 && network->cnn_outneu <= layer->neurons_max) neurons = network->cnn_outneu;
			frame_size      = layer->out_nbframes * ((layer->neurons_max + layer->split_out - 1) / layer->split_out);
			frame_size_user = layer->out_nbframes * ((neurons            + layer->split_out - 1) / layer->split_out);
		}
	}
	unsigned frame_extra = frame_size - frame_size_user;

	// FIXME Support width other than 32
	if(accreg_wdo != 32) {
		printf("Warning HwAcc : Hardware output width is %u but currently only 32 is supported\n", accreg_wdo);
	}

	unsigned nb32 = frames_nb * frame_size;
	unsigned nb32_rnd_if = uint_next_multiple(nb32, accreg_ifw32);
	int32_t* buf = (int32_t*)malloc(nb32_rnd_if * sizeof(*buf));
	// In case of timeout, clear the buffer for debug
	if(param_timeout_recv_us > 0) memset(buf, 0, nb32_rnd_if * sizeof(*buf));

	printf("Info HwAcc : Expecting to receive %u 32-bit words from the accelerator\n", nb32);
	if(param_debug==true) {
		printf("DEBUG HwAcc : Asking for %u 32-bit words (buffer of %u words)\n", nb32, nb32_rnd_if);
	}

	// For some HwAcc backends, concurrent access to the control channel must be protected
	// FIXME This may require a config flag in HwAcc object because some backends don't need this
	pthread_mutex_lock(&ctrl_mutex);
	accreg_set_nboutputs(nb32);
	pthread_mutex_unlock(&ctrl_mutex);

	// Launch the receive operation
	int recv_nb32 = fpga_recv32((uint32_t*)buf, nb32);
	if(param_debug == true) {
		printf("DEBUG HwAcc : Revc() returned %i\n", recv_nb32);
		pthread_mutex_lock(&ctrl_mutex);
		unsigned hwr_got = accreg_get_nboutputs();
		pthread_mutex_unlock(&ctrl_mutex);
		printf("DEBUG HwAcc : Hardware counters indicate the network produced %u results (%+i)\n", hwr_got, hwr_got - recv_nb32);
	}
	if(recv_nb32 < (int)nb32) {
		printf("Warning HwAcc : Received %i values from the accelerator, instead of %u\n", recv_nb32, nb32);
	}

	if(param_noout==false) {

		unsigned mask = (unsigned)~0;
		if(param_out_mask==true) mask = uint_genmask(layer->out_wdata);

		// Display
		unsigned bufidx = 0;
		for(unsigned f=0; f<frames_nb; f++) {
			if(Fo==stdout) printf("RESULT: Frame %u: ", f);
			for(unsigned r=0; r<frame_size_user; r++) {
				if(param_out_nl > 0 && r > 0 && r % param_out_nl == 0) fprintf(Fo, "\n");
				else if(r > 0 && param_out_sep != NULL) fprintf(Fo, "%s", param_out_sep);
				fprintf(Fo, param_out_format, buf[bufidx++] & mask);
			}
			bufidx += frame_extra;
			fprintf(Fo, "\n");
			if(param_out_nl > 0) fprintf(Fo, "\n");
		}  // Loop on frames

	}  // param_noout == false

	// Clean
	free(buf);

	return NULL;
}

int HwAcc_Common::write_frames_inout(const char* filename, layer_t* inlayer, layer_t* outlayer, layer_t* last_layer) {
	int64_t totime_file = 0;
	int64_t totime_nn = 0;

	int64_t oldtime, newtime;
	double diff;

	FILE* F = fopen(filename, "rb");
	if(F==NULL) {
		printf("ERROR HwAcc : Can't open file '%s'\n", filename);
		return -1;
	}

	// Set primary write mode
	accreg_set_wmode_frame();
	// Make sure the mode is correctly taken into account
	accreg_sync_read();

	// FIXME The network is only necessary to get a few values such as fsize and PAR
	Network* network = inlayer->network;
	unsigned fsize = inlayer->fsize;

	// Array to store data for a certain number of frames
	// Check maximum size for Riffa: 2G (minus 1) 32-bit words = 8k MB
	// FIXME This max should be provided by the HwAcc class, and adapted to the available RAM
	if(param_bufsz_mb >= 8192) param_bufsz_mb = 8192 - 1;

	// Compute the max number of frames that can fit in the user-specified buffer size
	unsigned max_transfers_nb = (param_bufsz_mb * 1024 * 1024 / 4) / accreg_ifw32;
	unsigned max_frames_nb = (max_transfers_nb * accreg_pari) / fsize;
	if(max_frames_nb==0) max_frames_nb = 1;
	// Take into account the max number of frames to process
	if(param_fn==0 || param_fn > max_frames_nb) {
		printf("Info HwAcc : Using frame batches of up to %u frames\n", max_frames_nb);
	}
	if(param_fn > 0 && max_frames_nb > param_fn) max_frames_nb = param_fn;

	// Compute the number of 32b words needed for the buffer, rounded to upper multiple of transfer size
	// Note : Frames are contiguous in input buffer
	unsigned alloc_transfers_nb = (uint64_t(max_frames_nb) * fsize + accreg_pari - 1) / accreg_pari;
	unsigned alloc_nb32 = alloc_transfers_nb * accreg_ifw32;

	// Allocate the buffer that will be sent directly to the hardware
	uint32_t* databuf = (uint32_t*)malloc(alloc_nb32 * sizeof(*databuf));
	int* framebuf = (int*)malloc(fsize * sizeof(*framebuf));

	// This is aligned to hardware transfer boundary
	unsigned databuf_ref32_transfer = 0;
	// This is the size of the buffer that is effectively used
	unsigned databuf_nb32 = 0;
	unsigned databuf_nbvalues = 0;

	// Accumulator of frame values
	uint64_t buf64 = 0;
	unsigned buf64_bits = 0;  // Number of bits in the current 32-bit word
	unsigned cur_transfer_data_nb = 0;  // Number of values in current hardware transfer
	uint32_t data_mask = uint_genmask(accreg_wdi);

	unsigned curframes_nb = 0;
	unsigned totalframes_nb = 0;

	load_warnings_clear();

	// Force set free run mode each time, to reset the output counter
	accreg_freerun_out_clear();
	if(param_freerun==true) {
		accreg_freerun_out_set();
	}

	// Only to know the execution time
	oldtime = Time64_GetReal();

	// Infinite loop that gets frames
	do {

		// Get one frame
		int r = loadfile_oneframe(F, framebuf, fsize, param_multiline);
		if(r < 0 && param_floop==true && param_fn > 0) {
			// Sanity check to avoid infinite loop
			if(totalframes_nb==0) break;
			// Rewind the file
			rewind(F);
			continue;
		}

		// Add the frame to the big buffer
		if(r >= 0) {

			// If needed, reorder image data
			if(inlayer->fx > 1 || inlayer->fy > 1) {
				unsigned fx = inlayer->fx;
				unsigned fy = inlayer->fy;
				unsigned fz = inlayer->fz;
				// Reorder
				reorder_to_zfirst_dim2(&framebuf, 1, fsize, fx, fy, fz, 0);
			}

			// For debug: a VHDL simulation can read this dumped data as input
			#if 0
			for(unsigned i=0; i<fsize; i++) {
				int val = framebuf[i];
				for(int sh=wdata-1; sh>=0; sh--) {
					printf("%c", '0' + ((val >> sh) & 0x01));
				}
				printf("\n");
			}
			#endif

			// Enqueue the data item to the buffer
			for(unsigned i=0; i<fsize; i++) {
				buf64 |= (uint64_t(framebuf[i]) & data_mask) << buf64_bits;
				buf64_bits += accreg_wdi;
				// Commit a 32b word when full
				if(buf64_bits >= 32) {
					databuf[databuf_nb32++] = buf64;
					buf64 >>= 32;
					buf64_bits -= 32;
				}
				// Commit a hardware transfer when full
				cur_transfer_data_nb ++;
				if(cur_transfer_data_nb == accreg_pari) {
					if(buf64_bits > 0) {
						databuf[databuf_nb32++] = buf64;
					}
					buf64 = 0;
					buf64_bits = 0;
					databuf_nbvalues += cur_transfer_data_nb;
					cur_transfer_data_nb = 0;
					databuf_ref32_transfer += accreg_ifw32;
					databuf_nb32 = databuf_ref32_transfer;
				}
			}

			// Increment frame counters
			curframes_nb ++;
			totalframes_nb ++;
		}

		// If the big buffer contains enough frames, send that
		if(
			(curframes_nb >= max_frames_nb) ||
			(param_fn > 0 && totalframes_nb >= param_fn) ||
			(curframes_nb > 0 && r<0)
		) {

			// Commit any remaining bits from the accumulator
			if(buf64_bits > 0) {
				databuf[databuf_nb32++] = buf64;
				databuf_nbvalues += cur_transfer_data_nb;
				buf64 = 0;
				buf64_bits = 0;
				cur_transfer_data_nb = 0;
			}

			// Only to know the execution time
			newtime = Time64_GetReal();
			totime_file += newtime - oldtime;

			printf("Info HwAcc: Starting the receiving thread...\n");

			#if 0
			unsigned debug_transfers_nb = (databuf_nb32 + accreg_ifw32 - 1) / accreg_ifw32;
			for(unsigned t=0; t<debug_transfers_nb; t+=accreg_ifw32) {
				printf("DEBUG HwAcc : FRAME transfer %u :\n", t);
				for(unsigned i=0; i<accreg_ifw32; i++) printf(" 0x%08x", databuf[t*accreg_ifw32 + i]);
				printf("\n");
			}
			#endif

			// ID for the listening thread
			pthread_t th_get;

			frame_thread_data_t thdata;
			thdata.hwacc = this;
			thdata.layer = outlayer;
			thdata.frames_nb = curframes_nb;

			// FIXME Reset the entire HW accelerator
			#if 1
			accreg_clear();
			accreg_sync_read();
			//if(param_hw_blind==false) {
			//	write_config_regs();
			//	accreg_sync_read();
			//}
			// Force set free run mode each time, to reset the output counter
			accreg_freerun_out_clear();
			if(param_freerun==true) {
				accreg_freerun_out_set();
			}
			#endif

			// Set configuration
			if(accreg_selout==true && network->param_selout==true && outlayer != last_layer) {
				accreg_set_recv1(outlayer->id);
			}
			else {
				accreg_set_recv_out();
			}
			accreg_set_recv2(0);
			accreg_sync_read();

			// Launch receive thread
			if(param_freerun==false) {
				pthread_create(&th_get, NULL, getoutputs_thread_wrapper, &thdata);
			}

			// Only to know the execution time
			oldtime = Time64_GetReal();

			// The amount of data that the FPGA has to receive is in number of clock cycles, hence the division by PAR_IN
			unsigned databuf_nbtransfers = databuf_nbvalues / network->layer_first->split_in;

			#if 0
			std::string dbg_filename = "debug-frames.hex";
			FILE* dbg_file_out = fopen(dbg_filename.c_str(), "wb");
			if(dbg_file_out != nullptr) {
				for(unsigned i=0; i<databuf_nb32; i+=accreg_ifw32) {
					unsigned local_nb32 = GetMin(accreg_ifw32, databuf_nb32 - i);  // End of the buffer when not a multiple of the interface width
					for(unsigned v=0; v<local_nb32; v++) fprintf(dbg_file_out, "%s%08x", v==0 ? "" : " ", databuf[i+v]);
					fprintf(dbg_file_out, "\n");
				}
				fclose(dbg_file_out);
			}
			#endif

			printf("Info HwAcc : Sending data buffer with %u frames, %u 32b words, %u network inputs\n",
				curframes_nb, databuf_nb32, databuf_nbtransfers
			);

			// For some HwAcc backends, concurrent access to the control channel must be protected
			// FIXME This may require a config flag in HwAcc object because some backends don't need this
			pthread_mutex_lock(&ctrl_mutex);
			accreg_set_nbinputs(databuf_nbtransfers);
			pthread_mutex_unlock(&ctrl_mutex);

			// Send the data buffer to the FPGA
			int sent_nb32 = fpga_send32(databuf, databuf_nb32);
			printf("Info HwAcc : Data sent\n");
			if(param_debug == true) {
				printf("DEBUG HwAcc : Send() returned %u\n", sent_nb32);
				pthread_mutex_lock(&ctrl_mutex);
				unsigned hwr = accreg_get_nbinputs();
				pthread_mutex_unlock(&ctrl_mutex);
				printf("DEBUG HwAcc : Hardware counters indicate the network received %u inputs (%+i)\n", hwr, hwr - databuf_nbtransfers);
			}
			if(sent_nb32 < (int)databuf_nb32) {
				printf("Warning HwAcc : Only %u 32b data words were sent to the accelerator, instead of %u\n", sent_nb32, databuf_nb32);
			}

			// Note: No need to have an additional wait loop because we do get results this time

			if(param_freerun==false) {
				printf("Info HwAcc : Waiting for results...\n");
				// Ensure the listening thread has finished
				pthread_join(th_get, NULL);
			}

			// Only to know the execution time
			newtime = Time64_GetReal();
			totime_nn += newtime - oldtime;
			oldtime = newtime;

			// Debug : To cover transmission latencies, also query and print the amount of sent data after the results have been received
			if(param_debug==true) {
				if(param_freerun==true) {
					// FIXME Arbitrary wait for the last frame to be fully processed
					// FIXME Replace by polling on busy flag (to be implemented)
					usleep(100*1000);
				}
				unsigned hwr = accreg_get_nbinputs();
				printf("DEBUG HwAcc : Hardware counters indicate the network received %u inputs (%+i)\n", hwr, hwr - databuf_nbtransfers);
			}

			// Reset counters for next big buffer of frames
			curframes_nb = 0;
			databuf_nb32 = 0;
			databuf_nbvalues = 0;
		}

		// Exit when the end of the file is reached
		if(r < 0) break;
		// Exit when enough frames have been sent
		if(param_fn > 0 && totalframes_nb >= param_fn) break;

	} while(1);  // Read the lines of the file

	// Clean
	free(databuf);
	free(framebuf);
	fclose(F);

	if(totalframes_nb==0) {
		printf("ERROR HwAcc : No frames were found in file '%s'\n", filename);
		return -1;
	}

	// Print stats
	printf("Stats HwAcc :\n");
	printf("  Frames ...... %u\n", totalframes_nb);
	diff = TimeDouble_From64(totime_file);
	printf("  Time, file .. %g s, %g frames/s\n", diff, totalframes_nb / diff);
	diff = TimeDouble_From64(totime_nn);
	printf("  Time, FPGA .. %g s, %g frames/s\n", diff, totalframes_nb / diff);

	return 0;
}

int HwAcc_Common::write_frames(Network* network, const char* filename) {
	layer_t* inlayer = NULL;
	layer_t* outlayer = NULL;

	// Get first and last layers of the network
	for(layer_t* layer = network->layer_first; layer != NULL; layer = layer->next) {
		if(layer->type != LAYER_FIFO) { inlayer = layer; break; }
	}
	for(layer_t* layer = network->layer_last; layer != NULL; layer = layer->prev) {
		if(layer->type != LAYER_FIFO) { outlayer = layer; break; }
	}

	layer_t* last_layer = outlayer;

	// If an output layer was selected, ensure it is handled by the hardware
	if(param_out_layer!=NULL) {
		if(accreg_selout==false && param_out_layer!=outlayer) {
			printf("ERROR HwAcc : The hardware supports only output at last layer\n");
			return -1;
		}
		if(network->param_selout==false && param_out_layer!=outlayer) {
			printf("Warning HwAcc : Using default output layer\n");
		}
		else {
			outlayer = param_out_layer;
		}
	}

	unsigned errors_nb = 0;
	if(inlayer==NULL) {
		printf("ERROR HwAcc : Could not find first layer\n");
		errors_nb ++;
	}
	if(outlayer==NULL) {
		printf("ERROR HwAcc : Could not find last layer\n");
		errors_nb ++;
	}
	if(errors_nb != 0) return -1;

	int z = write_frames_inout(filename, inlayer, outlayer, last_layer);

	return z;
}



//============================================
// Global usage
//============================================

void HwAcc_Common::run(Network* network) {

	// Ensure the hardware accelerator is initialized
	accreg_config_get();

	if(param_hw_blind == false) {
		// If no network is built, try to do it from hardware accelerator - FIXME This should be the default ?
		if(network->layers.size() == 0) {
			build_network(network);
		}
	}

	// Ensure the network is built
	check_have_hwacc(network);

	if(param_hw_blind == true) {
		// Update the config registers from the layer structures + send them to the FPGA
		write_config_regs(network);
	}
	else {
		if(network->param_cnn_origin != Network::CNN_ORIGIN_HARDWARE) {
			printf("Error HwAcc : The NN must be based on hardware design\n");
			exit(EXIT_FAILURE);
		}
	}

	// Print accelerator details
	accreg_config_print();

	// Send configuration data
	write_config(network);

	// Finally, send the frames
	if(filename_frames!=NULL) {
		write_frames(network, filename_frames);
	}
}

