// filters.cpp
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include "filters.hpp"

static double get_dist(int i, int j, int h, int w){
    double di = (i>h/2)? (i-h): i;
    double dj = (j>w/2)? (j-w): j;
    return sqrt(di*di + dj*dj);
}

fftw_complex* flatten_matrix(int w, int h, double** image){
    int size = w*h;
    fftw_complex* number = (fftw_complex*)calloc(size, sizeof(fftw_complex));
    for(int i = 0; i<h; i++)
        for(int j = 0; j < w; j++) number[i*w +j][0] = image[i][j];
    return number;
}

Filter* create_filter(int w, int h, void (*mask_func)(Filter*), size_t struct_size){
    Filter* f = (Filter*)malloc(struct_size);
    f->width = w; f->height = h; f->apply_mask = mask_func;
    f->img_in = (double**)malloc(h*sizeof(double*));
    f->img_out = (double**)malloc(h*sizeof(double*));

    for(int i = 0; i < h; i++) {
        f->img_in[i] = (double*)calloc(w, sizeof(double));
        f->img_out[i] = (double*)calloc(w, sizeof(double));
    }

    f->fft_in = fftw_alloc_complex(w*h);
    f->fft_out = fftw_alloc_complex(w*h);
    f->forward_plan = fftw_plan_dft_2d(h, w, f->fft_in, f->fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
    f->backward_plan = fftw_plan_dft_2d(h, w, f->fft_out, f->fft_in, FFTW_BACKWARD, FFTW_ESTIMATE);

    return f;
}

void rect_truncation_logic(Filter* self) {
    TruncationFilter* tf = (TruncationFilter*)self;
    int h = self->height;
    int w = self->width;

    // To keep 'percent' of the area, we keep sqrt(percent) of each dimension
    double ratio = sqrt(tf->percent);

    // Calculate the boundary for the low frequencies
    int limit_h = (int)(h * ratio / 2.0);
    int limit_w = (int)(w * ratio / 2.0);

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // Standard DFT layout: DC is at (0,0)
            // di/dj represent the distance to the nearest "corner" (low frequency)
            int di = (i > h / 2) ? (h - i) : i;
            int dj = (j > w / 2) ? (w - j) : j;

            if (di > limit_h || dj > limit_w) {
                self->fft_out[i * w + j][0] = 0.0;
                self->fft_out[i * w + j][1] = 0.0;
            }
        }
    }
}


void lpf_logic(Filter *self){
    double cutoff = ((lpFilter*) self)->cutoff;
    for(int i = 0; i < self->height; i++)
        for(int j =0; j < self->width; j++){
            if(get_dist(i,j, self->height, self->width) > cutoff) {
                self->fft_out[i*self->width +j][0] = 0.0 ;
                self->fft_out[i*self->width +j][1] = 0.0 ;
            }
        }
}

void hpf_logic(Filter *self){
    double cutoff = ((hpFilter*) self)->cutoff;
    for(int i = 0; i < self->height; i++)
        for(int j =0; j < self->width; j++){
            if(get_dist(i,j, self->height, self->width) <= cutoff) {
                self->fft_out[i*self->width +j][0] = 0.0 ;
                self->fft_out[i*self->width +j][1] = 0.0 ;
            }
        }
}

void bpf_logic(Filter *self){
    double low = ((bpFilter*) self)->low;
    double high = ((bpFilter*) self)->high;
    for(int i = 0; i < self->height; i++)
        for(int j =0; j < self->width; j++){
            double d = get_dist(i,j, self->height, self->width);
            if(d < low || d > high) {
                self->fft_out[i*self->width +j][0] = 0.0 ;
                self->fft_out[i*self->width +j][1] = 0.0 ;
            }
        }
}

void brf_logic(Filter *self){
    double low = ((bpFilter*) self)->low;
    double high = ((bpFilter*) self)->high;
    for(int i = 0; i < self->height; i++)
        for(int j =0; j < self->width; j++){
            double d = get_dist(i,j, self->height, self->width);
            if(d >= low && d<=high) {
                self->fft_out[i*self->width +j][0] = 0.0 ;
                self->fft_out[i*self->width +j][1] = 0.0 ;
            }
        }
}


void filter_execute(Filter *f) {
    // DO NOT fftw_free(f->fft_in) or reassign the pointer.
    // The plan is bound to the specific address allocated in create_filter.

    // Copy 2D img_in data into the 1D fft_in complex array
    for(int i = 0; i < f->height; i++) {
        for(int j = 0; j < f->width; j++) {
            int idx = i * f->width + j;
            f->fft_in[idx][0] = f->img_in[i][j]; // Real part
            f->fft_in[idx][1] = 0.0;             // Imaginary part
        }
    }

    // Execute the forward transform
    fftw_execute(f->forward_plan);

    // Apply the frequency domain mask
    if(f->apply_mask) f->apply_mask(f);

    // Execute the backward transform
    fftw_execute(f->backward_plan);

    // Normalize the result and copy back to img_out
    double norm = 1.0 / (f->width * f->height);
    for(int i = 0; i < f->height; i++) {
        for(int j = 0; j < f->width; j++) {
            // Note: FFTW backward transform stores result in f->fft_in
            // because of how plans were defined in create_filter
            f->img_out[i][j] = f->fft_in[i * f->width + j][0] * norm;
        }
    }
}


BMPImage* apply_frequency_filter_to_bmp(BMPImage* input, Filter* f) {
    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);

    // Ensure dimensions match
    if (w != f->width || h != f->height) return nullptr;

    BMPImage* output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);
    HSIImage* hsi = nullptr;

    // Load data into filter
    if (input->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(input);
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) f->img_in[i][j] = hsi->pixels[i][j].i;
    } else {
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) f->img_in[i][j] = input->pixels[i][j] / 255.0;
    }

    filter_execute(f);

    // Extract data from filter
    if (hsi) {
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) {
            hsi->pixels[i][j].i = std::clamp((float)f->img_out[i][j], 0.0f, 1.0f);
        }
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) {
            output->pixels[i][j] = (uint8_t)std::clamp(f->img_out[i][j] * 255.0, 0.0, 255.0);
        }
    }

    return output;
}

void destroy_filter(Filter *f){
    // Fixed memory leak loop condition (i < f->height)
    for(int i = 0; i < f->height; i++){ free(f->img_in[i]); free(f->img_out[i]);}
    free(f->img_in);
    free(f->img_out);
    fftw_destroy_plan(f->forward_plan);
    fftw_destroy_plan(f->backward_plan);
    fftw_free(f->fft_in);
    fftw_free(f->fft_out);
    free(f);
}
