// img_manip.hpp
#ifndef IMG_MANIP_HPP
#define IMG_MANIP_HPP

#include <bmp.hpp>
#include <vector>
#include <string>


typedef struct {
    float counts[256];
    float pdf[256];
    float cdf[256];
    int num_pixels;
    // New statistical fields
    double mean;
    double std_dev;
    double min;
    double max;
} ImageStats;

ImageStats calculate_stats_generic(BMPImage* img);

BMPImage* apply_histogram_stretching(BMPImage* input);
BMPImage* apply_gamma_correction(BMPImage* input, float gamma);
BMPImage* preprocess_for_edges(BMPImage* input, float gamma);
BMPImage* apply_histogram_equalization(BMPImage* input, bool plot, const std::string& title);
void plot_transformation_curve(const ImageStats& stats, const std::string& title);


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
static void shift_quadrants(double* src, double* dst, int w, int h);
void run_dft_analysis(BMPImage* img, const std::string& title, bool plot, const std::string& savePath);
void run_dct_analysis(BMPImage* img, const std::string& title, bool plot, const std::string& savePath);
void save_spectrum_plot(double* data, int w, int h, const std::string& title, const std::string& savePath, bool plot);
void plot_spectrum_gnuplot(double* mag_data, int w, int h, const std::string& title);

#endif
