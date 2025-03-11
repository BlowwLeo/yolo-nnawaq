
#ifndef _NNAWAQ_UTILS_H_
#define _NNAWAQ_UTILS_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


//================================================
// Useful functions
//================================================

unsigned uint_genmask(unsigned bits);
uint64_t u64_genmask(unsigned bits);

unsigned uint_bitsnb(unsigned v);
unsigned u64_bitsnb(uint64_t v);

unsigned uint_digitsnb(unsigned v);
unsigned u64_digitsnb(uint64_t v);

unsigned uint_rndpow2_ceil(unsigned v);
unsigned uint_rndpow2_floor(unsigned v);
static inline bool uint_ispow2(unsigned v) { return (v == uint_rndpow2_ceil(v)); }

double strtod_perc(const char *str);

void fprint_unit_double(FILE* F, char const * fmt, double d, char const * unit);
static inline void print_unit_double(char const * fmt, double d, char const * unit) {
	fprint_unit_double(stdout, fmt, d, unit);
}

static inline unsigned uint_next_multiple(unsigned num, unsigned div) {
	unsigned off = num % div;
	return num + (off != 0 ? div : 0) - off;
}
static inline void* ptr_next_multiple(void* ptr, unsigned div) {
	unsigned off = ((intptr_t)ptr) % div;
	return ((char*)ptr) + (off != 0 ? div : 0) - off;
}

// FIXME The best implem is not really known
static inline unsigned uint_round_up(unsigned num, unsigned mult) {
	return ((num + mult - 1) / mult) * mult;
}

//================================================
// Useful macros
//================================================

// To set fields from an arbitrary variable and returns the resulting patched value
// Input  r : source variable/register
// Input  v : value to set in designated field
// Input  m : mask of the field, aligned on position zero
// Input sh : shift to apply to the field mask
#define SetField0(r, v, m, sh) (((r) & ~((m) << (sh))) | (((v) & (m)) << (sh)))
// Variant where the mask is on the actual field position (shift already applied on it)
#define SetField(r, v, m, sh) (((r) & ~(m)) | (((v) << (sh)) & (m)))

// To simplify getting max and min values
#define GetMax(v0,v1) ((v0)>(v1) ? (v0) : (v1))
#define GetMin(v0,v1) ((v0)<(v1) ? (v0) : (v1))

// To conditonally (or not) free something and clear the pointer
#define TestFree(p)     do { if(p!=NULL) free(p); } while(0)
#define TestFreeNull(p) do { if(p!=NULL) { free(p); p = NULL; } } while(0)
#define FreeNull(p)     do { free(p); p = NULL; } while(0)

#define TestDo(p,f)     do { if(p!=NULL) f(p); } while(0)
#define TestDoNull(p,f) do { if(p!=NULL) { f(p); p = NULL; } } while(0)
#define DoNull(p,f)     do { f(p); p = NULL; } while(0)


//============================================
// Time
//============================================

int64_t Time64_GetReal();
double TimeDouble_From64(int64_t v64);
double TimeDouble_DiffCurrReal_From64(int64_t oldtime);


//============================================
// Generation of random config data
//============================================

int gencsv_rand(FILE* F, unsigned nrow, unsigned ncol, int rand_min, int rand_max, bool sgn, unsigned bits, char const * sep);
int gencsv_id(FILE* F, unsigned nrow, unsigned ncol, char const * sep);
int gencsv_seq(FILE* F, unsigned nrow, unsigned ncol, char const * sep);


#endif  // _NNAWAQ_UTILS_H_

