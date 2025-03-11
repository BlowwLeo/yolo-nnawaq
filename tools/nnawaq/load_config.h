
#pragma once

#include <stdio.h>


// To parse data files
void load_warnings_clear();


//================================================
// Arrays of integers
//================================================

int** array_create_dim2(unsigned nrows, unsigned ncol);

// Read files at integer
int loadfile_oneframe(FILE* F, int* buf, unsigned fsize, bool allow_multiline);
int loadfile(int** array, const char* filename, unsigned nframes, unsigned fsize, bool allow_multiline);

void array_fillrand_dim2(int** array, unsigned nrow, unsigned ncol, unsigned wdata, int rand_min, int rand_max);

int array_check_data_width(int** array, unsigned nrow, unsigned col1, unsigned col2, unsigned wdata, bool sdata);
int array_check_data_min_max(int** array, unsigned nrow, unsigned col1, unsigned col2, int min, int max);
int array_check_data_bin_sym(int** array, unsigned nrow, unsigned col1, unsigned col2);


//================================================
// Arrays of double
//================================================

double** array_create_double2(unsigned nrows, unsigned ncol);

int loadfile_oneframe_double(FILE* F, double* buf, unsigned fsize, bool allow_multiline);
double** loadfile_double2(char* filename, unsigned nframes, unsigned fsize, bool allow_multiline);

