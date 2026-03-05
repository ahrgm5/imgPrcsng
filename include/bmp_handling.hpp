#ifndef BMP_HANDLING_H
#define BMP_HANDLING_H

#include <stdint.h>


typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} __attribute__((packed)) BMPFileHeader;

typedef struct {
    uint32_t dib_header_size;
    int32_t width_px;
    int32_t height_px;
    uint16_t num_planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size_bytes;
    int32_t x_resolution_ppm;
    int32_t y_resolution_ppm;
    uint32_t num_colors;
    uint32_t important_colors;
} __attribute__((packed)) BMPInfoHeader;

typedef struct {
    uint8_t blue, green, red, reserved;
} RGBQuad;

typedef struct {
    BMPFileHeader file_header;
    BMPInfoHeader info_header;
    RGBQuad colors[256];
    unsigned char **pixels; 
} BMPImage;

typedef struct {
    float h; // Hue [0, 360]
    float s; // Saturation [0, 1]
    float i; // Intensity [0, 1]
} HSIPixel;

// Function prototypes
HSIPixel rgb_to_hsi_pixel(uint8_t r, uint8_t g, uint8_t b);
void hsi_to_rgb_pixel(HSIPixel hsi, uint8_t &r, uint8_t &g, uint8_t &b);

// Processing functions
HSIPixel** convert_rgb_to_hsi(BMPImage* img);
void update_rgb_from_hsi(BMPImage* img, HSIPixel** hsi_buffer);
void freeHSIBuffer(HSIPixel** buffer, int height);


BMPImage* readBMP(const char* filename);
BMPImage* createEmptyBMP(int width, int height, int bpp);
BMPImage* resize_nearest(BMPImage* input, int w_out, int h_out);
BMPImage* resize_bilinear(BMPImage* input, int w_out, int h_out);
BMPImage* extract_channel_info(BMPImage* input, int mode, int bytes); 
BMPImage* linear_quantization(BMPImage* img, int bits);

void freeBMPImage(BMPImage* img) ;
int allocatePixelMemory(BMPImage* img);
void writeBMP(const char* filename, BMPImage* img); 
void plot_histogram_gnuplot(BMPImage* img, const char* title);






#endif 
