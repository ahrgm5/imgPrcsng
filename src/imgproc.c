/* imgproc.c */
#define _POSIX_C_SOURCE 200809L

#include <imgproc.h>
#include <transforms.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================
 * INTERNAL HELPERS
 * ========================================================= */

static float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static double clampd(double v, double lo, double hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static float absf_delta(float a, float b) {
    float d = a - b;
    return (d < 0.0f) ? -d : d;
}

/*
 * shift_quadrants — rearranges a flat w*h buffer so that the DC
 * component (originally at the corners for DFT output) moves to
 * the centre of the image.
 */
static void shift_quadrants(const double *src, double *dst, int w, int h) {
    int w2, h2, i, j, ni, nj;

    w2 = w / 2;
    h2 = h / 2;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            ni          = (i + h2) % h;
            nj          = (j + w2) % w;
            dst[ni * w + nj] = src[i * w + j];
        }
    }
}


/* =========================================================
 * SECTION 1 — STATISTICS
 * ========================================================= */

ImageStats calculate_stats_generic(BMPImage *img) {
    ImageStats  stats;
    int         w, h, n, i, j, idx;
    double      sum, var_sum, val;
    float       cumulative;
    float      *intensities;
    HSIImage   *hsi;

    memset(&stats, 0, sizeof(ImageStats));
    if (!img) return stats;

    w = img->info_header.width_px;
    h = abs(img->info_header.height_px);
    n = w * h;

    stats.num_pixels = n;
    stats.min        = 255.0;
    stats.max        = 0.0;

    intensities = (float *)malloc(n * sizeof(float));
    if (!intensities) return stats;

    /* --- Extract per-pixel intensity -------------------------------- */
    if (img->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(img);
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                intensities[i * w + j] = hsi->pixels[i][j].i * 255.0f;
        freeHSIImage(hsi);
    } else {
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                intensities[i * w + j] = (float)img->pixels[i][j];
    }

    /* --- Counts, min, max, mean ------------------------------------ */
    sum = 0.0;
    for (i = 0; i < n; i++) {
        val = intensities[i];
        idx = (int)clampf(val, 0.0f, 255.0f);
        stats.counts[idx]++;
        sum += val;
        if (val < stats.min) stats.min = val;
        if (val > stats.max) stats.max = val;
    }
    stats.mean = sum / n;

    /* --- Standard deviation ---------------------------------------- */
    var_sum = 0.0;
    for (i = 0; i < n; i++) {
        double d = intensities[i] - stats.mean;
        var_sum += d * d;
    }
    stats.std_dev = sqrt(var_sum / n);

    /* --- PDF / CDF -------------------------------------------------- */
    cumulative = 0.0f;
    for (i = 0; i < 256; i++) {
        stats.pdf[i]  = stats.counts[i] / n;
        cumulative   += stats.pdf[i];
        stats.cdf[i]  = cumulative;
    }

    free(intensities);
    return stats;
}


/* =========================================================
 * SECTION 2 — SINGLE-IMAGE OPERATIONS  (returns BMPImage*)
 * ========================================================= */

BMPImage *apply_histogram_stretching(BMPImage *input, int plot) {
    int       w, h, i, j;
    BMPImage *output;
    ImageStats stats;

    w      = input->info_header.width_px;
    h      = abs(input->info_header.height_px);
    output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);
    if (!output) return NULL;

    stats = calculate_stats_generic(input);

    if (plot) plot_transformation_curve(&stats, "Histogram Stretching Transformation");

    if (input->info_header.bits_per_pixel == 24) {
        float    min_i, max_i, range;
        HSIImage *hsi = convert_bmp_to_hsi(input);

        min_i = 1.0f; max_i = 0.0f;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                float v = hsi->pixels[i][j].i;
                if (v < min_i) min_i = v;
                if (v > max_i) max_i = v;
            }
        }

        range = max_i - min_i;
        if (range > 0.0f) {
            for (i = 0; i < h; i++)
                for (j = 0; j < w; j++)
                    hsi->pixels[i][j].i = (hsi->pixels[i][j].i - min_i) / range;
        }
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        uint8_t min_v, max_v;
        float   range;

        min_v = 255; max_v = 0;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                if (input->pixels[i][j] < min_v) min_v = input->pixels[i][j];
                if (input->pixels[i][j] > max_v) max_v = input->pixels[i][j];
            }
        }
        range = (float)(max_v - min_v);
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                float val = (range == 0.0f)
                           ? 0.0f
                           : (float)(input->pixels[i][j] - min_v) / range;
                output->pixels[i][j] = (uint8_t)(val * 255.0f);
            }
        }
    }
    return output;
}

BMPImage *apply_gamma_correction(BMPImage *input, float gamma) {
    int       w, h, i, j;
    BMPImage *output;

    w      = input->info_header.width_px;
    h      = abs(input->info_header.height_px);
    output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);
    if (!output) return NULL;

    if (input->info_header.bits_per_pixel == 24) {
        HSIImage *hsi = convert_bmp_to_hsi(input);
        for (i = 0; i < h; i++)
            for (j = 0; j < w; j++)
                hsi->pixels[i][j].i = powf(hsi->pixels[i][j].i, 1.0f / gamma);
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                float norm = (float)input->pixels[i][j] / 255.0f;
                output->pixels[i][j] = (uint8_t)(powf(norm, gamma) * 255.0f);
            }
        }
    }
    return output;
}

BMPImage *preprocess_for_edges(BMPImage *input, float gamma, int plot) {
    BMPImage *stretched, *processed;

    stretched = apply_histogram_stretching(input, plot);
    processed = apply_gamma_correction(stretched, gamma);
    freeBMPImage(stretched);
    return processed;
}

BMPImage *apply_histogram_equalization(BMPImage *input, int plot) {
    int        w, h, i, j;
    BMPImage  *output;
    ImageStats stats;

    w      = input->info_header.width_px;
    h      = abs(input->info_header.height_px);
    output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);
    if (!output) return NULL;

    stats = calculate_stats_generic(input);

    if (plot) plot_transformation_curve(&stats, "Histogram Equalization Transformation");

    if (input->info_header.bits_per_pixel == 24) {
        HSIImage *hsi = convert_bmp_to_hsi(input);
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                int level = (int)(hsi->pixels[i][j].i * 255.0f);
                hsi->pixels[i][j].i = stats.cdf[level];
            }
        }
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                uint8_t level       = input->pixels[i][j];
                output->pixels[i][j] = (uint8_t)(stats.cdf[level] * 255.0f);
            }
        }
    }
    return output;
}

BMPImage *combine_images(BMPImage *img1, BMPImage *img2, float factor) {
    int       w, h, i, j;
    BMPImage *output;

    if (!img1 || !img2) return NULL;

    w = img1->info_header.width_px;
    h = abs(img1->info_header.height_px);

    if (w != img2->info_header.width_px ||
        h != abs(img2->info_header.height_px)) {
        fprintf(stderr, "Error: Dimension mismatch in combine_images.\n");
        return NULL;
    }

    output = createEmptyBMP(w, h, img1->info_header.bits_per_pixel);
    if (!output) return NULL;

    if (img1->info_header.bits_per_pixel == 24) {
        HSIImage *hsi1 = convert_bmp_to_hsi(img1);
        HSIImage *hsi2 = convert_bmp_to_hsi(img2);

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                float val = hsi1->pixels[i][j].i
                          + (factor * hsi2->pixels[i][j].i);
                hsi1->pixels[i][j].i = clampf(val, 0.0f, 1.0f);
            }
        }
        update_bmp_from_hsi(output, hsi1);
        freeHSIImage(hsi1);
        freeHSIImage(hsi2);
    } else {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                float v1     = img1->pixels[i][j] / 255.0f;
                float v2     = img2->pixels[i][j] / 255.0f;
                float result = v1 + (factor * v2);
                output->pixels[i][j] =
                    (uint8_t)(clampf(result, 0.0f, 1.0f) * 255.0f);
            }
        }
    }
    return output;
}


/* =========================================================
 * SECTION 3 — MULTI-IMAGE OPERATIONS  (returns BMPImages)
 * ========================================================= */

BMPImages sharpen_laplacian(BMPImage *input, float k, int kw, int kh) {
    BMPImages       res;
    LaplacianKernel *lap;
    BMPImage        *edges;

    res.count  = 2;
    res.images = (BMPImage **)malloc(sizeof(BMPImage *) * 2);

    lap = (LaplacianKernel *)create_kernel(kw, kh, laplacian_init,
                                           sizeof(LaplacianKernel));

    /* images[0]: raw Laplacian edge map */
    edges         = apply_kernel_to_bmp(input, (Kernel *)lap);
    res.images[0] = edges;

    /* images[1]: sharpened = original + k * edges */
    res.images[1] = combine_images(input, edges, k);

    destroy_kernel((Kernel *)lap);
    return res;
}

BMPImages unsharp_masking(BMPImage *input, float k, int kw, int kh) {
    BMPImages     res;
    AverageKernel *avg;
    BMPImage      *blurred, *mask;

    res.count  = 2;
    res.images = (BMPImage **)malloc(sizeof(BMPImage *) * 2);

    avg = (AverageKernel *)create_kernel(kw, kh, average_init,
                                         sizeof(AverageKernel));

    /* images[0]: blurred image */
    blurred       = apply_kernel_to_bmp(input, (Kernel *)avg);
    res.images[0] = blurred;

    /* detail mask = original - blurred */
    mask = combine_images(input, blurred, -1.0f);

    /* images[1]: sharpened = original + k * mask */
    res.images[1] = combine_images(input, mask, k);

    freeBMPImage(mask);
    destroy_kernel((Kernel *)avg);
    return res;
}

BMPImages segment_color_hsi(BMPImage *input,
                             const float *target_hues, int n_hues,
                             float hue_tol, float min_sat) {
    BMPImages output;
    int       w, h, k, i, j;
    float     delta;

    if (!input || input->info_header.bits_per_pixel != 24) {
        printf("segment_color_hsi: input must be a 24-bit colour image.\n");
        output.images = NULL;
        output.count  = 0;
        return output;
    }

    w = input->info_header.width_px;
    h = abs(input->info_header.height_px);

    output.count  = n_hues;
    output.images = (BMPImage **)malloc(sizeof(BMPImage *) * n_hues);

    for (k = 0; k < n_hues; k++) {
        HSIImage *hsi   = convert_bmp_to_hsi(input);
        float     max_d = 0.0f;
        int       any_miss = 0;

        output.images[k] = createEmptyBMP(w, h, 24);

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                float h_val = hsi->pixels[i][j].h;
                float s_val = hsi->pixels[i][j].s;

                /* Shortest angular distance on the hue circle */
                delta = absf_delta(h_val, target_hues[k]);
                if (delta > 180.0f) delta = 360.0f - delta;

                if (delta > hue_tol || s_val < min_sat) {
                    /* Black out non-matching pixels */
                    hsi->pixels[i][j].i = 0.0f;
                    if (delta > hue_tol) {
                        any_miss = 1;
                        if (delta > max_d) max_d = delta;
                    }
                }
            }
        }

        if (any_miss) {
            printf("Target hue %.1f deg — max deviation: %.2f deg\n",
                   target_hues[k], max_d);
        }

        update_bmp_from_hsi(output.images[k], hsi);
        freeHSIImage(hsi);
    }

    return output;
}


/* =========================================================
 * SECTION 7 — NOISE
 * ========================================================= */

void add_gaussian_noise(BMPImage *img, double variance) {
    double std_dev, u1, u2, z, noise;
    int    w, h, i, j, c, val;

    std_dev = sqrt(variance);
    w       = img->info_header.width_px;
    h       = abs(img->info_header.height_px);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            if (img->info_header.bits_per_pixel == 24) {
                for (c = 0; c < 3; c++) {
                    /* Box-Muller transform */
                    do { u1 = (double)rand() / RAND_MAX; } while (u1 == 0.0);
                    u2    = (double)rand() / RAND_MAX;
                    z     = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
                    noise = z * std_dev * 255.0;
                    val   = (int)img->pixels[i][j * 3 + c] + (int)noise;
                    if (val < 0)   val = 0;
                    if (val > 255) val = 255;
                    img->pixels[i][j * 3 + c] = (unsigned char)val;
                }
            } else {
                do { u1 = (double)rand() / RAND_MAX; } while (u1 == 0.0);
                u2    = (double)rand() / RAND_MAX;
                z     = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
                noise = z * std_dev * 255.0;
                val   = (int)img->pixels[i][j] + (int)noise;
                if (val < 0)   val = 0;
                if (val > 255) val = 255;
                img->pixels[i][j] = (unsigned char)val;
            }
        }
    }
}


/* =========================================================
 * SECTION 4 — QUALITY METRICS  (returns double)
 * ========================================================= */

double calculate_mse(BMPImage *orig, BMPImage *processed) {
    int     w, h, i, j;
    double  sum_sq, diff;

    if (!orig || !processed) return -1.0;

    w = orig->info_header.width_px;
    h = abs(orig->info_header.height_px);

    if (w != processed->info_header.width_px ||
        h != abs(processed->info_header.height_px)) {
        fprintf(stderr, "Error: dimension mismatch in calculate_mse.\n");
        return -1.0;
    }

    sum_sq = 0.0;

    if (orig->info_header.bits_per_pixel == 24) {
        HSIImage *hsi_orig = convert_bmp_to_hsi(orig);
        HSIImage *hsi_proc = convert_bmp_to_hsi(processed);
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                diff    = (double)(hsi_orig->pixels[i][j].i * 255.0f)
                        - (double)(hsi_proc->pixels[i][j].i * 255.0f);
                sum_sq += diff * diff;
            }
        }
        freeHSIImage(hsi_orig);
        freeHSIImage(hsi_proc);
    } else {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                diff    = (double)orig->pixels[i][j]
                        - (double)processed->pixels[i][j];
                sum_sq += diff * diff;
            }
        }
    }

    return sum_sq / (double)(w * h);
}

double calculate_snr(BMPImage *orig, BMPImage *processed) {
    double  mse, signal_power, val;
    int     w, h, i, j;

    mse = calculate_mse(orig, processed);
    if (mse <= 0.0) return 99.9;

    w            = orig->info_header.width_px;
    h            = abs(orig->info_header.height_px);
    signal_power = 0.0;

    if (orig->info_header.bits_per_pixel == 24) {
        HSIImage *hsi = convert_bmp_to_hsi(orig);
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                val           = (double)(hsi->pixels[i][j].i * 255.0f);
                signal_power += val * val;
            }
        }
        freeHSIImage(hsi);
    } else {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                val           = (double)orig->pixels[i][j];
                signal_power += val * val;
            }
        }
    }

    return 10.0 * log10((signal_power / (double)(w * h)) / mse);
}


/* =========================================================
 * SECTION 5 — SPECTRAL ANALYSIS  (void)
 * ========================================================= */

void run_dft_analysis(BMPImage *img, const char *title,
                      int plot, const char *save_path) {
    int           w, h, N, i, j;
    fftw_complex *in, *out;
    fftw_plan     p;
    double       *mag, *shifted, val;

    if (!img) return;

    w = img->info_header.width_px;
    h = abs(img->info_header.height_px);
    N = w * h;

    in  = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * N);
    out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * N);
    p   = fftw_plan_dft_2d(h, w, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            if (img->info_header.bits_per_pixel == 24) {
                val = (0.299  * img->pixels[i][j * 3 + 2]
                     + 0.587  * img->pixels[i][j * 3 + 1]
                     + 0.114  * img->pixels[i][j * 3 + 0]) / 255.0;
            } else {
                val = img->pixels[i][j] / 255.0;
            }
            in[i * w + j][0] = val;
            in[i * w + j][1] = 0.0;
        }
    }

    fftw_execute(p);

    mag = (double *)malloc(N * sizeof(double));
    for (i = 0; i < N; i++)
        mag[i] = log(1.0 + sqrt(out[i][0] * out[i][0]
                                + out[i][1] * out[i][1]));

    shifted = (double *)malloc(N * sizeof(double));
    shift_quadrants(mag, shifted, w, h);

    save_spectrum_plot(shifted, w, h, title, save_path, plot);

    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);
    free(mag);
    free(shifted);
}

void run_dct_analysis(BMPImage *img, const char *title,
                      int plot, const char *save_path) {
    int        w, h, N, i, j;
    double    *in, *out, *mag, *shifted, val;
    fftw_plan  p;

    if (!img) return;

    w = img->info_header.width_px;
    h = abs(img->info_header.height_px);
    N = w * h;

    in  = (double *)fftw_malloc(sizeof(double) * N);
    out = (double *)fftw_malloc(sizeof(double) * N);
    p   = fftw_plan_r2r_2d(h, w, in, out,
                            FFTW_REDFT10, FFTW_REDFT10, FFTW_ESTIMATE);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            if (img->info_header.bits_per_pixel == 24) {
                val = (0.299  * img->pixels[i][j * 3 + 2]
                     + 0.587  * img->pixels[i][j * 3 + 1]
                     + 0.114  * img->pixels[i][j * 3 + 0]) / 255.0;
            } else {
                val = img->pixels[i][j] / 255.0;
            }
            in[i * w + j] = val;
        }
    }

    fftw_execute(p);

    mag = (double *)malloc(N * sizeof(double));
    for (i = 0; i < N; i++)
        mag[i] = log(1.0 + fabs(out[i]));

    shifted = (double *)malloc(N * sizeof(double));
    shift_quadrants(mag, shifted, w, h);

    save_spectrum_plot(shifted, w, h, title, save_path, plot);

    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);
    free(mag);
    free(shifted);
}

void save_spectrum_plot(double *data, int w, int h,
                        const char *title, const char *save_path, int plot) {
    char  temp_filename[64];
    FILE *temp, *gp;
    int   i, j;

    if (!data) return;

    snprintf(temp_filename, sizeof(temp_filename),
             "tmp_spec_%d.txt", rand());

    temp = fopen(temp_filename, "w");
    if (!temp) return;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++)
            fprintf(temp, "%f ", data[i * w + j]);
        fprintf(temp, "\n");
    }
    fclose(temp);

    gp = popen("gnuplot", "w");
    if (gp) {
        if (save_path && save_path[0] != '\0') {
            fprintf(gp, "set terminal pngcairo size 800,600\n");
            fprintf(gp, "set output '%s'\n", save_path);
        } else if (plot) {
            fprintf(gp, "set terminal wxt persist\n");
        }

        fprintf(gp, "set title '%s'\n", title);
        fprintf(gp, "set palette gray\n");
        fprintf(gp, "set view map\n");
        fprintf(gp, "set size ratio -1\n");
        fprintf(gp, "unset key\n");
        fprintf(gp, "set xrange [0:%d]\n", w - 1);
        fprintf(gp, "set yrange [%d:0]\n", h - 1);
        fprintf(gp, "splot '%s' matrix with image\n", temp_filename);

        if (save_path && save_path[0] != '\0')
            fprintf(gp, "set output\n");

        fprintf(gp, "exit\n");
        pclose(gp);
    }

    remove(temp_filename);
}

void plot_spectrum_gnuplot(double *mag_data, int w, int h, const char *title) {
    FILE *gp;
    int   i, j;

    gp = popen("gnuplot -persist", "w");
    if (!gp) return;

    fprintf(gp, "set title '%s'\n", title);
    fprintf(gp, "set palette gray\n");
    fprintf(gp, "plot '-' matrix with image\n");

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++)
            fprintf(gp, "%f ", mag_data[i * w + j]);
        fprintf(gp, "\n");
    }
    fprintf(gp, "e\n");
    pclose(gp);
}


/* =========================================================
 * SECTION 6 — VISUALISATION  (void)
 * ========================================================= */

void plot_transformation_curve(const ImageStats *stats, const char *title) {
    FILE *gp;
    int   i;

    gp = popen("gnuplot -persistent", "w");
    if (!gp) return;

    fprintf(gp, "set title '%s'\n", title);
    fprintf(gp, "set xlabel 'Original Intensity (u)'\n");
    fprintf(gp, "set ylabel 'New Intensity (v)'\n");
    fprintf(gp, "plot '-' with lines lw 2 title 'T(u)'\n");

    for (i = 0; i < 256; i++)
        fprintf(gp, "%d %f\n", i, stats->cdf[i] * 255.0f);

    fprintf(gp, "e\n");
    pclose(gp);
}
