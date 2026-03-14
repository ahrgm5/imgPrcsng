#ifndef TRANSFORMS_H
#define TRANSFORMS_H

#include "bmp.h"
#include <fftw3.h>
#include <stddef.h>

/* =========================================================
 * SECTION 1 — SPATIAL KERNELS
 * =========================================================
 * Structs and functions are ordered by return type:
 *   Kernel construction  ->  Kernel*
 *   Kernel destruction   ->  void
 *   Kernel application   ->  BMPImage*
 * ========================================================= */

/* --- Kernel base type ------------------------------------ */

typedef struct Kernel {
    int    width;
    int    height;
    float **mask;
    void (*initialize)(struct Kernel *self);
} Kernel;

/* --- Concrete kernel types (sub-types of Kernel) --------- */

typedef struct {
    Kernel base;
    int    L;       /* Blur length in pixels */
    double theta;   /* Blur angle in degrees */
} MotionBlurKernel;

typedef struct {
    Kernel base;
} AverageKernel;

typedef struct {
    Kernel base;
    float  k;      /* Center scaling factor */
} LaplacianKernel;

typedef struct {
    Kernel base;
    double sigma;  /* Gaussian standard deviation */
} GaussianKernel;

typedef struct {
    Kernel base;
    int    horizontal; /* Non-zero => horizontal Sobel, else vertical */
} SobelKernel;

/* --- Kernel initialisation callbacks --------------------- */

void motion_blur_init(Kernel *self);
void average_init(Kernel *self);
void laplacian_init(Kernel *self);
void gaussian_init(Kernel *self);
void sobel_init(Kernel *self);

/* --- Kernel construction / destruction ------------------- */

Kernel *create_kernel(int w, int h, void (*init_func)(Kernel *), size_t struct_size);
void    destroy_kernel(Kernel *k);

/* --- Kernel application ---------------------------------- */

BMPImage *apply_kernel_to_bmp(BMPImage *input, Kernel *k);


/* =========================================================
 * SECTION 2 — FREQUENCY-DOMAIN FILTERS
 * =========================================================
 * Structs and functions are ordered by return type:
 *   Filter construction  ->  Filter*
 *   Filter destruction   ->  void
 *   Filter execution     ->  void  (modifies Filter in-place)
 *   Filter application   ->  BMPImage*
 * ========================================================= */

/* --- Transform domain selector --------------------------- */

typedef enum {
    FILTER_DOMAIN_DFT,
    FILTER_DOMAIN_DCT
} FilterDomain;

/* --- Filter base type ------------------------------------ */

typedef struct Filter {
    FilterDomain  domain;
    int           width;
    int           height;

    /*
     * apod_margin — cosine-taper border width in pixels (0 = disabled).
     *
     * The DFT assumes the image tiles periodically.  Real images do not,
     * so the hard discontinuity at every border injects broadband ringing
     * into the spectrum.  Setting apod_margin > 0 fades the border region
     * toward the image mean before transforming, which suppresses this
     * artifact.  A value of half the blur-kernel radius is typical; the
     * auto-constructor (create_wiener_filter_auto) sets it automatically.
     */
    int           apod_margin;

    double      **img_in;
    double      **img_out;

    /* DFT buffers and plans (used when domain == FILTER_DOMAIN_DFT) */
    fftw_complex *dft_in;
    fftw_complex *dft_out;
    fftw_plan     dft_forward;
    fftw_plan     dft_backward;

    /*
     * preloaded_G — pre-computed forward DFT of the input image.
     *
     * When non-NULL, filter_execute skips the load + apodise + forward FFT
     * steps and copies this buffer directly into dft_out instead.  This
     * allows the same forward transform to be shared across multiple filter
     * calls on the same image (e.g. the 25-candidate parameter search).
     *
     * Set via filter_preload_input_dft().  Not freed by destroy_filter()
     * since the buffer is owned by the caller and shared across filters.
     */
    fftw_complex *preloaded_G;

    /* DCT buffers and plans (used when domain == FILTER_DOMAIN_DCT) */
    double       *dct_in;
    double       *dct_out;
    fftw_plan     dct_forward;
    fftw_plan     dct_backward;

    void (*apply_mask)(struct Filter *self);
} Filter;

/* --- Concrete filter types (sub-types of Filter) --------- */

typedef struct { Filter base; double percent;           } TruncationFilter;
typedef struct { Filter base; double cutoff;            } LpFilter;
typedef struct { Filter base; double cutoff;            } HpFilter;
typedef struct { Filter base; double low;  double high; } BpFilter;
typedef struct { Filter base; double low;  double high; } BrFilter;
typedef struct { Filter base; double sigma;             } GaussianFilter;
typedef struct {
    Filter        base;
    fftw_complex *H;          /* OTF of the blur kernel in frequency domain    */
    double        K;          /* Flat NSR fallback (used when K_spectrum=NULL) */

    /*
     * K_spectrum — per-frequency noise-to-signal power ratio.
     *
     * When non-NULL, wiener_filter_logic uses K_spectrum[u*w+v] instead
     * of the scalar K.  This is the Wiener-Hunt adaptive form:
     *
     *   K(u,v) = sigma_n² / max( |G(u,v)|²/N - sigma_n², eps )
     *
     * where G is the DFT of the degraded image and sigma_n² is the noise
     * variance estimated from the finest wavelet HH subband.
     *
     * Built by wiener_build_K_spectrum(); freed by destroy_wiener_filter().
     * NULL means fall back to the scalar K field above.
     */
    double       *K_spectrum;
} WienerFilter;

/* --- Mask (filter logic) callbacks ----------------------- */

void wiener_filter_logic(Filter *self);
void rect_truncation_logic(Filter *self);
void lpf_logic(Filter *self);
void hpf_logic(Filter *self);
void bpf_logic(Filter *self);
void brf_logic(Filter *self);

/* --- Filter construction / destruction ------------------- */

Filter *create_filter(FilterDomain domain, int w, int h,
                      void (*mask_func)(Filter *), size_t struct_size);

Filter *create_wiener_filter(FilterDomain domain, int w, int h,
                             Kernel *blur_kernel, double K);

/*
 * create_wiener_filter_auto
 *
 * Fully automatic Wiener deconvolution filter.  Does three things that
 * the manual constructor does not:
 *
 *   1. Estimates noise sigma from the finest-level Haar HH subband of
 *      `noisy` (wavelet median estimator, no separate call needed).
 *
 *   2. Builds a per-frequency K_spectrum using the Wiener-Hunt formula
 *      so that regularisation is strong at blur nulls and weak where
 *      the signal dominates.
 *
 *   3. Sets apod_margin to half the largest kernel dimension so that
 *      border ringing is suppressed automatically.
 *
 * Use this instead of create_wiener_filter for best results.
 */
Filter *create_wiener_filter_auto(FilterDomain domain, int w, int h,
                                  Kernel *blur_kernel, BMPImage *noisy);

void destroy_filter(Filter *f);
void destroy_wiener_filter(Filter *f);

/* --- Noise estimation (shared between Wiener and wavelet) - */

/*
 * estimate_noise_sigma
 *
 * Estimates the standard deviation of additive white Gaussian noise
 * from the finest-level Haar HH subband of `img` using the robust
 * median estimator:
 *
 *   sigma = median( |c_HH1| ) / 0.6745
 *
 * Works on both 8-bit grayscale and 24-bit colour (intensity channel).
 * Does not require an existing WaveletTransform — allocates its own
 * temporary buffer.
 */
double estimate_noise_sigma(BMPImage *img);

/*
 * wiener_build_K_spectrum
 *
 * Computes the per-frequency NSR array for an already-constructed
 * WienerFilter and stores it in wf->K_spectrum.  Any previous
 * K_spectrum is freed first.
 *
 * `noisy`   — the degraded input image (used to estimate signal power).
 * `sigma_n` — noise standard deviation (e.g. from estimate_noise_sigma).
 *
 * After this call the scalar wf->K field is no longer used.
 */
void wiener_build_K_spectrum(WienerFilter *wf, BMPImage *noisy,
                             double sigma_n);

/* --- Filter execution ------------------------------------ */

void filter_execute(Filter *f);

/*
 * filter_preload_input_dft
 *
 * Computes the forward DFT of `img` (with apodisation) and stores the
 * result in `out_G` (caller-allocated, size w*h fftw_complex).
 * Assign out_G to f->preloaded_G before calling filter_execute to skip
 * the redundant forward transform on every subsequent call.
 *
 * Typical usage in a parameter search loop:
 *
 *   fftw_complex *G = fftw_alloc_complex(w * h);
 *   filter_preload_input_dft(any_filter, img_blur, G);
 *   // then for each candidate:
 *   f->preloaded_G = G;
 *   filter_execute(f);       // forward FFT skipped
 *   f->preloaded_G = NULL;   // don't let destroy_filter() misuse it
 */
void filter_preload_input_dft(Filter *f, BMPImage *img, fftw_complex *out_G);

/* --- Filter application ---------------------------------- */

BMPImage *apply_frequency_filter_to_bmp(BMPImage *input, Filter *f);


/* =========================================================
 * SECTION 3 — WAVELET TRANSFORM
 * =========================================================
 * Implements a 2-D multi-level Haar discrete wavelet transform
 * via the lifting scheme.  The decomposition is applied
 * row-wise then column-wise; the inverse exactly undoes it.
 *
 * After decomposition, img_out holds the full subband layout:
 *
 *   For a 2-level transform on a W x H image the layout is:
 *
 *   (0,0)           (W/2,0)
 *    +-------+---+--...--+
 *    |       |HL2|       |
 *    |  LL2  +---+  HL1  |
 *    |       |LH2|       |
 *    +-------+---+  HH2  |  <- level-2 detail in top-left quadrant
 *    |  LH1  |       HH1 |
 *    |       |           |
 *    +-------+-----------+
 *   (0,H/2)            (W,H)
 *
 * Structs and functions are ordered by return type:
 *   WaveletTransform* — construction
 *   void              — destruction
 *   void              — transform passes (decompose / reconstruct)
 *   void              — coefficient manipulation (threshold)
 *   BMPImage*         — application bridge
 * ========================================================= */

/* --- Wavelet type ---------------------------------------- */

typedef struct {
    int     levels;   /* Number of decomposition levels (>= 1)       */
    int     width;    /* Image width  (must be divisible by 2^levels) */
    int     height;   /* Image height (must be divisible by 2^levels) */
    double **img_in;  /* Input spatial data  [height][width]          */
    double **img_out; /* Output subband data [height][width]          */
} WaveletTransform;

/* --- Thresholding mode ----------------------------------- */

typedef enum {
    WAVELET_THRESH_HARD, /* Zero coefficients with |c| < thresh         */
    WAVELET_THRESH_SOFT  /* Shrink coefficients toward zero by thresh    */
} WaveletThreshMode;

/* --- Construction / destruction -------------------------- */

WaveletTransform *create_wavelet(int w, int h, int levels);
void              destroy_wavelet(WaveletTransform *wt);

/* --- Forward and inverse transforms ---------------------- */

/*
 * wavelet_decompose
 *   Reads wt->img_in, writes the multi-level subband layout
 *   into wt->img_out.  The LL subband at the coarsest level
 *   sits in the top-left corner; all detail bands (HL, LH, HH)
 *   are packed beside it at increasing scales.
 */
void wavelet_decompose(WaveletTransform *wt);

/*
 * wavelet_reconstruct
 *   Reads wt->img_out (which may have been modified, e.g. by
 *   wavelet_threshold), reconstructs the spatial image, and
 *   writes it back into wt->img_in.
 */
void wavelet_reconstruct(WaveletTransform *wt);

/* --- Coefficient manipulation ---------------------------- */

/*
 * wavelet_threshold
 *   Applies hard or soft thresholding to the detail subbands
 *   (HL, LH, HH) at every decomposition level.  The LL
 *   (approximation) subband is never modified.
 *
 *   Hard : c = (|c| >= thresh) ? c : 0
 *   Soft : c = sign(c) * max(|c| - thresh, 0)
 *
 *   Soft thresholding produces smoother results and is the
 *   standard choice for denoising.
 */
void wavelet_threshold(WaveletTransform *wt, double thresh,
                       WaveletThreshMode mode);

/*
 * wavelet_estimate_thresh
 *   Estimates a suitable threshold from the finest-level HH
 *   subband using the universal (VisuShrink) estimator:
 *
 *       sigma  = median(|HH1|) / 0.6745          (noise estimate)
 *       thresh = sigma * sqrt(2 * log(N))         (universal rule)
 *
 *   where N = width * height.  Returns the computed threshold
 *   so the caller can log or override it.
 */
double wavelet_estimate_thresh(const WaveletTransform *wt);

/* --- Application bridge ---------------------------------- */

/*
 * apply_wavelet_denoise
 *   Convenience function: allocates a WaveletTransform, loads
 *   the image, decomposes, thresholds, reconstructs, and returns
 *   a new BMPImage.  Handles both 8-bit grayscale and 24-bit
 *   colour (intensity channel only, via HSI).
 *
 *   Pass thresh <= 0 to use the automatic VisuShrink estimator.
 */
BMPImage *apply_wavelet_denoise(BMPImage *input, int levels,
                                double thresh, WaveletThreshMode mode);

/*
 * apply_wavelet_sharpen
 *   Decomposes the image, scales the detail subbands by
 *   (1 + gain) at the requested levels, then reconstructs.
 *   gain > 0 sharpens; gain < 0 (>-1) gently smooths.
 *
 *   sharpen_levels controls how many of the finest detail
 *   levels are boosted (1 = finest only, up to wt->levels).
 */
BMPImage *apply_wavelet_sharpen(BMPImage *input, int levels,
                                double gain, int sharpen_levels);

#endif /* TRANSFORMS_H */
