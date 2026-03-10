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
    // New statistical fields
    double mean;
    double std_dev;
    double min;
    double max;
} ImageStats;

ImageStats calculate_stats_generic(BMPImage* img);
BMPImage* apply_histogram_equalization(BMPImage* input);
void plot_transformation_curve(const ImageStats& stats, const std::string& title);

#endif
