#include "histogram_utils.hpp"
#include "bmp_handling.hpp"
#include "hsi_handling.hpp"
#include <vector>
#include <cmath>
#include <string>
#include <iostream>
#include <cstdio>
#include <algorithm>

ImageStats calculate_stats_generic(BMPImage* img) {
    ImageStats stats = {0}; // Zero-initialize all fields
    if (!img) return stats;

    int w = img->info_header.width_px;
    int h = abs(img->info_header.height_px);
    int n = w * h;
    stats.num_pixels = n;
    stats.min = 255.0;
    stats.max = 0.0;

    std::vector<float> intensities;
    intensities.reserve(n);

    // 1. Extract Intensity data based on BPP
    if (img->info_header.bits_per_pixel == 24) {
        HSIImage* hsi = convert_bmp_to_hsi(img);
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                float val = hsi->pixels[i][j].i * 255.0f;
                intensities.push_back(val);
            }
        }
        freeHSIImage(hsi);
    } else {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                intensities.push_back((float)img->pixels[i][j]);
            }
        }
    }

    // 2. Calculate PDF, CDF, Min, Max, and Mean
    double sum = 0.0;
    for (float val : intensities) {
        int idx = (int)std::clamp(val, 0.0f, 255.0f);
        stats.counts[idx]++;
        sum += val;
        if (val < stats.min) stats.min = val;
        if (val > stats.max) stats.max = val;
    }

    stats.mean = sum / n;

    // 3. Calculate Std Dev
    double var_sum = 0.0;
    for (float val : intensities) {
        var_sum += std::pow(val - stats.mean, 2);
    }
    stats.std_dev = std::sqrt(var_sum / n);

    // 4. Fill PDF/CDF
    float cumulative = 0;
    for (int i = 0; i < 256; i++) {
        stats.pdf[i] = stats.counts[i] / n;
        cumulative += stats.pdf[i];
        stats.cdf[i] = cumulative;
    }

    return stats;
}

BMPImage* apply_histogram_equalization(BMPImage* input) {
    if (!input) return nullptr;

    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);
    int bpp = input->info_header.bits_per_pixel;

    // Create the output image
    BMPImage* output = createEmptyBMP(w, h, bpp);

    if (bpp == 8) {
        // --- 8-bit Grayscale Logic ---
        std::vector<float> data(w * h);
        for(int i = 0; i < h; i++)
            for(int j = 0; j < w; j++)
                data[i * w + j] = input->pixels[i][j] / 255.0f;

        ImageStats stats = calculate_stats_generic(input);

        for(int i = 0; i < h; i++)
            for(int j = 0; j < w; j++)
                output->pixels[i][j] = (uint8_t)(stats.cdf[input->pixels[i][j]] * 255.0f);

        plot_transformation_curve(stats, "Grayscale Transformation Curve");

    } else if (bpp == 24) {
        // --- 24-bit Color Logic ---
        // 1. Convert to HSI to isolate the Intensity channel
        HSIImage* hsi = convert_bmp_to_hsi(input);

        // 2. Extract Intensity values into a flat buffer for stats
        std::vector<float> intensity_data(w * h);
        for(int i = 0; i < h; i++)
            for(int j = 0; j < w; j++)
                intensity_data[i * w + j] = hsi->pixels[i][j].i; // i is already 0.0-1.0

        // 3. Calculate stats on intensity
        ImageStats stats = calculate_stats_generic(input);

        // 4. Equalize the Intensity channel
        for(int i = 0; i < h; i++) {
            for(int j = 0; j < w; j++) {
                int idx = (int)(hsi->pixels[i][j].i * 255.0f);
                hsi->pixels[i][j].i = stats.cdf[idx]; // Apply CDF mapping
            }
        }

        // 5. Convert back to RGB and clean up
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);

        plot_transformation_curve(stats, "Color Image Intensity Transformation Curve");
    }

    return output;
}

void plot_transformation_curve(const ImageStats& stats, const std::string& title) {
    FILE* gp = popen("gnuplot -persistent", "w");
    if (!gp) return;
    fprintf(gp, "set title '%s'\nset xlabel 'Original Intensity (u)'\nset ylabel 'New Intensity (v)'\n", title.c_str());
    fprintf(gp, "plot '-' with lines lw 2 title 'T(u)'\n");
    for (int i = 0; i < 256; i++) fprintf(gp, "%d %f\n", i, stats.cdf[i] * 255.0f);
    fprintf(gp, "e\n"); pclose(gp);
}
