/* bmp.c */
#define _POSIX_C_SOURCE 200809L

#include "bmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================
 * INTERNAL HELPERS
 * ========================================================= */

static float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static int clampi(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static float minf3(float a, float b, float c) {
    float m = a;
    if (b < m) m = b;
    if (c < m) m = c;
    return m;
}

static int allocatePixelMemory(BMPImage *img) {
    int height, width, bpp, bytes_per_pixel, row_width, i;

    if (!img) return -1;

    height         = abs(img->info_header.height_px);
    width          = img->info_header.width_px;
    bpp            = img->info_header.bits_per_pixel;
    bytes_per_pixel = (bpp <= 8) ? 1 : (bpp / 8);
    row_width      = width * bytes_per_pixel;

    img->pixels = (unsigned char **)malloc(height * sizeof(unsigned char *));
    if (!img->pixels) return -1;

    for (i = 0; i < height; i++) {
        img->pixels[i] = (unsigned char *)calloc(1, row_width);
        if (!img->pixels[i]) return -1;
    }
    return 0;
}



/* =========================================================
 * BMP I/O
 * ========================================================= */

BMPImage *readBMP(const char *filename) {
    FILE     *fp;
    BMPImage *img;
    int       bpp, bytes_per_pixel, width, height;
    int       unpadded_row_size, padded_row_size;
    int       colors_to_read, i, idx;

    fp = fopen(filename, "rb");
    if (!fp) {
        printf("Could not read %s.\n", filename);
        return NULL;
    }

    img = (BMPImage *)calloc(1, sizeof(BMPImage));
    if (!img) { fclose(fp); return NULL; }

    fread(&img->file_header, sizeof(BMPFileHeader), 1, fp);
    fread(&img->info_header, sizeof(BMPInfoHeader), 1, fp);

    if (img->info_header.bits_per_pixel <= 8) {
        colors_to_read = img->info_header.num_colors
                       ? (int)img->info_header.num_colors : 256;
        fread(img->colors, sizeof(RGBQuad), colors_to_read, fp);
    }

    if (allocatePixelMemory(img) != 0) {
        fclose(fp);
        free(img);
        return NULL;
    }

    bpp             = img->info_header.bits_per_pixel;
    bytes_per_pixel = (bpp <= 8) ? 1 : (bpp / 8);
    width           = img->info_header.width_px;
    height          = abs(img->info_header.height_px);
    unpadded_row_size = width * bytes_per_pixel;
    padded_row_size   = (unpadded_row_size + 3) & (~3);

    fseek(fp, img->file_header.offset, SEEK_SET);
    for (i = 0; i < height; i++) {
        idx = (img->info_header.height_px > 0) ? (height - 1 - i) : i;
        fread(img->pixels[idx], 1, unpadded_row_size, fp);
        fseek(fp, padded_row_size - unpadded_row_size, SEEK_CUR);
    }

    fclose(fp);
    return img;
}

void writeBMP(const char *filename, BMPImage *img) {
    FILE    *fp;
    int      bpp, bytes_per_pixel, width, height;
    int      unpadded_row_size, padded_row_size, padding_len;
    uint8_t  padding[3] = {0};
    int      i, idx;

    fp = fopen(filename, "wb");
    if (!fp) return;

    bpp             = img->info_header.bits_per_pixel;
    bytes_per_pixel = (bpp <= 8) ? 1 : (bpp / 8);
    width           = img->info_header.width_px;
    height          = abs(img->info_header.height_px);
    unpadded_row_size = width * bytes_per_pixel;
    padded_row_size   = (unpadded_row_size + 3) & (~3);
    padding_len       = padded_row_size - unpadded_row_size;

    img->file_header.type   = 0x4D42;
    img->file_header.offset = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader)
                            + (bpp <= 8 ? 1024 : 0);
    img->file_header.size   = img->file_header.offset
                            + (padded_row_size * height);

    fwrite(&img->file_header, sizeof(BMPFileHeader), 1, fp);
    fwrite(&img->info_header, sizeof(BMPInfoHeader), 1, fp);

    if (bpp <= 8) fwrite(img->colors, sizeof(RGBQuad), 256, fp);

    for (i = 0; i < height; i++) {
        idx = (img->info_header.height_px > 0) ? (height - 1 - i) : i;
        fwrite(img->pixels[idx], 1, unpadded_row_size, fp);
        fwrite(padding, 1, padding_len, fp);
    }

    fclose(fp);
}



/* =========================================================
 * BMP CONSTRUCTION / DESTRUCTION
 * ========================================================= */

BMPImage *createEmptyBMP(int width, int height, int bpp) {
    BMPImage *img;
    int       i;

    img = (BMPImage *)calloc(1, sizeof(BMPImage));
    if (!img) return NULL;

    img->info_header.width_px        = width;
    img->info_header.height_px       = height;
    img->info_header.bits_per_pixel  = bpp;
    img->info_header.num_planes      = 1;
    img->info_header.dib_header_size = sizeof(BMPInfoHeader);

    if (bpp <= 8) {
        for (i = 0; i < 256; i++) {
            img->colors[i].blue     = (uint8_t)i;
            img->colors[i].green    = (uint8_t)i;
            img->colors[i].red      = (uint8_t)i;
            img->colors[i].reserved = 0;
        }
    }

    if (allocatePixelMemory(img) != 0) {
        free(img);
        return NULL;
    }
    return img;
}

void freeBMPImage(BMPImage *img) {
    int i, height;

    if (!img) return;

    if (img->pixels) {
        height = abs(img->info_header.height_px);
        for (i = 0; i < height; i++) {
            if (img->pixels[i]) free(img->pixels[i]);
        }
        free(img->pixels);
    }
    free(img);
}

void freeBMPImages(BMPImages res) {
    int i;
    for (i = 0; i < res.count; i++) {
        if (res.images[i]) freeBMPImage(res.images[i]);
    }
    if (res.images) free(res.images);
}



/* =========================================================
 * HSI CONSTRUCTION / DESTRUCTION
 * ========================================================= */

HSIImage *createEmptyHSI(int w, int h) {
    HSIImage *img;
    int       i;

    img = (HSIImage *)malloc(sizeof(HSIImage));
    if (!img) return NULL;

    img->width  = w;
    img->height = h;
    img->pixels = (HSIPixel **)malloc(h * sizeof(HSIPixel *));
    if (!img->pixels) { free(img); return NULL; }

    for (i = 0; i < h; i++) {
        img->pixels[i] = (HSIPixel *)malloc(w * sizeof(HSIPixel));
        if (!img->pixels[i]) {
            /* Partial cleanup */
            while (--i >= 0) free(img->pixels[i]);
            free(img->pixels);
            free(img);
            return NULL;
        }
    }
    return img;
}

void freeHSIImage(HSIImage *hsi) {
    int i;
    if (!hsi) return;
    for (i = 0; i < hsi->height; i++) free(hsi->pixels[i]);
    free(hsi->pixels);
    free(hsi);
}



/* =========================================================
 * COLOR SPACE CONVERSION
 * ========================================================= */

HSIPixel rgb_to_hsi_pixel(RGBQuad rgb) {
    float    r, g, b, num, den, min_val;
    HSIPixel hsi;

    r = rgb.red   / 255.0f;
    g = rgb.green / 255.0f;
    b = rgb.blue  / 255.0f;

    num = 0.5f * ((r - g) + (r - b));
    den = sqrtf((r - g)*(r - g) + (r - b)*(g - b));

    hsi.h = (den == 0.0f)
            ? 0.0f
            : acosf(clampf(num / (den + 1e-6f), -1.0f, 1.0f)) * 180.0f / (float)M_PI;

    if (b > g) hsi.h = 360.0f - hsi.h;

    hsi.i   = (r + g + b) / 3.0f;
    min_val = minf3(r, g, b);
    hsi.s   = (hsi.i <= 0.0f) ? 0.0f : 1.0f - (min_val / hsi.i);

    return hsi;
}

RGBQuad hsi_to_rgb_pixel(HSIPixel hsi) {
    float   r, g, b, h, s, i;
    RGBQuad rgb;

    h = hsi.h;
    s = hsi.s;
    i = hsi.i;

    if (h < 120.0f) {
        b = i * (1.0f - s);
        r = i * (1.0f + (s * cosf(h * (float)M_PI / 180.0f)
                          / cosf((60.0f - h) * (float)M_PI / 180.0f)));
        g = 3.0f * i - (r + b);
    } else if (h < 240.0f) {
        h -= 120.0f;
        r  = i * (1.0f - s);
        g  = i * (1.0f + (s * cosf(h * (float)M_PI / 180.0f)
                           / cosf((60.0f - h) * (float)M_PI / 180.0f)));
        b  = 3.0f * i - (r + g);
    } else {
        h -= 240.0f;
        g  = i * (1.0f - s);
        b  = i * (1.0f + (s * cosf(h * (float)M_PI / 180.0f)
                           / cosf((60.0f - h) * (float)M_PI / 180.0f)));
        r  = 3.0f * i - (g + b);
    }

    rgb.blue     = (uint8_t)clampf(b * 255.0f, 0.0f, 255.0f);
    rgb.green    = (uint8_t)clampf(g * 255.0f, 0.0f, 255.0f);
    rgb.red      = (uint8_t)clampf(r * 255.0f, 0.0f, 255.0f);
    rgb.reserved = 0;

    return rgb;
}

HSIImage *convert_bmp_to_hsi(BMPImage *img) {
    int       h, w, i, j;
    HSIImage *out;
    RGBQuad   pixel;

    if (!img || img->info_header.bits_per_pixel != 24) return NULL;

    h   = abs(img->info_header.height_px);
    w   = img->info_header.width_px;
    out = createEmptyHSI(w, h);
    if (!out) return NULL;

    for (i = 0; i < h; i++) {
        unsigned char *row = img->pixels[i];
        for (j = 0; j < w; j++) {
            pixel.blue     = row[j * 3 + 0];
            pixel.green    = row[j * 3 + 1];
            pixel.red      = row[j * 3 + 2];
            pixel.reserved = 0;
            out->pixels[i][j] = rgb_to_hsi_pixel(pixel);
        }
    }
    return out;
}

void update_bmp_from_hsi(BMPImage *img, HSIImage *hsi) {
    int     i, j;
    RGBQuad rgb;

    if (!img || !hsi) return;

    for (i = 0; i < hsi->height; i++) {
        unsigned char *row = img->pixels[i];
        for (j = 0; j < hsi->width; j++) {
            rgb = hsi_to_rgb_pixel(hsi->pixels[i][j]);
            row[j * 3 + 0] = rgb.blue;
            row[j * 3 + 1] = rgb.green;
            row[j * 3 + 2] = rgb.red;
        }
    }
}



/* =========================================================
 * PIXEL OPERATIONS
 * ========================================================= */

BMPImage *extract_channel_info(BMPImage *input, int mode, int bytes) {
    int       w, h, i, j;
    BMPImage *output;
    uint8_t   bv, gv, rv;

    if (!input || input->info_header.bits_per_pixel != 24) return NULL;

    w      = input->info_header.width_px;
    h      = abs(input->info_header.height_px);
    output = createEmptyBMP(w, h, (bytes == 1) ? 8 : 24);
    if (!output) return NULL;

    for (i = 0; i < h; i++) {
        if (bytes == 1) {
            for (j = 0; j < w; j++) {
                bv = input->pixels[i][j * 3 + 0];
                gv = input->pixels[i][j * 3 + 1];
                rv = input->pixels[i][j * 3 + 2];
                switch (mode) {
                    case 0: output->pixels[i][j] = (uint8_t)(0.114f * bv); break;
                    case 1: output->pixels[i][j] = (uint8_t)(0.587f * gv); break;
                    case 2: output->pixels[i][j] = (uint8_t)(0.299f * rv); break;
                    case 3:
                    default:
                        output->pixels[i][j] = (uint8_t)(0.299f * rv
                                                        + 0.587f * gv
                                                        + 0.114f * bv);
                        break;
                }
            }
        } else {
            if (mode == 3) {
                memcpy(output->pixels[i], input->pixels[i], w * 3);
            } else {
                for (j = 0; j < w; j++) {
                    output->pixels[i][j * 3 + mode] =
                        input->pixels[i][j * 3 + mode];
                }
            }
        }
    }
    return output;
}

BMPImage *resize_nearest(BMPImage *input, int w_out, int h_out) {
    BMPImage *output;
    float     scale_x, scale_y;
    int       h_in, w_in, i, j, src_x, src_y;

    output  = createEmptyBMP(w_out, h_out, input->info_header.bits_per_pixel);
    if (!output) return NULL;

    scale_x = (float)w_out / input->info_header.width_px;
    scale_y = (float)h_out / abs(input->info_header.height_px);
    h_in    = abs(input->info_header.height_px);
    w_in    = input->info_header.width_px;

    for (i = 0; i < h_out; i++) {
        for (j = 0; j < w_out; j++) {
            src_x = clampi((int)(j / scale_x), 0, w_in - 1);
            src_y = clampi((int)(i / scale_y), 0, h_in - 1);
            output->pixels[i][j] = input->pixels[src_y][src_x];
        }
    }
    return output;
}

BMPImage *resize_bilinear(BMPImage *input, int w_out, int h_out) {
    BMPImage *output;
    int       w_in, h_in, i, j, x1, y1, x2, y2;
    float     x, y, wx, wy, val;

    output = createEmptyBMP(w_out, h_out, input->info_header.bits_per_pixel);
    if (!output) return NULL;

    w_in = input->info_header.width_px;
    h_in = abs(input->info_header.height_px);

    for (i = 0; i < h_out; i++) {
        for (j = 0; j < w_out; j++) {
            x  = (float)j * (w_in - 1) / (w_out - 1);
            y  = (float)i * (h_in - 1) / (h_out - 1);
            x1 = (int)x;
            y1 = (int)y;
            x2 = clampi(x1 + 1, 0, w_in - 1);
            y2 = clampi(y1 + 1, 0, h_in - 1);
            wx = x - x1;
            wy = y - y1;

            val = (1.0f - wx) * (1.0f - wy) * input->pixels[y1][x1]
                + wx           * (1.0f - wy) * input->pixels[y1][x2]
                + (1.0f - wx) *          wy  * input->pixels[y2][x1]
                + wx           *          wy  * input->pixels[y2][x2];

            output->pixels[i][j] = (uint8_t)roundf(val);
        }
    }
    return output;
}

BMPImage *linear_quantization(BMPImage *img, int bits) {
    int       levels, h, w, i, j;
    float     step;
    BMPImage *out;

    levels = (int)pow(2.0, bits);
    if (levels < 2 || !img) return NULL;

    step = 255.0f / (levels - 1);
    h    = abs(img->info_header.height_px);
    w    = img->info_header.width_px;
    out  = createEmptyBMP(w, h, 8);
    if (!out) return NULL;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            out->pixels[i][j] =
                (uint8_t)(roundf(img->pixels[i][j] / step) * step);
        }
    }
    return out;
}


/* =========================================================
 * DISPLAY / DIAGNOSTICS
 * ========================================================= */

void plot_histogram_gnuplot(BMPImage *img, const char *title) {
    int  hist[256];
    int  h, w, i, j;
    FILE *gp;

    memset(hist, 0, sizeof(hist));
    h = abs(img->info_header.height_px);
    w = img->info_header.width_px;

    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++)
            hist[img->pixels[i][j]]++;

    gp = popen("gnuplot -persist", "w");
    if (!gp) return;

    fprintf(gp, "set title '%s'\n", title);
    fprintf(gp, "set xlabel 'Intensity'\n");
    fprintf(gp, "set ylabel 'Count'\n");
    fprintf(gp, "plot '-' with boxes notitle\n");

    for (i = 0; i < 256; i++) fprintf(gp, "%d %d\n", i, hist[i]);
    fprintf(gp, "e\n");

    pclose(gp);
}
