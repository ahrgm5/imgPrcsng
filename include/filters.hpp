// filters.hpp
#ifndef INCLUDE_FILTERS_HPP_
#define INCLUDE_FILTERS_HPP_

#include <fftw3.h>
#include "bmp_handling.hpp"
#include "hsi_handling.hpp"
#include <stddef.h>

typedef struct Filter {
    int width, height;
    double** img_in;
    double** img_out;
    fftw_complex* fft_in;
    fftw_complex* fft_out;
    fftw_plan forward_plan;
    fftw_plan backward_plan;
    void (*apply_mask)(struct Filter *self);
} Filter;

typedef struct{ Filter base; double percent;} TruncationFilter;
typedef struct{ Filter base; double cutoff; } lpFilter;
typedef struct{ Filter base; double cutoff; } hpFilter;
typedef struct{ Filter base; double low, high; } bpFilter;
typedef struct{ Filter base; double low, high; } brFilter;
typedef struct{ Filter base; double sigma; } GaussianFilter;

Filter* create_filter(int w, int h, void (*mask_func)(Filter*), size_t struct_size);
void filter_execute(Filter *f);
void destroy_filter(Filter *f);

// Application bridge
BMPImage* apply_frequency_filter_to_bmp(BMPImage* input, Filter* f);



// Generalized logic function
void rect_truncation_logic(Filter* self);
void lpf_logic(Filter *self);
void hpf_logic(Filter *self);
void bpf_logic(Filter *self);
void brf_logic(Filter *self);

#endif /* INCLUDE_FILTERS_HPP_ */
