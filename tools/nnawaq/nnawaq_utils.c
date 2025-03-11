
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


//============================================
// Miscellaneous
//============================================

// Generate a mask of a certain number of lower bits set
unsigned uint_genmask(unsigned bits) {
	if(bits == 0) return 0;
	if(bits >= 8*sizeof(unsigned)) return (~(unsigned)0);
	return (~(unsigned)0) >> (8*sizeof(unsigned) - bits);
}
uint64_t u64_genmask(unsigned bits) {
	if(bits == 0) return 0;
	if(bits >= 8*sizeof(uint64_t)) return (~(uint64_t)0);
	return (~(uint64_t)0) >> (8*sizeof(uint64_t) - bits);
}

// Count the number of bits needed to store the value
// This also returns 1 for the value 0
unsigned uint_bitsnb(unsigned v) {
	unsigned c = 1;
	while(v > 1) { v >>= 1; c++; }
	return c;
}
unsigned u64_bitsnb(uint64_t v) {
	unsigned c = 1;
	while(v > 1) { v >>= 1; c++; }
	return c;
}

// Count the number of decimal digits needed to store the value
// This also returns 1 for the value 0
unsigned uint_digitsnb(unsigned v) {
	unsigned c = 1;
	while(v >= 10) { v /= 10; c++; }
	return c;
}
unsigned u64_digitsnb(uint64_t v) {
	unsigned c = 1;
	while(v >= 10) { v /= 10; c++; }
	return c;
}

// Round to a power of 2
unsigned uint_rndpow2_ceil(unsigned v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;

	// And another operation for systems where an unsigned is 64 bits
	#ifdef __SIZEOF_INT__
		#if __SIZEOF_INT__ > 4
			v |= v >> 32;
		#endif
	#else
		// this code should be optimized out by the compiler
		v = sizeof(v) > 4 ? v | v >> 32 : v;
	#endif

	v++;
	return v;
}
unsigned uint_rndpow2_floor(unsigned v) {
	unsigned p = uint_rndpow2_ceil(v);
	if(p != v) p /= 2;
	return p;
}

// Print a double number, with units
void fprint_unit_double(FILE* F, char const * fmt, double d, char const * unit) {
	int p = 0;
	while(fabs(d) >= 1000000 && p < 5) { d /= 1000; p ++; }
	while(fabs(d) < 1 && p > -5) { d *= 1000; p --; }

	fprintf(F, fmt, d);

	if(p!=0 || unit!=NULL) {
		const char* exp_pos = "kMGTP";
		const char* exp_neg = "munpf";

		char e = 0;
		if(p > 0) e = exp_pos[p-1];
		if(p < 0) e = exp_neg[-p-1];

		fputc(' ', F);
		if(e != 0) fputc(e, F);
		if(unit != NULL) fprintf(F, "%s", unit);
	}
}

// Parse a double, handle percent
double strtod_perc(const char *str) {
	char* endptr = NULL;
	double d = strtod(str, &endptr);
	if((*endptr)=='%') d /= 100;
	return d;
}


//============================================
// Time
//============================================

#define TIME64PERSEC 1000000000

double TimeDouble_FromTS(struct timespec *ts) {
	return (double)ts->tv_sec + ((double)ts->tv_nsec)/1.0e9;
}
double TimeDouble_From64(int64_t v64) {
	return ((double)v64) / 1.0e9;
}
int64_t Time64_FromTS(struct timespec *ts) {
	return (int64_t)ts->tv_sec * TIME64PERSEC + ts->tv_nsec;
}
void TimeSpec_From64(struct timespec *ts, int64_t v64) {
	ts->tv_sec = v64 / TIME64PERSEC;
	ts->tv_nsec = v64 % TIME64PERSEC;
}

// Important: This returns the system absolute time
void TimeSpec_GetReal(struct timespec *ts) {
	clock_gettime(CLOCK_REALTIME, ts);
}

int64_t Time64_GetReal() {
	struct timespec ts;
	TimeSpec_GetReal(&ts);
	return Time64_FromTS(&ts);
}

double TimeDouble_GetDiff_FromTS(struct timespec *oldtime, struct timespec *newtime) {
	return TimeDouble_From64(Time64_FromTS(newtime) -  Time64_FromTS(oldtime));
}

double TimeDouble_DiffCurrReal_From64(int64_t oldtime) {
	return TimeDouble_From64(Time64_GetReal() - oldtime);
}


//============================================
// Generation of random config data
//============================================

// FIXME We may want to have only min or max specified, also handled signedness
int gencsv_rand(FILE* F, unsigned nrow, unsigned ncol, int rand_min, int rand_max, bool sgn, unsigned bits, char const * sep) {
	if(sep == NULL) sep = ",";

	unsigned sh = 0;
	if(bits > 0 && bits < 32) sh = 32 - bits;

	unsigned rand_range = 0;
	if(rand_min < rand_max) {
		rand_range = rand_max - rand_min + 1;
	}

	for(unsigned l=0; l<nrow; l++) {
		for(unsigned c=0; c<ncol; c++) {
			if(c > 0) fputs(sep, F);

			// Generate the value in the desired range, if applicable
			int val = rand();
			if(rand_range > 0) {
				val = rand_min + (((unsigned)val) % rand_range);
			}

			// Crop, sign extension, and print
			if(sgn == true) {
				val = (val << sh) >> sh;
				fprintf(F, "%i", val);
			}
			else {
				unsigned u = val;
				u = (u << sh) >> sh;
				fprintf(F, "%u", u);
			}

		}  // col
		fprintf(F, "\n");

	}  // row

	return 0;
}

// Generation of non-random config data, useful for debug

int gencsv_id(FILE* F, unsigned nrow, unsigned ncol, char const * sep) {
	if(sep == NULL) sep = ",";
	for(unsigned r=0; r<nrow; r++) {
		for(unsigned c=0; c<ncol; c++) {
			if(c > 0) fputs(sep, F);
			fprintf(F, "%u", (c==r) ? 1 : 0);
		}
		fprintf(F, "\n");
	}
	return 0;
}

int gencsv_seq(FILE* F, unsigned nrow, unsigned ncol, char const * sep) {
	if(sep == NULL) sep = ",";
	for(unsigned r=0; r<nrow; r++) {
		for(unsigned c=0; c<ncol; c++) {
			if(c > 0) fputs(sep, F);
			fprintf(F, "%u", c);
		}
		fprintf(F, "\n");
	}
	return 0;
}

