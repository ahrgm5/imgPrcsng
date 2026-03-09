#include "histogram_utils.hpp"
#include <iostream>
#include <cstdio>
#include <algorithm>

ImageStats calculate_stats_generic(float* intensity_data, int n) {
    ImageStats stats = {0};
    stats.num_pixels = n;
    for (int i = 0; i < n; i++) {
        int val = (int)std::clamp(intensity_data[i] * 255.0f, 0.0f, 255.0f);
        stats.counts[val]++;
    }
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
    int w = input->info_header.width_px, h = abs(input->info_header.height_px);
    BMPImage* output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);

    if (input->info_header.bits_per_pixel == 8) {
        std::vector<float> data(w * h);
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) data[i*w+j] = input->pixels[i][j] / 255.0f;
        ImageStats stats = calculate_stats_generic(data.data(), w * h);
        for(int i=0; i<h; i++) for(int j=0; j<w; j++)
            output->pixels[i][j] = (uint8_t)(stats.cdf[input->pixels[i][j]] * 255.0f);

        // Plot the transformation curve u vs v
        plot_transformation_curve(stats, "Transformation Curve (u vs v)");
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
