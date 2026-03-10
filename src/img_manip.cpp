// img_manip.cpp
#include "img_manip.hpp"
#include "spatial_kernels.hpp"
#include "histogram_utils.hpp"
#include "hsi_handling.hpp"
#include <fftw3.h>
#include <iostream>
#include <cmath>
#include <algorithm>



void freeBMPImages(BMPImages res) {
    for (int i = 0; i < res.count; i++) {
        if (res.images[i]) freeBMPImage(res.images[i]);
    }
    if (res.images) free(res.images);
}

BMPImages sharpen_laplacian(BMPImage* input, float k, int kw, int kh) {
    BMPImages res;
    res.count = 2;
    res.images = (BMPImage**)malloc(sizeof(BMPImage*) * res.count);

    // 1. Create the Laplacian kernel with arbitrary dimensions
    LaplacianKernel* lap = (LaplacianKernel*)create_kernel(kw, kh, laplacian_init, sizeof(LaplacianKernel));
    //lap->k = k; // Set the sharpening factor k for the kernel initialization
    // 2. Output[0]: The Edge Map (Raw convolution result)
    res.images[0] = apply_kernel_to_bmp(input,(Kernel*) lap);

    // 3. Output[1]: The Sharpened Image
    // Uses: Original + (k * Edges). k controls the sharpening intensity.
    res.images[1] = combine_images(input, res.images[0], 1.0f+k); // factor = 1.0f + k to add edges back to original

    destroy_kernel((Kernel*)lap);
    return res;
}

BMPImages unsharp_masking(BMPImage* input, float k, int kw, int kh) {
    BMPImages res;
    res.count = 2;
    res.images = (BMPImage**)malloc(sizeof(BMPImage*) * res.count);

    // 1. Create the Box/Averaging kernel
    AverageKernel* avg = (AverageKernel*)create_kernel(kw, kh, average_init, sizeof(AverageKernel));

    // 2. Output[0]: The Blurred Image
    res.images[0] = apply_kernel_to_bmp(input, (Kernel*)avg);

    // 3. Output[1]: The Detail Map (High-pass)
    // Uses factor -1.0 to perform: Original - Blurred
    BMPImage* mask = combine_images(input, res.images[0], -1.0f);


    // 1. Create Detail Mask: Mask = Original - Blur (factor = -1.0)
     //= combine_images(input, blurred, -1.0f);

    // 2. Add weighted mask back: Result = Original + k*Mask (factor = k)
    res.images[1] = combine_images(input, mask, k);


    destroy_kernel((Kernel*)avg);
    return res;
}



BMPImage* combine_images(BMPImage* img1, BMPImage* img2, float factor) {
    if (!img1 || !img2) return nullptr;

    int w = img1->info_header.width_px;
    int h = abs(img1->info_header.height_px);

    // Validate dimensions match
    if (w != img2->info_header.width_px || h != abs(img2->info_header.height_px)) {
        std::cerr << "Error: Dimension mismatch in combine_images." << std::endl;
        return nullptr;
    }

    BMPImage* output = createEmptyBMP(w, h, img1->info_header.bits_per_pixel);

    if (img1->info_header.bits_per_pixel == 24) {
        // Handle 24-bit using HSI to preserve color integrity
        HSIImage* hsi1 = convert_bmp_to_hsi(img1);
        HSIImage* hsi2 = convert_bmp_to_hsi(img2);

        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                float val = hsi1->pixels[i][j].i + (factor * hsi2->pixels[i][j].i); // Average to prevent overflow
                hsi1->pixels[i][j].i = std::clamp(val, 0.0f, 1.0f);
            }
        }
        update_bmp_from_hsi(output, hsi1);
        freeHSIImage(hsi1);
        freeHSIImage(hsi2);
    } else {
        // Handle 8-bit grayscale
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                float val1 = img1->pixels[i][j] / 255.0f;
                float val2 = img2->pixels[i][j] / 255.0f;
                float result = val1 + (factor * val2); // Average to prevent overflow
                output->pixels[i][j] = (uint8_t)std::clamp(result * 255.0f, 0.0f, 255.0f);
            }
        }
    }

    return output;
}





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






BMPImage* sharpen_laplacian(BMPImage* input, float k) {
    LaplacianKernel* lap_kernel = (LaplacianKernel*)create_kernel(3, 3, laplacian_init, sizeof(LaplacianKernel));

    BMPImage* edges = apply_kernel_to_bmp(input, (Kernel*)lap_kernel);
    destroy_kernel((Kernel*)lap_kernel);

    // Sharpening is Original + Edges (factor = 1.0)
    BMPImage* sharpened = combine_images(input, edges, 1.0f);

   // BMPImage** return_out = { sharpened, edges};
    freeBMPImage(edges);
    return sharpened;
}

BMPImage* unsharp_masking(BMPImage* input, float k) {
    Kernel* avg_kernel = create_kernel(3, 3, average_init, sizeof(AverageKernel));
    BMPImage* blurred = apply_kernel_to_bmp(input, avg_kernel);
    destroy_kernel(avg_kernel);

    // 1. Create Detail Mask: Mask = Original - Blur (factor = -1.0)
    BMPImage* mask = combine_images(input, blurred, -1.0f);

    // 2. Add weighted mask back: Result = Original + k*Mask (factor = k)
    BMPImage* sharpened = combine_images(input, mask, k);

    freeBMPImage(blurred);
    freeBMPImage(mask);
    return sharpened;
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
