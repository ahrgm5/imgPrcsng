#include <iostream>
#include <vector>
#include <string>
#include <math.h>


#include "filters.hpp"
#include "img_manip.hpp"
#include "bmp_handling.hpp"
#include "histogram_utils.hpp"
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
        // Sharpening using 3x3 Laplacian (k=1.5)
        BMPImages leaf_lap = sharpen_laplacian(leaf, 2.5f, 3, 3);
        writeBMP((path+"/Problem2/leaf_edges.bmp").c_str(), leaf_lap.images[0]);
        writeBMP((path+"/Problem2/leaf_sharpened_laplacian.bmp").c_str(), leaf_lap.images[1]);

        // Sharpening using 3x3 Blurring (k=1.5)
        BMPImages leaf_unsharp = unsharp_masking(leaf, 2.5f, 3, 3);
        writeBMP((path +"/Problem2/leaf_blurred.bmp").c_str(), leaf_unsharp.images[0]);
        writeBMP((path +"/Problem2/leaf_sharpened_unsharp_masking.bmp").c_str(), leaf_unsharp.images[1]);

        freeBMPImage(leaf);
        freeBMPImages(leaf_lap);
        freeBMPImages(leaf_unsharp);
    }



    // ==========================================================
        // TASK 3: High-Frequency Emphasis
        // ==========================================================
        std::cout << "\n--- Task 3: High-Frequency Emphasis (hex.bmp) ---" << std::endl;


        // FIX: Read a new image into memory instead of using the freed 'leaf' pointer
        BMPImage* test_img = readBMP((src_images + "/leaf/leaf.bmp").c_str());
        BMPImage* gray_test_img = extract_channel_info(test_img, 3, 1);

    	writeBMP((path + "/Problem3/test.bmp").c_str(), test_img);
    	writeBMP((path + "/Problem3/gray_test.bmp").c_str(), gray_test_img);

// Order 1
        if(test_img && gray_test_img) {


            printf("%i Bytes per pixel \n", (int)test_img->info_header.bits_per_pixel);

            int w = test_img->info_header.width_px;
            int h = abs(test_img->info_header.height_px);

            // FIX: Use a sensible cutoff frequency, like 1/4th of the shortest dimension
            double f_c = std::min(w, h) /150.0;

            // FIX: Assign lpf_logic to the low-pass filter
            lpFilter* lp = (lpFilter*)create_filter(w, h, lpf_logic, sizeof(lpFilter));
            hpFilter* hp = (hpFilter*)create_filter(w, h, hpf_logic, sizeof(hpFilter));

            lp->cutoff = f_c;
            hp->cutoff = f_c;

            // FIX: Pass the correct filter pointer to the correct output variable
            BMPImage* lp_filtered_img = apply_frequency_filter_to_bmp(test_img, (Filter*)lp);
            BMPImage* gray_lp_filtered_img = apply_frequency_filter_to_bmp(gray_test_img, (Filter*)lp);

            BMPImage* hp_filtered_img = apply_frequency_filter_to_bmp(test_img, (Filter*)hp);
            BMPImage* gray_hp_filtered_img = apply_frequency_filter_to_bmp(gray_test_img, (Filter*)hp);

            BMPImage* final_hp_hex = apply_histogram_equalization(hp_filtered_img);
            BMPImage* final_gray_hp_hex = apply_histogram_equalization(gray_hp_filtered_img);



            writeBMP((path + "/Problem3/order1/test_lpf.bmp").c_str(), lp_filtered_img);
            writeBMP((path + "/Problem3/order1/gray_test_lpf.bmp").c_str(), gray_lp_filtered_img);

            writeBMP((path + "/Problem3/order1/test_hpf.bmp").c_str(), hp_filtered_img);
            writeBMP((path + "/Problem3/order1/gray_test_hpf.bmp").c_str(), gray_hp_filtered_img);

            writeBMP((path + "/Problem3/order1/test_equalized.bmp").c_str(), final_hp_hex);
            writeBMP((path + "/Problem3/order1/gray_test_equalized.bmp").c_str(), final_gray_hp_hex);

            freeBMPImage(lp_filtered_img);
            freeBMPImage(hp_filtered_img);
            freeBMPImage(gray_lp_filtered_img);
            freeBMPImage(gray_hp_filtered_img);
            freeBMPImage(final_gray_hp_hex);
            freeBMPImage(final_hp_hex);



// order 2


                        // FIX: Pass the correct filter pointer to the correct output variable

           BMPImage* eq_color = apply_histogram_equalization(test_img);
           BMPImage* eq_gray = apply_histogram_equalization(gray_test_img);


           BMPImage* eq_color_lpf = apply_frequency_filter_to_bmp(test_img, (Filter*)lp);
           BMPImage* eq_gray_lpf = apply_frequency_filter_to_bmp(gray_test_img, (Filter*)lp);

           BMPImage* eq_color_hpf = apply_frequency_filter_to_bmp(eq_color, (Filter*)hp);
           BMPImage* eq_gray_hpf = apply_frequency_filter_to_bmp(eq_gray, (Filter*)hp);

           freeBMPImage(test_img); // Safely free test_img here
           freeBMPImage(gray_test_img); // Safely free gray_test_img here

           writeBMP((path + "/Problem3/order2/test_equalized.bmp").c_str(), eq_color);
           writeBMP((path + "/Problem3/order2/gray_test_equalized.bmp").c_str(), eq_gray);

           writeBMP((path + "/Problem3/order2/test_lpf.bmp").c_str(), eq_color_lpf);
           writeBMP((path + "/Problem3/order2/gray_test_lpf.bmp").c_str(), eq_gray_lpf);

           writeBMP((path + "/Problem3/order2/test_hpf.bmp").c_str(), eq_color_hpf);
           writeBMP((path + "/Problem3/order2/gray_test_hpf.bmp").c_str(), eq_gray_hpf);


           destroy_filter((Filter*)lp);
           destroy_filter((Filter*)hp);

           freeBMPImage(eq_color);
           freeBMPImage(eq_gray);

           freeBMPImage(eq_color_hpf);
           freeBMPImage(eq_gray_hpf);

      }
        else {
            printf("Error while reading input file\n");
        }



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
        std::string names[] = {(path +"/Problem4/recon_quarter.bmp").c_str(), (path + "/Problem4/recon_sixteneenth.bmp").c_str()};

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

