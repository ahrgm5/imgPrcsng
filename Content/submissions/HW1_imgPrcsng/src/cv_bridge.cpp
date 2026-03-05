#include "cv_bridge.hpp"
#include <iostream>
#include <cmath>

// --- 1. Conversion Utilities ---
cv::Mat convertBmpToMat(BMPImage* img) {
    if (!img || !img->pixels) return cv::Mat();

    int height = abs(img->info_header.height_px);
    int width = img->info_header.width_px;
    int bpp = img->info_header.bits_per_pixel;
    
    // Determine OpenCV type based on bits per pixel
    int type = (bpp == 24) ? CV_8UC3 : CV_8UC1;
    int bytes_per_pixel = (bpp <= 8) ? 1 : 3;

    cv::Mat outMat(height, width, type);

    // Copy memory row by row from your allocated 2D array into the cv::Mat
    for (int i = 0; i < height; i++) {
        std::memcpy(outMat.ptr(i), img->pixels[i], width * bytes_per_pixel);
    }

    return outMat;
}

// --- 2. OpenCV Operations ---
cv::Mat resize_cv(const cv::Mat& input, int w_out, int h_out, bool use_bilinear) {
    cv::Mat output;
    int interpolation = use_bilinear ? cv::INTER_LINEAR : cv::INTER_NEAREST;
    cv::resize(input, output, cv::Size(w_out, h_out), 0, 0, interpolation);
    return output;
}

cv::Mat quantize_cv(const cv::Mat& input, int bits) {
    int levels = std::pow(2, bits);
    if (levels < 2) return input.clone();

    float step = 255.0f / (levels - 1);
    
    cv::Mat result;
    input.convertTo(result, CV_32F);           
    result = result / step;                    
    
    for(int i = 0; i < result.rows; i++) {
        for(int j = 0; j < result.cols; j++) {
            result.at<float>(i,j) = std::round(result.at<float>(i,j));
        }
    }
    
    result = result * step;                    
    result.convertTo(result, CV_8U);           
    return result;
}

// --- 3. Error Analysis ---
double calculateMSE(const cv::Mat& img1, const cv::Mat& img2) {
    if (img1.empty() || img2.empty() || img1.size() != img2.size() || img1.type() != img2.type()) {
        std::cerr << "Error: Images must have the same dimensions and type for MSE." << std::endl;
        return -1.0;
    }

    cv::Mat diff;
    cv::absdiff(img1, img2, diff);      // |img1 - img2|
    diff.convertTo(diff, CV_32F);       // Convert to float to prevent overflow during squaring
    diff = diff.mul(diff);              // Square the differences

    cv::Scalar s = cv::sum(diff);       // Sum all squared errors
    double sse = s.val[0] + s.val[1] + s.val[2]; // Sum across channels (if color)
    
    return sse / (double)(img1.channels() * img1.total());
}
