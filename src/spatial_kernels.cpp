// spatial_kernels.cpp
#include "spatial_kernels.hpp"
#include <stdlib.h>
#include <math.h>
#include <algorithm>

Kernel* create_kernel(int w, int h, void (*init_func)(Kernel*), size_t struct_size) {
    Kernel* k = (Kernel*)malloc(struct_size);
    if (!k) return nullptr;
    k->width = w; k->height = h;
    k->initialize = init_func;

    k->mask = (float**)malloc(h * sizeof(float*));
    for (int i = 0; i < h; i++) k->mask[i] = (float*)calloc(w, sizeof(float));

    if (k->initialize) k->initialize(k);
    return k;
}

void destroy_kernel(Kernel* k) {
    if (!k) return;
    for (int i = 0; i < k->height; i++) free(k->mask[i]);
    free(k->mask);
    free(k);
}

void average_init(Kernel* self) {
	AverageKernel* ak = (AverageKernel*)self;

    float val = 1.0f/ (self->width * self->height);
    for (int i = 0; i < self->height; i++)
        for (int j = 0; j < self->width; j++) self->mask[i][j] = val;
}

void laplacian_init(Kernel* self) {
	LaplacianKernel* lk = (LaplacianKernel*)self;
	float k = (lk->k ==0)? 1.0f : lk->k; // Default to 1.0 if k is 0 to avoid zeroing the center

	if(lk->base.width == 3 && lk->base.height == 3) {

		// Standard 3x3 Laplacian kernel with k as the center value
		float laplacian_3x3[3][3] = {
			{0, -1, 0},
			{-1, 4, -1},
			{0, -1, 0}
		};
		for(int i=0; i<3; i++) for(int j=0; j<3; j++) self->mask[i][j] = laplacian_3x3[i][j];
	}
		// For very small kernels, we can only set the center to a positive value and the rest to negative

	else{
    int cy = self->height / 2, cx = self->width / 2;
    for (int i = 0; i < self->height; i++)
        for (int j = 0; j < self->width; j++) self->mask[i][j] = -1.0f;
    self->mask[cy][cx] = (float)((self->width * self->height - 1.0f)*k ); // Center value adjusted by k
}}


void gaussian_init(Kernel* self) {
    GaussianKernel* gk = (GaussianKernel*)self;
    // Default fallback if sigma is 0
    double s = (gk->sigma <= 0.0) ? 1.0 : gk->sigma;
    double sum = 0.0;
    int cy = self->height / 2, cx = self->width / 2;

    for (int i = 0; i < self->height; i++) {
        for (int j = 0; j < self->width; j++) {
            int y = i - cy, x = j - cx;
            self->mask[i][j] = (float)(exp(-(x*x + y*y) / (2 * s * s)) / (2 * M_PI * s * s));
            sum += self->mask[i][j];
        }
    }
    for (int i = 0; i < self->height; i++)
        for (int j = 0; j < self->width; j++) self->mask[i][j] /= (float)sum;
}

void sobel_init(Kernel* self) {
    SobelKernel* sk = (SobelKernel*)self;
    if (sk->horizontal) {
        float h_sobel[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
        for(int i=0; i<3; i++) for(int j=0; j<3; j++) self->mask[i][j] = h_sobel[i][j];
    } else {
        float v_sobel[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
        for(int i=0; i<3; i++) for(int j=0; j<3; j++) self->mask[i][j] = v_sobel[i][j];
    }
}

static float convolve(int x, int y, int w, int h, float** buffer, Kernel* k) {
    float sum = 0.0f;
    int dy = k->height / 2, dx = k->width / 2;
    for (int i = 0; i < k->height; i++) {
        for (int j = 0; j < k->width; j++) {
            int py = std::clamp(y + i - dy, 0, h - 1);
            int px = std::clamp(x + j - dx, 0, w - 1);
            sum += buffer[py][px] * k->mask[i][j];
        }
    }
    return sum;
}

BMPImage* apply_kernel_to_bmp(BMPImage* input, Kernel* k) {
    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);

    BMPImage* output = createEmptyBMP(w, h, input->info_header.bits_per_pixel);

    float** intensity = (float**)malloc(h * sizeof(float*));
    float** result = (float**)malloc(h * sizeof(float*));
    for (int i = 0; i < h; i++) {
        intensity[i] = (float*)malloc(w * sizeof(float));
        result[i] = (float*)malloc(w * sizeof(float));
    }

    HSIImage* hsi = nullptr;
    if (input->info_header.bits_per_pixel == 24) {
        hsi = convert_bmp_to_hsi(input);
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) intensity[i][j] = hsi->pixels[i][j].i;
    } else {
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) intensity[i][j] = input->pixels[i][j] / 255.0f;
    }

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            result[i][j] = convolve(j, i, w, h, intensity, k);
        }
    }

    if (hsi) {
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) hsi->pixels[i][j].i = std::clamp(result[i][j], 0.0f, 1.0f);
        update_bmp_from_hsi(output, hsi);
        freeHSIImage(hsi);
    } else {
        for(int i=0; i<h; i++) for(int j=0; j<w; j++) output->pixels[i][j] = (uint8_t)std::clamp(result[i][j] * 255.0f, 0.0f, 255.0f);
    }

    for (int i = 0; i < h; i++) { free(intensity[i]); free(result[i]); }
    free(intensity); free(result);

    return output;
}
