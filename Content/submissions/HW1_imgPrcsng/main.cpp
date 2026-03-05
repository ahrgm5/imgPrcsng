#include <iostream>
#include <vector>
#include "bmp_handling.hpp"
#include "cv_bridge.hpp"
#include <opencv2/opencv.hpp>

using namespace std;

int main() {
    // ==========================================
    // INITIALIZATION
    // ==========================================
    string basePath = "/home/ahrgm/Projects/imgPrcsng/Content/Course_Materials/HW/HW1/";
    string imgPath256 = "/home/ahrgm/Projects/imgPrcsng/src/images/lenna/lenna256.bmp";
    string imgPathColor = "/home/ahrgm/Projects/imgPrcsng/src/images/lenna/lenna512_color.bmp";
    
    
    BMPImage* img = readBMP(imgPath256.c_str());
    BMPImage* color_img = readBMP(imgPathColor.c_str());
    // Convert to OpenCV Mat for baseline comparison
    cv::Mat cv_img = convertBmpToMat(img);


    // ==========================================
    // PROBLEM 1: Resizing
    // ==========================================
    cout << "--- Problem 1: Resizing Comparison ---" << endl;

    // --- A. Bilinear Interpolation ---
    // Custom
    BMPImage* bl_img = resize_bilinear(img, 64, 64);
    BMPImage* restored_bl_img = resize_bilinear(bl_img, 256, 256);
    
    writeBMP((basePath + "Problem1/imageshrunk_bilinear.bmp").c_str(), bl_img);    
    writeBMP((basePath + "Problem1/imagerestored_bilinear.bmp").c_str(), restored_bl_img);

    // OpenCV
    cv::Mat shrunk_cv_bl = resize_cv(cv_img, 64, 64, true);
    cv::Mat restored_cv_bl = resize_cv(shrunk_cv_bl, 256, 256, true);
    
    cv::imwrite(basePath + "Problem1/cv_imageshrunk_bilinear.bmp", shrunk_cv_bl);
    cv::imwrite(basePath + "Problem1/cv_imagerestored_bilinear.bmp", restored_cv_bl);

    // Compare
    cv::Mat restored_custom_mat_bl = convertBmpToMat(restored_bl_img);
    double mse_bilinear = calculateMSE(restored_custom_mat_bl, restored_cv_bl);
    cout << "MSE for Bilinear Resizing: " << mse_bilinear << endl;



    // --- B. Nearest Neighbor Interpolation ---
    // Custom
    BMPImage* nn_img = resize_nearest(img, 64, 64);    
    BMPImage* restored_nn_img = resize_nearest(nn_img, 256, 256);
    
    writeBMP((basePath + "Problem1/imageshrunk_nn.bmp").c_str(), nn_img); 
    writeBMP((basePath + "Problem1/imagerestored_nn.bmp").c_str(), restored_nn_img);

    // OpenCV
    cv::Mat shrunk_cv_nn = resize_cv(cv_img, 64, 64, false);
    cv::Mat restored_cv_nn = resize_cv(shrunk_cv_nn, 256, 256, false);
    
    cv::imwrite(basePath + "Problem1/cv_imageshrunk_nn.bmp", shrunk_cv_nn);
    cv::imwrite(basePath + "Problem1/cv_imagerestored_nn.bmp", restored_cv_nn);

    // Compare
    cv::Mat restored_custom_mat_nn = convertBmpToMat(restored_nn_img);
    cout << "MSE for Nearest Neighbor Resizing: " << calculateMSE(restored_custom_mat_nn, restored_cv_nn) << endl;
    
    
    freeBMPImage(bl_img);
    freeBMPImage(restored_bl_img);
    freeBMPImage(nn_img);
    freeBMPImage(restored_nn_img);


    // ==========================================
    // PROBLEM 2: Quantization
    // ==========================================
    cout << "\n--- Problem 2: Quantization Comparison ---" << endl;
    
    int bits = 4; 
    
    // Custom
    BMPImage* quantized = linear_quantization(img, bits);
    writeBMP((basePath + "Problem2/output_quantized.bmp").c_str(), quantized);
    plot_histogram_gnuplot(quantized, "Histogram:Uniform Quantization");
    
    // OpenCV
    cv::Mat quantized_cv = quantize_cv(cv_img, bits);
    cv::imwrite(basePath + "Problem2/output_quantized_cv.bmp", quantized_cv);
    
    // Compare
    cv::Mat quantized_custom_mat = convertBmpToMat(quantized);
    double mse_quant = calculateMSE(quantized_custom_mat, quantized_cv);
    cout << "MSE for " << bits << "-bit Quantization: " << mse_quant << endl;

    freeBMPImage(img);         // Free original image
    freeBMPImage(quantized);   // Free custom quantized image


    // ==========================================
    // PROBLEM 3: Channel Extraction
    // ==========================================
    cout << "\n--- Problem 3: Channel Extraction ---" << endl;


    if (!color_img) {
        cout << "Failed to load color image: " << imgPathColor << endl;
        return -1;
    }

    // Extraction mode & byte indicators
    int blue = 0, green = 1, red = 2, full = 3, grey = 1, clr = 3;

    // --- A. Custom Extraction (Magnitudes & Colors) ---
    BMPImage* blue_mag  = extract_channel_info(color_img, blue, grey);
    BMPImage* green_mag = extract_channel_info(color_img, green, grey);
    BMPImage* red_mag   = extract_channel_info(color_img, red, grey);
    BMPImage* grey_full = extract_channel_info(color_img, full, grey);
    
    writeBMP((basePath + "Problem3/magnitude_blue.bmp").c_str(), blue_mag);    
    writeBMP((basePath + "Problem3/magnitude_green.bmp").c_str(), green_mag);
    writeBMP((basePath + "Problem3/magnitude_red.bmp").c_str(), red_mag);
    writeBMP((basePath + "Problem3/grayscale_total.bmp").c_str(), grey_full);
    
    freeBMPImage(blue_mag); freeBMPImage(green_mag); freeBMPImage(red_mag); freeBMPImage(grey_full);
  
    BMPImage* blu_mag  = extract_channel_info(color_img, blue, clr);
    BMPImage* grn_mag  = extract_channel_info(color_img, green, clr);
    BMPImage* rd_mag   = extract_channel_info(color_img, red, clr);
    BMPImage* color    = extract_channel_info(color_img, full, clr);
  
    writeBMP((basePath + "Problem3/color_blue.bmp").c_str(), blu_mag);
    writeBMP((basePath + "Problem3/color_green.bmp").c_str(), grn_mag);
    writeBMP((basePath + "Problem3/color_red.bmp").c_str(), rd_mag);
    writeBMP((basePath + "Problem3/color.bmp").c_str(), color); 
    
    freeBMPImage(blu_mag); freeBMPImage(grn_mag); freeBMPImage(rd_mag); freeBMPImage(color);


    // --- B. OpenCV Extraction (Magnitudes & Colors) ---
    cv::Mat cv_color = cv::imread(imgPathColor, cv::IMREAD_COLOR);
    if (!cv_color.empty()) {
        vector<cv::Mat> channels(3);
        cv::split(cv_color, channels); // OpenCV splits into B, G, R

        // Magnitude Equivalents (1-channel)
        cv::imwrite(basePath + "Problem3/cv_magnitude_blue.bmp", channels[0]);
        cv::imwrite(basePath + "Problem3/cv_magnitude_green.bmp", channels[1]);
        cv::imwrite(basePath + "Problem3/cv_magnitude_red.bmp", channels[2]);
        
        cv::Mat cv_gray_full;
        cv::cvtColor(cv_color, cv_gray_full, cv::COLOR_BGR2GRAY);
        cv::imwrite(basePath + "Problem3/cv_grayscale_total.bmp", cv_gray_full);

        // Color Equivalents (3-channel)
        cv::Mat black = cv::Mat::zeros(cv_color.size(), CV_8UC1);
        cv::Mat b_clr, g_clr, r_clr;
        
        cv::merge(vector<cv::Mat>{channels[0], black, black}, b_clr);
        cv::merge(vector<cv::Mat>{black, channels[1], black}, g_clr);
        cv::merge(vector<cv::Mat>{black, black, channels[2]}, r_clr);

        cv::imwrite(basePath + "Problem3/cv_color_blue.bmp", b_clr);
        cv::imwrite(basePath + "Problem3/cv_color_green.bmp", g_clr);
        cv::imwrite(basePath + "Problem3/cv_color_red.bmp", r_clr);
        cv::imwrite(basePath + "Problem3/cv_color.bmp", cv_color); // Original for completeness
    }

    freeBMPImage(color_img);

    cout << "Processing complete! All images have been saved." << endl;

    return 0;
}
