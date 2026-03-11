// filters.cpp
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include "filters.hpp"

// =========================================================
// HELPER FUNCTIONS
// =========================================================

// Calculates distance from the origin.
// DFT origin is at the 4 corners, DCT origin is strictly top-left (0,0).
static double get_freq_dist(int i, int j, int h, int w, FilterDomain domain) {
    if (domain == FILTER_DOMAIN_DFT) {
        // DFT DC component is split across the corners
        double di = (i > h / 2) ? (h - i) : i;
        double dj = (j > w / 2) ? (w - j) : j;
        return sqrt(di * di + dj * dj);
    } else {
        // DCT DC component is strictly at the (0,0) top-left origin
        return sqrt((double)(i * i + j * j));
    }
}

// =========================================================
// CORE FILTER LIFECYCLE & EXECUTION
// =========================================================

Filter* create_filter(FilterDomain domain, int w, int h, void (*mask_func)(Filter*), size_t struct_size){
    // Use calloc to initialize all unused domain pointers to NULL
    Filter* f = (Filter*)calloc(1, struct_size);
    f->domain = domain;
    f->width = w;
    f->height = h;
    f->apply_mask = mask_func;

    f->img_in = (double**)malloc(h * sizeof(double*));
    f->img_out = (double**)malloc(h * sizeof(double*));
    for(int i = 0; i < h; i++) {
        f->img_in[i] = (double*)calloc(w, sizeof(double));
        f->img_out[i] = (double*)calloc(w, sizeof(double));
    }

    // Allocate only the necessary FFTW buffers and plans for the requested domain
    if (domain == FILTER_DOMAIN_DFT) {
        f->dft_in = fftw_alloc_complex(w * h);
        f->dft_out = fftw_alloc_complex(w * h);
        f->dft_forward = fftw_plan_dft_2d(h, w, f->dft_in, f->dft_out, FFTW_FORWARD, FFTW_ESTIMATE);
        f->dft_backward = fftw_plan_dft_2d(h, w, f->dft_out, f->dft_in, FFTW_BACKWARD, FFTW_ESTIMATE);
    } else {
        f->dct_in = (double*)fftw_malloc(sizeof(double) * w * h);
        f->dct_out = (double*)fftw_malloc(sizeof(double) * w * h);
        f->dct_forward = fftw_plan_r2r_2d(h, w, f->dct_in, f->dct_out, FFTW_REDFT10, FFTW_REDFT10, FFTW_ESTIMATE);
        f->dct_backward = fftw_plan_r2r_2d(h, w, f->dct_out, f->dct_in, FFTW_REDFT01, FFTW_REDFT01, FFTW_ESTIMATE);
    }
    return f;
}

void filter_execute(Filter *f) {
    int w = f->width;
    int h = f->height;

    if (f->domain == FILTER_DOMAIN_DFT) {
        // --- DFT Execution Path ---
        // 1. Copy spatial data into complex input buffer
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                f->dft_in[i*w+j][0] = f->img_in[i][j];
                f->dft_in[i*w+j][1] = 0.0;
            }
        }

        // 2. Forward Transform (Spatial -> Frequency)
        fftw_execute(f->dft_forward);

        // 3. Apply Mask (Modifies f->dft_out)
        if(f->apply_mask) f->apply_mask(f);

        // 4. Inverse Transform (Frequency -> Spatial)
        fftw_execute(f->dft_backward);

        // 5. Normalize and extract
        double norm = 1.0 / (w * h);
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                f->img_out[i][j] = f->dft_in[i*w+j][0] * norm;
            }
        }
    } else {
        // --- DCT Execution Path ---
        // 1. Copy spatial data into real input buffer
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                f->dct_in[i*w+j] = f->img_in[i][j];
            }
        }

        // 2. Forward Transform (Spatial -> Frequency)
        fftw_execute(f->dct_forward);

        // 3. Apply Mask (Modifies f->dct_out)
        if(f->apply_mask) f->apply_mask(f);

        // 4. Inverse Transform (Frequency -> Spatial)
        fftw_execute(f->dct_backward);

        // 5. Normalize and extract (DCT-II/IDCT-II requires 1/(4N) scaling)
        double norm = 1.0 / (4.0 * w * h);
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                f->img_out[i][j] = f->dct_in[i*w+j] * norm;
            }
        }
    }
}

void destroy_filter(Filter *f){
    for(int i = 0; i < f->height; i++){
        free(f->img_in[i]);
        free(f->img_out[i]);
    }
    free(f->img_in);
    free(f->img_out);

    // Safely free the domain-specific pointers
    if (f->domain == FILTER_DOMAIN_DFT) {
        fftw_destroy_plan(f->dft_forward);
        fftw_destroy_plan(f->dft_backward);
        fftw_free(f->dft_in);
        fftw_free(f->dft_out);
    } else {
        fftw_destroy_plan(f->dct_forward);
        fftw_destroy_plan(f->dct_backward);
        fftw_free(f->dct_in);
        fftw_free(f->dct_out);
    }
    free(f);
}

// =========================================================
// UNIVERSAL MASKS (Filters)
// =========================================================

void rect_truncation_logic(Filter* self) {
    TruncationFilter* tf = (TruncationFilter*)self;
    double ratio = sqrt(tf->percent);
    int limit_h = (int)(self->height * ratio / 2.0);
    int limit_w = (int)(self->width * ratio / 2.0);

    for (int i = 0; i < self->height; i++) {
        for (int j = 0; j < self->width; j++) {
            bool mask_out = false;

            if (self->domain == FILTER_DOMAIN_DFT) {
                int di = (i > self->height / 2) ? (self->height - i) : i;
                int dj = (j > self->width / 2) ? (self->width - j) : j;
                if (di > limit_h || dj > limit_w) mask_out = true;
            } else {
                // DCT: Energy is packed at the top-left, not corners
                if (i > limit_h * 2 || j > limit_w * 2) mask_out = true;
            }

            if (mask_out) {
                if (self->domain == FILTER_DOMAIN_DFT) {
                    self->dft_out[i * self->width + j][0] = 0.0;
                    self->dft_out[i * self->width + j][1] = 0.0;
                } else {
                    self->dct_out[i * self->width + j] = 0.0;
                }
            }
        }
    }
}

void lpf_logic(Filter *self){
    double cutoff = ((lpFilter*) self)->cutoff;
    for(int i = 0; i < self->height; i++) {
        for(int j = 0; j < self->width; j++) {
            if(get_freq_dist(i, j, self->height, self->width, self->domain) > cutoff) {
                if (self->domain == FILTER_DOMAIN_DFT) {
                    self->dft_out[i * self->width + j][0] = 0.0;
                    self->dft_out[i * self->width + j][1] = 0.0;
                } else {
                    self->dct_out[i * self->width + j] = 0.0;
                }
            }
        }
    }
}

void hpf_logic(Filter *self){
    double cutoff = ((hpFilter*) self)->cutoff;
    for(int i = 0; i < self->height; i++) {
        for(int j = 0; j < self->width; j++) {
            if(get_freq_dist(i, j, self->height, self->width, self->domain) <= cutoff) {
                if (self->domain == FILTER_DOMAIN_DFT) {
                    self->dft_out[i * self->width + j][0] = 0.0;
                    self->dft_out[i * self->width + j][1] = 0.0;
                } else {
                    self->dct_out[i * self->width + j] = 0.0;
                }
            }
        }
    }
}

void bpf_logic(Filter *self){
    double low = ((bpFilter*) self)->low;
    double high = ((bpFilter*) self)->high;
    for(int i = 0; i < self->height; i++) {
        for(int j = 0; j < self->width; j++) {
            double d = get_freq_dist(i, j, self->height, self->width, self->domain);
            if(d < low || d > high) {
                if (self->domain == FILTER_DOMAIN_DFT) {
                    self->dft_out[i * self->width + j][0] = 0.0;
                    self->dft_out[i * self->width + j][1] = 0.0;
                } else {
                    self->dct_out[i * self->width + j] = 0.0;
                }
            }
        }
    }
}

void brf_logic(Filter *self){
    double low = ((brFilter*) self)->low;
    double high = ((brFilter*) self)->high;
    for(int i = 0; i < self->height; i++) {
        for(int j = 0; j < self->width; j++) {
            double d = get_freq_dist(i, j, self->height, self->width, self->domain);
            if(d >= low && d <= high) {
                if (self->domain == FILTER_DOMAIN_DFT) {
                    self->dft_out[i * self->width + j][0] = 0.0;
                    self->dft_out[i * self->width + j][1] = 0.0;
                } else {
                    self->dct_out[i * self->width + j] = 0.0;
                }
            }
        }
    }
}

// =========================================================
// APPLICATION BRIDGE
// =========================================================

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
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                f->img_in[i][j] = hsi->pixels[i][j].i;
            }
        }
    } else {
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                f->img_in[i][j] = input->pixels[i][j] / 255.0;
            }
        }
    }

    // Execute transform, mask, and inverse transform across chosen domain
    filter_execute(f);

    // Extract data from filter
    if (hsi) {
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                hsi->pixels[i][j].i = std::clamp((float)f->img_out[i][j], 0.0f, 1.0f);
            }
        }
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                output->pixels[i][j] = (uint8_t)std::clamp(f->img_out[i][j] * 255.0, 0.0, 255.0);
            }
        }
    }

    return output;
}
