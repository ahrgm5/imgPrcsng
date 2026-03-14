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
    BMPImage* pout = readBMP((src_images + "pout.bmp").c_str());
    if (pout) {
        BMPImage* pout_eq = apply_histogram_equalization(pout, plot);
        writeBMP((path + "Problem2/2.1/pout_equalized.bmp").c_str(), pout_eq);

        BMPImage* pout_stretched = apply_histogram_stretching(pout, plot);
        writeBMP((path + "Problem2/2.1/pout_stretched.bmp").c_str(), pout_stretched);

        freeBMPImage(pout);
        freeBMPImage(pout_eq);
    }

    // ==========================================================
    // TASK 2.2: Spatial Sharpening (leaf.bmp)
    // ==========================================================
    std::cout << "\n--- Task 2.2: Spatial Sharpening ---" << std::endl;
    // Task requires sharpening leaf.jpg using spatial processing techniques [cite: 14]
    BMPImages leaf1, leaf2 ;
    leaf1.count = 2; // Color & Grayscale
    leaf2.count = 3; // Color, Resized & grayscale

    leaf1.images = (BMPImage**)malloc(sizeof(BMPImage*) *leaf1.count);
    leaf2.images = (BMPImage**)malloc(sizeof(BMPImage*) *leaf2.count);



    leaf1.images[0] = readBMP((src_images + "leaf.bmp").c_str());
    leaf1.images[1] = extract_channel_info(leaf1.images[0], 3, 1);
    writeBMP((path + "Problem2/2.2/alpha.bmp").c_str(), leaf1.images[0]);

    int width = leaf1.images[0]->info_header.width_px, height = leaf1.images[0]->info_header.height_px;

    leaf2.images[0] = readBMP((src_images + "leaf2.bmp").c_str());
    leaf2.images[1] = resize_bilinear(leaf2.images[0], width, height);
    leaf2.images[2] = extract_channel_info(leaf2.images[1], 3, 1);



    if (leaf1.images) {
//Order 1
    	BMPImage* prepped1 = preprocess_for_edges(leaf1.images[0], 2.0f, plot);
        BMPImage* prepped2 = apply_histogram_equalization(leaf1.images[0], plot);
        writeBMP((path + "Problem2/2.2/alpha_stretched.bmp").c_str(), prepped1);
        writeBMP((path + "Problem2/2.2/alpha_equalized.bmp").c_str(), prepped2);

        // Laplacian Sharpening
        BMPImages leaf_lap = sharpen_laplacian(leaf1.images[0], 1.0f, 3, 3);
        BMPImages leaf_lap1 = sharpen_laplacian(prepped1, 1.0f, 3, 3);
        BMPImages leaf_lap2 = sharpen_laplacian(prepped2, 1.0f, 3, 3);
//Order 2
        BMPImage* lap_prepped1 = preprocess_for_edges(leaf_lap.images[1], 2.0f, plot);
        BMPImage* lap_prepped2 = apply_histogram_equalization(leaf_lap.images[1], plot);


        writeBMP((path + "Problem2/2.2/laplacian/alpha_stretched_order1.bmp").c_str(), prepped1);
        writeBMP((path + "Problem2/2.2/laplacian/alpha_equalized_order1.bmp").c_str(), prepped2);
        writeBMP((path + "Problem2/2.2/alpha_laplacian.bmp").c_str(), leaf_lap.images[1]);
        writeBMP((path + "Problem2/2.2/laplacian/alpha_stretched_order2.bmp").c_str(), leaf_lap1.images[1]);
        writeBMP((path + "Problem2/2.2/laplacian/alpha_equalized_order2.bmp").c_str(), leaf_lap2.images[1]);


        // Unsharp Masking
        BMPImages leaf_unsharp = unsharp_masking(leaf1.images[0], 1.0f, 3, 3);
        BMPImages leaf_unsharp1 = unsharp_masking(prepped1, 1.0f, 3, 3);
        BMPImages leaf_unsharp2 = unsharp_masking(prepped2, 1.0f, 3, 3);

        BMPImage* unsharp_prepped1 = preprocess_for_edges(leaf_unsharp.images[1], 2.0f, plot);
        BMPImage* unsharp_prepped2 = apply_histogram_equalization(leaf_unsharp.images[1], plot);


        writeBMP((path + "Problem2/2.2/unsharp/alpha_stretched_order1.bmp").c_str(), prepped1);
        writeBMP((path + "Problem2/2.2/unsharp/alpha_equalized_order1.bmp").c_str(), prepped2);
        writeBMP((path + "Problem2/2.2/alpha_unsharp.bmp").c_str(), leaf_unsharp.images[1]);
        writeBMP((path + "Problem2/2.2/unsharp/alpha_streched_order2.bmp").c_str(), leaf_unsharp1.images[1]);
        writeBMP((path + "Problem2/2.2/unsharp/alpha_equalized_order2.bmp").c_str(), leaf_unsharp2.images[1]);


        printf("Results before Histogram Techniques\n");
        double mse_lap = calculate_mse(leaf2.images[1], leaf_lap.images[1]);
        double snr_lap = calculate_snr(leaf1.images[0], leaf_lap.images[1]);

        double mse_unsharp = calculate_mse(leaf2.images[1], leaf_unsharp.images[1]);
        double snr_unsharp = calculate_snr(leaf1.images[0], leaf_unsharp.images[1]);

        printf(" Laplacian Kernel SNR w/ Original: %.2f dB  \n Laplacian Kernel MSE w/ Enhanced Image : %.2f  \n  \n", snr_unsharp ,mse_unsharp);
        printf(" Unsharp Masking Kernel SNR w/ Original: %.2f dB   \n Unsharp Masking Kernel MSE w/ Enhanced Image: %.2f  \n \n", snr_lap, mse_lap);



        printf("Results with Histogram Stretching with Gamma Correction\n");
        double mse_lap1 = calculate_mse(leaf2.images[2], leaf_lap1.images[1]);
        double snr_lap1 = calculate_snr(leaf1.images[1], leaf_lap1.images[1]);

        double mse_unsharp1 = calculate_mse(leaf2.images[2], leaf_unsharp1.images[1]);
        double snr_unsharp1 = calculate_snr(leaf1.images[1], leaf_unsharp1.images[1]);

        printf(" Laplacian Kernel SNR w/ Original: %.2f dB  \n Laplacian Kernel MSE w/ Enhanced Image : %.2f  \n  \n", snr_unsharp1 ,mse_unsharp1);
        printf(" Unsharp Masking Kernel SNR w/ Original: %.2f dB   \n Unsharp Masking Kernel MSE w/ Enhanced Image: %.2f  \n \n", snr_lap1, mse_lap1);




        printf("Results with Histogram Equalization\n");
        double mse_lap2 = calculate_mse(leaf2.images[2], leaf_lap2.images[1]);
        double snr_lap2 = calculate_snr(leaf1.images[1], leaf_lap2.images[1]);

        double mse_unsharp2 = calculate_mse(leaf2.images[2], leaf_unsharp2.images[1]);
        double snr_unsharp2 = calculate_snr(leaf1.images[1], leaf_unsharp2.images[1]);


        printf(" Laplacian Kernel SNR w/ Original: %.2f dB  \n Laplacian Kernel MSE w/ Enhanced Image : %.2f  \n  \n", snr_unsharp2 ,mse_unsharp2);
        printf(" Unsharp Masking Kernel SNR w/ Original: %.2f dB   \n Unsharp Masking Kernel MSE w/ Enhanced Image: %.2f  \n \n", snr_lap2, mse_lap2);




        freeBMPImages(leaf1);

        freeBMPImages(leaf2);
        freeBMPImage(prepped1);
        freeBMPImage(prepped2);
        freeBMPImages(leaf_lap1);
        freeBMPImages(leaf_lap2);
        freeBMPImages(leaf_unsharp1);
        freeBMPImages(leaf_unsharp2);
    }

    // ==========================================================
    // TASK 3: High-Frequency Emphasis Order Analysis
    // ==========================================================
    std::cout << "\n--- Task 3: High-Frequency Emphasis (Order Analysis) ---" << std::endl;
    // Task requires combining high-frequency emphasis and histogram equalization [cite: 19, 20]
    // Must test if the order of applying these two operations matters [cite: 25]
    BMPImage* test_img = readBMP((src_images + "leaf.bmp").c_str());
    BMPImage* gray_test_img = extract_channel_info(test_img, 3, 1); // Convert to grayscale for frequency processing

    if (test_img) {
        int w = test_img->info_header.width_px;
        int h = abs(test_img->info_header.height_px);
        hpFilter* hp = (hpFilter*)create_filter(FILTER_DOMAIN_DFT, w, h, hpf_logic, sizeof(hpFilter));
        hp->cutoff = std::min(w,h)/100;

        // Order 1: High-Frequency Emphasis Filter -> Histogram Equalization
        BMPImage* hpf_only = apply_frequency_filter_to_bmp(gray_test_img, (Filter*)hp);
        BMPImage* order1 = apply_histogram_equalization(hpf_only, plot);
        writeBMP((path + "Problem3/order1_hpf_then_histeq.bmp").c_str(), order1);

        // Order 2: Histogram Equalization -> High-Frequency Emphasis Filter
        BMPImage* eq_only = apply_histogram_equalization(gray_test_img, plot);
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

        std::vector<std::string> task4_imgs = { "leaf.bmp", "baboon.bmp", "monkeyking.bmp", "sunflower.bmp", "hex.bmp"};

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
