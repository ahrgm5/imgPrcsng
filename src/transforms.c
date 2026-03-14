/* transforms.c */
#include "transforms.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================
 * INTERNAL HELPERS
 * ========================================================= */

/*
 * Forward declarations for static helper functions that are defined
 * in Section 3 (Wavelet) but called earlier in Section 2b (Wiener auto).
 * Without these, the compiler sees undeclared identifiers when it reaches
 * estimate_noise_sigma() and wiener_build_K_spectrum().
 */
static double **alloc_2d(int rows, int cols);
static void     free_2d(double **arr);
static void     haar_forward_1d(double *x, int n);

/* Comparator for qsort — used in both median estimators below */
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static double clampd(double v, double lo, double hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static int clampi(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/*
 * get_freq_dist — radial distance from the DC origin.
 *
 * DFT: DC is split across the four corners; distance is measured
 *      from the nearest corner.
 * DCT: DC is strictly at (0,0) top-left.
 */
static double get_freq_dist(int i, int j, int h, int w, FilterDomain domain) {
    double di, dj;
    if (domain == FILTER_DOMAIN_DFT) {
        di = (i > h / 2) ? (double)(h - i) : (double)i;
        dj = (j > w / 2) ? (double)(w - j) : (double)j;
    } else {
        di = (double)i;
        dj = (double)j;
    }
    return sqrt(di * di + dj * dj);
}

/* =========================================================
 * SECTION 1 — SPATIAL KERNELS
 * =========================================================
 * Order within section:
 *   1. Kernel* — construction
 *   2. void    — destruction
 *   3. void    — initialisation callbacks
 *   4. BMPImage* — application
 * ========================================================= */

/* ---------------------------------------------------------
 * Kernel construction
 * --------------------------------------------------------- */

Kernel *create_kernel(int w, int h, void (*init_func)(Kernel *), size_t struct_size) {
    Kernel *k;
    int     i;

    k = (Kernel *)malloc(struct_size);
    if (!k) return NULL;
    memset(k, 0, struct_size);

    k->width      = w;
    k->height     = h;
    k->initialize = init_func;

    k->mask = (float **)malloc(h * sizeof(float *));
    if (!k->mask) { free(k); return NULL; }

    for (i = 0; i < h; i++) {
        k->mask[i] = (float *)calloc(w, sizeof(float));
        if (!k->mask[i]) {
            while (--i >= 0) free(k->mask[i]);
            free(k->mask);
            free(k);
            return NULL;
        }
    }

    if (k->initialize) k->initialize(k);
    return k;
}

/* ---------------------------------------------------------
 * Kernel destruction
 * --------------------------------------------------------- */

void destroy_kernel(Kernel *k) {
    int i;
    if (!k) return;
    for (i = 0; i < k->height; i++) free(k->mask[i]);
    free(k->mask);
    free(k);
}

/* ---------------------------------------------------------
 * Kernel initialisation callbacks
 * --------------------------------------------------------- */

void motion_blur_init(Kernel *self) {
    MotionBlurKernel *mbk = (MotionBlurKernel *)self;
    int    L, cx, cy, j;
    double rad, cos_t, sin_t, offset;
    float  scale;
    int    px, py;

    L = mbk->L;
    if (L <= 1) return;

    rad   = mbk->theta * M_PI / 180.0;
    cos_t = cos(rad);
    sin_t = sin(rad);
    scale = 1.0f / (float)L;
    cy    = self->height / 2;
    cx    = self->width  / 2;

    for (j = 0; j < L; j++) {
        offset = (double)j - (L / 2.0);
        px     = (int)round(cx + offset * cos_t);
        py     = (int)round(cy + offset * sin_t);

        if (px >= 0 && px < self->width && py >= 0 && py < self->height) {
            self->mask[py][px] = scale;
        }
    }
}

void average_init(Kernel *self) {
    float val;
    int   i, j;

    val = 1.0f / (float)(self->width * self->height);
    for (i = 0; i < self->height; i++)
        for (j = 0; j < self->width; j++)
            self->mask[i][j] = val;
}

void laplacian_init(Kernel *self) {
    int   cy, cx, i, j;

    if (self->width == 3 && self->height == 3) {
        float lap[3][3] = {
            {-1.0f, -1.0f, -1.0f},
            {-1.0f,  8.0f, -1.0f},
            {-1.0f, -1.0f, -1.0f}
        };
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                self->mask[i][j] = lap[i][j];
    } else {
        cy = self->height / 2;
        cx = self->width  / 2;
        for (i = 0; i < self->height; i++)
            for (j = 0; j < self->width; j++)
                self->mask[i][j] = -1.0f;
        self->mask[cy][cx] = (float)(self->width * self->height - 1);
    }
}

void gaussian_init(Kernel *self) {
    GaussianKernel *gk = (GaussianKernel *)self;
    double          s, sum;
    int             cy, cx, i, j, y, x;

    s   = (gk->sigma <= 0.0) ? 1.0 : gk->sigma;
    sum = 0.0;
    cy  = self->height / 2;
    cx  = self->width  / 2;

    for (i = 0; i < self->height; i++) {
        for (j = 0; j < self->width; j++) {
            y = i - cy;
            x = j - cx;
            self->mask[i][j] = (float)(
                exp(-(x * x + y * y) / (2.0 * s * s))
                / (2.0 * M_PI * s * s));
            sum += self->mask[i][j];
        }
    }

    for (i = 0; i < self->height; i++)
        for (j = 0; j < self->width; j++)
            self->mask[i][j] /= (float)sum;
}

void sobel_init(Kernel *self) {
    SobelKernel *sk = (SobelKernel *)self;
    int          i, j;

    if (sk->horizontal) {
        float h_sobel[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                self->mask[i][j] = h_sobel[i][j];
    } else {
        float v_sobel[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                self->mask[i][j] = v_sobel[i][j];
    }
}

/* ---------------------------------------------------------
 * Kernel application
 * --------------------------------------------------------- */

static float convolve(int x, int y, int w, int h,
                      float **buffer, Kernel *k) {
    float sum;
    int   dy, dx, i, j, py, px;

    sum = 0.0f;
    dy  = k->height / 2;
    dx  = k->width  / 2;

    for (i = 0; i < k->height; i++) {
        for (j = 0; j < k->width; j++) {
            py   = clampi(y + i - dy, 0, h - 1);
            px   = clampi(x + j - dx, 0, w - 1);
            sum += buffer[py][px] * k->mask[i][j];
        }
    }
    return sum;
}

BMPImage *apply_kernel_to_bmp(BMPImage *input, Kernel *k) {
    int       w, h, i, j;
    BMPImage *output;
    float   **intensity;
    float   **result;
    HSIImage *hsi;

    w      = input->info_header.width_px;
    h      = abs(input->info_header.height_px);
    output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);
    if (!output) return NULL;

    intensity = (float **)malloc(h * sizeof(float *));
    result    = (float **)malloc(h * sizeof(float *));
    for (i = 0; i < h; i++) {
        intensity[i] = (float *)malloc(w * sizeof(float));
        result[i]    = (float *)malloc(w * sizeof(float));
    }

    hsi = NULL;
    if (input->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(input);
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                intensity[i][j] = hsi->pixels[i][j].i;
    } else {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                intensity[i][j] = input->pixels[i][j] / 255.0f;
    }

    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++)
            result[i][j] = convolve(j, i, w, h, intensity, k);

    if (hsi) {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                hsi->pixels[i][j].i = clampf(result[i][j], 0.0f, 1.0f);
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                output->pixels[i][j] =
                    (uint8_t)clampf(result[i][j] * 255.0f, 0.0f, 255.0f);
    }

    for (i = 0; i < h; i++) { free(intensity[i]); free(result[i]); }
    free(intensity);
    free(result);

    return output;
}


/* =========================================================
 * SECTION 2 — FREQUENCY-DOMAIN FILTERS
 * =========================================================
 * Order within section:
 *   1. Filter*   — construction (generic then specialised)
 *   2. void      — destruction
 *   3. void      — mask (filter logic) callbacks
 *   4. void      — filter execution
 *   5. BMPImage* — filter application
 * ========================================================= */

/* ---------------------------------------------------------
 * Filter construction
 * --------------------------------------------------------- */

Filter *create_filter(FilterDomain domain, int w, int h,
                      void (*mask_func)(Filter *), size_t struct_size) {
    Filter *f;
    int     i;

    f = (Filter *)calloc(1, struct_size);
    if (!f) return NULL;

    f->domain      = domain;
    f->width       = w;
    f->height      = h;
    f->apply_mask  = mask_func;

    f->img_in  = (double **)malloc(h * sizeof(double *));
    f->img_out = (double **)malloc(h * sizeof(double *));
    for (i = 0; i < h; i++) {
        f->img_in[i]  = (double *)calloc(w, sizeof(double));
        f->img_out[i] = (double *)calloc(w, sizeof(double));
    }

    if (domain == FILTER_DOMAIN_DFT) {
        f->dft_in      = fftw_alloc_complex(w * h);
        f->dft_out     = fftw_alloc_complex(w * h);
        f->dft_forward = fftw_plan_dft_2d(h, w,
                             f->dft_in, f->dft_out,
                             FFTW_FORWARD,  FFTW_ESTIMATE);
        f->dft_backward = fftw_plan_dft_2d(h, w,
                              f->dft_out, f->dft_in,
                              FFTW_BACKWARD, FFTW_ESTIMATE);
    } else {
        f->dct_in  = (double *)fftw_malloc(sizeof(double) * w * h);
        f->dct_out = (double *)fftw_malloc(sizeof(double) * w * h);
        f->dct_forward  = fftw_plan_r2r_2d(h, w,
                               f->dct_in,  f->dct_out,
                               FFTW_REDFT10, FFTW_REDFT10, FFTW_ESTIMATE);
        f->dct_backward = fftw_plan_r2r_2d(h, w,
                               f->dct_out, f->dct_in,
                               FFTW_REDFT01, FFTW_REDFT01, FFTW_ESTIMATE);
    }
    return f;
}

Filter *create_wiener_filter(FilterDomain domain, int w, int h,
                             Kernel *blur_kernel, double K) {
    Filter       *f;
    WienerFilter *wf;
    fftw_complex *kernel_spatial;
    fftw_plan     p;
    int           kh, kw, i, j, ni, nj;

    if (domain != FILTER_DOMAIN_DFT) {
        printf("Error: Wiener filter requires FILTER_DOMAIN_DFT.\n");
        return NULL;
    }

    f = create_filter(domain, w, h, wiener_filter_logic, sizeof(WienerFilter));
    if (!f) return NULL;

    wf    = (WienerFilter *)f;
    wf->K = K;
    wf->H = fftw_alloc_complex(w * h);

    /* Build a zero-padded spatial buffer for the kernel centred to
       avoid phase shifts after the forward DFT. */
    kernel_spatial = fftw_alloc_complex(w * h);
    for (i = 0; i < h * w; i++) {
        kernel_spatial[i][0] = 0.0;
        kernel_spatial[i][1] = 0.0;
    }

    kh = blur_kernel->height;
    kw = blur_kernel->width;
    for (i = 0; i < kh; i++) {
        for (j = 0; j < kw; j++) {
            ni = (i - kh / 2 + h) % h;
            nj = (j - kw / 2 + w) % w;
            kernel_spatial[ni * w + nj][0] = blur_kernel->mask[i][j];
        }
    }

    p = fftw_plan_dft_2d(h, w, kernel_spatial, wf->H,
                         FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(p);
    fftw_destroy_plan(p);
    fftw_free(kernel_spatial);

    return f;
}

/* ---------------------------------------------------------
 * Filter destruction
 * --------------------------------------------------------- */

void destroy_filter(Filter *f) {
    int i;

    if (!f) return;

    for (i = 0; i < f->height; i++) {
        free(f->img_in[i]);
        free(f->img_out[i]);
    }
    free(f->img_in);
    free(f->img_out);

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

void destroy_wiener_filter(Filter *f) {
    WienerFilter *wf;
    if (!f) return;
    wf = (WienerFilter *)f;
    if (wf->H)          fftw_free(wf->H);
    if (wf->K_spectrum) free(wf->K_spectrum);
    destroy_filter(f);
}

/* ---------------------------------------------------------
 * Mask (filter logic) callbacks
 * --------------------------------------------------------- */

void wiener_filter_logic(Filter *self) {
    WienerFilter *wf;
    int           N, i;
    double        K_val, a, b, den, g_r, g_i;
    fftw_complex *G;

    wf = (WienerFilter *)self;
    N  = self->width * self->height;
    G  = self->dft_out;

    for (i = 0; i < N; i++) {
        a = wf->H[i][0];
        b = wf->H[i][1];

        /*
         * Use the per-frequency spectrum when available; fall back to
         * the scalar K otherwise.  The adaptive spectrum was built from
         * the actual input power spectrum so regularisation is strongest
         * exactly where the blur OTF is weakest.
         */
        K_val = (wf->K_spectrum != NULL) ? wf->K_spectrum[i] : wf->K;

        den = (a * a + b * b) + K_val;   /* |H|² + K(u,v) */
        g_r = G[i][0];
        g_i = G[i][1];

        /* F'(u,v) = G(u,v) · H*(u,v) / (|H(u,v)|² + K(u,v)) */
        G[i][0] = (g_r * a + g_i * b) / den;
        G[i][1] = (g_i * a - g_r * b) / den;
    }
}

void rect_truncation_logic(Filter *self) {
    TruncationFilter *tf;
    double            ratio;
    int               limit_h, limit_w, i, j, di, dj;
    int               mask_out;

    tf      = (TruncationFilter *)self;
    ratio   = sqrt(tf->percent);
    limit_h = (int)(self->height * ratio / 2.0);
    limit_w = (int)(self->width  * ratio / 2.0);

    for (i = 0; i < self->height; i++) {
        for (j = 0; j < self->width; j++) {
            mask_out = 0;

            if (self->domain == FILTER_DOMAIN_DFT) {
                di = (i > self->height / 2) ? (self->height - i) : i;
                dj = (j > self->width  / 2) ? (self->width  - j) : j;
                if (di > limit_h || dj > limit_w) mask_out = 1;
            } else {
                if (i > limit_h * 2 || j > limit_w * 2) mask_out = 1;
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

void lpf_logic(Filter *self) {
    double cutoff;
    int    i, j;

    cutoff = ((LpFilter *)self)->cutoff;

    for (i = 0; i < self->height; i++) {
        for (j = 0; j < self->width; j++) {
            if (get_freq_dist(i, j, self->height, self->width,
                              self->domain) > cutoff) {
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

void hpf_logic(Filter *self) {
    double cutoff;
    int    i, j;

    cutoff = ((HpFilter *)self)->cutoff;

    for (i = 0; i < self->height; i++) {
        for (j = 0; j < self->width; j++) {
            if (get_freq_dist(i, j, self->height, self->width,
                              self->domain) <= cutoff) {
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

void bpf_logic(Filter *self) {
    double low, high, d;
    int    i, j;

    low  = ((BpFilter *)self)->low;
    high = ((BpFilter *)self)->high;

    for (i = 0; i < self->height; i++) {
        for (j = 0; j < self->width; j++) {
            d = get_freq_dist(i, j, self->height, self->width, self->domain);
            if (d < low || d > high) {
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

void brf_logic(Filter *self) {
    double low, high, d;
    int    i, j;

    low  = ((BrFilter *)self)->low;
    high = ((BrFilter *)self)->high;

    for (i = 0; i < self->height; i++) {
        for (j = 0; j < self->width; j++) {
            d = get_freq_dist(i, j, self->height, self->width, self->domain);
            if (d >= low && d <= high) {
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

/* ---------------------------------------------------------
 * Apodisation helpers  (static)
 *
 * A cosine taper blends border pixels toward the image mean,
 * eliminating the hard wrap-around discontinuity that the DFT
 * assumes at every edge.  Without this, a large motion-blur kernel
 * produces prominent ringing bands 1–2 kernel-widths inside the
 * image border after deconvolution.
 *
 * The taper weight at position p in a dimension of size n with
 * margin m is:
 *
 *   w(p) = 0.5 * (1 - cos(pi * p / m))     if p < m
 *   w(p) = 0.5 * (1 - cos(pi * (n-1-p)/m)) if p >= n-m
 *   w(p) = 1.0                               otherwise
 *
 * The 2-D weight is the product of the row and column weights.
 * Each sample is replaced by:
 *
 *   x'(i,j) = w(i,j) * x(i,j) + (1 - w(i,j)) * mean
 *
 * --------------------------------------------------------- */

static double taper_weight(int pos, int size, int margin) {
    if (margin <= 0) return 1.0;
    if (pos < margin)
        return 0.5 * (1.0 - cos(M_PI * pos / (double)margin));
    if (pos >= size - margin)
        return 0.5 * (1.0 - cos(M_PI * (size - 1 - pos) / (double)margin));
    return 1.0;
}

/* Apodise a complex (DFT) input buffer in-place.
   Only the real channel [0] is relevant; imaginary [1] stays 0. */
static void apodise_complex(fftw_complex *buf, int w, int h, int margin) {
    double mean, wt, wx, wy;
    int    i, j;

    /* Compute mean of real channel */
    mean = 0.0;
    for (i = 0; i < h * w; i++) mean += buf[i][0];
    mean /= (double)(w * h);

    for (i = 0; i < h; i++) {
        wy = taper_weight(i, h, margin);
        for (j = 0; j < w; j++) {
            wx = taper_weight(j, w, margin);
            wt = wx * wy;
            buf[i * w + j][0] = wt * buf[i * w + j][0] + (1.0 - wt) * mean;
        }
    }
}

/* Apodise a real (DCT) input buffer in-place. */
static void apodise_real(double *buf, int w, int h, int margin) {
    double mean, wt, wx, wy;
    int    i, j;

    mean = 0.0;
    for (i = 0; i < h * w; i++) mean += buf[i];
    mean /= (double)(w * h);

    for (i = 0; i < h; i++) {
        wy = taper_weight(i, h, margin);
        for (j = 0; j < w; j++) {
            wx = taper_weight(j, w, margin);
            wt = wx * wy;
            buf[i * w + j] = wt * buf[i * w + j] + (1.0 - wt) * mean;
        }
    }
}

/* ---------------------------------------------------------
 * Filter execution
 * --------------------------------------------------------- */

void filter_execute(Filter *f) {
    int    w, h, i, j;
    double norm;

    w = f->width;
    h = f->height;

    if (f->domain == FILTER_DOMAIN_DFT) {
        /* 1. Load spatial data into complex input buffer */
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                f->dft_in[i * w + j][0] = f->img_in[i][j];
                f->dft_in[i * w + j][1] = 0.0;
            }
        }

        /* 1b. Apodise border region to suppress periodic-extension ringing */
        if (f->apod_margin > 0)
            apodise_complex(f->dft_in, w, h, f->apod_margin);

        /* 2. Forward transform  (spatial -> frequency) */
        fftw_execute(f->dft_forward);

        /* 3. Apply frequency-domain mask */
        if (f->apply_mask) f->apply_mask(f);

        /* 4. Inverse transform (frequency -> spatial) */
        fftw_execute(f->dft_backward);

        /* 5. Normalise and extract */
        norm = 1.0 / (w * h);
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                f->img_out[i][j] = f->dft_in[i * w + j][0] * norm;

    } else {
        /* 1. Load spatial data into real input buffer */
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                f->dct_in[i * w + j] = f->img_in[i][j];

        /* 1b. Apodise border region */
        if (f->apod_margin > 0)
            apodise_real(f->dct_in, w, h, f->apod_margin);

        /* 2. Forward transform (spatial -> frequency) */
        fftw_execute(f->dct_forward);

        /* 3. Apply frequency-domain mask */
        if (f->apply_mask) f->apply_mask(f);

        /* 4. Inverse transform (frequency -> spatial) */
        fftw_execute(f->dct_backward);

        /* 5. Normalise and extract  (DCT-II/IDCT-II scaling: 1/(4N)) */
        norm = 1.0 / (4.0 * w * h);
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                f->img_out[i][j] = f->dct_in[i * w + j] * norm;
    }
}

/* ---------------------------------------------------------
 * Filter application
 * --------------------------------------------------------- */

BMPImage *apply_frequency_filter_to_bmp(BMPImage *input, Filter *f) {
    int       w, h, i, j;
    BMPImage *output;
    HSIImage *hsi;

    w = input->info_header.width_px;
    h = abs(input->info_header.height_px);

    if (w != f->width || h != f->height) return NULL;

    output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);
    if (!output) return NULL;

    hsi = NULL;

    /* Load image data into filter input buffer */
    if (input->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(input);
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                f->img_in[i][j] = hsi->pixels[i][j].i;
    } else {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                f->img_in[i][j] = input->pixels[i][j] / 255.0;
    }

    /* Forward transform -> mask -> inverse transform */
    filter_execute(f);

    /* Extract results back into the output image */
    if (hsi) {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                hsi->pixels[i][j].i =
                    (float)clampd(f->img_out[i][j], 0.0, 1.0);
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                output->pixels[i][j] =
                    (uint8_t)clampd(f->img_out[i][j] * 255.0, 0.0, 255.0);
    }

    return output;
}


/* =========================================================
 * SECTION 2b — WIENER FILTER IMPROVEMENTS
 * =========================================================
 * Order:
 *   1. double          — estimate_noise_sigma   (standalone estimator)
 *   2. void            — wiener_build_K_spectrum (adaptive NSR array)
 *   3. Filter*         — create_wiener_filter_auto (full auto constructor)
 * ========================================================= */

/* ---------------------------------------------------------
 * double — estimate_noise_sigma
 *
 * Performs a single-level 1-D Haar forward pass on every row
 * then every column to get the finest HH subband, then applies
 * the robust median noise estimator.  Allocates and frees its
 * own temporary buffer — no WaveletTransform required.
 * --------------------------------------------------------- */

double estimate_noise_sigma(BMPImage *img) {
    int      w, h, N, i, j, k, hw, hh, n_hh;
    double **buf, *row_tmp, *col_tmp, *abs_hh;
    double   median, sigma;
    HSIImage *hsi;

    w = img->info_header.width_px;
    h = abs(img->info_header.height_px);

    buf = alloc_2d(h, w);
    if (!buf) return 0.0;

    /* Load normalised intensity */
    if (img->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(img);
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                buf[i][j] = hsi->pixels[i][j].i;
        freeHSIImage(hsi);
    } else {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                buf[i][j] = img->pixels[i][j] / 255.0;
    }

    row_tmp = (double *)malloc(w * sizeof(double));
    col_tmp = (double *)malloc(h * sizeof(double));

    /* --- Row-wise Haar forward pass --- */
    for (i = 0; i < h; i++) {
        memcpy(row_tmp, buf[i], w * sizeof(double));
        haar_forward_1d(row_tmp, w);
        memcpy(buf[i], row_tmp, w * sizeof(double));
    }

    /* --- Column-wise Haar forward pass --- */
    for (j = 0; j < w; j++) {
        for (i = 0; i < h; i++) col_tmp[i] = buf[i][j];
        haar_forward_1d(col_tmp, h);
        for (i = 0; i < h; i++) buf[i][j]  = col_tmp[i];
    }

    free(row_tmp);
    free(col_tmp);

    /*
     * Extract |c| from the HH1 subband — bottom-right quadrant:
     *   rows [h/2, h),  cols [w/2, w)
     */
    hw   = w / 2;
    hh   = h / 2;
    n_hh = hw * hh;
    N    = n_hh;   /* used for naming clarity below */

    abs_hh = (double *)malloc(N * sizeof(double));
    k = 0;
    for (i = hh; i < h; i++)
        for (j = hw; j < w; j++)
            abs_hh[k++] = fabs(buf[i][j]);

    free_2d(buf);

    /* qsort replaces the original O(n²) insertion sort */
    qsort(abs_hh, N, sizeof(double), cmp_double);

    median = (N % 2 == 0)
           ? (abs_hh[N / 2 - 1] + abs_hh[N / 2]) * 0.5
           : abs_hh[N / 2];

    sigma = median / 0.6745;

    free(abs_hh);
    return sigma;
}

/* ---------------------------------------------------------
 * void — wiener_build_K_spectrum
 *
 * Computes the Wiener-Hunt per-frequency NSR:
 *
 *   noise_power  = sigma_n²
 *   sig_est(u,v) = max( |G(u,v)|²/N - noise_power,
 *                       FLOOR * noise_power )
 *   K(u,v)       = noise_power / sig_est(u,v)
 *
 * where G is the DFT of the normalised noisy image and N = w*h.
 *
 * The floor factor (1e-4) prevents K from blowing up at very low
 * signal frequencies, and also stops K from falling below ~1e-4
 * at high-signal frequencies so the filter always has some
 * regularisation against noise amplification.
 * --------------------------------------------------------- */

void wiener_build_K_spectrum(WienerFilter *wf, BMPImage *noisy,
                              double sigma_n) {
    Filter       *f;
    fftw_complex *gbuf;
    fftw_plan     p;
    double        noise_power, sig_est, norm;
    int           w, h, N, i, j;
    HSIImage     *hsi;

    f           = (Filter *)wf;
    w           = f->width;
    h           = f->height;
    N           = w * h;
    noise_power = sigma_n * sigma_n;

    /* Free any existing spectrum */
    if (wf->K_spectrum) { free(wf->K_spectrum); wf->K_spectrum = NULL; }

    wf->K_spectrum = (double *)malloc(N * sizeof(double));
    if (!wf->K_spectrum) return;

    /* Compute DFT of the normalised noisy image */
    gbuf = fftw_alloc_complex(N);
    hsi  = NULL;

    if (noisy->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(noisy);
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                gbuf[i * w + j][0] = hsi->pixels[i][j].i;
                gbuf[i * w + j][1] = 0.0;
            }
        }
        freeHSIImage(hsi);
    } else {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                gbuf[i * w + j][0] = noisy->pixels[i][j] / 255.0;
                gbuf[i * w + j][1] = 0.0;
            }
        }
    }

    p = fftw_plan_dft_2d(h, w, gbuf, gbuf, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(p);
    fftw_destroy_plan(p);

    /*
     * norm = 1/N converts the raw DFT magnitude² into average power per
     * sample, making it comparable to sigma_n² which is also per-sample.
     */
    norm = 1.0 / (double)N;

    for (i = 0; i < N; i++) {
        double gr = gbuf[i][0], gi = gbuf[i][1];
        double psd = (gr * gr + gi * gi) * norm;

        /* Subtract noise floor to get signal-only power estimate */
        sig_est = psd - noise_power;

        /*
         * Floor at 1e-4 * noise_power:
         *  - Prevents division by zero at dead frequencies.
         *  - Keeps K <= 1e4 so the filter never fully cancels a frequency.
         */
        if (sig_est < 1e-4 * noise_power)
            sig_est = 1e-4 * noise_power;

        wf->K_spectrum[i] = noise_power / sig_est;
    }

    fftw_free(gbuf);
}

/* ---------------------------------------------------------
 * Filter* — create_wiener_filter_auto
 * --------------------------------------------------------- */

Filter *create_wiener_filter_auto(FilterDomain domain, int w, int h,
                                   Kernel *blur_kernel, BMPImage *noisy) {
    Filter       *f;
    WienerFilter *wf;
    double        sigma_n;
    int           max_kdim;

    f = create_wiener_filter(domain, w, h, blur_kernel, 0.0);
    if (!f) return NULL;

    wf = (WienerFilter *)f;

    /* Step 1: estimate noise sigma from wavelet HH1 subband */
    sigma_n = estimate_noise_sigma(noisy);
    printf("Wiener auto: estimated noise sigma = %.5f\n", sigma_n);

    /* Step 2: build per-frequency K from Wiener-Hunt formula */
    wiener_build_K_spectrum(wf, noisy, sigma_n);

    /*
     * Step 3: set apodisation margin to half the largest kernel
     * dimension.  This ensures the taper covers at least one full
     * kernel footprint at each border, which is where wrap-around
     * ringing originates.
     */
    max_kdim       = (blur_kernel->width > blur_kernel->height)
                   ? blur_kernel->width : blur_kernel->height;
    f->apod_margin = max_kdim / 2 + 1;

    return f;
}


/* =========================================================
 * SECTION 3 — WAVELET TRANSFORM
 * =========================================================
 * Order:
 *   1. Internal helpers         (static)
 *   2. WaveletTransform*        construction
 *   3. void                     destruction
 *   4. void                     1-D lifting primitives   (static)
 *   5. void                     2-D level pass helpers   (static)
 *   6. void                     decompose / reconstruct  (public)
 *   7. void                     threshold / estimate     (public)
 *   8. BMPImage*                application bridges      (public)
 * ========================================================= */

/* ---------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------- */

/*
 * alloc_2d / free_2d
 *
 * Allocate / free a contiguous row-pointer array so that
 * buf[i][j] works while the underlying memory is one block.
 * This keeps cache behaviour predictable and simplifies free.
 */
static double **alloc_2d(int rows, int cols) {
    double **arr;
    double  *block;
    int      i;

    arr   = (double **)malloc(rows * sizeof(double *));
    if (!arr) return NULL;
    block = (double *)calloc(rows * cols, sizeof(double));
    if (!block) { free(arr); return NULL; }
    for (i = 0; i < rows; i++)
        arr[i] = block + i * cols;
    return arr;
}

static void free_2d(double **arr) {
    if (!arr) return;
    free(arr[0]);   /* free the single contiguous block */
    free(arr);
}

/*
 * copy_2d — copy src[rows][cols] into dst[rows][cols]
 */
static void copy_2d(double **dst, double **src, int rows, int cols) {
    int i;
    for (i = 0; i < rows; i++)
        memcpy(dst[i], src[i], cols * sizeof(double));
}

/* ---------------------------------------------------------
 * WaveletTransform* — construction
 * --------------------------------------------------------- */

WaveletTransform *create_wavelet(int w, int h, int levels) {
    WaveletTransform *wt;

    wt = (WaveletTransform *)malloc(sizeof(WaveletTransform));
    if (!wt) return NULL;

    wt->width  = w;
    wt->height = h;
    wt->levels = levels;

    wt->img_in  = alloc_2d(h, w);
    wt->img_out = alloc_2d(h, w);

    if (!wt->img_in || !wt->img_out) {
        free_2d(wt->img_in);
        free_2d(wt->img_out);
        free(wt);
        return NULL;
    }
    return wt;
}

/* ---------------------------------------------------------
 * void — destruction
 * --------------------------------------------------------- */

void destroy_wavelet(WaveletTransform *wt) {
    if (!wt) return;
    free_2d(wt->img_in);
    free_2d(wt->img_out);
    free(wt);
}

/* ---------------------------------------------------------
 * void — 1-D Haar lifting primitives  (static)
 *
 * Both operate on a strided view of a row or column via a
 * temporary scratch buffer, so they work for both orientations
 * without separate row/column code paths.
 *
 * Forward lifting (predict-then-update, unnormalised):
 *   d[i] = odd[i]  - even[i]                  (predict)
 *   a[i] = even[i] + floor(d[i] / 2)          (update)
 *
 * Using the normalised (energy-preserving) variant instead:
 *   a[i] = (x[2i] + x[2i+1]) * SQRT2_INV
 *   d[i] = (x[2i] - x[2i+1]) * SQRT2_INV
 *
 * The normalised form keeps coefficient magnitudes comparable
 * across levels and is what most wavelet denoising literature
 * assumes when setting a single global threshold.
 * --------------------------------------------------------- */

#define HAAR_SCALE  0.7071067811865476   /* 1 / sqrt(2) */

/*
 * haar_forward_1d
 *
 * Applies one level of the forward Haar transform to the first
 * `n` elements of array `x` (n must be even).
 * Result: x[0..n/2-1] = averages, x[n/2..n-1] = differences.
 */
static void haar_forward_1d(double *x, int n) {
    double *tmp;
    int     h, i;

    h   = n / 2;
    tmp = (double *)malloc(n * sizeof(double));
    if (!tmp) return;

    for (i = 0; i < h; i++) {
        tmp[i]     = (x[2 * i]     + x[2 * i + 1]) * HAAR_SCALE;
        tmp[h + i] = (x[2 * i]     - x[2 * i + 1]) * HAAR_SCALE;
    }
    memcpy(x, tmp, n * sizeof(double));
    free(tmp);
}

/*
 * haar_inverse_1d
 *
 * Inverse of haar_forward_1d.
 * Input:  x[0..n/2-1] = averages, x[n/2..n-1] = differences.
 * Output: x[0..n-1]   = reconstructed signal.
 */
static void haar_inverse_1d(double *x, int n) {
    double *tmp;
    int     h, i;

    h   = n / 2;
    tmp = (double *)malloc(n * sizeof(double));
    if (!tmp) return;

    for (i = 0; i < h; i++) {
        tmp[2 * i]     = (x[i] + x[h + i]) * HAAR_SCALE;
        tmp[2 * i + 1] = (x[i] - x[h + i]) * HAAR_SCALE;
    }
    memcpy(x, tmp, n * sizeof(double));
    free(tmp);
}

/* ---------------------------------------------------------
 * void — 2-D level pass helpers  (static)
 *
 * Each helper operates on the top-left (bw x bh) sub-block of
 * the buffer, which is the LL subband at the current level.
 * --------------------------------------------------------- */

/*
 * haar_forward_2d_level
 *
 * One forward 2-D Haar pass on a (bh x bw) sub-block of `buf`.
 * After the pass the sub-block contains:
 *   [ LL | HL ]
 *   [ LH | HH ]
 * where each quadrant is (bh/2 x bw/2).
 */
static void haar_forward_2d_level(double **buf, int bw, int bh) {
    double *row_buf;
    double *col_buf;
    int     i, j;

    /* Temporary scratch buffers — maximum of bw and bh */
    row_buf = (double *)malloc(bw * sizeof(double));
    col_buf = (double *)malloc(bh * sizeof(double));
    if (!row_buf || !col_buf) { free(row_buf); free(col_buf); return; }

    /* --- Pass 1: transform each row of the sub-block --- */
    for (i = 0; i < bh; i++) {
        memcpy(row_buf, buf[i], bw * sizeof(double));
        haar_forward_1d(row_buf, bw);
        memcpy(buf[i], row_buf, bw * sizeof(double));
    }

    /* --- Pass 2: transform each column of the sub-block --- */
    for (j = 0; j < bw; j++) {
        for (i = 0; i < bh; i++) col_buf[i] = buf[i][j];
        haar_forward_1d(col_buf, bh);
        for (i = 0; i < bh; i++) buf[i][j]  = col_buf[i];
    }

    free(row_buf);
    free(col_buf);
}

/*
 * haar_inverse_2d_level
 *
 * One inverse 2-D Haar pass on a (bh x bw) sub-block of `buf`.
 * This exactly undoes haar_forward_2d_level.
 */
static void haar_inverse_2d_level(double **buf, int bw, int bh) {
    double *row_buf;
    double *col_buf;
    int     i, j;

    row_buf = (double *)malloc(bw * sizeof(double));
    col_buf = (double *)malloc(bh * sizeof(double));
    if (!row_buf || !col_buf) { free(row_buf); free(col_buf); return; }

    /* --- Pass 1: inverse-transform each column first --- */
    for (j = 0; j < bw; j++) {
        for (i = 0; i < bh; i++) col_buf[i] = buf[i][j];
        haar_inverse_1d(col_buf, bh);
        for (i = 0; i < bh; i++) buf[i][j]  = col_buf[i];
    }

    /* --- Pass 2: inverse-transform each row --- */
    for (i = 0; i < bh; i++) {
        memcpy(row_buf, buf[i], bw * sizeof(double));
        haar_inverse_1d(row_buf, bw);
        memcpy(buf[i], row_buf, bw * sizeof(double));
    }

    free(row_buf);
    free(col_buf);
}

/* ---------------------------------------------------------
 * void — wavelet_decompose / wavelet_reconstruct  (public)
 * --------------------------------------------------------- */

void wavelet_decompose(WaveletTransform *wt) {
    int bw, bh, lv;

    /* Start by copying img_in into img_out; all transform
       passes happen in-place on img_out from this point. */
    copy_2d(wt->img_out, wt->img_in, wt->height, wt->width);

    bw = wt->width;
    bh = wt->height;

    for (lv = 0; lv < wt->levels; lv++) {
        haar_forward_2d_level(wt->img_out, bw, bh);
        /* Next level operates only on the LL quadrant */
        bw /= 2;
        bh /= 2;
    }
}

void wavelet_reconstruct(WaveletTransform *wt) {
    int  bw_stack[32];   /* max 32 levels is far beyond any practical use */
    int  bh_stack[32];
    int  lv, bw, bh;

    /* Record the sub-block sizes used during decomposition
       so we can unwind them in reverse order. */
    bw = wt->width;
    bh = wt->height;
    for (lv = 0; lv < wt->levels; lv++) {
        bw_stack[lv] = bw;
        bh_stack[lv] = bh;
        bw /= 2;
        bh /= 2;
    }

    /* Reconstruction goes from coarsest level to finest */
    for (lv = wt->levels - 1; lv >= 0; lv--) {
        haar_inverse_2d_level(wt->img_out, bw_stack[lv], bh_stack[lv]);
    }

    /* Copy the reconstructed data back into img_in */
    copy_2d(wt->img_in, wt->img_out, wt->height, wt->width);
}

/* ---------------------------------------------------------
 * void — wavelet_threshold / wavelet_estimate_thresh (public)
 * --------------------------------------------------------- */

void wavelet_threshold(WaveletTransform *wt, double thresh,
                       WaveletThreshMode mode) {
    int    lv, bw, bh, i, j;
    double v, sign_v;

    /* Walk the same sub-block schedule as decompose, but touch
       only the three detail quadrants (HL, LH, HH) at each level.
       The LL (approximation) sub-block is never modified. */

    bw = wt->width;
    bh = wt->height;

    for (lv = 0; lv < wt->levels; lv++) {
        int hw = bw / 2;
        int hh = bh / 2;

        /* HL: rows [0, hh), cols [hw, bw) */
        for (i = 0; i < hh; i++) {
            for (j = hw; j < bw; j++) {
                v = wt->img_out[i][j];
                if (mode == WAVELET_THRESH_HARD) {
                    wt->img_out[i][j] = (v >= -thresh && v <= thresh) ? 0.0 : v;
                } else {
                    sign_v = (v > 0.0) ? 1.0 : (v < 0.0) ? -1.0 : 0.0;
                    wt->img_out[i][j] = sign_v
                                      * clampd(fabs(v) - thresh, 0.0, fabs(v));
                }
            }
        }

        /* LH: rows [hh, bh), cols [0, hw) */
        for (i = hh; i < bh; i++) {
            for (j = 0; j < hw; j++) {
                v = wt->img_out[i][j];
                if (mode == WAVELET_THRESH_HARD) {
                    wt->img_out[i][j] = (v >= -thresh && v <= thresh) ? 0.0 : v;
                } else {
                    sign_v = (v > 0.0) ? 1.0 : (v < 0.0) ? -1.0 : 0.0;
                    wt->img_out[i][j] = sign_v
                                      * clampd(fabs(v) - thresh, 0.0, fabs(v));
                }
            }
        }

        /* HH: rows [hh, bh), cols [hw, bw) */
        for (i = hh; i < bh; i++) {
            for (j = hw; j < bw; j++) {
                v = wt->img_out[i][j];
                if (mode == WAVELET_THRESH_HARD) {
                    wt->img_out[i][j] = (v >= -thresh && v <= thresh) ? 0.0 : v;
                } else {
                    sign_v = (v > 0.0) ? 1.0 : (v < 0.0) ? -1.0 : 0.0;
                    wt->img_out[i][j] = sign_v
                                      * clampd(fabs(v) - thresh, 0.0, fabs(v));
                }
            }
        }

        /* Descend into the LL sub-block for the next level */
        bw = hw;
        bh = hh;
    }
}

double wavelet_estimate_thresh(const WaveletTransform *wt) {
    /*
     * VisuShrink universal threshold estimator.
     *
     * We use the finest-level HH subband (bottom-right quadrant of
     * the first decomposition) to estimate the noise standard
     * deviation via the robust median estimator:
     *
     *   sigma = median(|c|) / 0.6745
     *
     * then apply the universal rule:
     *
     *   thresh = sigma * sqrt(2 * log(N))
     *
     * where N = total number of pixels.
     */
    int     hw, hh, i, j, k, n;
    double *abs_vals;
    double  median, sigma, thresh;

    hw = wt->width  / 2;
    hh = wt->height / 2;
    n  = hw * hh;

    abs_vals = (double *)malloc(n * sizeof(double));
    if (!abs_vals) return 0.0;

    /* Collect |c| from the HH1 subband:
       rows [hh, 2*hh), cols [hw, 2*hw) */
    k = 0;
    for (i = hh; i < 2 * hh; i++)
        for (j = hw; j < 2 * hw; j++)
            abs_vals[k++] = fabs(wt->img_out[i][j]);

    /* qsort replaces the original O(n²) insertion sort */
    qsort(abs_vals, n, sizeof(double), cmp_double);

    median = (n % 2 == 0)
           ? (abs_vals[n / 2 - 1] + abs_vals[n / 2]) * 0.5
           : abs_vals[n / 2];

    sigma  = median / 0.6745;
    thresh = sigma * sqrt(2.0 * log((double)(wt->width * wt->height)));

    free(abs_vals);
    return thresh;
}

/* ---------------------------------------------------------
 * Internal: load a BMPImage into the wt->img_in plane and
 * extract back from wt->img_in into a new BMPImage.
 * Split out so both bridge functions share the same I/O code.
 * --------------------------------------------------------- */

static HSIImage *load_image_into_wavelet(BMPImage *input,
                                         WaveletTransform *wt) {
    int      w, h, i, j;
    HSIImage *hsi;

    w   = wt->width;
    h   = wt->height;
    hsi = NULL;

    if (input->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(input);
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                wt->img_in[i][j] = (double)hsi->pixels[i][j].i;
    } else {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                wt->img_in[i][j] = input->pixels[i][j] / 255.0;
    }
    return hsi;   /* caller owns hsi; NULL for 8-bit images */
}

static BMPImage *extract_image_from_wavelet(BMPImage   *input,
                                             WaveletTransform *wt,
                                             HSIImage   *hsi) {
    int       w, h, i, j;
    BMPImage *output;

    w      = wt->width;
    h      = wt->height;
    output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);
    if (!output) return NULL;

    if (hsi) {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                hsi->pixels[i][j].i =
                    (float)clampd(wt->img_in[i][j], 0.0, 1.0);
        update_bmp_from_hsi(output, hsi);
    } else {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                output->pixels[i][j] =
                    (uint8_t)(clampd(wt->img_in[i][j], 0.0, 1.0) * 255.0);
    }
    return output;
}

/* ---------------------------------------------------------
 * BMPImage* — apply_wavelet_denoise  (public)
 * --------------------------------------------------------- */

BMPImage *apply_wavelet_denoise(BMPImage *input, int levels,
                                double thresh, WaveletThreshMode mode) {
    WaveletTransform *wt;
    HSIImage         *hsi;
    BMPImage         *output;
    int               w, h;

    if (!input) return NULL;

    w = input->info_header.width_px;
    h = abs(input->info_header.height_px);

    wt = create_wavelet(w, h, levels);
    if (!wt) return NULL;

    hsi = load_image_into_wavelet(input, wt);

    wavelet_decompose(wt);

    /* Automatic threshold estimation when caller passes thresh <= 0 */
    if (thresh <= 0.0)
        thresh = wavelet_estimate_thresh(wt);

    wavelet_threshold(wt, thresh, mode);

    wavelet_reconstruct(wt);

    output = extract_image_from_wavelet(input, wt, hsi);

    if (hsi) freeHSIImage(hsi);
    destroy_wavelet(wt);

    return output;
}

/* ---------------------------------------------------------
 * BMPImage* — apply_wavelet_sharpen  (public)
 * --------------------------------------------------------- */

BMPImage *apply_wavelet_sharpen(BMPImage *input, int levels,
                                double gain, int sharpen_levels) {
    WaveletTransform *wt;
    HSIImage         *hsi;
    BMPImage         *output;
    int               w, h;
    int               lv, bw, bh, hw, hh, i, j;
    int               first_detail_level;

    if (!input) return NULL;

    w = input->info_header.width_px;
    h = abs(input->info_header.height_px);

    wt = create_wavelet(w, h, levels);
    if (!wt) return NULL;

    hsi = load_image_into_wavelet(input, wt);

    wavelet_decompose(wt);

    /*
     * Walk the subband schedule and boost detail coefficients
     * at the `sharpen_levels` finest levels.
     *
     * Level 0 is the finest (largest subbands); level (levels-1)
     * is the coarsest.  We boost from level 0 up to
     * min(sharpen_levels, levels) - 1.
     */
    bw = wt->width;
    bh = wt->height;
    first_detail_level = levels - sharpen_levels;
    if (first_detail_level < 0) first_detail_level = 0;

    for (lv = 0; lv < levels; lv++) {
        hw = bw / 2;
        hh = bh / 2;

        if (lv >= first_detail_level) {
            double scale = 1.0 + gain;

            /* HL subband */
            for (i = 0; i < hh; i++)
                for (j = hw; j < bw; j++)
                    wt->img_out[i][j] *= scale;

            /* LH subband */
            for (i = hh; i < bh; i++)
                for (j = 0; j < hw; j++)
                    wt->img_out[i][j] *= scale;

            /* HH subband */
            for (i = hh; i < bh; i++)
                for (j = hw; j < bw; j++)
                    wt->img_out[i][j] *= scale;
        }

        bw = hw;
        bh = hh;
    }

    wavelet_reconstruct(wt);

    output = extract_image_from_wavelet(input, wt, hsi);

    if (hsi) freeHSIImage(hsi);
    destroy_wavelet(wt);

    return output;
}
