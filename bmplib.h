/* bmplib - bmplib.h
 *
 * Copyright (c) 2024, Rupert Weber.
 *
 * This file is part of bmplib.
 * bmplib is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License as 
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. 
 * If not, see <https://www.gnu.org/licenses/>
 */


#ifndef BMPLIB_H
#define BMPLIB_H
#ifdef __cplusplus
        extern "C" {
#endif

#if defined(__GNUC__)
        #define DEPR(m) __attribute__ ((deprecated(m)))
#else
        #define DEPR(m)
#endif

typedef struct Bmphandle *BMPHANDLE;


/*
 * result codes
 *
 * BMP_RESULT_OK         all is good, proceed.
 *
 * BMP_RESULT_ERROR      something went wrong, image wasn't read
 *                       from / written to file.
 *                       Use bmp_errmsg() to get a textual error
 *                       message.
 *
 * BMP_RESULT_TRUNCATED  Some error occurred while loading the
 *                       image, but image data maybe partially
 *                       intact.
 *                       Use bmp_errmsg() to get a textual error
 *                       message.
 *
 * BMP_RESULT_INVALID    Some or all of the pixel values were
 *                       invalid. This happens when indexed
 *                       images point to colors beyond the given
 *                       palette size or to pixels outside of the
*                        image dimensions. If the image is also
 *                       truncated, BMP_RESULT_TRUNCATED will be
 *                       returned instead.
 *
 * BMP_RESULT_PNG        The BMP file contains an embedded PNG
 * BMP_RESULT_JPEG       or JPEG file.
 *                       The file pointer is left in such a state
 *                       that the PNG/JPEG can be read, e.g. by
 *                       passing it to libpng/libjpeg.
 *                       (Obviously, don't use any absolute
 *                       positioning functions like rewind() or
 *                       fseek(...,SEEK_SET)), as the file still
 *                       contains the BMP wrapping.
 *
 * BMP_RESULT_INSANE     Header claims that the image is very large.
 *                       (Default limit: 500MB). You cannot load
 *                       the image unless you first call
 *                       bmpread_set_insanity_limit() to set a new
 *                       sufficiently high limit.
 */
enum Bmpresult {
        BMP_RESULT_OK = 0,
        BMP_RESULT_INVALID,
        BMP_RESULT_TRUNCATED,
        BMP_RESULT_INSANE,
        BMP_RESULT_PNG,
        BMP_RESULT_JPEG,
        BMP_RESULT_ERROR,
};
typedef enum Bmpresult BMPRESULT;


/*
 * 64-bit BMPs: conversion of RGBA (16bit) values
 *
 * BMP_CONV64_16BIT_SRGB  (default) convert components to 'normal'
 *                        16bit with sRGB gamma. Assumes components
 *                        are stored as s2.13 linear.
 *
 * BMP_CONV64_16BIT       convert components from s2.13 to
 *                        'normal' 16bit. No gamma conversion.
 *
 * BMP_CONV64_NONE        Leave components as they are, which
 *                        might always(?) be s2.13 linear.
 */
enum Bmpconv64 {
        BMP_CONV64_16BIT_SRGB, /* default */
        BMP_CONV64_16BIT,
        BMP_CONV64_NONE
};
typedef enum Bmpconv64 BMPCONV64;


/*
 * BMP info header versions
 *
 * There doesn't seem to be consensus on whether the
 * BITMAPINFOHEADER is version 1 (with the two Adobe
 * extensions being v2 and v3) or version 3 (with the
 * older BITMAPCIREHEADER and OS22XBITMAPHEADER being
 * v1 and v2).
 * I am going with BITMAPINFOHEADER = v3
 */
enum BmpInfoVer {
        BMPINFO_CORE_OS21 = 1,  /* 12 bytes */
        BMPINFO_OS22,           /* 16 / 40(!) / 64 bytes */
        BMPINFO_V3,             /* 40 bytes */
        BMPINFO_V3_ADOBE1,      /* 52 bytes, unofficial */
        BMPINFO_V3_ADOBE2,      /* 56 bytes, unofficial */
        BMPINFO_V4,             /* 108 bytes */
        BMPINFO_V5,             /* 124 bytes */
        BMPINFO_FUTURE          /* future versions, larger than 124 bytes */
};
typedef enum BmpInfoVer BMPINFOVER;


/*
 * RLE type
 *
 * BMP_RLE_NONE  no RLE
 *
 * BMP_RLE_AUTO  RLE4 for color tables with 16 or fewer
 *               colors.
 *
 * BMP_RLE_RLE8  always use RLE8, regardless of color
 *               table size.
 */
enum BmpRLEtype {
        BMP_RLE_NONE,
        BMP_RLE_AUTO,
        BMP_RLE_RLE8
};
typedef enum BmpRLEtype BMPRLETYPE;


/*
 * undefined pixels in RLE images
 *
 * BMP_UNDEFINED_TO_ZERO   set undefined pixels to 0 (= first
 *                         entry in color table).
 *
 * BMP_UNDEFINED_TO_ALPHA  (default) make undefined pixels
 *                         transparent. Always adds an alpha
 *                         channel to the result.
 *
 */
enum BmpUndefined {
        BMP_UNDEFINED_LEAVE,
        BMP_UNDEFINED_TO_ZERO DEPR("use BMP_UNDEFINED_LEAVE instead") = 0,
        BMP_UNDEFINED_TO_ALPHA  /* default */
};
typedef enum BmpUndefined BMPUNDEFINED;


/*
 * orientation
 *
 * Only relevant when reading the image line-by-line.
 * When reading the image as a whole, it will *always*
 * be returned in top-down orientation. bmpread_orientation()
 * still gives the orientation of the BMP file.
 */
enum BmpOrient {
        BMP_ORIENT_BOTTOMUP,
        BMP_ORIENT_TOPDOWN
};
typedef enum BmpOrient BMPORIENT;


BMPHANDLE bmpread_new(FILE *file);

BMPRESULT bmpread_load_info(BMPHANDLE h);

BMPRESULT bmpread_dimensions(BMPHANDLE  h,
                             int       *width,
                             int       *height,
                             int       *channels,
                             int       *bitsperchannel,
                             BMPORIENT *orientation);

int       bmpread_width(BMPHANDLE h);
int       bmpread_height(BMPHANDLE h);
int       bmpread_channels(BMPHANDLE h);
int       bmpread_bits_per_channel(BMPHANDLE h);
BMPORIENT bmpread_orientation(BMPHANDLE h);

int       bmpread_resolution_xdpi(BMPHANDLE h);
int       bmpread_resolution_ydpi(BMPHANDLE h);

size_t    bmpread_buffersize(BMPHANDLE h);

BMPRESULT bmpread_load_image(BMPHANDLE h, unsigned char **buffer);
BMPRESULT bmpread_load_line(BMPHANDLE h, unsigned char **buffer);


int       bmpread_num_palette_colors(BMPHANDLE h);
BMPRESULT bmpread_load_palette(BMPHANDLE h, unsigned char **palette);


void      bmpread_set_undefined(BMPHANDLE h, BMPUNDEFINED mode);
void      bmpread_set_insanity_limit(BMPHANDLE h, size_t limit);

int bmpread_is_64bit(BMPHANDLE h);
BMPRESULT bmpread_set_64bit_conv(BMPHANDLE h, BMPCONV64 conv);

BMPINFOVER  bmpread_info_header_version(BMPHANDLE h);
int         bmpread_info_header_size(BMPHANDLE h);
int         bmpread_info_compression(BMPHANDLE h);
int         bmpread_info_bitcount(BMPHANDLE h);
const char* bmpread_info_header_name(BMPHANDLE h);
const char* bmpread_info_compression_name(BMPHANDLE h);
BMPRESULT   bmpread_info_channel_bits(BMPHANDLE h, int *r, int *g, int *b, int *a);




BMPHANDLE bmpwrite_new(FILE *file);

BMPRESULT bmpwrite_set_dimensions(BMPHANDLE h,
                                  unsigned  width,
                                  unsigned  height,
                                  unsigned  channels,
                                  unsigned  bits_per_channel);
BMPRESULT bmpwrite_set_resolution(BMPHANDLE h, int xdpi, int ydpi);
BMPRESULT bmpwrite_set_output_bits(BMPHANDLE h, int red, int green, int blue, int alpha);
BMPRESULT bmpwrite_set_palette(BMPHANDLE h, int numcolors, const unsigned char *palette);
BMPRESULT bmpwrite_allow_2bit(BMPHANDLE h);
BMPRESULT bmpwrite_set_rle(BMPHANDLE h, BMPRLETYPE type);
BMPRESULT bmpwrite_set_orientation(BMPHANDLE h, BMPORIENT orientation);

BMPRESULT bmpwrite_save_image(BMPHANDLE h, const unsigned char *image);
BMPRESULT bmpwrite_save_line(BMPHANDLE h, const unsigned char *line);



void        bmp_free(BMPHANDLE h);

const char* bmp_errmsg(BMPHANDLE h);

const char* bmp_version(void);


/* these functions are kept for binary compatibility and will be
 * removed from future versions:
 */


int  DEPR("use bmpread_orientation() instead") bmpread_topdown(BMPHANDLE h);
void DEPR("use bmpread_set_undefined instead") bmpread_set_undefined_to_alpha(BMPHANDLE h, int mode);

#ifdef __cplusplus
}
#endif

#endif /* BMPLIB_H */
