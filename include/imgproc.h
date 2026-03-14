#ifndef IMGPROC_H
#define IMGPROC_H

#include <bmp.h>

/* =========================================================
 * SECTION 1 — STATISTICS
 * Returns: ImageStats (by value)
 * ========================================================= */

ImageStats calculate_stats_generic(BMPImage *img);

/* =========================================================
 * SECTION 2 — SINGLE-IMAGE OPERATIONS
 * Returns: BMPImage*
 * ========================================================= */

BMPImage *apply_histogram_stretching(BMPImage *input, int plot);
BMPImage *apply_gamma_correction(BMPImage *input, float gamma);
BMPImage *preprocess_for_edges(BMPImage *input, float gamma, int plot);
BMPImage *apply_histogram_equalization(BMPImage *input, int plot);
BMPImage *combine_images(BMPImage *img1, BMPImage *img2, float factor);

/* =========================================================
 * SECTION 3 — MULTI-IMAGE OPERATIONS
 * Returns: BMPImages (by value)
 * ========================================================= */

/* sharpen_laplacian  — images[0]=edge map, images[1]=sharpened */
BMPImages sharpen_laplacian(BMPImage *input, float k, int kw, int kh);

/* unsharp_masking    — images[0]=blurred,  images[1]=sharpened */
BMPImages unsharp_masking(BMPImage *input, float k, int kw, int kh);

/*
 * segment_color_hsi
 *
 * target_hues  — array of hue centre values (degrees, 0-360)
 * n_hues       — number of elements in target_hues
 * hue_tol      — half-width tolerance in degrees around each target hue
 * min_sat      — minimum saturation for a pixel to be kept
 *
 * Returns one output image per target hue (output.count == n_hues).
 */
BMPImages segment_color_hsi(BMPImage *input,
                             const float *target_hues, int n_hues,
                             float hue_tol, float min_sat);

/* =========================================================
 * SECTION 4 — QUALITY METRICS
 * Returns: double
 * ========================================================= */

/* =========================================================
 * SECTION 7 — NOISE
 * ========================================================= */

/*
 * add_gaussian_noise
 *
 * Adds zero-mean Gaussian noise with the given variance (in normalised
 * 0-1 units) to every pixel in-place.  Uses the Box-Muller transform.
 * Works on both 8-bit grayscale and 24-bit colour images.
 */
void add_gaussian_noise(BMPImage *img, double variance);

double calculate_mse(BMPImage *orig, BMPImage *processed);
double calculate_snr(BMPImage *orig, BMPImage *processed);

/* =========================================================
 * SECTION 5 — SPECTRAL ANALYSIS  (void — side-effects only)
 * ========================================================= */

void run_dft_analysis(BMPImage *img, const char *title,
                      int plot, const char *save_path);

void run_dct_analysis(BMPImage *img, const char *title,
                      int plot, const char *save_path);

void save_spectrum_plot(double *data, int w, int h,
                        const char *title, const char *save_path, int plot);

void plot_spectrum_gnuplot(double *mag_data, int w, int h, const char *title);

/* =========================================================
 * SECTION 6 — VISUALISATION  (void — side-effects only)
 * ========================================================= */

void plot_transformation_curve(const ImageStats *stats, const char *title);

#endif /* IMGPROC_H */
