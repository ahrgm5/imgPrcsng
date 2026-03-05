#include "img_manip.hpp"
#include <fftw3.h>
#include <iostream>
#include <cmath>
#include <algorithm>



 // Internal helper to apply a 3x3 kernel to a 2D float array (for HSI intensity)
void apply_kernel_hsi(HSIPixel** hsi, int w, int h, const float kernel[3][3]) {
    // Create a temporary copy of intensities to avoid using filtered pixels for the next calculation
    std::vector<std::vector<float>> temp_i(h, std::vector<float>(w));
    
    for (int i = 1; i < h - 1; i++) {
        for (int j = 1; j < w - 1; j++) {
            float sum = 0;
            for (int m = -1; m <= 1; m++) {
                for (int n = -1; n <= 1; n++) {
                    sum += hsi[i + m][j + n].i * kernel[m + 1][n + 1];
                }
            }
            temp_i[i][j] = sum;
        }
    }
    // Update the actual HSI buffer
    for (int i = 1; i < h - 1; i++) {
        for (int j = 1; j < w - 1; j++) {
            hsi[i][j].i = std::clamp(temp_i[i][j], 0.0f, 1.0f);
        }
    }
}

// --- Combined Laplacian Sharpening ---
BMPImage* sharpen_laplacian(BMPImage* input, float k, bool isColor) {
    int h = abs(input->info_header.height_px);
    int w = input->info_header.width_px;
    BMPImage* out = createEmptyBMP(w, h, isColor ? 24 : 8);

    if (isColor) {
        HSIPixel** hsi = convert_rgb_to_hsi(input);
        for (int i = 1; i < h - 1; i++) {
            for (int j = 1; j < w - 1; j++) {
                float lap = (4 * hsi[i][j].i) - (hsi[i-1][j].i + hsi[i+1][j].i + hsi[i][j-1].i + hsi[i][j+1].i);
                hsi[i][j].i = std::clamp(hsi[i][j].i + (k * lap), 0.0f, 1.0f);
            }
        }
        // Convert back to RGB pixels in 'out'
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                uint8_t r, g, b;
                hsi_to_rgb_pixel(hsi[i][j], r, g, b);
                out->pixels[i][j*3] = b; out->pixels[i][j*3+1] = g; out->pixels[i][j*3+2] = r;
            }
        }
        freeHSIBuffer(hsi, h);
    } else {
        // Grayscale 8-bit logic
        for (int i = 1; i < h - 1; i++) {
            for (int j = 1; j < w - 1; j++) {
                int lap = (4 * input->pixels[i][j]) - (input->pixels[i-1][j] + input->pixels[i+1][j] + input->pixels[i][j-1] + input->pixels[i][j+1]);
                out->pixels[i][j] = (uint8_t)std::clamp((int)input->pixels[i][j] + (int)(k * lap), 0, 255);
            }
        }
    }
    return out;
}

// --- Combined Unsharp Masking ---
BMPImage* unsharp_masking(BMPImage* input, float k, bool isColor) {
    int h = abs(input->info_header.height_px);
    int w = input->info_header.width_px;
    BMPImage* out = createEmptyBMP(w, h, isColor ? 24 : 8);

    if (isColor) {
        HSIPixel** hsi = convert_rgb_to_hsi(input);
        // 1. Create blurred intensity
        std::vector<std::vector<float>> blurred(h, std::vector<float>(w));
        for (int i = 1; i < h - 1; i++) {
            for (int j = 1; j < w - 1; j++) {
                float avg = 0;
                for (int m = -1; m <= 1; m++) 
                    for (int n = -1; n <= 1; n++) avg += hsi[i+m][j+n].i;
                blurred[i][j] = avg / 9.0f;
            }
        }
        // 2. Apply Mask: I_new = I_orig + k * (I_orig - I_blur)
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                float mask = hsi[i][j].i - blurred[i][j];
                hsi[i][j].i = std::clamp(hsi[i][j].i + (k * mask), 0.0f, 1.0f);
                uint8_t r, g, b;
                hsi_to_rgb_pixel(hsi[i][j], r, g, b);
                out->pixels[i][j*3] = b; out->pixels[i][j*3+1] = g; out->pixels[i][j*3+2] = r;
            }
        }
        freeHSIBuffer(hsi, h);
    } else {
        // Grayscale 8-bit logic... (similar to previous unsharp masking implementation)
        // [Previous grayscale unsharp logic here]
    }
    return out;
}












BMPImage* histogram_equalization_custom(BMPImage* input) {
    int h = abs(input->info_header.height_px);
    int w = input->info_header.width_px;
    int total = h * w;

// 1. Calculate Histogram
    int hist[256] = {0};
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            hist[input->pixels[i][j]]++;
        }
    }

    // 2. Calculate Normalized CDF (This is our transformation function T(r))
    std::vector<float> cdf(256, 0.0f);
    float sum = 0;
    for (int i = 0; i < 256; i++) {
        sum += hist[i];
        cdf[i] = sum / total;
    }

    // 3. Plot the CDF (Transformation function u vs v)
    plot_cdf_gnuplot(cdf, "Transformation Function (CDF) for pout.bmp");

    // 4. Create Equalized Image
    BMPImage* output = createEmptyBMP(w, h, 8);
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // v = round( (L-1) * CDF(u) )
            output->pixels[i][j] = (uint8_t)std::round(cdf[input->pixels[i][j]] * 255.0f);
        }
    }

    return output;
}

// --- Modified Gnuplot Function for CDF ---
void plot_cdf_gnuplot(const std::vector<float>& cdf, const std::string& title) {
    FILE *gp = popen("gnuplot -persist", "w");
    if (gp == NULL) {
        printf("Error: Could not open gnuplot.\n");
        return;
    }

    // Formatting Gnuplot to show the u vs v transformation
    fprintf(gp, "set title '%s'\n", title.c_str());
    fprintf(gp, "set xlabel 'Input Intensity (u)'\n");
    fprintf(gp, "set ylabel 'Output Intensity (v)'\n");
    fprintf(gp, "set grid\n");
    fprintf(gp, "set xrange [0:255]\n");
    fprintf(gp, "set yrange [0:1]\n"); // Normalized CDF
    fprintf(gp, "plot '-' with lines lw 2 title 'T(u)'\n");

    for (int i = 0; i < 256; i++) {
        fprintf(gp, "%d %f\n", i, cdf[i]);
    }

    fprintf(gp, "e\n");
    pclose(gp);
}





void run_fft_analysis_centered(BMPImage* input, const std::string& title) {
    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);
    int N = w * h;

    fftw_complex *in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    fftw_complex *out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    fftw_plan p = fftw_plan_dft_2d(h, w, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    // Apply (-1)^(x+y) centering during input loading
    for(int i=0; i<h; i++) {
        for(int j=0; j<w; j++) {
            double multiplier = ((i + j) % 2 == 0) ? 1.0 : -1.0;
            in[i * w + j][0] = (double)input->pixels[i][j] * multiplier; // Real
            in[i * w + j][1] = 0.0;       // Imaginary
        }
    }

    fftw_execute(p);

    // Magnitude is now automatically centered
    double* mag = new double[N];
    for(int i=0; i<N; i++) {
        mag[i] = log(1.0 + sqrt(out[i][0]*out[i][0] + out[i][1]*out[i][1]));
    }

    plot_spectrum_gnuplot(mag, w, h, title);

    fftw_destroy_plan(p);
    fftw_free(in); fftw_free(out);
    delete[] mag;
}

void plot_spectrum_gnuplot(double* mag_data, int w, int h, const std::string& title) {
    FILE *gp = popen("gnuplot -persist", "w");
    if (!gp) return;
    fprintf(gp, "set title '%s'\n", title.c_str());
    fprintf(gp, "set palette gray\nset size square\nunset colorbox\n");
    fprintf(gp, "plot '-' matrix with image\n");
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) fprintf(gp, "%f ", mag_data[i * w + j]);
        fprintf(gp, "\n");
    }
    fprintf(gp, "e\n");
    pclose(gp);
}
