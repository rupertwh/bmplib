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
extern "C"
{
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


enum BmpInfoVer {
        BMPINFO_CORE_OS21 = 1,  /* 12 bytes */
        BMPINFO_OS22,           /* 16 / 40(!) / 64 bytes */
        BMPINFO_V3,             /* 40 bytes */
        BMPINFO_V3_ADOBE1,      /* 52 bytes, unofficial */
        BMPINFO_V3_ADOBE2,      /* 56 bytes, unofficial */
        BMPINFO_V4,             /* 108 bytes */
        BMPINFO_V5,             /* 124 bytes */
        BMPINFO_FUTURE,         /* future versions, larger than 124 bytes */
};



typedef struct Bmphandle *BMPHANDLE;
typedef enum Bmpresult    BMPRESULT;


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

size_t    bmpread_buffersize(BMPHANDLE h);

BMPRESULT bmpread_load_image(BMPHANDLE h, char **buffer);
BMPRESULT bmpread_load_line(BMPHANDLE h, char **buffer);


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

BMPRESULT bmpwrite_set_output_bits(BMPHANDLE h, int red, int green, int blue, int alpha);

BMPRESULT bmpwrite_save_image(BMPHANDLE h, void *image);



void        bmp_free(BMPHANDLE h);

const char* bmp_errmsg(BMPHANDLE h);

const char* bmp_version(void);


#ifdef __cplusplus
}
#endif

#endif /* BMPLIB_H */
