#ifndef HISTOGRAM_UTILS_HPP
#define HISTOGRAM_UTILS_HPP

#include "bmp_handling.hpp"
#include "hsi_handling.hpp"
#include <vector>
#include <string>

typedef struct {
    float counts[256];
    float pdf[256];
    float cdf[256];
    int num_pixels;
} ImageStats;

ImageStats calculate_stats_generic(float* intensity_data, int n);
BMPImage* apply_histogram_equalization(BMPImage* input);
void plot_transformation_curve(const ImageStats& stats, const std::string& title);

#endif
