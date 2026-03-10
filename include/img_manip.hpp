// img_manip.hpp
#ifndef IMG_MANIP_HPP
#define IMG_MANIP_HPP

#include "bmp_handling.hpp"
#include <string>


BMPImage* unsharp_masking(BMPImage* input, float k);
BMPImage* sharpen_laplacian(BMPImage* input);

// Combines two images: out = img1 + (factor * img2)
BMPImage* combine_images(BMPImage* img1, BMPImage* img2, float factor);

double calculate_snr(BMPImage* orig, BMPImage* processed);
void run_fft_analysis_centered(BMPImage* input, const std::string& title);
void plot_spectrum_gnuplot(double* mag_data, int w, int h, const std::string& title);

#endif
