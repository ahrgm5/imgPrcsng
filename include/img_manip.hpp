#ifndef IMG_MANIP_HPP
#define IMG_MANIP_HPP

#include "bmp_handling.hpp"
#include <fftw3.h>
#include <string>
#include <vector>

// Problem 2: Spatial
BMPImage* unsharp_masking(BMPImage* input, float k);
BMPImage* histogram_equalization_custom(BMPImage* input);

void apply_kernel_hsi(HSIPixel** hsi, int w, int h, const float kernel[3][3]);
BMPImage* unsharp_masking(BMPImage* input, float k, bool isColor);
BMPImage* sharpen_laplacian(BMPImage* input, float k, bool isColor);


void plot_cdf_gnuplot(const std::vector<float>& cdf, const std::string& title);
// Problem 4: Frequency Domain (Integrated Shift)
void run_fft_analysis_centered(BMPImage* input, const std::string& title);
void plot_spectrum_gnuplot(double* mag_data, int w, int h, const std::string& title);

#endif
