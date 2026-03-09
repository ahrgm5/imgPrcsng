#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <cstring>
#include <algorithm>
#include "bmp_handling.hpp"

void freeBMPImage(BMPImage* img) {
    if (img) {
        if (img->pixels) {
            int height = abs(img->info_header.height_px);
            for (int i = 0; i < height; i++) {
                if (img->pixels[i]) free(img->pixels[i]);
            }
            free(img->pixels);
        }
        free(img);
    }
}

int allocatePixelMemory(BMPImage* img) {
    if (!img) return -1;
    int height = abs(img->info_header.height_px);
    int width = img->info_header.width_px;
    int bpp = img->info_header.bits_per_pixel;
    int bytes_per_pixel = (bpp <= 8) ? 1 : (bpp / 8);
    int row_width = width * bytes_per_pixel;

    img->pixels = (unsigned char**)malloc(height * sizeof(unsigned char*));
    if (!img->pixels) return -1;

    for (int i = 0; i < height; i++) {
        img->pixels[i] = (unsigned char*)calloc(1, row_width);
        if (!img->pixels[i]) return -1;
    }
    return 0;
}

BMPImage* createEmptyBMP(int width, int height, int bpp) {
    BMPImage* img = (BMPImage*)calloc(1, sizeof(BMPImage));
    if (!img) return NULL;
    img->info_header.width_px = width;
    img->info_header.height_px = height;
    img->info_header.bits_per_pixel = bpp;
    img->info_header.num_planes = 1;
    img->info_header.dib_header_size = sizeof(BMPInfoHeader);

    if (bpp <= 8) {
        for (int i = 0; i < 256; i++) img->colors[i] = (RGBQuad){(uint8_t)i, (uint8_t)i, (uint8_t)i, 0};
    }

    if (allocatePixelMemory(img) != 0) {
        free(img);
        return NULL;
    }
    return img;
}

BMPImage* readBMP(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        printf("Could not read %s.\n", filename);
        return NULL;
    }

    BMPImage* img = (BMPImage*)calloc(1, sizeof(BMPImage));
    fread(&(img->file_header), sizeof(BMPFileHeader), 1, fp);
    fread(&(img->info_header), sizeof(BMPInfoHeader), 1, fp);

    if (img->info_header.bits_per_pixel <= 8) {
        int colors_to_read = img->info_header.num_colors ? img->info_header.num_colors : 256;
        fread(img->colors, sizeof(RGBQuad), colors_to_read, fp);
    }

    if (allocatePixelMemory(img) != 0) {
        fclose(fp);
        free(img);
        return NULL;
    }

    int bpp = img->info_header.bits_per_pixel;
    int bytes_per_pixel = (bpp <= 8) ? 1 : (bpp / 8);
    int width = img->info_header.width_px;
    int height = abs(img->info_header.height_px);
    int unpadded_row_size = width * bytes_per_pixel;
    int padded_row_size = (unpadded_row_size + 3) & (~3);

    fseek(fp, img->file_header.offset, SEEK_SET);
    for (int i = 0; i < height; i++) {
        int idx = (img->info_header.height_px > 0) ? (height - 1 - i) : i;
        fread(img->pixels[idx], 1, unpadded_row_size, fp);
        fseek(fp, padded_row_size - unpadded_row_size, SEEK_CUR);
    }
    fclose(fp);
    return img;
}

void writeBMP(const char* filename, BMPImage* img) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return;

    int bpp = img->info_header.bits_per_pixel;
    int bytes_per_pixel = (bpp <= 8) ? 1 : (bpp / 8);
    int width = img->info_header.width_px;
    int height = abs(img->info_header.height_px);
    int unpadded_row_size = width * bytes_per_pixel;
    int padded_row_size = (unpadded_row_size + 3) & (~3);

    img->file_header.type = 0x4D42;
    img->file_header.offset = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + (bpp <= 8 ? 1024 : 0);
    img->file_header.size = img->file_header.offset + (padded_row_size * height);

    fwrite(&(img->file_header), sizeof(BMPFileHeader), 1, fp);
    fwrite(&(img->info_header), sizeof(BMPInfoHeader), 1, fp);

    if (bpp <= 8) fwrite(img->colors, sizeof(RGBQuad), 256, fp);

    uint8_t padding[3] = {0};
    int padding_len = padded_row_size - unpadded_row_size;

    for (int i = 0; i < height; i++) {
        int idx = (img->info_header.height_px > 0) ? (height - 1 - i) : i;
        fwrite(img->pixels[idx], 1, unpadded_row_size, fp);
        fwrite(padding, 1, padding_len, fp);
    }
    fclose(fp);
}

BMPImage* extract_channel_info(BMPImage* input, int mode, int bytes) {
    if (!input || input->info_header.bits_per_pixel != 24) return NULL;
    int w = input->info_header.width_px, h = abs(input->info_header.height_px);
    BMPImage* output = createEmptyBMP(w, h, (bytes == 1) ? 8 : 24);

    for (int i = 0; i < h; i++) {
        if (bytes == 1) {
            for (int j = 0; j < w; j++) {
                uint8_t b = input->pixels[i][j * 3 + 0], g = input->pixels[i][j * 3 + 1], r = input->pixels[i][j * 3 + 2];
                switch(mode) {
                    case 0: output->pixels[i][j] = (uint8_t)(0.114f*b); break;
                    case 1: output->pixels[i][j] = (uint8_t)(0.587f*g); break;
                    case 2: output->pixels[i][j] = (uint8_t)(0.299f*r); break;
                    case 3: output->pixels[i][j] = (uint8_t)(0.299f*r + 0.587f*g + 0.114f*b); break;
                }
            }
        } else {
            if (mode == 3) {
                // FIXED: Use memcpy to avoid pointer aliasing which causes double-free
                std::memcpy(output->pixels[i], input->pixels[i], w * 3);
            } else {
                for (int j = 0; j < w; j++) {
                    output->pixels[i][j*3 + mode] = input->pixels[i][j*3 + mode];
                }
            }
        }
    }
    return output;
}

BMPImage* resize_nearest(BMPImage* input, int w_out, int h_out) {
    BMPImage* output = createEmptyBMP(w_out, h_out, input->info_header.bits_per_pixel);
    float scale_x = (float)w_out / input->info_header.width_px;
    float scale_y = (float)h_out / abs(input->info_header.height_px);
    int h_in = abs(input->info_header.height_px), w_in = input->info_header.width_px;

    for (int i = 0; i < h_out; i++) {
        for (int j = 0; j < w_out; j++) {
            int src_x = std::min((int)(j / scale_x), w_in - 1);
            int src_y = std::min((int)(i / scale_y), h_in - 1);
            output->pixels[i][j] = input->pixels[src_y][src_x];
        }
    }
    return output;
}

BMPImage* resize_bilinear(BMPImage* input, int w_out, int h_out) {
    BMPImage* output = createEmptyBMP(w_out, h_out, input->info_header.bits_per_pixel);
    int w_in = input->info_header.width_px, h_in = abs(input->info_header.height_px);

    for (int i = 0; i < h_out; i++) {
        for (int j = 0; j < w_out; j++) {
            float x = (float)j * (w_in - 1) / (w_out - 1);
            float y = (float)i * (h_in - 1) / (h_out - 1);
            int x1 = (int)x, y1 = (int)y;
            int x2 = std::min(x1 + 1, w_in - 1), y2 = std::min(y1 + 1, h_in - 1);
            float wx = x - x1, wy = y - y1;

            float val = (1-wx)*(1-wy)*input->pixels[y1][x1] + wx*(1-wy)*input->pixels[y1][x2] +
                        (1-wx)*wy*input->pixels[y2][x1] + wx*wy*input->pixels[y2][x2];
            output->pixels[i][j] = (uint8_t)std::round(val);
        }
    }
    return output;
}

BMPImage* linear_quantization(BMPImage* img, int bits) {
    int levels = (int)pow(2, bits);
    if (levels < 2 || !img) return NULL;
    float step = 255.0f / (levels - 1);
    int h = abs(img->info_header.height_px), w = img->info_header.width_px;
    BMPImage* out = createEmptyBMP(w, h, 8);
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            out->pixels[i][j] = (uint8_t)(std::round(img->pixels[i][j] / step) * step);
        }
    }
    return out;
}

void plot_histogram_gnuplot(BMPImage* img, const char* title) {
    int hist[256] = {0}, h = abs(img->info_header.height_px), w = img->info_header.width_px;
    for (int i = 0; i < h; i++) for (int j = 0; j < w; j++) hist[img->pixels[i][j]]++;
    FILE *gp = popen("gnuplot -persist", "w");
    if (!gp) return;
    fprintf(gp, "set title '%s'\nset xlabel 'Intensity'\nset ylabel 'Count'\n", title);
    fprintf(gp, "plot '-' with boxes notitle\n");
    for (int i = 0; i < 256; i++) fprintf(gp, "%d %d\n", i, hist[i]);
    fprintf(gp, "e\n"); pclose(gp);
}
