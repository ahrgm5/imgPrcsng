#include <bmp.hpp>
#include <kernels.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <math.h>

#include "filters.hpp"
#include "img_manip.hpp"

int main() {
    std::cout << "Starting HW#2 Image Processing Assignment..." << std::endl;

    std::string path = "/home/ahrgm/Projects/imgPrcsng/Course/HW/HW2/";
    std::string src_images = "/home/ahrgm/Projects/imgPrcsng/Course/images/";

    bool plot = false;  // Plot for the apply histogram equalization function

    // ==========================================================
    // TASK 2.1: Histogram Equalization (pout.bmp)
    // ==========================================================
    std::cout << "\n--- Task 2.1: Histogram Equalization ---" << std::endl;
    // Task requires implementing histogram equalization and applying it to pout [cite: 8]
    BMPImage* pout = readBMP((src_images + "/2.1/pout.bmp").c_str());
    if (pout) {
        BMPImage* pout_eq = apply_histogram_equalization(pout, plot, "Intensity Transformation u vs v");
        writeBMP((path + "Problem2/2.1/pout_equalized.bmp").c_str(), pout_eq);

        BMPImage* pout_stretched = apply_histogram_stretching(pout);
        writeBMP((path + "Problem2/2.1/pout_stretched.bmp").c_str(), pout_stretched);

        freeBMPImage(pout);
        freeBMPImage(pout_eq);
    }

    // ==========================================================
    // TASK 2.2: Spatial Sharpening (leaf.bmp)
    // ==========================================================
    std::cout << "\n--- Task 2.2: Spatial Sharpening ---" << std::endl;
    // Task requires sharpening leaf.jpg using spatial processing techniques [cite: 14]
    BMPImage* leaf1 = readBMP((src_images + "leaf/leaf.bmp").c_str());
    if (leaf1) {
        BMPImage* prepped = preprocess_for_edges(leaf1, 0.8f);

        // Laplacian Sharpening
        BMPImages leaf_lap = sharpen_laplacian(prepped, 1.0f, 3, 3);
        writeBMP((path + "Problem2/2.2/leaf_sharpened_laplacian.bmp").c_str(), leaf_lap.images[1]);

        // Unsharp Masking
        BMPImages leaf_unsharp = unsharp_masking(prepped, 1.0f, 3, 3);
        writeBMP((path + "Problem2/2.2/leaf_sharpened_unsharp.bmp").c_str(), leaf_unsharp.images[1]);

        freeBMPImage(leaf1);
        freeBMPImage(prepped);
        freeBMPImages(leaf_lap);
        freeBMPImages(leaf_unsharp);
    }

    // ==========================================================
    // TASK 3: High-Frequency Emphasis Order Analysis
    // ==========================================================
    std::cout << "\n--- Task 3: High-Frequency Emphasis (Order Analysis) ---" << std::endl;
    // Task requires combining high-frequency emphasis and histogram equalization [cite: 19, 20]
    // Must test if the order of applying these two operations matters [cite: 25]
    BMPImage* test_img = readBMP((src_images + "/leaf/leaf.bmp").c_str());
    BMPImage* gray_test_img = extract_channel_info(test_img, 3, 1); // Convert to grayscale for frequency processing

    if (test_img) {
        int w = test_img->info_header.width_px;
        int h = abs(test_img->info_header.height_px);
        hpFilter* hp = (hpFilter*)create_filter(FILTER_DOMAIN_DFT, w, h, hpf_logic, sizeof(hpFilter));
        hp->cutoff = 40.0;

        // Order 1: High-Frequency Emphasis Filter -> Histogram Equalization
        BMPImage* hpf_only = apply_frequency_filter_to_bmp(test_img, (Filter*)hp);
        BMPImage* order1 = apply_histogram_equalization(hpf_only, plot, "");
        writeBMP((path + "Problem3/order1_hpf_then_histeq.bmp").c_str(), order1);

        // Order 2: Histogram Equalization -> High-Frequency Emphasis Filter
        BMPImage* eq_only = apply_histogram_equalization(test_img, plot, "");
        BMPImage* order2 = apply_frequency_filter_to_bmp(eq_only, (Filter*)hp);
        writeBMP((path + "Problem3/order2_histeq_then_hpf.bmp").c_str(), order2);

        destroy_filter((Filter*)hp);
        freeBMPImage(test_img); freeBMPImage(hpf_only); freeBMPImage(order1);
        freeBMPImage(eq_only); freeBMPImage(order2);
    }

    // ==========================================================
        // TASK 4: DFT/DCT Analysis & Truncation (Organized Tree)
        // ==========================================================
        std::cout << "\n--- Task 4: DFT/DCT Analysis & Truncation ---" << std::endl;

        std::vector<std::string> task4_imgs = {"baboon.bmp", "monkeyking.bmp", "sunflower.bmp", "hex.bmp"};

        for (const std::string& img_name : task4_imgs) {
            std::cout << "Processing: " << img_name << "..." << std::endl;

            BMPImage* color_img = readBMP((src_images + img_name).c_str());
            if (!color_img) continue;

            // 4.1: Convert to Grayscale
            // (Assuming extract_channel_info(img, type, channel) extracts intensity)
            BMPImage* gray_img = extract_channel_info(color_img, 3, 1);

            // --- PREVENTING THE MEMORY ERROR ---
            // We store the strings in variables so they stay alive until the functions finish.
            std::string base = img_name.substr(0, img_name.find_last_of("."));

            std::string dft_title = "Centered DFT - " + img_name;
            std::string dft_spec_path = path + "Problem4/Spectrums/" + base + "_dft_spectrum.png";

            std::string dct_title = "Centered DCT - " + img_name;
            std::string dct_spec_path = path + "Problem4/Spectrums/" + base + "_dct_spectrum.png";

            // 4.1: Plot and Save 2D log magnitude
            run_dft_analysis(gray_img, dft_title.c_str(), plot, dft_spec_path.c_str());
            run_dct_analysis(gray_img, dct_title.c_str(), plot, dct_spec_path.c_str());

            // 4.2: Truncation Analysis (25% and 6.25%)
            double percentages[] = {0.25, 0.0625};
            std::string labels[] = {"quarter", "sixteenth"};
            int w = gray_img->info_header.width_px;
            int h = abs(gray_img->info_header.height_px);

            for (int i = 0; i < 2; i++) {
                // Setup Truncation Filters
                TruncationFilter* tf_DFT = (TruncationFilter*)create_filter(FILTER_DOMAIN_DFT, w, h, rect_truncation_logic, sizeof(TruncationFilter));
                tf_DFT->percent = percentages[i];

                TruncationFilter* tf_DCT = (TruncationFilter*)create_filter(FILTER_DOMAIN_DCT, w, h, rect_truncation_logic, sizeof(TruncationFilter));
                tf_DCT->percent = percentages[i];

                // Reconstruct Images
                BMPImage* recon_dft = apply_frequency_filter_to_bmp(gray_img, (Filter*)tf_DFT);
                BMPImage* recon_dct = apply_frequency_filter_to_bmp(gray_img, (Filter*)tf_DCT);

                // Compute SNR
                double snr_dft = calculate_snr(gray_img, recon_dft);
                double snr_dct = calculate_snr(gray_img, recon_dct);
                printf("  [%s] Trunc: %.2f%% | DFT SNR: %.2f dB | DCT SNR: %.2f dB\n", img_name.c_str(), percentages[i]*100, snr_dft, snr_dct);

                // Organized Output Names
                std::string dft_out = path + "Problem4/DFT_recon/" + base + "_recon_" + labels[i] + ".bmp";
                std::string dct_out = path + "Problem4/DCT_recon/" + base + "_recon_" + labels[i] + ".bmp";

                writeBMP(dft_out.c_str(), recon_dft);
                writeBMP(dct_out.c_str(), recon_dct);

                // Cleanup Truncation
                destroy_filter((Filter*)tf_DFT); destroy_filter((Filter*)tf_DCT);
                freeBMPImage(recon_dft); freeBMPImage(recon_dct);
            }

            freeBMPImage(color_img);
            freeBMPImage(gray_img);
        }

        std::cout << "\nAssignment Completed." << std::endl;
        return 0;
    }
