#include <stdlib.h>
#include <stdio.h>
#define BIT_MIN 0
#define BIT_MAX 255
#define SCALE_FACTOR 50

int main(){


// Pointer to the image address we want to read from

FILE * fIn  = fopen("lenna_512.bmp", "rb");

if(fIn == NULL){	printf("Error: Could not open input file\n");
			return 1;
}



// Pointer to the new image address we want to open for writing
FILE * fOut  = fopen("lenna_scaled512.bmp", "wb");
if(fOut == NULL){	printf("Error: Could not open output file\n");
			return 1;
}

// Allocate space for the temporary storage image header and color table 
unsigned char image_hdr[54];
unsigned char color_table[1024];


// Read the first 54 characters from (image header) from the input and store it

for(int i =0; i <54 ; i++)
{
	image_hdr[i] = getc(fIn);
}

// Write the first 54 characters stored variables to the new image file 

fwrite(image_hdr, sizeof(unsigned char), 54, fOut);


// Accessing variables stored in the image header 
// Derefernce a pointer, type casted from a referenced variable 

int height = *(int*)&image_hdr[22];
int width = *(int *)&image_hdr[18];
int bitDepth  = *(int *)&image_hdr[28];
int imageSize = height* width;


// If the image is greyscale 
if(bitDepth<= 8)
{ 
	fread(color_table, sizeof(unsigned char), 1024, fIn);
	fwrite(color_table, sizeof(unsigned char), 1024, fOut);
}

unsigned char * buffer = (unsigned char *)malloc(imageSize);
if(buffer == NULL){	printf("Error: Could not allocate memory for buffer\n");
			return 1;
}

fread(buffer, sizeof(unsigned char), imageSize, fIn);

int temp;


for(int i = 0; i <= imageSize; i++)
{
// Alter the bit value of each character by some scale factor
	temp = buffer[i] + SCALE_FACTOR;

// If the temp exceeds the bit max value, set it to the max, leave it otherwise  	
	buffer[i] = (temp > BIT_MAX)? BIT_MAX : (temp < BIT_MIN)? BIT_MIN :temp;

}


fwrite(buffer, sizeof(unsigned char), imageSize, fOut);


free(buffer);
fclose(fIn);
fclose(fOut);


return 0;
}
