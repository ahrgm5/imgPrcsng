/* main.c */
#define _POSIX_C_SOURCE 200809L
#include <bmp.h>
#include <transforms.h>
#include <imgproc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <fftw3.h>

/* =========================================================
 * HELPER — path concatenation
 * ========================================================= */

static void build_path(char *out, size_t out_size,
                       const char *base, const char *rel) {
    snprintf(out, out_size, "%s%s", base, rel);
}

/* =========================================================
 * MAIN
 * ========================================================= */

int main(void) {
    struct timespec t_start, t_end, t_lap;
    double elapsed;

    clock_gettime(CLOCK_MONOTONIC, &t_start);
    t_lap = t_start;

    /* Disable stdout buffering so timer lines appear immediately in the IDE */
    setvbuf(stdout, NULL, _IONBF, 0);

#define PRINT_LAP(label) \
    do { \
        clock_gettime(CLOCK_MONOTONIC, &t_end); \
        elapsed = (t_end.tv_sec  - t_lap.tv_sec) \
                + (t_end.tv_nsec - t_lap.tv_nsec) * 1e-9; \
        printf("[timer] %-40s  %8.3f s\n", (label), elapsed); \
        fflush(stdout); \
        t_lap = t_end; \
    } while (0)

    const char *src_images = "/home/ahrgm/Projects/imgPrcsngC/Course/images/";
    const char *save_path  = "/home/ahrgm/Projects/imgPrcsngC/Course/HW/HW3/";

    char path_buf[512];

    /* Ensure output directories exist */
    system("mkdir -p /home/ahrgm/Projects/imgPrcsngC/Course/HW/HW3/Problem1/1.1");
    system("mkdir -p /home/ahrgm/Projects/imgPrcsngC/Course/HW/HW3/Problem1/1.2");
    system("mkdir -p /home/ahrgm/Projects/imgPrcsngC/Course/HW/HW3/Problem2");

    /* =========================================================
     * Problem 1.1 — Known degradation and Wiener restoration
     * ========================================================= */
    {
        BMPImage         *building, *blurred, *restored;
        MotionBlurKernel *mbk;
        Filter           *wf;
        int               w, h;

        build_path(path_buf, sizeof(path_buf), src_images, "building.bmp");
        building = readBMP(path_buf);

        if (building) {
            w = building->info_header.width_px;
            h = abs(building->info_header.height_px);

            /* (a) Degrade: 7-pixel motion blur at 45 degrees + noise */
            mbk        = (MotionBlurKernel *)create_kernel(
                              31, 31, NULL, sizeof(MotionBlurKernel));
            mbk->L     = 7;
            mbk->theta = 45.0;
            motion_blur_init((Kernel *)mbk);

            blurred = apply_kernel_to_bmp(building, (Kernel *)mbk);
            add_gaussian_noise(blurred, 0.01);

            build_path(path_buf, sizeof(path_buf),
                       save_path, "Problem1/1.1/degraded.bmp");
            writeBMP(path_buf, blurred);

            /*
             * (b) Restore with adaptive Wiener filter.
             *
             * create_wiener_filter_auto:
             *   - estimates noise sigma from the wavelet HH1 subband of
             *     `blurred` (no manual K needed)
             *   - builds a per-frequency K_spectrum via the Wiener-Hunt
             *     formula so regularisation is strongest at blur nulls
             *   - sets apod_margin to suppress border ringing
             */
            wf       = create_wiener_filter_auto(FILTER_DOMAIN_DFT, w, h,
                                                 (Kernel *)mbk, blurred);
            restored = apply_frequency_filter_to_bmp(blurred, wf);

            build_path(path_buf, sizeof(path_buf),
                       save_path, "Problem1/1.1/restored_wiener.bmp");
            writeBMP(path_buf, restored);

            {
                double mse_w = calculate_mse(building, restored);
                double snr_w = calculate_snr(building, restored);
                printf("1.1 Wiener  — MSE: %.4f  SNR: %.2f dB\n", mse_w, snr_w);
            }

            /*
             * (c) Also restore with wavelet denoising for comparison.
             *
             * The wavelet denoiser can reduce residual noise left by the
             * Wiener filter (it handles the smooth AWGN the Wiener left
             * behind).  Pass thresh=0 to use the automatic VisuShrink
             * estimator, which uses the same underlying sigma estimate.
             */
            {
                BMPImage *wav_denoised;
                double    mse_wv, snr_wv;

                wav_denoised = apply_wavelet_denoise(blurred, 3, 0.0,
                                                     WAVELET_THRESH_SOFT);
                build_path(path_buf, sizeof(path_buf),
                           save_path, "Problem1/1.1/restored_wavelet.bmp");
                writeBMP(path_buf, wav_denoised);

                mse_wv = calculate_mse(building, wav_denoised);
                snr_wv = calculate_snr(building, wav_denoised);
                printf("1.1 Wavelet — MSE: %.4f  SNR: %.2f dB\n",
                       mse_wv, snr_wv);

                freeBMPImage(wav_denoised);
            }

            destroy_wiener_filter(wf);
            destroy_kernel((Kernel *)mbk);
            freeBMPImage(building);
            freeBMPImage(blurred);
            freeBMPImage(restored);
        }
    }
    PRINT_LAP("Problem 1.1 (Wiener + wavelet restore)");

    /* =========================================================
     * Problem 1.2 — Parameter search for urbanblur.bmp
     * ========================================================= */
    {
        BMPImage         *img_blur, *img_real, *res, *best_img;
        MotionBlurKernel *mbk;
        Kernel           *base_k;
        Filter           *f;
        int               w, h, li, ai, y, x;
        double            mse, min_mse;
        int               best_L;
        double            best_theta;
        double            sigma_n;
        double           *shared_K;
        fftw_complex     *precomp_G;

        static const int    blur_lengths[] = {1, 3, 5, 7, 9};
        static const double blur_angles[]  = {0.0, 45.0, 90.0, 135.0, 180.0};

        build_path(path_buf, sizeof(path_buf), src_images, "urbanblur.bmp");
        img_blur = readBMP(path_buf);

        build_path(path_buf, sizeof(path_buf), src_images, "urban0.bmp");
        img_real = readBMP(path_buf);

        if (!img_blur || !img_real) {
            fprintf(stderr, "Error: could not load urbanblur.bmp or urban0.bmp\n");
            if (img_blur) freeBMPImage(img_blur);
            if (img_real) freeBMPImage(img_real);
            goto done;
        }

        w = img_blur->info_header.width_px;
        h = abs(img_blur->info_header.height_px);

        mbk    = (MotionBlurKernel *)create_kernel(
                      31, 31, NULL, sizeof(MotionBlurKernel));
        base_k = (Kernel *)mbk;

        min_mse    = 1e9;
        best_L     = 0;
        best_theta = 0.0;
        best_img   = NULL;

        /*
         * Pre-compute the three quantities that are identical for every
         * candidate in the search grid — they depend only on img_blur,
         * not on the kernel parameters (L, theta):
         *
         *   1. sigma_n     — noise std-dev from the Haar HH1 subband
         *   2. shared_K    — per-frequency NSR array (Wiener-Hunt formula)
         *   3. precomp_G   — forward DFT of the apodised blurred image
         *
         * Without this, create_wiener_filter_auto recomputes all three
         * from scratch on every one of the 25 iterations.
         */
        {
            Filter       *f_tmp;
            WienerFilter *wf_tmp;

            /* Use a dummy L=1 kernel just to set apod_margin consistently */
            mbk->L = 1; mbk->theta = 0.0;
            motion_blur_init(base_k);
            f_tmp  = create_wiener_filter(FILTER_DOMAIN_DFT, w, h,
                                          base_k, 0.0);
            /* apod_margin for a 31x31 kernel = 31/2 + 1 = 16 */
            f_tmp->apod_margin = 16;

            sigma_n = estimate_noise_sigma(img_blur);
            printf("Pre-computed noise sigma = %.5f\n", sigma_n);

            wiener_build_K_spectrum((WienerFilter *)f_tmp, img_blur, sigma_n);

            /* Steal K_spectrum from tmp filter into shared_K */
            wf_tmp = (WienerFilter *)f_tmp;
            shared_K = wf_tmp->K_spectrum;
            wf_tmp->K_spectrum = NULL;   /* prevent free inside destroy */

            /* Pre-compute G = FFT(apodised img_blur) */
            precomp_G = fftw_alloc_complex(w * h);
            filter_preload_input_dft(f_tmp, img_blur, precomp_G);

            destroy_wiener_filter(f_tmp);
        }

        for (li = 0; li < 5; li++) {
            for (ai = 0; ai < 5; ai++) {
                /* Reset kernel mask */
                for (y = 0; y < base_k->height; y++)
                    for (x = 0; x < base_k->width; x++)
                        base_k->mask[y][x] = 0.0f;

                mbk->L     = blur_lengths[li];
                mbk->theta = blur_angles[ai];
                motion_blur_init(base_k);

                /*
                 * Build only the kernel-specific part (OTF of the blur PSF).
                 * K_spectrum and apod_margin come from the pre-computed values.
                 */
                f = create_wiener_filter(FILTER_DOMAIN_DFT, w, h,
                                         base_k, 0.0);
                {
                    WienerFilter *wf = (WienerFilter *)f;

                    /* Attach shared K_spectrum (not owned — don't free it) */
                    if (wf->K_spectrum) free(wf->K_spectrum);
                    wf->K_spectrum = shared_K;

                    f->apod_margin  = 16;

                    /* Point to the pre-computed G so filter_execute skips
                     * the redundant forward FFT of img_blur */
                    f->preloaded_G = precomp_G;
                }

                res = apply_frequency_filter_to_bmp(img_blur, f);

                /* Detach shared pointers before destroy to avoid double-free */
                ((WienerFilter *)f)->K_spectrum = NULL;
                f->preloaded_G = NULL;

                if (res) {
                    mse = calculate_mse(img_real, res);
                    if (mse < min_mse) {
                        min_mse    = mse;
                        best_L     = blur_lengths[li];
                        best_theta = blur_angles[ai];
                        if (best_img) freeBMPImage(best_img);
                        best_img = res;
                    } else {
                        freeBMPImage(res);
                    }
                }
                destroy_wiener_filter(f);
            }
        }

        fftw_free(precomp_G);
        free(shared_K);

        printf("1.2 Best Result: L=%d  Theta=%.1f  MSE=%.4f\n",
               best_L, best_theta, min_mse);

        if (best_img) {
            build_path(path_buf, sizeof(path_buf),
                       save_path, "Problem1/1.2/best_restoration.bmp");
            writeBMP(path_buf, best_img);
            freeBMPImage(best_img);
        }

        destroy_kernel(base_k);
        freeBMPImage(img_blur);
        freeBMPImage(img_real);
    }
    PRINT_LAP("Problem 1.2 (parameter search)");





    /* =========================================================
     * Problem 2 — HSI colour segmentation
     * ========================================================= */
    {
        BMPImage  *color_img;
        BMPImages  results;
        int        i;

        static const char *names[]       = {"red", "orange", "green",
                                            "blue", "purple"};
        static const float target_hues[] = {0.0f, 20.0f, 120.0f,
                                            220.0f, 300.0f};
        static const int   n_hues        = 5;
        const float        hue_tol       = 20.0f;   /* widened for orange/red proximity */
        const float        min_sat       = 0.15f;

        build_path(path_buf, sizeof(path_buf), src_images, "color_image.bmp");
        color_img = readBMP(path_buf);

        if (color_img) {
            results = segment_color_hsi(color_img, target_hues, n_hues,
                                        hue_tol, min_sat);

            for (i = 0; i < results.count; i++) {
                char rel[64];
                snprintf(rel, sizeof(rel), "/Problem2/%s.bmp", names[i]);
                build_path(path_buf, sizeof(path_buf), save_path, rel);
                writeBMP(path_buf, results.images[i]);
            }

            freeBMPImages(results);
            freeBMPImage(color_img);
        }
    }
    PRINT_LAP("Problem 2 (HSI segmentation)");

done:
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    elapsed = (t_end.tv_sec  - t_start.tv_sec)
            + (t_end.tv_nsec - t_start.tv_nsec) * 1e-9;
    printf("[timer] %-40s  %8.3f s\n", "TOTAL", elapsed);
    fflush(stdout);

    return 0;
}
