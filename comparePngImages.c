
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>



typedef struct {
    unsigned char r, g, b, a;
} PixelRGBA;


typedef struct {
    unsigned char* data;  // Pointer to RGB or RGBA data
    int width;
    int height;
    int channels;         // 3 for RGB, 4 for RGBA
    long totalLengthInPixels;
    long pixelsProcessed; // 0
} RawImage;


enum Error { NoError, OpenFileError, ReadFileError, MemAllocError, PngError, WriteFileError} err;

char* errorMessages[] = {
    "No errors",
    "Can't open file",
    "Can't read file",
    "Can't allocate enough memory",
    "Error related to libpng",
    "Can't write file"
};


void readPngFile(const char* filename, RawImage *image) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        err = OpenFileError;
        return;
    }

    // Create and initialize png_struct
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        err = PngError;
        return;
    }

    // Create and initialize png_info
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        err = PngError;
        return;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        err = PngError;
        return;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    // Get image info
    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    // Adjustments based on color type
    if (bit_depth == 16) {
        png_set_strip_16(png);  // Reduce 16-bit images to 8-bit
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);  // Convert palette to RGB
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);  // Expand grayscale
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);  // Add alpha if transparency info is present
    }
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);  // Add alpha channel if needed
    }

    png_read_update_info(png, info);

    int channels = png_get_channels(png, info);  // Get the number of channels

    // Allocate memory for image data
    unsigned char* data = (unsigned char*)malloc(width * height * channels);
    if (!data) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        err = MemAllocError;
        return;
    }

    png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
    for (int y = 0; y < height; y++) {
        row_pointers[y] = data + y * width * channels;
    }

    // Read the image
    png_read_image(png, row_pointers);

    // Cleanup
    fclose(fp);
    png_destroy_read_struct(&png, &info, NULL);
    free(row_pointers);

    // Store image data
    image->data = data;
    image->width = width;
    image->height = height;
    image->channels = channels;
    image->pixelsProcessed = 0;
    image->totalLengthInPixels = width*height;
    err = NoError;
}


int arePixelsSame(PixelRGBA a, PixelRGBA b) {
    return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;
}


int main( int argc, char ** argv ) {

    if (argc != 3) {
        printf("Usage: comparePngImages img1.png img2.png\n");
        return 1;
    }

    RawImage image1, image2;

    readPngFile(argv[1], &image1);

    if (err != NoError) {
        printf("%s\n", errorMessages[err]);
        return 1;
    }

    readPngFile(argv[2], &image2);

    if (err != NoError) {
        printf("%s", errorMessages[err]);
        return 1;
    }

    if (image1.width != image2.width) {
        printf("Image1 width %d, Image2 width %d\n", image1.width, image2.width);
        return 1;
    }

    if (image1.height != image2.height) {
        printf("Image1 height %d, Image2 height %d\n", image1.height, image2.height);
        return 1;
    }

    for (int i = 0; i<image1.height; i++) {
        for (int j = 0; j<image1.width; j++) {
            int index = i*image1.width + j;

            int offset1 = index * image1.channels;
            int offset2 = index * image2.channels;

            PixelRGBA p1 = *(PixelRGBA*) &image1.data[offset1];
            PixelRGBA p2 = *(PixelRGBA*) &image2.data[offset2];

            if (image1.channels == 3) p1.a = 255;
            if (image2.channels == 3) p2.a = 255;

        
            printf("%3d, %3d: (%3hhu,%3hhu,%3hhu,%3hhu) vs (%3hhu,%3hhu,%3hhu,%3hhu) %s\n",
            j, i,
            image1.data[offset1],
            image1.data[offset1+1],
            image1.data[offset1+2],
            image1.channels==3 ? 255 : image1.data[offset1+3],
            image2.data[offset2],
            image2.data[offset2+1],
            image2.data[offset2+2],
            image2.channels==3 ? 255 : image2.data[offset2+3],
            arePixelsSame(p1, p2) ? "" : "!"
            );
            
        }
    }



    return 0;
}

