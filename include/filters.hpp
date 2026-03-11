#ifndef INCLUDE_FILTERS_HPP_
#define INCLUDE_FILTERS_HPP_

#include <bmp.hpp>
#include <fftw3.h>
#include <stddef.h>

// 1. Define the domains
typedef enum {
    FILTER_DOMAIN_DFT,
    FILTER_DOMAIN_DCT
} FilterDomain;

// 2. Unified Filter Struct
typedef struct Filter {
    FilterDomain domain;
    int width, height;
    double** img_in;
    double** img_out;

    // DFT Pointers and Plans
    fftw_complex* dft_in;
    fftw_complex* dft_out;
    fftw_plan dft_forward;
    fftw_plan dft_backward;

    // DCT Pointers and Plans
    double* dct_in;
    double* dct_out;
    fftw_plan dct_forward;
    fftw_plan dct_backward;

    void (*apply_mask)(struct Filter *self);
} Filter;

// 3. Existing Filter Types remain completely unchanged
typedef struct{ Filter base; double percent;} TruncationFilter;
typedef struct{ Filter base; double cutoff; } lpFilter;
typedef struct{ Filter base; double cutoff; } hpFilter;
typedef struct{ Filter base; double low, high; } bpFilter;
typedef struct{ Filter base; double low, high; } brFilter;
typedef struct{ Filter base; double sigma; } GaussianFilter;

// 4. Update the Constructor Signature
Filter* create_filter(FilterDomain domain, int w, int h, void (*mask_func)(Filter*), size_t struct_size);

void filter_execute(Filter *f);
void destroy_filter(Filter *f);
BMPImage* apply_frequency_filter_to_bmp(BMPImage* input, Filter* f);

void rect_truncation_logic(Filter* self);
void lpf_logic(Filter *self);
void hpf_logic(Filter *self);
void bpf_logic(Filter *self);
void brf_logic(Filter *self);

#endif /* INCLUDE_FILTERS_HPP_ */
