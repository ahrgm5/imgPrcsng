// spatial_kernels.hpp
#ifndef SPATIAL_KERNELS_HPP
#define SPATIAL_KERNELS_HPP

#include "bmp_handling.hpp"
#include "hsi_handling.hpp"
#include <stddef.h>

typedef struct Kernel {
    int width, height;
    float** mask;
    void (*initialize)(struct Kernel* self);
} Kernel;

typedef struct { Kernel base; } AverageKernel;
typedef struct { Kernel base; float k;} LaplacianKernel;
typedef struct { Kernel base; double sigma; } GaussianKernel;
typedef struct { Kernel base; bool horizontal; } SobelKernel;

Kernel* create_kernel(int w, int h, void (*init_func)(Kernel*), size_t struct_size);
void destroy_kernel(Kernel* k);

void average_init(Kernel* self);
void laplacian_init(Kernel* self);
void gaussian_init(Kernel* self);
void sobel_init(Kernel* self);

// Now correctly returns a new image
BMPImage* apply_kernel_to_bmp(BMPImage* input, Kernel* k);

#endif
