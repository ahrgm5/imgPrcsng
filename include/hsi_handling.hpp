// hsi_handling.hpp
#ifndef HSI_HANDLING_H
#define HSI_HANDLING_H

#include "bmp_handling.hpp"

typedef struct {
    float h, s, i;
} HSIPixel;

typedef struct {
    int height, width;
    HSIPixel** pixels;
} HSIImage;

HSIPixel rgb_to_hsi_pixel(RGBQuad rgb);
RGBQuad  hsi_to_rgb_pixel(HSIPixel hsi);
HSIImage* convert_bmp_to_hsi(BMPImage* img);
void      update_bmp_from_hsi(BMPImage* img, HSIImage* hsi);
HSIImage* createEmptyHSI(int w, int h);
void freeHSIImage(HSIImage* hsi);

#endif
