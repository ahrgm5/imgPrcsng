#ifndef BMP_HANDLING_HPP
#define BMP_HANDLING_HPP

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1, reserved2;
    uint32_t offset;
} BMPFileHeader;

typedef struct {
    uint32_t dib_header_size; int32_t width_px, height_px;
    uint16_t num_planes, bits_per_pixel; uint32_t compression, image_size_bytes;
    int32_t x_res, y_res; uint32_t num_colors, important_colors;
} BMPInfoHeader;
#pragma pack(pop)

typedef struct { uint8_t blue, green, red, reserved; } RGBQuad;

typedef struct {
    BMPFileHeader file_header;
    BMPInfoHeader info_header;
    RGBQuad colors[256];      // Palette for 8-bit images
    unsigned char **pixels;   // Raw pixel rows
} BMPImage;

typedef struct {
    BMPImage** images;
    int count;
} BMPImages;

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


BMPImage* readBMP(const char* filename);
void writeBMP(const char* filename, BMPImage* img);
BMPImage* createEmptyBMP(int width, int height, int bpp);
void freeBMPImage(BMPImage* img);
BMPImage* extract_channel_info(BMPImage* input, int mode, int bytes);
BMPImage* resize_nearest(BMPImage* input, int w_out, int h_out);
BMPImage* resize_bilinear(BMPImage* input, int w_out, int h_out);
BMPImage* linear_quantization(BMPImage* img, int bits);
void plot_histogram_gnuplot(BMPImage* img, const char* title);

#endif
