
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>


typedef struct {
    unsigned char r, g, b, a;
} PixelRGBA;

typedef struct {
    unsigned char* data;
    int width;
    int height;
    long bytesProcessed;
    long totalLengthInBytes;
} QoifImage;

typedef struct {
    PixelRGBA* data;  // Pointer to RGB or RGBA data
    long pixelsAdded;
} RawImage;

typedef struct {
    unsigned char r, g, b;
} ChunkRGB;

typedef struct {
    unsigned char r, g, b, a;
} ChunkRGBA;

typedef struct {
    unsigned char index;
} ChunkINDEX;

typedef struct {
    unsigned char dr, dg, db;
} ChunkDIFF;

typedef struct {
    unsigned char dg, drdg, dbdg;
} ChunkLUMA;

typedef struct {
    unsigned char run;
} ChunkRUN;

typedef struct {

} ChunkNONE;

typedef struct {
    int type; // 0=RGB, 1=RGBA, 2=INDEX, 3=DIFF, 4=LUMA, 5=RUN, 6=NONE
    union {
        ChunkRGB RGB;
        ChunkRGBA RGBA;
        ChunkINDEX INDEX;
        ChunkDIFF DIFF;
        ChunkLUMA LUMA;
        ChunkRUN RUN;
        ChunkNONE NONE;
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

void writeChunkRGB(RawImage *raw, QoifChunk chunk) {
    PixelRGBA *cur = raw->data + raw->pixelsAdded;
    PixelRGBA* prev;
    if (raw->pixelsAdded==0) {
        prev = &(PixelRGBA){0,0,0,255};
    }
    else {
        prev = cur-1;
    }
    cur->r = chunk.RGB.r;
    cur->g = chunk.RGB.g;
    cur->b = chunk.RGB.b;
    cur->a = prev->a;

    raw->pixelsAdded++;
}
void writeChunkRGBA(RawImage *raw, QoifChunk chunk) {
    PixelRGBA *cur = raw->data + raw->pixelsAdded;
    cur->r = chunk.RGBA.r;
    cur->g = chunk.RGBA.g;
    cur->b = chunk.RGBA.b;
    cur->a = chunk.RGBA.a;
    raw->pixelsAdded++;
}

void writeChunkRUN(RawImage *raw, QoifChunk chunk) {
    PixelRGBA *cur = raw->data + raw->pixelsAdded;
    PixelRGBA* prev;
    if (raw->pixelsAdded==0) {
        prev = &(PixelRGBA){0,0,0,255};
    }
    else {
        prev = cur-1;
    }
    for (int i = 0; i<chunk.RUN.run+1; i++) {
        cur->r = prev->r;
        cur->g = prev->g;
        cur->b = prev->b;
        cur->a = prev->a;
        raw->pixelsAdded++;
        cur++;
    }
}

void writeChunkDIFF(RawImage *raw, QoifChunk chunk) {
    PixelRGBA *cur = raw->data + raw->pixelsAdded;
    PixelRGBA* prev;
    if (raw->pixelsAdded==0) {
        prev = &(PixelRGBA){0,0,0,255};
    }
    else {
        prev = cur-1;
    }
    cur->r = prev->r + chunk.DIFF.dr -2;
    cur->g = prev->g + chunk.DIFF.dg -2;
    cur->b = prev->b + chunk.DIFF.db -2;
    cur->a = prev->a;
    raw->pixelsAdded++;
}

void writeChunkLUMA(RawImage *raw, QoifChunk chunk) {
    PixelRGBA *cur = raw->data + raw->pixelsAdded;
    PixelRGBA* prev;
    if (raw->pixelsAdded==0) {
        prev = &(PixelRGBA){0,0,0,255};
    }
    else {
        prev = cur-1;
    }
    cur->g = prev->g + chunk.LUMA.dg - 32;
    cur->r = prev->r + chunk.LUMA.drdg + chunk.LUMA.dg -32 -8;
    cur->b = prev->b + chunk.LUMA.dbdg + chunk.LUMA.dg -32 -8;
    cur->a = prev->a;
    raw->pixelsAdded++;
}

void writeChunkINDEX(RawImage *raw, QoifChunk chunk, PixelRGBA* palette) {
    PixelRGBA *cur = raw->data + raw->pixelsAdded;
    cur->r = palette[chunk.INDEX.index].r;
    cur->g = palette[chunk.INDEX.index].g;
    cur->b = palette[chunk.INDEX.index].b;
    cur->a = palette[chunk.INDEX.index].a;
    raw->pixelsAdded++;
}




void addToPalette( PixelRGBA pixel, PixelRGBA* palette ) {
    // Note: assumes minimum 64 length. UB if not.
    int index = ( pixel.r*3 + pixel.g*5 + pixel.b*7 + pixel.a*11 ) % 64;
    palette[ index ] = pixel;
}

int getFromPalette( PixelRGBA pixel, PixelRGBA* palette ) {
    // Note: assumes minimum 64 length. UB if not.
    int index = ( pixel.r*3 + pixel.g*5 + pixel.b*7 + pixel.a*11 ) % 64;
    return index;
}


QoifChunk fetchNextChunk( QoifImage *qoif, RawImage raw, PixelRGBA palette[64]) {

    // assuming pixelsProcessed != totalLengthInPixels
    PixelRGBA* cur = ((PixelRGBA*) raw.data) + raw.pixelsAdded;
    PixelRGBA* prev;
    if (raw.pixelsAdded==0) {
        prev = &(PixelRGBA){0,0,0,255};
    }
    else {
        prev = cur-1;
    }

    unsigned char* chunk = (qoif->data + qoif->bytesProcessed);

    if ( *chunk==0xfe ) {
        // RGB
        if (qoif->bytesProcessed+3 >= qoif->totalLengthInBytes) {
            // not enough bytes. finalize
            return (QoifChunk) {
                .type = 6,
            };
        }
        else {
            qoif->bytesProcessed += 4;
            return (QoifChunk) {
                .type = 0,
                .RGB = (ChunkRGB) {
                    .r= *(chunk+1),
                    .g= *(chunk+2),
                    .b= *(chunk+3),
                }
            };
        }
    }
    else if ( *chunk==0xff ) {
        // RGBA
        if (qoif->bytesProcessed+4 >= qoif->totalLengthInBytes) {
            // not enough bytes. finalize
            return (QoifChunk) {
                .type = 6,
            };
        }
        else {
            qoif->bytesProcessed += 5;
            return (QoifChunk) {
                .type = 1,
                .RGBA = (ChunkRGBA) {
                    .r= *(chunk+1),
                    .g= *(chunk+2),
                    .b= *(chunk+3),
                    .a= *(chunk+4),
                }
            };
        }
    }
    else if ( *chunk>>6 == 1 ) {
        // DIFF
        unsigned char dr = (*chunk >> 4) & 3;
        unsigned char dg = (*chunk >> 2) & 3;
        unsigned char db = (*chunk) & 3;
        qoif->bytesProcessed += 1;
        return (QoifChunk) {
            .type = 3,
            .DIFF = (ChunkDIFF) {
                .dr = dr,
                .dg = dg,
                .db = db
            }
        };
    }
    else if ( *chunk>>6 == 2 ) {
        // LUMA
        if (qoif->bytesProcessed+1 >= qoif->totalLengthInBytes) {
            // not enough bytes. finalize
            return (QoifChunk) {
                .type = 6,
            };
        }
        unsigned char dg = (*chunk) & 0x3f;
        unsigned char drdg = (*(chunk+1) >> 4) & 0xf;
        unsigned char dbdg = (*(chunk+1)) & 0xf;
        qoif->bytesProcessed += 2;
        return (QoifChunk) {
            .type = 4,
            .LUMA = (ChunkLUMA) {
                .dg = dg,
                .drdg = drdg,
                .dbdg = dbdg
            }
        };
    }
    else if ( *chunk>>6 == 3 ) {
        // RUN
        unsigned char run = (*chunk) & 0x3f;
        qoif->bytesProcessed += 1;
        return (QoifChunk) {
            .type = 5,
            .RUN = (ChunkRUN) {
                .run = run,
            }
        };
    }
    else if ( *chunk>>6 == 0 ) {
        // INDEX
        unsigned char index = (*chunk) & 0x3f;
        qoif->bytesProcessed += 1;
        return (QoifChunk) {
            .type = 2,
            .INDEX = (ChunkINDEX) {
                .index = index,
            }
        };
    } 

    return (QoifChunk) {
        .type = 6
    };

}



void readQoifFile(const char* filename, QoifImage *qoif, RawImage *image) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        err = OpenFileError;
        return;
    }

    // Read the qoif file
    fseek(fp, 0, SEEK_END);
    qoif->totalLengthInBytes = ftell(fp);
    qoif->bytesProcessed = 14; // skip the header
    fseek(fp, 0, SEEK_SET);  /* same as rewind(f); */

    qoif->data = malloc(qoif->totalLengthInBytes);
    fread(qoif->data, qoif->totalLengthInBytes, 1, fp);
    fclose(fp);

    // Allocate for raw image
    qoif->width = qoif->data[4]*(1<<24) + qoif->data[5]*(1<<16) + qoif->data[6]*(1<<8) + qoif->data[7];
    qoif->height = qoif->data[8]*(1<<24) + qoif->data[9]*(1<<16) + qoif->data[10]*(1<<8) + qoif->data[11];

    image->data = (PixelRGBA*)malloc( qoif->width * qoif->height * 4 );
    if (!image->data) {
        err = MemAllocError;
        return;
    }

    image->pixelsAdded = 0;
    err = NoError;
}


void saveAsPngFile(char* rgbaPixelsStart, int width, int height, char* filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        err = OpenFileError;
        return;
    }

    // Initialize the PNG structures
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        err = PngError;
        fclose(fp);
        return;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        err = PngError;
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return;
    }

    if (setjmp(png_jmpbuf(png))) {
        err = PngError;
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return;
    }

    // Set the output file
    png_init_io(png, fp);

    // Write the PNG header info (color type: PNG_COLOR_TYPE_RGBA)
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png, info);

    // Write the pixel data
    png_bytep rows[height];
    for (int y = 0; y < height; y++) {
        rows[y] = (png_bytep)(rgbaPixelsStart + y * width * 4); // RGBA is 4 bytes per pixel
    }
    png_write_image(png, rows);

    // End the writing process
    png_write_end(png, NULL);

    // Clean up
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    err = NoError;
}




int main(int argc, char** argv) {

    if (argc != 3) {
        puts("Usage: decode filename.qoi outputname.png");
        return 1;
    }

    RawImage raw;
    QoifImage qoif;
    PixelRGBA palette[64] = {0};

    readQoifFile(argv[1], &qoif, &raw);

    if (err != NoError) {
        printf("%s\n", errorMessages[err]);
        return 1;
    }

    while(1) {
        if (qoif.bytesProcessed + 8 >= qoif.totalLengthInBytes) break;
        if (raw.pixelsAdded >= qoif.width * qoif.height) break;


        QoifChunk chunk = fetchNextChunk(&qoif, raw, palette);
        if (chunk.type == 0) writeChunkRGB(&raw, chunk);
        if (chunk.type == 1) writeChunkRGBA(&raw, chunk);
        if (chunk.type == 2) writeChunkINDEX(&raw, chunk, palette);
        if (chunk.type == 3) writeChunkDIFF(&raw, chunk);
        if (chunk.type == 4) writeChunkLUMA(&raw, chunk);
        if (chunk.type == 5) writeChunkRUN(&raw, chunk);
        if (chunk.type == 6) break;

        PixelRGBA* lastPixel = ((PixelRGBA*) raw.data) + raw.pixelsAdded - 1;
        addToPalette(*lastPixel, palette);
    }

    saveAsPngFile((char*) raw.data, qoif.width, qoif.height, argv[2]);

    if (err != NoError) {
        printf("%s\n", errorMessages[err]);
        return 1;
    }
    return 0;
}

