// Load CSV files into arrays

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

#include "nnawaq_utils.h"
#include "load_config.h"


//============================================
// Miscellaneous shared variables
//============================================

// To emit warnings about overfull lines
static unsigned warnings_max_nb = 10;
static unsigned overfull_cur_nb = 0;
static unsigned underfull_cur_nb = 0;

void load_warnings_clear() {
	overfull_cur_nb = 0;
	underfull_cur_nb = 0;
}


//============================================
// Arrays of integers
//============================================

// Create a 2-dimensions array
int** array_create_dim2(unsigned nrows, unsigned ncol) {
	int **array = (int**)malloc(nrows * sizeof(*array));
	array[0] = (int*)malloc(nrows * ncol * sizeof(**array));
	for(unsigned i=1; i<nrows; i++) array[i] = array[i-1] + ncol;
	return array;
}

// Read one frame from the file
// File encoding: data items are signed decimal values separated by other characters
// Return the number of values obtained, or -1 if already at the end of the file
int loadfile_oneframe(FILE* F, int* buf, unsigned fsize, bool allow_multiline) {
	// A buffer to read one line
	static size_t linebuf_size = 0;
	static char* linebuf = NULL;
	ssize_t r;

	// Current number of values obtained
	unsigned items_nb = 0;

	do {

		// Get one line = one frame
		r = getline(&linebuf, &linebuf_size, F);
		// Exit when the end of the file is reached
		if(r < 0) break;

		// Parse the line
		char* ptr = linebuf;
		bool valneg = false;
		int32_t val = 0;

		char c = *(ptr++);
		while(c!=0) {

			// Skip until the next value
			if(c=='-') valneg = true;
			else if(c=='+') {}
			else if(c >= '0' && c <= '9') val = (c - '0');
			else if(c == 0) break;
			else { c = *(ptr++); continue; }

			// Here it is the beginning of a value. Check if it is within bounds.
			if(items_nb >= fsize) {
				if(overfull_cur_nb < warnings_max_nb) {
					printf("Warning: Cropping overfull frame (more than %u items)\n", fsize);
					overfull_cur_nb++;
					if(overfull_cur_nb == warnings_max_nb-1) {
						printf("  Note: This warning was printed %u times. Next occurrences will not be displayed.\n", overfull_cur_nb);
					}
				}
				break;
			}

			// Read the rest of the value
			c = *(ptr++);
			while(c >= '0' && c <= '9') {
				val = 10 * val + (c - '0');
				c = *(ptr++);
			};

			// Here we have a value, apply the sign
			if(valneg==true) val = -val;

			// Store the value
			buf[items_nb++] = val;

			// Clear the variables for the next value
			valneg = false;
			val = 0;

		}  // Loop that reads one line of the file

		// Exit when enough data have been obtained
		if(items_nb >= fsize) break;

		// If not wanting multi-line, exit
		if(allow_multiline==false) break;

	} while(1);  // Read the lines of the file

	if(r < 0 && items_nb==0) return -1;

	if(items_nb < fsize) {
		if(underfull_cur_nb < warnings_max_nb) {
			printf("Warning: Underfull frame: %u items instead of %u\n", items_nb, fsize);
			underfull_cur_nb++;
			if(underfull_cur_nb == warnings_max_nb-1) {
				printf("  Note: This warning was printed %u times. Next occurrences will not be displayed.\n", underfull_cur_nb);
			}
		}
	}

	return items_nb;
}

// Load one file, return a 2D array, one row per frame
int loadfile(int** array, const char* filename, unsigned nframes, unsigned fsize, bool allow_multiline) {
	printf("INFO: Reading file '%s'\n", filename);

	FILE* F = fopen(filename, "rb");
	if(F==NULL) {
		printf("ERROR: Can't open file '%s'\n", filename);
		return 1;
	}

	unsigned curframe = 0;

	load_warnings_clear();

	do {

		// Get one frame
		int r = loadfile_oneframe(F, array[curframe], fsize, allow_multiline);
		if(r > 0) curframe ++;

		// If the big buffer contains enough frames, exit
		if(curframe >= nframes) break;

		// Exit when the end of the file is reached
		if(r < 0) {
			printf("Warning: Only got %u frames instead of %u\n", curframe, nframes);
			break;
		}

	} while(1);  // Read the lines of the file

	// Clean
	fclose(F);

	return 0;
}

void array_fillrand_dim2(int** array, unsigned nrow, unsigned ncol, unsigned wdata, int rand_min, int rand_max) {
	// Fill the array
	if(rand_min == rand_max) {
		for(unsigned r=0; r<nrow; r++) {
			for(unsigned c=0; c<ncol; c++) array[r][c] = rand_min;
		}
	}
	if(rand_min > rand_max) {
		for(unsigned r=0; r<nrow; r++) {
			for(unsigned c=0; c<ncol; c++) array[r][c] = rand();
		}
	}
	else {
		for(unsigned r=0; r<nrow; r++) {
			for(unsigned c=0; c<ncol; c++) array[r][c] = rand_min + rand() % (rand_max - rand_min + 1);
		}
	}

	// If needed, reduce the number of significant bits
	if(wdata > 0 && wdata < 32) {
		unsigned sh = 32 - wdata;
		for(unsigned r=0; r<nrow; r++) {
			for(unsigned c=0; c<ncol; c++) {
				int val = array[r][c];
				val = (val << sh) >> sh;
				array[r][c] = val;
			}
		}
	}
}

// Return the number of cells whose value would exceed the specified bit width
int array_check_data_width(int** array, unsigned nrow, unsigned col1, unsigned col2, unsigned wdata, bool sdata) {
	if(wdata >= 32) return 0;

	unsigned sh = 32 - wdata;
	unsigned errors_nb = 0;

	// Signed
	if(sdata == true) {
		for(unsigned r=0; r<nrow; r++) {
			for(unsigned c=col1; c<col2; c++) {
				int val = array[r][c];
				if( ((val << sh) >> sh) != val ) errors_nb++;
			}
		}
	}

	// Unsigned
	else {
		for(unsigned r=0; r<nrow; r++) {
			for(unsigned c=col1; c<col2; c++) {
				unsigned val = array[r][c];
				if( ((val << sh) >> sh) != val ) errors_nb++;
			}
		}
	}

	return errors_nb;
}
// Return the number of cells whose range does not fit
int array_check_data_min_max(int** array, unsigned nrow, unsigned col1, unsigned col2, int min, int max) {
	unsigned errors_nb = 0;
	for(unsigned r=0; r<nrow; r++) {
		for(unsigned c=col1; c<col2; c++) {
			int val = array[r][c];
			if(val < min || val > max) errors_nb++;
		}
	}
	return errors_nb;
}
// Return the number of cells whose value is not binary -1/+1
int array_check_data_bin_sym(int** array, unsigned nrow, unsigned col1, unsigned col2) {
	unsigned errors_nb = 0;
	for(unsigned r=0; r<nrow; r++) {
		for(unsigned c=col1; c<col2; c++) {
			int val = array[r][c];
			if(val != -1 && val != 1) errors_nb++;
		}
	}
	return errors_nb;
}


//============================================
// Arrays of doubles
//============================================

// Create a 2-dimensions array
double** array_create_double2(unsigned nrows, unsigned ncol) {
	double **array = (double**)malloc(nrows * sizeof(*array));
	array[0] = (double*)malloc(nrows * ncol * sizeof(**array));
	for(unsigned i=1; i<nrows; i++) array[i] = array[i-1] + ncol;
	return array;
}

// Read one frame from the file
// File encoding: data items are signed floating-point values separated by other characters
// Return the number of values obtained, or -1 if already at the end of the file
int loadfile_oneframe_double(FILE* F, double* buf, unsigned fsize, bool allow_multiline) {
	// A buffer to read one line
	static size_t linebuf_size = 0;
	static char* linebuf = NULL;
	ssize_t r;

	// Current number of values obtained
	unsigned items_nb = 0;

	do {

		// Get one line = one frame
		r = getline(&linebuf, &linebuf_size, F);
		// Exit when the end of the file is reached
		if(r < 0) break;

		// Parse the line
		char* ptr = linebuf;
		char* ptr_next = linebuf;

		while(ptr!=NULL) {

			// Skip until the next value
			char c = *ptr;
			if(c==0) break;
			if(c!='-' && c!='+' && (c < '0' || c > '9')) { ptr++; continue; }

			double val = strtod(ptr, &ptr_next);
			// FIXME get errors ?
			if(ptr_next==ptr) break;

			// Here we got a value value. Check if it is within bounds.
			if(items_nb >= fsize) {
				if(overfull_cur_nb < warnings_max_nb) {
					printf("Warning: Cropping overfull frame (more than %u items)\n", fsize);
					if(overfull_cur_nb == warnings_max_nb-1) {
						printf("  Note: This warning was printed %u times. Next occurrences will not be displayed.\n", overfull_cur_nb);
					}
					overfull_cur_nb++;
				}
				break;
			}

			// Store the value
			buf[items_nb++] = val;

			// Next value
			ptr = ptr_next;

		}  // Loop that reads one line of the file

		// Exit when enough data have been obtained
		if(items_nb >= fsize) break;

		// If not wanting multi-line, exit
		if(allow_multiline==false) break;

	} while(1);  // Read the lines of the file

	if(r < 0 && items_nb==0) return -1;

	if(items_nb < fsize) {
		if(underfull_cur_nb < warnings_max_nb) {
			printf("Warning: Underfull frame: %u items instead of %u\n", items_nb, fsize);
			if(underfull_cur_nb == warnings_max_nb-1) {
				printf("  Note: This warning was printed %u times. Next occurrences will not be displayed.\n", underfull_cur_nb);
			}
			underfull_cur_nb++;
		}
	}

	#if 0
	printf("FRAME:");
	for(unsigned i=0; i<items_nb; i++) printf(" %g", buf[i]);
	printf("\n");
	#endif

	return items_nb;
}

// Load one file as double, return a 2D array, one row per frame
double** loadfile_double2(char* filename, unsigned nframes, unsigned fsize, bool allow_multiline) {
	printf("INFO: Reading file '%s'\n", filename);

	FILE* F = fopen(filename, "rb");
	if(F==NULL) {
		printf("ERROR: Can't open file '%s'\n", filename);
		return NULL;
	}

	double** array = array_create_double2(nframes, fsize);

	unsigned curframe = 0;

	load_warnings_clear();

	do {

		// Get one frame
		int r = loadfile_oneframe_double(F, array[curframe], fsize, allow_multiline);
		if(r > 0) curframe ++;

		// If the big buffer contains enough frames, exit
		if(curframe >= nframes) break;

		// Exit when the end of the file is reached
		if(r < 0) {
			printf("Warning: Only got %u frames instead of %u\n", curframe, nframes);
			break;
		}

	} while(1);  // Read the lines of the file

	// Clean
	fclose(F);

	return array;
}

