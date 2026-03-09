// img_manip.cpp
#include "img_manip.hpp"
#include "spatial_kernels.hpp"
#include "histogram_utils.hpp"
#include "hsi_handling.hpp"
#include <fftw3.h>
#include <iostream>
#include <cmath>
#include <algorithm>


double calculate_mse(BMPImage* orig, BMPImage* processed) {
    if (!orig || !processed) return -1.0;

    int w = orig->info_header.width_px;
    int h = abs(orig->info_header.height_px);

    // Safety check for dimensions
    if (w != processed->info_header.width_px || h != abs(processed->info_header.height_px)) {
        std::cerr << "Error: Image dimensions do not match for MSE calculation." << std::endl;
        return -1.0;
    }

    double sum_squared_error = 0.0;
    int total_pixels = w * h;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // For 8-bit images, we compare pixel values directly.
            // For 24-bit images, we compare the Intensity (I) channel.
            double diff;
            if (orig->info_header.bits_per_pixel == 8) {
                diff = (double)orig->pixels[i][j] - (double)processed->pixels[i][j];
            } else {
                // If color, you should ideally convert to HSI first or average the RGB
                // Here we assume 8-bit grayscale for HW2 Task 4 logic.
                diff = (double)orig->pixels[i][j] - (double)processed->pixels[i][j];
            }
            sum_squared_error += diff * diff;
        }
    }

    return sum_squared_error / (double)total_pixels;
}


double calculate_snr(BMPImage* orig, BMPImage* processed) {
    double mse = calculate_mse(orig, processed);
    if (mse <= 0) return 99.9; // Avoid division by zero for identical images

    int w = orig->info_header.width_px;
    int h = abs(orig->info_header.height_px);
    double signal_power = 0.0;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            double val = (double)orig->pixels[i][j];
            signal_power += val * val;
        }
    }

    double mean_signal_power = signal_power / (w * h);
    return 10.0 * log10(mean_signal_power / mse);
}



BMPImage* sharpen_laplacian(BMPImage* input) {
    // Utilize the spatial_kernels API dynamically
    Kernel* lap_kernel = create_kernel(3, 3, laplacian_init, sizeof(LaplacianKernel));
    BMPImage* edges = apply_kernel_to_bmp(input, lap_kernel);
    destroy_kernel(lap_kernel);

    // Edges are handled correctly through the unified pipeline
    return edges;
}

BMPImage* unsharp_masking(BMPImage* input, float k) {
    int h = abs(input->info_header.height_px);
    int w = input->info_header.width_px;
    BMPImage* out = createEmptyBMP(w, h, input->info_header.bits_per_pixel);

    Kernel* avg_kernel = create_kernel(3, 3, average_init, sizeof(AverageKernel));
    BMPImage* blurred = apply_kernel_to_bmp(input, avg_kernel);
    destroy_kernel(avg_kernel);

    if (input->info_header.bits_per_pixel == 24) {
        HSIImage* hsi_orig = convert_bmp_to_hsi(input);
        HSIImage* hsi_blur = convert_bmp_to_hsi(blurred);

        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                float mask = hsi_orig->pixels[i][j].i - hsi_blur->pixels[i][j].i;
                hsi_orig->pixels[i][j].i = std::clamp(hsi_orig->pixels[i][j].i + (k * mask), 0.0f, 1.0f);
            }
        }
        update_bmp_from_hsi(out, hsi_orig);
        freeHSIImage(hsi_orig);
        freeHSIImage(hsi_blur);
    } else {
        for(int i=0; i<h; i++) {
            for(int j=0; j<w; j++) {
                float mask = (input->pixels[i][j] - blurred->pixels[i][j]) / 255.0f;
                float orig = input->pixels[i][j] / 255.0f;
                out->pixels[i][j] = (uint8_t)std::clamp((orig + (k * mask)) * 255.0f, 0.0f, 255.0f);
            }
        }
    }

    // Clean up the temporary blurred image to prevent leaks
    freeBMPImage(blurred);

    return out;
}



void run_fft_analysis_centered(BMPImage* input, const std::string& title) {
    if (!input) return;

    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);
    int N = w * h;

    HSIImage* hsi = nullptr;
    if (input->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(input);
    }

    fftw_complex *in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    fftw_complex *out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    fftw_plan p = fftw_plan_dft_2d(h, w, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            double multiplier = ((i + j) % 2 == 0) ? 1.0 : -1.0;
            if (hsi) in[i * w + j][0] = (double)hsi->pixels[i][j].i * multiplier;
            else in[i * w + j][0] = (double)(input->pixels[i][j] / 255.0) * multiplier;
            in[i * w + j][1] = 0.0;
        }
    }

    fftw_execute(p);

    double* mag = new double[N];
    for (int i = 0; i < N; i++) {
        mag[i] = log(1.0 + sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]));
    }

    plot_spectrum_gnuplot(mag, w, h, title);

    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);
    delete[] mag;
    if(hsi) freeHSIImage(hsi);
}

void plot_spectrum_gnuplot(double* mag_data, int w, int h, const std::string& title) {
    FILE *gp = popen("gnuplot -persist", "w");
    if (!gp) return;
    fprintf(gp, "set title '%s'\nset palette gray\nplot '-' matrix with image\n", title.c_str());
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) fprintf(gp, "%f ", mag_data[i * w + j]);
        fprintf(gp, "\n");
    }
    fprintf(gp, "e\n"); pclose(gp);
}
