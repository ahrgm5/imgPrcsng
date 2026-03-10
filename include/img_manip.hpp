// img_manip.hpp
#ifndef IMG_MANIP_HPP
#define IMG_MANIP_HPP

#include "bmp_handling.hpp"
#include <string>


typedef struct {
    BMPImage** images;
    int count;
} BMPImages;

void freeBMPImages(BMPImages res);

// Laplacian: returns [0] = Edges, [1] = Sharpened
BMPImages sharpen_laplacian(BMPImage* input, float k, int kw, int kh);
// Averaging: returns [0] = Blurred, [1] = Details (Original - Blurred)
BMPImages unsharp_masking(BMPImage* input, float k, int kw, int kh);
// Existing utility functions
BMPImage* combine_images(BMPImage* img1, BMPImage* img2, float factor);

// Combines two images: out = img1 + (factor * img2)
BMPImage* combine_images(BMPImage* img1, BMPImage* img2, float factor);
double calculate_mse(BMPImage* orig, BMPImage* processed);
double calculate_snr(BMPImage* orig, BMPImage* processed);
void run_fft_analysis_centered(BMPImage* input, const std::string& title);
void plot_spectrum_gnuplot(double* mag_data, int w, int h, const std::string& title);

#endif
