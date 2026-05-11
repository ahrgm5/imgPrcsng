/* main_aberration_section.c
 *
 * Drop-in addition for main.c — paste these blocks after the existing
 * Problem 2 section, before the `done:` label.
 *
 * Requires:
 *   #include <aberration.h>
 *   #include <aberration_net.h>
 * at the top of main.c alongside the existing includes.
 */

    /* =========================================================
     * Problem 3 — Interferogram ISP Pipeline
     *
     * Runs the full physics-based aberration extraction pipeline:
     *   denoise → Wiener → WVD phase extraction →
     *   quality-guided unwrap → Zernike fit → Seidel coefficients
     * ========================================================= */
    {
        BMPImage                 *interferogram;
        AberrationPipelineResult *result;

        system("mkdir -p /home/ahrgm/Projects/imgPrcsngC/Course/HW/HW3/Problem3");

        build_path(path_buf, sizeof(path_buf),
                   src_images, "interferogram.bmp");
        interferogram = readBMP(path_buf);

        if (interferogram) {
            /*
             * Pass NULL for blur_kernel to skip the Wiener stage
             * (useful when the interferogram has no optical blur).
             * Pass use_quality_unwrap=1 for quality-guided flood-fill.
             */
            result = run_aberration_pipeline(interferogram,
                                             /*blur_kernel=*/NULL,
                                             /*use_quality_unwrap=*/1);

            if (result) {
                /* Save phase images */
                if (result->denoised) {
                    build_path(path_buf, sizeof(path_buf),
                               save_path, "Problem3/denoised.bmp");
                    writeBMP(path_buf, result->denoised);
                }
                if (result->phase_image) {
                    build_path(path_buf, sizeof(path_buf),
                               save_path, "Problem3/unwrapped_phase.bmp");
                    writeBMP(path_buf, result->phase_image);
                }

                /* Print Seidel summary (already printed inside pipeline) */
                printf("3.0 ISP — Strehl: %.4f  RMS: %.6f waves\n",
                       result->seidel.strehl,
                       result->seidel.rms_wavefront);

                free_aberration_result(result);
            }
            freeBMPImage(interferogram);
        } else {
            printf("Problem 3: interferogram.bmp not found — skipping.\n");
        }
    }
    PRINT_LAP("Problem 3 (interferogram ISP pipeline)");


    /* =========================================================
     * Problem 4 — Neural Network Training
     *
     * 4.1  Load labelled dataset (interferograms + CSV ground truth)
     * 4.2  Train Model A: interferogram image → Seidel regression
     * 4.3  Train Model B: Seidel coefficients → quality classification
     * ========================================================= */
    {
        AberrationDataset *dataset;
        NetConfig          cfg;

        system("mkdir -p /home/ahrgm/Projects/imgPrcsngC/Course/HW/HW3/Problem4");

        /*
         * Expected CSV format (header + one row per image):
         *   filename,W040,W131,W222,W220,W311,rms
         *
         * Images must be BMP files inside `src_images`.
         * If the dataset file does not exist the training block is skipped
         * gracefully — the inference demo in Problem 5 still runs if
         * pre-trained weights are present.
         */
        build_path(path_buf, sizeof(path_buf),
                   src_images, "aberration_labels.csv");

        dataset = load_aberration_dataset(src_images, path_buf);

        if (dataset && dataset->count > 0) {
            printf("Problem 4: loaded %d labelled interferograms.\n",
                   dataset->count);

            net_default_config(&cfg);
            cfg.epochs      = 30;     /* reduce for quick demo */
            cfg.batch_size  = 8;
            cfg.learning_rate = 5e-4;
            cfg.verbose     = 5;
            cfg.save_every  = 10;

            build_path(path_buf, sizeof(path_buf),
                       save_path, "Problem4");
            cfg.checkpoint_dir = path_buf;   /* save checkpoints here */

            train_aberration_models(dataset, &cfg, path_buf);
            free_aberration_dataset(dataset);
        } else {
            printf("Problem 4: no labelled dataset found — "
                   "training skipped.\n");
            if (dataset) free_aberration_dataset(dataset);
        }
    }
    PRINT_LAP("Problem 4 (neural network training)");


    /* =========================================================
     * Problem 5 — Inference Pipeline (physics + neural)
     *
     * Combines:
     *   1. Physics-based Seidel extraction (aberration.c)
     *   2. ImageNet regression prediction
     *   3. SeidelNet quality classification
     * on a fresh interferogram.
     * ========================================================= */
    {
        BMPImage  *test_ifgm;
        ImageNet  *img_net  = NULL;
        SeidelNet *sei_net  = NULL;
        char       model_a[512], model_b[512];

        build_path(path_buf, sizeof(path_buf),
                   src_images, "interferogram.bmp");
        test_ifgm = readBMP(path_buf);

        /* Load pre-trained weights if available */
        build_path(model_a, sizeof(model_a),
                   save_path, "Problem4/image_net_final.xml");
        build_path(model_b, sizeof(model_b),
                   save_path, "Problem4/seidel_net_final.xml");

        {
            FILE *fa = fopen(model_a, "r");
            FILE *fb = fopen(model_b, "r");

            if (fa) {
                fclose(fa);
                img_net = create_image_net();
                if (image_net_load(img_net, model_a) < 0) {
                    free_image_net(img_net);
                    img_net = NULL;
                    printf("Problem 5: could not load ImageNet weights.\n");
                }
            } else {
                printf("Problem 5: ImageNet weights not found — "
                       "skipping regression branch.\n");
            }

            if (fb) {
                fclose(fb);
                sei_net = create_seidel_net();
                if (seidel_net_load(sei_net, model_b) < 0) {
                    free_seidel_net(sei_net);
                    sei_net = NULL;
                    printf("Problem 5: could not load SeidelNet weights.\n");
                }
            } else {
                printf("Problem 5: SeidelNet weights not found — "
                       "skipping classification branch.\n");
            }
        }

        if (test_ifgm) {
            run_inference_pipeline(test_ifgm, NULL, img_net, sei_net);
            freeBMPImage(test_ifgm);
        }

        if (img_net)  free_image_net(img_net);
        if (sei_net)  free_seidel_net(sei_net);
    }
    PRINT_LAP("Problem 5 (inference pipeline)");
