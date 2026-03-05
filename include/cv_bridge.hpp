#ifndef CV_BRIDGE_HPP
#define CV_BRIDGE_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include "bmp_handling.hpp"

cv::Mat convertBmpToMat(BMPImage* img);
cv::Mat resize_cv(const cv::Mat& input, int w_out, int h_out, bool use_bilinear);
cv::Mat quantize_cv(const cv::Mat& input, int bits);
double calculateMSE(const cv::Mat& img1, const cv::Mat& img2);

#endif
