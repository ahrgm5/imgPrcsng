// img_manip.cpp
#include "img_manip.hpp"
#include <bmp.hpp>
#include <fftw3.h>

#include <kernels.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>

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


BMPImage* apply_histogram_stretching(BMPImage* input) {
    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);
    BMPImage* output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);

    // Use HSI for 24-bit images to avoid color distortion and "cut" images
    if (input->info_header.bits_per_pixel == 24) {
        HSIImage* hsi = convert_bmp_to_hsi(input);
        float min_i = 1.0f, max_i = 0.0f;

        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                if (hsi->pixels[i][j].i < min_i) min_i = hsi->pixels[i][j].i;
                if (hsi->pixels[i][j].i > max_i) max_i = hsi->pixels[i][j].i;
            }
        }

        float range = max_i - min_i;
        if (range > 0) {
            for (int i = 0; i < h; i++) {
                for (int j = 0; j < w; j++) {
                    hsi->pixels[i][j].i = (hsi->pixels[i][j].i - min_i) / range;
                }
            }
        }
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        // Standard 8-bit grayscale logic
        uint8_t min_v = 255, max_v = 0;
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                if (input->pixels[i][j] < min_v) min_v = input->pixels[i][j];
                if (input->pixels[i][j] > max_v) max_v = input->pixels[i][j];
            }
        }
        float range = (float)(max_v - min_v);
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                float val = (range == 0) ? 0 : (float)(input->pixels[i][j] - min_v) / range;
                output->pixels[i][j] = (uint8_t)(val * 255.0f);
            }
        }
    }
    return output;
}

BMPImage* apply_gamma_correction(BMPImage* input, float gamma) {
    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);
    BMPImage* output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);

    if (input->info_header.bits_per_pixel == 24) {
        HSIImage* hsi = convert_bmp_to_hsi(input);
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                hsi->pixels[i][j].i = powf(hsi->pixels[i][j].i, gamma);
            }
        }
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                float norm = (float)input->pixels[i][j] / 255.0f;
                output->pixels[i][j] = (uint8_t)(powf(norm, gamma) * 255.0f);
            }
        }
    }
    return output;
}

BMPImage* preprocess_for_edges(BMPImage* input, float gamma) {
    // 1. Perform linear stretch to normalize the intensity range
    BMPImage* stretched = apply_histogram_stretching(input);

    // 2. Apply gamma correction to the stretched result
    BMPImage* processed = apply_gamma_correction(stretched, gamma);

    // 3. Clean up the intermediate stretched image to prevent memory leaks
    freeBMPImage(stretched);

    return processed;
}


BMPImage* apply_histogram_equalization(BMPImage* input, bool plot, const std::string& title) {
    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);
    BMPImage* output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);

    // 1. Calculate Statistics for the Transformation
    ImageStats stats = calculate_stats_generic(input);

    // 2. Conditionally Plot the CDF/Transformation Curve
    if (plot) {
        plot_transformation_curve(stats, title); //
    }

    // 3. Apply Equalization based on Bit Depth
    if (input->info_header.bits_per_pixel == 24) {
        // Color Image: Equalize Intensity Channel only
        HSIImage* hsi = convert_bmp_to_hsi(input); //
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                // Map current intensity (0.0-1.0) using the CDF
                int level = (int)(hsi->pixels[i][j].i * 255.0f);
                hsi->pixels[i][j].i = stats.cdf[level];
            }
        }
        update_bmp_from_hsi(output, hsi); //
        freeHSIImage(hsi);
    } else {
        // Grayscale Image: Direct Pixel Mapping
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                uint8_t level = input->pixels[i][j];
                output->pixels[i][j] = (uint8_t)(stats.cdf[level] * 255.0f);
            }
        }
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
    res.images[1] = combine_images(input, res.images[0], 1.0f*k); // factor = 1.0f + k to add edges back to original

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

    if (w != processed->info_header.width_px || h != abs(processed->info_header.height_px)) {
        std::cerr << "Error: Image dimensions do not match for MSE calculation." << std::endl;
        return -1.0;
    }

    double sum_squared_error = 0.0;
    int total_pixels = w * h;

    if (orig->info_header.bits_per_pixel == 24) {
        // For 24-bit color, convert to HSI and compare Intensity (I)
        HSIImage* hsi_orig = convert_bmp_to_hsi(orig);
        HSIImage* hsi_proc = convert_bmp_to_hsi(processed);

        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                // Scale 0.0-1.0 intensity back to 0.0-255.0 to match the 8-bit MSE scale
                double diff = (double)(hsi_orig->pixels[i][j].i * 255.0f) -
                             (double)(hsi_proc->pixels[i][j].i * 255.0f);
                sum_squared_error += diff * diff;
            }
        }
        freeHSIImage(hsi_orig);
        freeHSIImage(hsi_proc);
    } else {
        // For 8-bit grayscale, compare pixel values directly
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                double diff = (double)orig->pixels[i][j] - (double)processed->pixels[i][j];
                sum_squared_error += diff * diff;
            }
        }
    }

    return sum_squared_error / (double)total_pixels;
}




double calculate_snr(BMPImage* orig, BMPImage* processed) {
    double mse = calculate_mse(orig, processed);
    if (mse <= 0) return 99.9; // Signal is identical to original

    int w = orig->info_header.width_px;
    int h = abs(orig->info_header.height_px);
    double signal_power = 0.0;

    if (orig->info_header.bits_per_pixel == 24) {
        HSIImage* hsi = convert_bmp_to_hsi(orig);
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                double val = (double)(hsi->pixels[i][j].i * 255.0f);
                signal_power += val * val;
            }
        }
        freeHSIImage(hsi);
    } else {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                double val = (double)orig->pixels[i][j];
                signal_power += val * val;
            }
        }
    }

    double mean_signal_power = signal_power / (double)(w * h);
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

static void shift_quadrants(double* src, double* dst, int w, int h) {
    int w2 = w / 2;
    int h2 = h / 2;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            int ni = (i + h2) % h;
            int nj = (j + w2) % w;
            dst[ni * w + nj] = src[i * w + j];
        }
    }
}


void run_dft_analysis(BMPImage* img, const std::string& title, bool plot, const std::string& savePath) {
    if (!img) return;
    int w = img->info_header.width_px;
    int h = abs(img->info_header.height_px);
    int N = w * h;

    // 1. Allocate FFTW buffers
    fftw_complex* in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    fftw_complex* out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    fftw_plan p = fftw_plan_dft_2d(h, w, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    // 2. Load grayscale intensity (normalized 0-1)
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            double val = (img->info_header.bits_per_pixel == 24) ?
                         (0.299*img->pixels[i][j*3+2] + 0.587*img->pixels[i][j*3+1] + 0.114*img->pixels[i][j*3]) / 255.0 :
                         img->pixels[i][j] / 255.0;
            in[i * w + j][0] = val;
            in[i * w + j][1] = 0.0;
        }
    }

    fftw_execute(p);

    // 3. Calculate Log Magnitude
    double* mag = new double[N];
    for (int i = 0; i < N; i++) mag[i] = log(1.0 + sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]));


    // CENTER THE DC COMPONENT
        double* shifted = new double[N];
        shift_quadrants(mag, shifted, w, h);

        save_spectrum_plot(shifted, w, h, title, savePath , plot);
        fftw_destroy_plan(p); fftw_free(in); fftw_free(out);
        delete[] mag; delete[] shifted;
}




void run_dct_analysis(BMPImage* img, const std::string& title, bool plot, const std::string& savePath){
    if (!img) return;
    int w = img->info_header.width_px;
    int h = abs(img->info_header.height_px);
    int N = w * h;

    double* in = (double*) fftw_malloc(sizeof(double) * N);
    double* out = (double*) fftw_malloc(sizeof(double) * N);
    fftw_plan p = fftw_plan_r2r_2d(h, w, in, out, FFTW_REDFT10, FFTW_REDFT10, FFTW_ESTIMATE);

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            double val = (img->info_header.bits_per_pixel == 24) ?
                         (0.299*img->pixels[i][j*3+2] + 0.587*img->pixels[i][j*3+1] + 0.114*img->pixels[i][j*3]) / 255.0 :
                         img->pixels[i][j] / 255.0;
            in[i * w + j] = val;
        }
    }

    fftw_execute(p);

    double* mag = new double[N];
    for (int i = 0; i < N; i++) mag[i] = log(1.0 + fabs(out[i]));

    // CENTER THE DC COMPONENT
        double* shifted = new double[N];
        shift_quadrants(mag, shifted, w, h);

        save_spectrum_plot(shifted, w, h, title, savePath , plot);

        fftw_destroy_plan(p); fftw_free(in); fftw_free(out);
        delete[] mag; delete[] shifted;

}


void save_spectrum_plot(double* data, int w, int h, const std::string& title, const std::string& savePath, bool plot) {
    if (!data) return;

    // 1. Write data to a unique temporary file
    std::string temp_filename = "tmp_" + std::to_string(rand()) + ".txt";
    FILE* temp = fopen(temp_filename.c_str(), "w");
    if (!temp) return;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            fprintf(temp, "%f ", data[i * w + j]);
        }
        fprintf(temp, "\n");
    }
    fclose(temp);

    // 2. Open Gnuplot pipe
    FILE* gp = popen("gnuplot", "w");
    if (gp) {
        // If savePath is provided, prioritize saving to file
        if (!savePath.empty()) {
            fprintf(gp, "set terminal pngcairo size 800,600\n");
            fprintf(gp, "set output '%s'\n", savePath.c_str());
        } else if (plot) {
            fprintf(gp, "set terminal wxt persist\n");
        }



        fprintf(gp, "set title '%s'\n", title.c_str());
        fprintf(gp, "set palette gray\n");
        fprintf(gp, "set view map\n");
        fprintf(gp, "set size ratio -1\n");

        // --- REMOVE FILENAME/LEGEND ---
                fprintf(gp, "unset key\n");
        // ------------------------------


        fprintf(gp, "set xrange [0:%d]\n", w - 1);
        fprintf(gp, "set yrange [%d:0]\n", h - 1); // Flip y-axis to match image coordinates
        fprintf(gp, "splot '%s' matrix with image\n", temp_filename.c_str());

        // --- FIXES ADDED HERE ---
        if (!savePath.empty()) {
            fprintf(gp, "set output\n"); // Flushes and closes the image file
        }
        fprintf(gp, "exit\n");           // Tells gnuplot to exit cleanly
        // ------------------------

        pclose(gp);
    }

    // Clean up temp file
    remove(temp_filename.c_str());
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
