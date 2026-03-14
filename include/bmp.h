#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================
 * BMP FILE STRUCTURES
 * ========================================================= */

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} BMPFileHeader;

typedef struct {
    uint32_t dib_header_size;
    int32_t  width_px;
    int32_t  height_px;
    uint16_t num_planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size_bytes;
    int32_t  x_res;
    int32_t  y_res;
    uint32_t num_colors;
    uint32_t important_colors;
} BMPInfoHeader;
#pragma pack(pop)

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
} RGBQuad;


/* =========================================================
 * IMAGE STATISTICS
 * ========================================================= */

typedef struct {

    float  counts[256];
    float  pdf[256];
    float  cdf[256];
    int    num_pixels;
    double mean;
    double std_dev;
    double min;
    double max;
} ImageStats;


/* =========================================================
 * BMP IMAGE
 * ========================================================= */

typedef struct {
    BMPFileHeader  file_header;
    BMPInfoHeader  info_header;
    RGBQuad        colors[256];   /* Palette for 8-bit images      */
    unsigned char **pixels;       /* Row-major pixel data          */
    ImageStats     stats;         /* Cached per-image statistics   */
} BMPImage;

typedef struct {
    BMPImage **images;
    int        count;
} BMPImages;


/* =========================================================
 * HSI COLOR SPACE
 * ========================================================= */

typedef struct {
    float h;   /* Hue        [0, 360) */
    float s;   /* Saturation [0, 1]   */
    float i;   /* Intensity  [0, 1]   */
} HSIPixel;

typedef struct {
    int       height;
    int       width;
    HSIPixel **pixels;
} HSIImage;


/* =========================================================
 * BMP I/O
 * ========================================================= */

BMPImage *readBMP(const char *filename);
void      writeBMP(const char *filename, BMPImage *img);


/* =========================================================
 * BMP CONSTRUCTION / DESTRUCTION
 * ========================================================= */

BMPImage *createEmptyBMP(int width, int height, int bpp);
void      freeBMPImage(BMPImage *img);
void      freeBMPImages(BMPImages res);


/* =========================================================
 * HSI CONSTRUCTION / DESTRUCTION
 * ========================================================= */

HSIImage *createEmptyHSI(int w, int h);
void      freeHSIImage(HSIImage *hsi);


/* =========================================================
 * COLOR SPACE CONVERSION
 * ========================================================= */

HSIPixel  rgb_to_hsi_pixel(RGBQuad rgb);
RGBQuad   hsi_to_rgb_pixel(HSIPixel hsi);
HSIImage *convert_bmp_to_hsi(BMPImage *img);
void      update_bmp_from_hsi(BMPImage *img, HSIImage *hsi);


/* =========================================================
 * PIXEL OPERATIONS
 * ========================================================= */

BMPImage *extract_channel_info(BMPImage *input, int mode, int bytes);
BMPImage *resize_nearest(BMPImage *input, int w_out, int h_out);
BMPImage *resize_bilinear(BMPImage *input, int w_out, int h_out);
BMPImage *linear_quantization(BMPImage *img, int bits);


/* =========================================================
 * DISPLAY / DIAGNOSTICS
 * ========================================================= */


void plot_histogram_gnuplot(BMPImage *img, const char *title);

#endif /* BMP_H */
