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


/****************************************************************
 * result codes
 *
 * BMP_RESULT_OK         all is good, proceed
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
 *                       palette size. If the image is also
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
 *
 ****************************************************************/



enum Bmpresult {
        BMP_RESULT_OK = 0,
        BMP_RESULT_ERROR,
        BMP_RESULT_TRUNCATED,
        BMP_RESULT_INVALID,
        BMP_RESULT_PNG,
        BMP_RESULT_JPEG,
        BMP_RESULT_INSANE,
};



typedef struct Bmphandle *BMPHANDLE;
typedef enum Bmpresult    BMPRESULT;


/****************************************************************
 * conversion of 64bit BMPs
 *
 * I have very little information on 64bit BMPs. It seems that
 * the RGBA components are (always?) stored as s2.13 fixed-point
 * numbers. And according to Jason Summers' BMP Suite the
 * RGB components are encoded in linear light. As that's the only
 * sample of a 64-bit BMP I have, that's what I am going with
 * for now.
 * But that also means that there is no one obvious format in
 * which to return the data.
 * Possible choices are:
 * 1. return the values untouched, which means the caller has to
 *    be aware of the s2.13 format. (BMP_64CONV_NONE)
 * 2. return the values as normal 16bit values, left in linear
 *    light (BMP_64CONV_16BIT)
 * 3. return the values as normal 16bit values, converted to sRGB
 *    gamma. (BMP_64CONV_16BIT_SRGB)
 *
 * Choice 3 (16bit sRGB gamma) seems to be the most sensible default,
 * as it will work well for all callers which are not aware/don't
 * care about 64bit BMPs and just want to use/diplay them like any
 * other BMP. (Even though this goes against my original intent to
 * not have bmplib do any color conversions)
 *
 * Note: the s2.13 format allows for negative values and values
 * greater than 1! When converting to 16bit, these values will be
 * clipped to 0...1.
 *
 * In any case, we'll need to provide functions to check if a BMP is
 * 64bit and to set the conversion:
 *   bmpread_is_64bit()
 *   bmpread_set_64bit_conv()
 *******************************************************************/

enum Bmp64bitconv {
        BMP_CONV64_16BIT_SRGB,  /* default */
        BMP_CONV64_16BIT,
        BMP_CONV64_NONE
};

int bmpread_is_64bit(BMPHANDLE h);
BMPRESULT bmpread_set_64bit_conv(BMPHANDLE h, enum Bmp64bitconv conv);


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


enum BmpRLEtype {
        BMP_RLE_NONE = 0,
        BMP_RLE_AUTO,
        BMP_RLE_RLE8
};


BMPHANDLE bmpread_new(FILE *file);

BMPRESULT bmpread_load_info(BMPHANDLE h);

BMPRESULT bmpread_dimensions(BMPHANDLE h,
                             int      *width,
                             int      *height,
                             int      *channels,
                             int      *bitsperchannel,
                             int      *topdown);

int       bmpread_width(BMPHANDLE h);
int       bmpread_height(BMPHANDLE h);
int       bmpread_channels(BMPHANDLE h);
int       bmpread_bits_per_channel(BMPHANDLE h);
int       bmpread_topdown(BMPHANDLE h);

int       bmpread_resolution_xdpi(BMPHANDLE h);
int       bmpread_resolution_ydpi(BMPHANDLE h);

size_t    bmpread_buffersize(BMPHANDLE h);

BMPRESULT bmpread_load_image(BMPHANDLE h, unsigned char **buffer);
BMPRESULT bmpread_load_line(BMPHANDLE h, unsigned char **buffer);


int       bmpread_num_palette_colors(BMPHANDLE h);
BMPRESULT bmpread_load_palette(BMPHANDLE h, unsigned char **palette);


void      bmpread_set_undefined_to_alpha(BMPHANDLE h, int yes);
void      bmpread_set_insanity_limit(BMPHANDLE h, size_t limit);



int       bmpread_info_header_version(BMPHANDLE h);
int       bmpread_info_header_size(BMPHANDLE h);
int       bmpread_info_compression(BMPHANDLE h);
int       bmpread_info_bitcount(BMPHANDLE h);
const char* bmpread_info_header_name(BMPHANDLE h);
const char* bmpread_info_compression_name(BMPHANDLE h);
BMPRESULT bmpread_info_channel_bits(BMPHANDLE h, int *r, int *g, int *b, int *a);




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
BMPRESULT bmpwrite_set_rle(BMPHANDLE h, enum BmpRLEtype type);

BMPRESULT bmpwrite_save_image(BMPHANDLE h, const unsigned char *image);
BMPRESULT bmpwrite_save_line(BMPHANDLE h, const unsigned char *line);



void        bmp_free(BMPHANDLE h);

const char* bmp_errmsg(BMPHANDLE h);

const char* bmp_version(void);


#ifdef __cplusplus
}
#endif

#endif /* BMPLIB_H */
