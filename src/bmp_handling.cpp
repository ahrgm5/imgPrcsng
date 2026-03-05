#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <algorithm> // for std::min
#include "bmp_handling.hpp"


// gcc bmp_handling.c -o output -lm 
// gnuplot is a dependency no linked to compiler





// Convert a single RGB triplet to HSI
HSIPixel rgb_to_hsi_pixel(uint8_t r_in, uint8_t g_in, uint8_t b_in) {
    float r = r_in / 255.0f;
    float g = g_in / 255.0f;
    float b = b_in / 255.0f;

    HSIPixel hsi;
    float num = 0.5f * ((r - g) + (r - b));
    float den = sqrtf((r - g)*(r - g) + (r - b)*(g - b));
    
    // Hue calculation
    if (den == 0) hsi.h = 0;
    else {
        float theta = acosf(num / den) * 180.0f / M_PI;
        hsi.h = (b <= g) ? theta : (360.0f - theta);
    }

    // Intensity calculation
    hsi.i = (r + g + b) / 3.0f;

    // Saturation calculation
    float min_val = std::min({r, g, b});
    if (hsi.i == 0) hsi.s = 0;
    else hsi.s = 1.0f - (min_val / hsi.i);

    return hsi;
}

// Convert HSI back to RGB
void hsi_to_rgb_pixel(HSIPixel hsi, uint8_t &r_out, uint8_t &g_out, uint8_t &b_out) {
    float r, g, b;
    float h = hsi.h;
    float s = hsi.s;
    float i = hsi.i;

    if (h < 120) {
        b = i * (1 - s);
        r = i * (1 + (s * cosf(h * M_PI / 180.0f) / cosf((60 - h) * M_PI / 180.0f)));
        g = 3 * i - (r + b);
    } else if (h < 240) {
        h -= 120;
        r = i * (1 - s);
        g = i * (1 + (s * cosf(h * M_PI / 180.0f) / cosf((60 - h) * M_PI / 180.0f)));
        b = 3 * i - (r + g);
    } else {
        h -= 240;
        g = i * (1 - s);
        b = i * (1 + (s * cosf(h * M_PI / 180.0f) / cosf((60 - h) * M_PI / 180.0f)));
        r = 3 * i - (g + b);
    }

    r_out = (uint8_t)std::clamp(r * 255.0f, 0.0f, 255.0f);
    g_out = (uint8_t)std::clamp(g * 255.0f, 0.0f, 255.0f);
    b_out = (uint8_t)std::clamp(b * 255.0f, 0.0f, 255.0f);
}

// Helper to convert an entire 24-bit BMP to an HSI buffer
HSIPixel** convert_rgb_to_hsi(BMPImage* img) {
    if (img->info_header.bits_per_pixel != 24) return nullptr;

    int h = abs(img->info_header.height_px);
    int w = img->info_header.width_px;
    HSIPixel** buffer = (HSIPixel**)malloc(h * sizeof(HSIPixel*));

    for (int i = 0; i < h; i++) {
        buffer[i] = (HSIPixel*)malloc(w * sizeof(HSIPixel));
        for (int j = 0; j < w; j++) {
            // BMP stores pixels as BGR
            uint8_t b = img->pixels[i][j * 3];
            uint8_t g = img->pixels[i][j * 3 + 1];
            uint8_t r = img->pixels[i][j * 3 + 2];
            buffer[i][j] = rgb_to_hsi_pixel(r, g, b);
        }
    }
    return buffer;
}

void freeHSIBuffer(HSIPixel** buffer, int height) {
    if (buffer) {
        for (int i = 0; i < height; i++) {
            if (buffer[i]) {
                free(buffer[i]);
            }
        }
        free(buffer);
    }
}

// --- 2. Dedicated Memory Management ---
void freeBMPImage(BMPImage* img) {
    if (img) {
        if (img->pixels) {
            for (int i = 0; i < abs(img->info_header.height_px); i++) free(img->pixels[i]);
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
    int row_width = width * bytes_per_pixel; // size of the row in terms of memory

    img->pixels = (unsigned char**)malloc(height * sizeof(unsigned char*)); // Allocation of pixel memory for the first column 
    if (!img->pixels) return -1;

    for (int i = 0; i < height; i++) {
        img->pixels[i] = (unsigned char*)calloc(1, row_width); // Allocation of pixel memory of each row at the ith position in the first column
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
    
    allocatePixelMemory(img);
    return img;
}

// --- 3. File I/O ---

BMPImage* readBMP(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp){
    printf("Could not read %s. Please make sure the file exists.\n", filename);
    return NULL;}

    BMPImage* img = (BMPImage*)calloc(1, sizeof(BMPImage));
    fread(&(img->file_header), sizeof(BMPFileHeader), 1, fp);
    fread(&(img->info_header), sizeof(BMPInfoHeader), 1, fp);

    // Only read color table if the image is 8-bit or less
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
    
    // Calculate the row size including padding (must be multiple of 4)
    int unpadded_row_size = width * bytes_per_pixel;
    int padded_row_size = (unpadded_row_size + 3) & (~3);

    fseek(fp, img->file_header.offset, SEEK_SET);
    for (int i = 0; i < height; i++) {
        // BMP is stored bottom-up: index the 2D array accordingly
        int idx = (img->info_header.height_px > 0) ? (height - 1 - i) : i;
        fread(img->pixels[idx], 1, unpadded_row_size, fp);
        // Skip padding bytes at the end of the row
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

    // Update headers for correct offset and size
    img->file_header.type = 0x4D42;
    img->file_header.offset = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + (bpp <= 8 ? 1024 : 0);
    img->file_header.size = img->file_header.offset + (padded_row_size * height);

    fwrite(&(img->file_header), sizeof(BMPFileHeader), 1, fp);
    fwrite(&(img->info_header), sizeof(BMPInfoHeader), 1, fp);
    
    if (bpp <= 8) {
        fwrite(img->colors, sizeof(RGBQuad), 256, fp);
    }

    uint8_t padding[3] = {0};
    int padding_len = padded_row_size - unpadded_row_size;

    for (int i = 0; i < height; i++) {
        int idx = (img->info_header.height_px > 0) ? (height - 1 - i) : i;
        fwrite(img->pixels[idx], 1, unpadded_row_size, fp);
        fwrite(padding, 1, padding_len, fp);
    }
    fclose(fp);
}

// --- 4. Color to Grayscale & Channel Magnitude ---



BMPImage * extract_channel_info(BMPImage* input, int mode, int bytes) {
    if (input->info_header.bits_per_pixel != 24) {
        printf("Error: Extraction requires 24-bit color input.\n");
        return NULL;
    }

    int w = input->info_header.width_px;
    int h = abs(input->info_header.height_px);
    
    
    BMPImage * output;
    
    if (bytes== 1){ output = createEmptyBMP(w, h, 8);
    
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // 24-bit BMP stores pixels as B, G, R
            uint8_t b = input->pixels[i][j * 3 + 0];
            uint8_t g = input->pixels[i][j * 3 + 1]; // Each color has an offset from the column address
            uint8_t r = input->pixels[i][j * 3 + 2];

            switch(mode) {
                case 0: output->pixels[i][j] = (uint8_t)(0.114f*b); break; // Blue Magnitude
                case 1: output->pixels[i][j] = (uint8_t)(0.587f*g); break; // Green Magnitude
                case 2: output->pixels[i][j] = (uint8_t)(0.299f*r); break; // Red Magnitude
                case 3: output->pixels[i][j] = (uint8_t)(0.299f*r + 0.587f*g + 0.114f*b);  // Luminosity Grayscale
	    
		    break;
            }
        }
    }
    
}
    if (bytes == 3){ output = createEmptyBMP(w,h,24);
    
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            // 24-bit BMP stores pixels as B, G, R
            uint8_t b = input->pixels[i][j * 3 + 0];
            uint8_t g = input->pixels[i][j * 3 + 1]; // Each color has an offset from the column address
            uint8_t r = input->pixels[i][j * 3 + 2];

            switch(mode) {
                case 0: output->pixels[i][j*3 + 0] = b; break; // Blue Magnitude
                case 1: output->pixels[i][j*3 + 1] = g; break; // Green Magnitude
                case 2: output->pixels[i][j*3 + 2] = r; break; // Red Magnitude
                case 3: output->pixels[i] = input->pixels[i]; // Original image 
	    
		    break;
            }
        }
    }
    
    }
    return output;
}


// --- 5. Interpolation Functions ---

// Nearest Neighbor: Simple sampling
BMPImage* resize_nearest(BMPImage* input, int w_out, int h_out) {

    BMPImage* output = createEmptyBMP(w_out, h_out, input->info_header.bits_per_pixel);
    
    float scale_x = (float)w_out / input->info_header.width_px;
    float scale_y = (float)h_out / abs(input->info_header.height_px);

    int h_in = abs(input->info_header.height_px);
    int w_in = input->info_header.width_px;

    for (int i = 0; i < h_out; i++) {
        for (int j = 0; j < w_out; j++) {
            int src_x = (int)floor(j / scale_x);
            int src_y = (int)floor(i / scale_y);

            if (src_x >= w_in) src_x = w_in - 1;
            if (src_y >= h_in) src_y = h_in - 1;

            output->pixels[i][j] = input->pixels[src_y][src_x];
        }
    }
    return output;
}



// Bilinear Interpolation: Weighted average
BMPImage* resize_bilinear(BMPImage* input, int w_out, int h_out) {

    BMPImage* output = createEmptyBMP(w_out, h_out, input->info_header.bits_per_pixel);
    
    int w_in = input->info_header.width_px;
    int h_in = abs(input->info_header.height_px);

    for (int i = 0; i < h_out; i++) {
        for (int j = 0; j < w_out; j++) {
            float x = (float)j * (float)(w_in - 1) / (float)(w_out - 1);
            float y = (float)i * (float)(h_in - 1) / (float)(h_out - 1);

            int x1 = (int)floor(x);
            int y1 = (int)floor(y);
            int x2 = (int)ceil(x);
            int y2 = (int)ceil(y);

            if (x2 >= w_in) x2 = w_in - 1;
            if (y2 >= h_in) y2 = h_in - 1;

            float wx = x - x1;
            float wy = y - y1;

            float q11 = (float)input->pixels[y1][x1];
            float q21 = (float)input->pixels[y1][x2];
            float q12 = (float)input->pixels[y2][x1];
            float q22 = (float)input->pixels[y2][x2];

            float r1 = q11 * (1.0f - wx) + q21 * wx;
            float r2 = q12 * (1.0f - wx) + q22 * wx;
            float val = r1 * (1.0f - wy) + r2 * wy;

            output->pixels[i][j] = (uint8_t)round(val);
        }
    }
    return output;
}

// --- 5. Linear (Uniform) Quantizer ---
// Maps [0-255] to 'levels' uniformly distributed values
BMPImage * linear_quantization(BMPImage* img, int bits) {
   
   int levels = (int)pow(2,bits);
   
   if (levels < 2) return NULL;
  

    // Uniform step size to preserve range 0 to 255
    float step = 255.0f / (levels - 1);

    int height = abs(img->info_header.height_px);
    int width = img->info_header.width_px;
    
    BMPImage* new_img = createEmptyBMP(width, height, 8);

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            uint8_t old_pixel = img->pixels[i][j];
            
            // 1. Determine level index
            float level_idx = round(old_pixel / step);
            // 2. Map back to 0-255 based on that level
            uint8_t new_pixel = (uint8_t)(level_idx * step);

            new_img->pixels[i][j] = new_pixel;
        }
    }
return new_img;
}



// --- 6. Gnuplot Visualization ---
void plot_histogram_gnuplot(BMPImage* img, const char* title) {
    int hist[256] = {0};
    int height = abs(img->info_header.height_px);
    int width = img->info_header.width_px;

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            hist[img->pixels[i][j]]++;    // For a histogram index ranging 0-255, the value of each pixel is accounted for row by row
        }
    }

    FILE *gp = popen("gnuplot -persist", "w"); // writing a process file 'gp' to the terminal
    if (gp == NULL) {
        printf("Error: Could not open gnuplot. Ensure it is installed.\n");
        return;
    }

    fprintf(gp, "set title '%s'\n", title);
    fprintf(gp, "set xlabel 'Pixel Intensity (0-255)'\n");
    fprintf(gp, "set ylabel 'Count'\n");
    fprintf(gp, "set style fill solid 0.5 border -1\n");
    fprintf(gp, "set boxwidth 0.5\n");
    fprintf(gp, "set grid y\n");
    fprintf(gp, "plot '-' using 1:2 with boxes notitle\n");

    for (int i = 0; i < 256; i++) {
        fprintf(gp, "%d %d\n", i, hist[i]);
    }

    fprintf(gp, "e\n");
    pclose(gp);
}


