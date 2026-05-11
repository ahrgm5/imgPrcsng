#ifndef ABERRATION_H
#define ABERRATION_H

/*
 * aberration.h
 *
 * ISP pipeline for optical aberration analysis from interferograms.
 *
 * Pipeline overview
 * -----------------
 *   Stage 1 — Pre-processing
 *       readBMP / Wiener deconvolution (existing transforms.h)
 *       Wavelet denoise              (existing transforms.h)
 *
 *   Stage 2 — Fringe analysis
 *       Wigner-Ville Distribution (WVD) — instantaneous frequency map
 *       Phase extraction via WVD ridge detection
 *
 *   Stage 3 — Phase unwrapping
 *       Quality-guided flood-fill unwrapper
 *
 *   Stage 4 — Zernike / Seidel decomposition
 *       Zernike polynomial fit to unwrapped phase
 *       Zernike → Seidel coefficient conversion
 *
 * All structs follow the existing project conventions:
 *   - Heap-allocated with explicit create/free functions
 *   - Row-major 2-D arrays via double **  (same as WaveletTransform)
 *   - Separate .c implementation file
 *
 * Seidel coefficient ordering (classical 5 primary aberrations):
 *   [0] W040  Spherical aberration
 *   [1] W131  Coma
 *   [2] W222  Astigmatism
 *   [3] W220  Field curvature
 *   [4] W311  Distortion
 */

#include <bmp.h>
#include <transforms.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * CONSTANTS
 * ========================================================= */

#define SEIDEL_COUNT      5     /* Number of primary Seidel aberrations   */
#define ZERNIKE_MAX_ORDER 6     /* Max radial order for Zernike fit        */
                                /* Nterms = (order+1)(order+2)/2 = 28     */
#define ZERNIKE_TERMS    28     /* Pre-computed for order 6                */

/* =========================================================
 * WIGNER-VILLE DISTRIBUTION
 * ========================================================= */

/*
 * WVDResult
 *
 * Time-frequency energy distribution for a 1-D signal slice.
 * For a 2-D interferogram the WVD is computed along each row
 * (or column) to build an instantaneous-frequency map.
 *
 *  n_samples  — length of the original 1-D signal
 *  n_freqs    — number of frequency bins  (== n_samples, two-sided)
 *  data       — WVD matrix [n_freqs][n_samples], real-valued
 */
typedef struct {
    int     n_samples;
    int     n_freqs;
    double **data;          /* [n_freqs][n_samples] */
} WVDResult;

/* Allocate / free */
WVDResult *create_wvd(int n_samples);
void       free_wvd(WVDResult *wvd);

/*
 * compute_wvd_1d
 *
 * Computes the discrete pseudo-WVD (DPWVD) of a real signal `x`
 * of length `n`.  Uses a rectangular analysis window of half-length
 * `half_win` (set to n/4 if <= 0).  Requires FFTW internally.
 *
 * Returns a newly allocated WVDResult; caller must free_wvd().
 */
WVDResult *compute_wvd_1d(const double *x, int n, int half_win);

/*
 * wvd_instantaneous_frequency
 *
 * Extracts the instantaneous frequency (IF) at each sample from
 * a WVDResult by finding the frequency-axis ridge (peak energy).
 * Output `if_out` must be pre-allocated to `wvd->n_samples` doubles.
 * Frequencies are returned normalised to [0, 0.5] (cycles/sample).
 */
void wvd_instantaneous_frequency(const WVDResult *wvd, double *if_out);

/* =========================================================
 * PHASE EXTRACTION
 * ========================================================= */

/*
 * PhaseMap
 *
 * Wrapped or unwrapped 2-D phase in radians.
 *   width, height — image dimensions
 *   data          — [height][width], row-major
 *   quality       — [height][width] phase quality metric ∈ [0,1]
 *                   used by the unwrapper; NULL until computed
 */
typedef struct {
    int     width;
    int     height;
    double **data;
    double **quality;   /* may be NULL before compute_phase_quality() */
} PhaseMap;

PhaseMap *create_phase_map(int w, int h);
void      free_phase_map(PhaseMap *pm);

/*
 * extract_phase_wvd
 *
 * Computes the wrapped phase map of an interferogram by running the
 * DPWVD row-wise and extracting the IF ridge at each pixel.
 * The IF is integrated to give a wrapped phase estimate.
 *
 *   half_win  — WVD window half-length (0 = auto)
 *
 * Returns a newly allocated PhaseMap (wrapped, quality not set).
 */
PhaseMap *extract_phase_wvd(BMPImage *interferogram, int half_win);

/*
 * compute_phase_quality
 *
 * Fills pm->quality with a local phase derivative variance metric.
 * Must be called before unwrap_phase_quality.
 * Allocates pm->quality if NULL.
 */
void compute_phase_quality(PhaseMap *pm);

/* =========================================================
 * PHASE UNWRAPPING
 * ========================================================= */

/*
 * unwrap_phase_quality
 *
 * Quality-guided flood-fill phase unwrapper (Ghiglia & Pritt style).
 * Requires pm->quality to be non-NULL (call compute_phase_quality first).
 * Modifies pm->data in-place: wrapped → unwrapped phase in radians.
 */
void unwrap_phase_quality(PhaseMap *pm);

/*
 * unwrap_phase_simple
 *
 * Fast row-then-column Itoh unwrapper.  No quality weighting.
 * Modifies pm->data in-place.  Use when the interferogram SNR is high.
 */
void unwrap_phase_simple(PhaseMap *pm);

/* =========================================================
 * ZERNIKE POLYNOMIALS
 * ========================================================= */

/*
 * ZernikeBasis
 *
 * Pre-computed Zernike polynomial values over the unit pupil disk.
 * basis[k][i*w+j] = Z_k(rho, theta) for pixel (i,j) inside the pupil.
 *
 *   n_terms  — number of terms (ZERNIKE_TERMS for order 6)
 *   width, height — image dimensions (same as PhaseMap)
 *   basis    — [n_terms][height*width]  (flattened rows)
 *   pupil_mask — [height*width], 1 inside the unit pupil circle, 0 outside
 */
typedef struct {
    int     n_terms;
    int     width;
    int     height;
    double **basis;      /* [n_terms][height*width] */
    int    *pupil_mask;  /* [height*width], boolean */
    int     n_pupil;     /* number of pixels inside pupil */
} ZernikeBasis;

ZernikeBasis *create_zernike_basis(int w, int h, int n_terms);
void          free_zernike_basis(ZernikeBasis *zb);

/*
 * fit_zernike
 *
 * Least-squares fit of Zernike coefficients to the unwrapped phase.
 * Uses the normal equations Z^T Z c = Z^T phi solved via Cholesky.
 *
 *   phase       — unwrapped PhaseMap
 *   zb          — pre-computed ZernikeBasis
 *   coeffs_out  — caller-allocated array of zb->n_terms doubles
 */
void fit_zernike(const PhaseMap *phase, const ZernikeBasis *zb,
                 double *coeffs_out);

/* =========================================================
 * SEIDEL COEFFICIENTS
 * ========================================================= */

/*
 * SeidelCoeffs
 *
 * Classical primary (Seidel) aberration coefficients derived from
 * the Zernike fit.  Stored in wave-aberration function units (waves).
 *
 *   W[0]  W040  Spherical aberration
 *   W[1]  W131  Coma
 *   W[2]  W222  Astigmatism
 *   W[3]  W220  Field curvature
 *   W[4]  W311  Distortion
 *
 *   rms_wavefront — RMS wavefront error (waves) = sqrt(sum c_k^2, k>0)
 *   strehl        — Maréchal approximation: exp(-(2π σ)²)
 */
typedef struct {
    double W[SEIDEL_COUNT];
    double rms_wavefront;
    double strehl;
} SeidelCoeffs;

/*
 * zernike_to_seidel
 *
 * Converts Zernike coefficients (OSA/ANSI ordering) to classical
 * Seidel coefficients using the standard linear relations.
 *
 *   zernike_coeffs — array of ZERNIKE_TERMS doubles from fit_zernike
 *   sc_out         — output SeidelCoeffs struct (filled by this call)
 */
void zernike_to_seidel(const double *zernike_coeffs, SeidelCoeffs *sc_out);

/*
 * print_seidel
 *
 * Human-readable dump of a SeidelCoeffs struct to stdout.
 */
void print_seidel(const SeidelCoeffs *sc);

/* =========================================================
 * FULL ISP PIPELINE
 * ========================================================= */

/*
 * AberrationPipelineResult
 *
 * Aggregate output of run_aberration_pipeline().
 * All heap-allocated members are owned by this struct;
 * free with free_aberration_result().
 */
typedef struct {
    PhaseMap    *wrapped_phase;     /* after WVD extraction          */
    PhaseMap    *unwrapped_phase;   /* after quality-guided unwrap   */
    double      *zernike_coeffs;    /* [ZERNIKE_TERMS]               */
    SeidelCoeffs seidel;            /* primary aberration summary    */
    BMPImage    *denoised;          /* wavelet-denoised interferogram*/
    BMPImage    *phase_image;       /* unwrapped phase as 8-bit BMP  */
} AberrationPipelineResult;

void free_aberration_result(AberrationPipelineResult *r);

/*
 * run_aberration_pipeline
 *
 * End-to-end pipeline:
 *   1. Wavelet denoise (levels=3, auto threshold, soft)
 *   2. Wiener deconvolution with auto-estimated PSF
 *   3. WVD phase extraction
 *   4. Phase quality computation + quality-guided unwrapping
 *   5. Zernike fit (up to order ZERNIKE_MAX_ORDER)
 *   6. Seidel coefficient computation
 *
 *   interferogram — raw 8-bit or 24-bit BMP interferogram
 *   blur_kernel   — optional known PSF (pass NULL for identity/skip)
 *   use_quality_unwrap — 1 for quality-guided, 0 for simple Itoh
 *
 * Returns a newly allocated AberrationPipelineResult.
 */
AberrationPipelineResult *run_aberration_pipeline(BMPImage *interferogram,
                                                  Kernel   *blur_kernel,
                                                  int       use_quality_unwrap);

/*
 * phase_map_to_bmp
 *
 * Converts an unwrapped phase map to a displayable 8-bit grayscale BMP
 * by linearly scaling [min_phase, max_phase] → [0, 255].
 */
BMPImage *phase_map_to_bmp(const PhaseMap *pm);

#ifdef __cplusplus
}
#endif

#endif /* ABERRATION_H */
