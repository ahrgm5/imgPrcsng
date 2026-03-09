#include <iostream>
#include <vector>
#include <string>

#include "bmp_handling.hpp"
#include "img_manip.hpp"
#include "histogram_utils.hpp"
#include "filters.hpp"
#include "spatial_kernels.hpp"

int main() {
    std::cout << "Starting HW#2 Image Processing Assignment..." << std::endl;

    std::string path = "/home/ahrgm/Projects/imgPrcsng/Course/HW/HW2/";

    std::string src_images = "/home/ahrgm/Projects/imgPrcsng/Course/images/";

    // ==========================================================
    // TASK 2.1: Histogram Equalization
    // ==========================================================
    std::cout << "\n--- Task 2.1: Histogram Equalization (pout.bmp) ---" << std::endl;
    BMPImage* pout = readBMP((src_images +"pout.bmp").c_str());
    if (pout) {
        // Implementation of histogram equalization
        BMPImage* pout_eq = apply_histogram_equalization(pout);
        writeBMP((path + "/Problem2/pout_equalized.bmp").c_str(), pout_eq);

        // plot_transformation_curve() is called inside apply_histogram_equalization
        // to satisfy Task 2.1.1 (u vs v plot)

        freeBMPImage(pout);
        freeBMPImage(pout_eq);
    }

    // ==========================================================
    // TASK 2.2: Spatial Sharpening (leaf.bmp) [cite: 51]
    // ==========================================================
    std::cout << "\n--- Task 2.2: Spatial Sharpening (leaf.bmp) ---" << std::endl;
    BMPImage* leaf = readBMP((src_images +"/leaf/leaf.bmp").c_str());
    if (leaf) {
        // Sharpening using Laplacian (k=1.0)
        BMPImage* leaf_lap = sharpen_laplacian(leaf);
        writeBMP((path+"/Problem2/leaf_sharpened_laplacian.bmp").c_str(), leaf_lap);

        // Sharpening using Unsharp Masking (k=1.0)
        BMPImage* leaf_unsharp = unsharp_masking(leaf, 0.5f);
        writeBMP((path +"/Problem2/leaf_sharpened_unsharp.bmp").c_str(), leaf_unsharp);

        freeBMPImage(leaf);
        freeBMPImage(leaf_lap);
        freeBMPImage(leaf_unsharp);
    }




/*
    // ==========================================================
        // TASK 3: High-Frequency Emphasis
        // ==========================================================
        std::cout << "\n--- Task 3: High-Frequency Emphasis (hex.bmp) ---" << std::endl;
        BMPImage* hex_img = readBMP((src_images + "hex.bmp").c_str());
        if (hex_img) {
            int w = hex_img->info_header.width_px;
            int h = abs(hex_img->info_header.height_px);

            Filter* hp = create_filter(w, h, hpf_logic, sizeof(hpFilter));
            ((hpFilter*)hp)->cutoff = 30.0;

            // Use the helper function to ensure data is copied and clamped correctly
            BMPImage* filtered_img = apply_frequency_filter_to_bmp(hex_img, hp);

            // Perform Histogram Equalization on result
            BMPImage* final_xray = apply_histogram_equalization(filtered_img);
            writeBMP((path + "/Problem3/xray_hfe_equalized.bmp").c_str(), final_xray);

            destroy_filter(hp);
            freeBMPImage(hex_img);
            freeBMPImage(filtered_img);
            freeBMPImage(final_xray);
        }
*/

    // ==========================================================
    // TASK 4: DFT Analysis and Truncation
    // ==========================================================
    std::cout << "\n--- Task 4: DFT Analysis and Truncation (baboon.bmp) ---" << std::endl;
    BMPImage* baboon = readBMP((src_images +"baboon.bmp").c_str());
    if (baboon) {
        // 4.1 Plot 2D Log Magnitude
        run_fft_analysis_centered(baboon, "2D DFT Magnitude - Baboon");

        int w = baboon->info_header.width_px;
        int h = abs(baboon->info_header.height_px);

        // 4.2 Truncation (25% and 6.25%)
        double percentages[] = {0.25, 0.0625};
        std::string names[] = {"recon_25.bmp", "recon_6_25.bmp"};

        for (int i = 0; i < 2; i++) {
            TruncationFilter* tf = (TruncationFilter*)create_filter(w, h, rect_truncation_logic, sizeof(TruncationFilter));
            tf->percent = percentages[i];

            // Reconstruct image
            BMPImage* recon = apply_frequency_filter_to_bmp(baboon, (Filter*)tf);

            // Compute SNR
            double snr = calculate_snr(baboon, recon);
            std::cout << "SNR for " << (percentages[i] * 100) << "% truncation: " << snr << " dB" << std::endl;

            writeBMP(names[i].c_str(), recon);
            destroy_filter((Filter*)tf);
            freeBMPImage(recon);
        }
        freeBMPImage(baboon);
    }

    std::cout << "\nAssignment Tasks Completed Successfully." << std::endl;
    return 0;
}
