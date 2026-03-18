#pragma once
#include "bmp.h"

static void show_bmp(BMPImage *img, const char *label) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/jup_%p.bmp", (void*)img);
    writeBMP(path, img);
    printf("__DISPLAY_BMP__:%s\n", path);
    fflush(stdout);
}


/*
static void show_bmps(BMPImages imgs, const char *label) {

    for(int i =0; i <count; i++){
           
    char path[64];
    snprintf(path, sizeof(path), "/tmp/jup_%p.bmp", (void*)imgs.images[i]);
    writeBMP(path, imgs.images[i]);
    printf("__DISPLAY_BMP__:%s\n", path);
    fflush(stdout);

    }

}

*/
static void show_bmp_row(BMPImages imgs, const char **titles, int n) {
    int count = imgs.count;
    if (count == 0) {
        fprintf(stderr, "[show_bmp_row] no images to display\n");
        return;
    }

    // Write all BMPs to disk first, before touching stdout
    char paths[32][64];  // adjust size to your max expected count
    for (int i = 0; i < count; i++) {
        if (!imgs.images[i]) {
            fprintf(stderr, "[show_bmp_row] images[%d] is NULL, aborting row\n", i);
            return;
        }
        snprintf(paths[i], sizeof(paths[i]),
                 "/tmp/jup_row_%d_%p.bmp", i, (void*)imgs.images[i]);
        writeBMP(paths[i], imgs.images[i]);
    }

    // Now build and flush the protocol line atomically
    printf("__DISPLAY_BMP_ROW__:");
    for (int i = 0; i < count; i++) {
        if (i > 0) printf("|");
        printf("%s|%s", paths[i], titles && titles[i] ? titles[i] : "");
    }
    printf("\n");
    fflush(stdout);
}