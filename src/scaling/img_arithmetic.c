#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h> // Make sure to link with -ljpeg

#define BIT_MIN 0
#define BIT_MAX 255
#define SCALE_FACTOR 50

int main() {
    const char *input_filename = "lenna.jpg";
    const char *output_filename = "lenna_scaled.jpg";

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *infile;
    JSAMPARRAY buffer;
    int row_stride;

    // 1. Open Input File
    if ((infile = fopen(input_filename, "rb")) == NULL) {
        fprintf(stderr, "Error: Could not open input file %s\n", input_filename);
        return 1;
    }

    // 2. Initialize Decompressor
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    // Get image dimensions
    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int num_components = cinfo.output_components; // 3 for RGB, 1 for Greyscale
    row_stride = width * num_components;

    // 3. Prepare Output File
    struct jpeg_compress_struct cinfo_out;
    struct jpeg_error_mgr jerr_out;
    FILE *outfile;
    JSAMPROW row_pointer[1];

    if ((outfile = fopen(output_filename, "wb")) == NULL) {
        fprintf(stderr, "Error: Could not open output file %s\n", output_filename);
        return 1;
    }

    cinfo_out.err = jpeg_std_error(&jerr_out);
    jpeg_create_compress(&cinfo_out);
    jpeg_stdio_dest(&cinfo_out, outfile);

    cinfo_out.image_width = width;
    cinfo_out.image_height = height;
    cinfo_out.input_components = num_components;
    cinfo_out.in_color_space = JCS_RGB; // Output as RGB
    jpeg_set_defaults(&cinfo_out);
    jpeg_set_quality(&cinfo_out, 90, TRUE); // Quality 90
    jpeg_start_compress(&cinfo_out, TRUE);

    // 4. Process Pixels (Read -> Modify -> Write)
    // Allocate a 1-row buffer
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);

        // Apply brightness adjustment
        for (int i = 0; i < row_stride; i++) {
            int temp = buffer[0][i] + SCALE_FACTOR;
            buffer[0][i] = (temp > BIT_MAX) ? BIT_MAX : (temp < BIT_MIN) ? BIT_MIN : temp;
        }

        // Write row
        row_pointer[0] = buffer[0];
        jpeg_write_scanlines(&cinfo_out, row_pointer, 1);
    }

    // 5. Cleanup
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    jpeg_finish_compress(&cinfo_out);
    jpeg_destroy_compress(&cinfo_out);
    fclose(infile);
    fclose(outfile);

    printf("Image saved to %s\n", output_filename);
    return 0;
}
