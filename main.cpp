#include <iostream>
#include "bmp_handling.hpp"
#include "img_manip.hpp"

int main() {
    std::string path = "/home/ahrgm/Projects/imgPrcsng/Content/Course_Materials/HW/HW2/";
    
    std::string src_images = "/home/ahrgm/Projects/imgPrcsng/imgPrcsng/src/images/";
    
    
    
    BMPImage* leaf = readBMP((src_images + "/leaf/leaf.bmp").c_str());
if (leaf) {
    // Apply unsharp masking with k=1.0
    BMPImage* sharp_leaf = unsharp_masking(leaf, 0.5f, true);
    BMPImage* sharp_leaf2 = sharpen_laplacian(leaf, .75f, true);   
    writeBMP((path + "Problem2/unmask_sharpened.bmp").c_str(), sharp_leaf);
    writeBMP((path + "Problem2/laplacian_sharpened.bmp").c_str(), sharp_leaf2);
    
    freeBMPImage(leaf);
    freeBMPImage(sharp_leaf);
    freeBMPImage(sharp_leaf2);
}


    // 2.1 Equalization
    BMPImage* pout = readBMP((src_images + "pout.bmp").c_str());
    if (pout) {
        BMPImage* eq = histogram_equalization_custom(pout);
        writeBMP((path + "Problem2/pout_result.bmp").c_str(), eq);
        freeBMPImage(pout); freeBMPImage(eq);
    }

    // 4.1 FFTW Centered Analysis
    BMPImage* sunflower = readBMP((src_images + "sunflower.bmp").c_str());
    if (sunflower) {
        run_fft_analysis_centered(sunflower, "Centered DFT Magnitude");
        freeBMPImage(sunflower);
    }

    return 0;
}
