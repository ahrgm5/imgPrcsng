// hsi_handling.cpp
#include "hsi_handling.hpp"
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

HSIImage* createEmptyHSI(int w, int h) {
    HSIImage* img = (HSIImage*)malloc(sizeof(HSIImage));
    img->width = w; img->height = h;
    img->pixels = (HSIPixel**)malloc(h * sizeof(HSIPixel*));
    for (int i = 0; i < h; i++) img->pixels[i] = (HSIPixel*)malloc(w * sizeof(HSIPixel));
    return img;
}

HSIPixel rgb_to_hsi_pixel(RGBQuad rgb) {
    float r = rgb.red / 255.0f, g = rgb.green / 255.0f, b = rgb.blue / 255.0f;
    HSIPixel hsi;
    float num = 0.5f * ((r - g) + (r - b));
    float den = sqrtf((r - g)*(r - g) + (r - b)*(g - b));
    hsi.h = (den == 0) ? 0 : acosf(std::clamp(num / (den + 1e-6f), -1.0f, 1.0f)) * 180.0f / M_PI;
    if (b > g) hsi.h = 360.0f - hsi.h;
    hsi.i = (r + g + b) / 3.0f;
    float min_val = std::min({r, g, b});
    hsi.s = (hsi.i <= 0) ? 0 : 1.0f - (min_val / hsi.i);
    return hsi;
}

RGBQuad hsi_to_rgb_pixel(HSIPixel hsi) {
    float r, g, b, h = hsi.h, s = hsi.s, i = hsi.i;
    if (h < 120) {
        b = i * (1 - s);
        r = i * (1 + (s * cosf(h * M_PI / 180.0f) / cosf((60 - h) * M_PI / 180.0f)));
        g = 3 * i - (r + b);
    } else if (h < 240) {
        h -= 120; r = i * (1 - s);
        g = i * (1 + (s * cosf(h * M_PI / 180.0f) / cosf((60 - h) * M_PI / 180.0f)));
        b = 3 * i - (r + g);
    } else {
        h -= 240; g = i * (1 - s);
        b = i * (1 + (s * cosf(h * M_PI / 180.0f) / cosf((60 - h) * M_PI / 180.0f)));
        r = 3 * i - (g + b);
    }
    return (RGBQuad){ (uint8_t)std::clamp(b * 255.0f, 0.0f, 255.0f), (uint8_t)std::clamp(g * 255.0f, 0.0f, 255.0f), (uint8_t)std::clamp(r * 255.0f, 0.0f, 255.0f), 0 };
}

HSIImage* convert_bmp_to_hsi(BMPImage* img) {
    if (!img || img->info_header.bits_per_pixel != 24) return nullptr;
    int h = abs(img->info_header.height_px), w = img->info_header.width_px;
    HSIImage* out = createEmptyHSI(w, h);
    for (int i = 0; i < h; i++) {
        unsigned char* row = img->pixels[i];
        for (int j = 0; j < w; j++) {
            RGBQuad pixel = { row[j*3+0], row[j*3+1], row[j*3+2], 0 };
            out->pixels[i][j] = rgb_to_hsi_pixel(pixel);
        }
    }
    return out;
}

void update_bmp_from_hsi(BMPImage* img, HSIImage* hsi) {
    if (!img || !hsi) return;
    for (int i = 0; i < hsi->height; i++) {
        unsigned char* row = img->pixels[i];
        for (int j = 0; j < hsi->width; j++) {
            RGBQuad rgb = hsi_to_rgb_pixel(hsi->pixels[i][j]);
            row[j*3+0] = rgb.blue; row[j*3+1] = rgb.green; row[j*3+2] = rgb.red;
        }
    }
}

void freeHSIImage(HSIImage* hsi) {
    if(!hsi) return;
    for(int i=0; i<hsi->height; i++) free(hsi->pixels[i]);
    free(hsi->pixels); free(hsi);
}
