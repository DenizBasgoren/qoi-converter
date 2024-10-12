
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>


typedef struct {
    char magic[4]; // magic bytes "qoif"
    char width[4]; // image width in pixels (BE)
    char height[4]; // image height in pixels (BE)
    uint8_t channels; // 3 = RGB, 4 = RGBA
    uint8_t colorspace; // 0 = sRGB with linear alpha
    // 1 = all channels linear
} QoifHeader;

typedef struct {
    unsigned char r, g, b, a;
} PixelRGBA;

typedef struct {
    unsigned char* data;
    long bytesAdded;
} QoifImage;

typedef struct {
    unsigned char* data;  // Pointer to RGB or RGBA data
    int width;
    int height;
    int channels;         // 3 for RGB, 4 for RGBA
    long totalLengthInPixels;
    long pixelsProcessed; // 0
} RawImage;

typedef struct {
    unsigned char r, g, b;
} ChunkRGB;

typedef struct {
    unsigned char r, g, b, a;
} ChunkRGBA;

typedef struct {
    char index;
} ChunkINDEX;

typedef struct {
    signed char dr, dg, db;
} ChunkDIFF;

typedef struct {
    signed char dg, drdg, dbdg;
} ChunkLUMA;

typedef struct {
    char run;
} ChunkRUN;

typedef struct {
    int type; // 0=RGB, 1=RGBA, 2=INDEX, 3=DIFF, 4=LUMA, 5=RUN
    int pixelsCovered; // 1 for all except RUN
    union {
        ChunkRGB RGB;
        ChunkRGBA RGBA;
        ChunkINDEX INDEX;
        ChunkDIFF DIFF;
        ChunkLUMA LUMA;
        ChunkRUN RUN;
    };
} QoifChunk;


enum Error { NoError, OpenFileError, ReadFileError, MemAllocError, PngError, WriteFileError} err;

char* errorMessages[] = {
    "No errors",
    "Can't open file",
    "Can't read file",
    "Can't allocate enough memory",
    "Error related to libpng",
    "Can't write file"
};

void writeChunkRGB(QoifImage *qoif, QoifChunk chunk) {
    unsigned char* startAddr = qoif->data + qoif->bytesAdded;
    startAddr[0] = 0xfe;
    startAddr[1] = chunk.RGB.r;
    startAddr[2] = chunk.RGB.g;
    startAddr[3] = chunk.RGB.b;
    qoif->bytesAdded += 4;
}

void writeChunkRGBA(QoifImage *qoif, QoifChunk chunk) {
    unsigned char* startAddr = qoif->data + qoif->bytesAdded;
    startAddr[0] = 0xff;
    startAddr[1] = chunk.RGBA.r;
    startAddr[2] = chunk.RGBA.g;
    startAddr[3] = chunk.RGBA.b;
    startAddr[4] = chunk.RGBA.a;
    qoif->bytesAdded += 5;
}

void writeChunkINDEX(QoifImage *qoif, QoifChunk chunk) {
    // Note: assuming index<64. Safe if not.
    unsigned char* startAddr = qoif->data + qoif->bytesAdded;
    startAddr[0] = chunk.INDEX.index;
    startAddr[0] &= 0x3f;
    qoif->bytesAdded += 1;
}

void writeChunkDIFF(QoifImage *qoif, QoifChunk chunk) {
    // Note: assuming -2 <= dr,dg,db <= 1. Safe if not.
    unsigned char* startAddr = qoif->data + qoif->bytesAdded;
    startAddr[0] = 0x40;
    startAddr[0] |= ((chunk.DIFF.dr+2) & 0x03) << 4;
    startAddr[0] |= ((chunk.DIFF.dg+2) & 0x03) << 2;
    startAddr[0] |= ((chunk.DIFF.db+2) & 0x03);
    qoif->bytesAdded += 1;
}

void writeChunkLUMA(QoifImage *qoif, QoifChunk chunk) {
    // Note: assuming -32 <= dg <= 31   and   -8 <= drdg,dbdg <= 7
    unsigned char* startAddr = qoif->data + qoif->bytesAdded;
    startAddr[0] = 0x80;
    startAddr[0] |= ((chunk.LUMA.dg+32) & 0x3f);
    startAddr[1] = 0;
    startAddr[1] |= ((chunk.LUMA.drdg+8) & 0x0f) << 4;
    startAddr[1] |= ((chunk.LUMA.dbdg+8) & 0x0f);
    qoif->bytesAdded += 2;
}

void writeChunkRUN(QoifImage *qoif, QoifChunk chunk) {
    // Note: assuming 1 <= run <= 62. Safe if not.
    unsigned char* startAddr = qoif->data + qoif->bytesAdded;
    startAddr[0] = 0xc0;
    startAddr[0] |= ((chunk.RUN.run-1) & 0x3f);
    qoif->bytesAdded += 1;
}

void writeHeader(QoifImage *qoif, int w, int h, int isRGBA) {
    QoifHeader* header = (QoifHeader*) (qoif->data + qoif->bytesAdded);
    header->magic[0] = 'q';
    header->magic[1] = 'o';
    header->magic[2] = 'i';
    header->magic[3] = 'f';
    header->width[3] = w % 256;
    header->width[2] = (w/(1<<8)) % 256;
    header->width[1] = (w/(1<<16)) % 256;
    header->width[0] = (w/(1<<24)) % 256;
    header->height[3] = h % 256;
    header->height[2] = (h/(1<<8)) % 256;
    header->height[1] = (h/(1<<16)) % 256;
    header->height[0] = (h/(1<<24)) % 256;
    header->channels = isRGBA ? 4 : 3;
    header->colorspace = 1;
    qoif->bytesAdded += 14;
}

void writeFooter(QoifImage *qoif) {
    unsigned char* startAddr = qoif->data + qoif->bytesAdded;
    for (int i = 0; i<7; i++) {
        startAddr[i] = 0;
    }
    startAddr[7] = 1;
    qoif->bytesAdded += 8;
}

void addToPalette( PixelRGBA pixel, PixelRGBA* palette ) {
    // Note: assumes minimum 64 length. UB if not.
    int index = ( pixel.r*3 + pixel.g*5 + pixel.b*7 + pixel.a*11 ) % 64;
    palette[ index ] = pixel;
}

PixelRGBA getFromPalette( PixelRGBA pixel, PixelRGBA* palette ) {
    // Note: assumes minimum 64 length. UB if not.
    int index = ( pixel.r*3 + pixel.g*5 + pixel.b*7 + pixel.a*11 ) % 64;
    PixelRGBA p = palette[index];
    return palette[ index ];
}

int getIndexFromPalette( PixelRGBA pixel, PixelRGBA* palette ) {
    return ( pixel.r*3 + pixel.g*5 + pixel.b*7 + pixel.a*11 ) % 64;
}



void createQoifBuffer( RawImage raw, QoifImage *qoif) {
    long size = raw.width * raw.height * raw.channels;
    qoif->data = (unsigned char*)malloc(size*2 + 22); // minimum file size: 22
    qoif->bytesAdded = 0;
    if (qoif->data == NULL) {
        err = MemAllocError;
        return;
    }
}

QoifChunk decideNextChunk( RawImage raw, PixelRGBA palette[64]) {
    QoifChunk result;
    // assuming pixelsProcessed != totalLengthInPixels
    PixelRGBA* cur = ((PixelRGBA*) raw.data) + raw.pixelsProcessed;
    PixelRGBA* prev;
    if (raw.pixelsProcessed==0) {
        prev = &(PixelRGBA){0,0,0,255};
    }
    else {
        prev = cur-1;
    }
    int dr = cur->r - prev->r;
    int dg = cur->g - prev->g;
    int db = cur->b - prev->b;
    int da = cur->a - prev->a;
    if (dr==0 && dg==0 && db==0 && da==0) {
        PixelRGBA* next = cur+1;
        while(
            (next-cur) < 62 &&
            (next-cur) + raw.pixelsProcessed < raw.totalLengthInPixels &&
            cur->r == next->r &&
            cur->g == next->g &&
            cur->b == next->b &&
            cur->a == next->a
        ) {
            next++;
        }
        return (QoifChunk) {
            .type = 5,
            .pixelsCovered = next-cur,
            .RUN = (ChunkRUN) {
                .run = next-cur
            }
        };
        
    }
    PixelRGBA hashed = getFromPalette(*cur, palette);
    int hashedIndex = getIndexFromPalette(*cur, palette);
    if ( cur->r - hashed.r == 0 &&
         cur->g - hashed.g == 0 &&
         cur->b - hashed.b == 0 &&
         cur->a - hashed.a == 0 ) {
            return (QoifChunk) {
                .type = 2,
                .pixelsCovered = 1,
                .INDEX = (ChunkINDEX) {
                    .index = hashedIndex,
                }
            };
    }
    if ( -2 <= dr   &&   dr <= 1 &&
         -2 <= dg   &&   dg <= 1 &&
         -2 <= db   &&   db <= 1 &&
         da == 0) {
            return (QoifChunk) {
                .type = 3,
                .pixelsCovered = 1,
                .DIFF = (ChunkDIFF) {
                    .dr=dr, .dg=dg, .db=db
                }
            };
         }
    if ( -32 <= dg     &&   dg <= 31 &&
         -8 <= dr-dg   &&   dr-dg <= 7 &&
         -8 <= db-dg   &&   db-dg <= 7 &&
         da == 0) {
            return (QoifChunk) {
                .type = 4,
                .pixelsCovered = 1,
                .LUMA = (ChunkLUMA) {
                    .dg=dg, .drdg=dr-dg, .dbdg=db-dg
                }
            };
         }
    if (raw.channels==4 && da!=0) {
        return (QoifChunk) {
                .type = 1,
                .pixelsCovered = 1,
                .RGBA = (ChunkRGBA) {
                    .r=cur->r,
                    .g=cur->g,
                    .b=cur->b,
                    .a=cur->a
                }
            };
    }
    return (QoifChunk) {
                .type = 0,
                .pixelsCovered = 1,
                .RGB = (ChunkRGB) {
                    .r=cur->r,
                    .g=cur->g,
                    .b=cur->b,
                }
            };
}


void writeBody( QoifImage *qoif, RawImage raw ) {
    PixelRGBA palette[64] = {0};
    PixelRGBA* currentPixel;
    
    while( raw.pixelsProcessed < raw.totalLengthInPixels ) {

        currentPixel = ((PixelRGBA*) raw.data) + raw.pixelsProcessed;

        QoifChunk chunk = decideNextChunk(raw, palette);
        if (chunk.type==0) writeChunkRGB(qoif, chunk);
        else if (chunk.type==1) writeChunkRGBA(qoif, chunk);
        else if (chunk.type==2) writeChunkINDEX(qoif, chunk);
        else if (chunk.type==3) writeChunkDIFF(qoif, chunk);
        else if (chunk.type==4) writeChunkLUMA(qoif, chunk);
        else if (chunk.type==5) writeChunkRUN(qoif, chunk);
        addToPalette(*currentPixel, palette);
        raw.pixelsProcessed += chunk.pixelsCovered;
    }
}


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

void saveToFile( QoifImage qoif, char* filename ) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        err = OpenFileError;
        return;
    }
    size_t written = fwrite(qoif.data, 1, qoif.bytesAdded, file);
    if (written != qoif.bytesAdded) {
        err = WriteFileError;
        fclose(file);
        return;
    }
    fclose(file);
    err = NoError;
    return;
}


int main(int argc, char** argv) {

    if (argc != 3) {
        puts("Usage: encode filename.png outputname.qoi");
        return 1;
    }

    RawImage raw;
    readPngFile(argv[1], &raw);
    
    if (err != NoError) {
        printf("%s\n", errorMessages[err]);
        return 1;
    }


    QoifImage qoif;
    createQoifBuffer(raw, &qoif);
    writeHeader(&qoif, raw.width, raw.height, raw.channels==4);
    writeBody(&qoif, raw);
    writeFooter(&qoif);
    saveToFile(qoif, argv[2]);

    if (err != NoError) {
        printf("%s\n", errorMessages[err]);
        return 1;
    }
    return 0;
}

