/* bmplib - bmplib.h
 *
 * Copyright (c) 2024, 2025, Rupert Weber.
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

#if defined (WIN32) || defined (_WIN32)
	#ifdef BMPLIB_LIB
		#define APIDECL __declspec(dllexport)
	#else
		#define APIDECL __declspec(dllimport)
	#endif
#else
	#define APIDECL
#endif


typedef union Bmphandle *BMPHANDLE;


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
 *
 * BMP_RESULT_ARRAY      The BMP file contains an OS/2 bitmap array.
 *
 */
enum Bmpresult {
	BMP_RESULT_OK = 0,
	BMP_RESULT_INVALID,
	BMP_RESULT_TRUNCATED,
	BMP_RESULT_INSANE,
	BMP_RESULT_PNG,
	BMP_RESULT_JPEG,
	BMP_RESULT_ERROR,
	BMP_RESULT_ARRAY
};
typedef enum Bmpresult BMPRESULT;


/*
 * 64-bit BMPs: conversion of RGBA (16bit) values
 *
 * BMP_CONV64_SRGB   (default) Assume components are
 *                   stored in linear light and convert
 *                   to sRGB gamma.
 *
 * BMP_CONV64_LINEAR No gamma conversion.
 *
 * BMP_CONV64_NONE   Leave components as they are. This is
 *                   a shortcut for the combination
 *                    - bmpread_set_64bit_conv(BMP_CONV_LINEAR) and
 *                    - bmp_set_number_format(BMP_FORMAT_S2_13).
 */
enum Bmpconv64 {
	BMP_CONV64_SRGB   = 0,  /* default */
	BMP_CONV64_LINEAR,
	BMP_CONV64_NONE,
};
typedef enum Bmpconv64 BMPCONV64;


/*
 * BMP info header versions
 *
 * There doesn't seem to be consensus on whether the BITMAPINFOHEADER is
 * version 1 (with the two Adobe extensions being v2 and v3) or version 3
 * (with the older BITMAPCOREHEADER and OS22XBITMAPHEADER being v1 and v2).
 * I am going with BITMAPINFOHEADER = v3.
 */
enum BmpInfoVer {
	BMPINFO_CORE_OS21 = 1,  /* 12 bytes */
	BMPINFO_OS22,           /* 16 / 40(!) / up to 64 bytes */
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
 * BMP_UNDEFINED_LEAVE     leaves image buffer at whatever pixel value it was
 *                         initialized to. (0, i.e. first entry in color table if
 *                         buffer was allocated by bmplib).
 *
 * BMP_UNDEFINED_TO_ALPHA  (default) make undefined pixels
 *                         transparent. Always adds an alpha
 *                         channel to the result.
 *
 */
enum BmpUndefined {
	BMP_UNDEFINED_LEAVE,
	BMP_UNDEFINED_TO_ALPHA,  /* default */
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


/*
 * number format
 *
 * format of input/output RGB(A) image data. (indexed image data
 * is always 8-bit integer).
 *
 * BMP_FORMAT_INT    8/16/32-bit integer
 *
 * BMP_FORMAT_FLOAT  32-bit float
 *
 * BMP_FORMAT_S2_13  16-bit s2.13 fixed point with range from
 *                   -4.0 to +3.999...
 */
enum BmpFormat {
	BMP_FORMAT_INT,
	BMP_FORMAT_FLOAT,
	BMP_FORMAT_S2_13
};
typedef enum BmpFormat BMPFORMAT;


enum BmpIntent {
	BMP_INTENT_NONE,
	BMP_INTENT_BUSINESS,        /* saturation */
	BMP_INTENT_GRAPHICS,        /* relative colorimetric */
	BMP_INTENT_IMAGES,          /* perceptive */
	BMP_INTENT_ABS_COLORIMETRIC /* absolute colorimetric */
};
typedef enum BmpIntent BMPINTENT;


enum BmpImagetype {
	BMP_IMAGETYPE_NONE,
	BMP_IMAGETYPE_BM = 0x4d42,
	BMP_IMAGETYPE_CI = 0x4943,
	BMP_IMAGETYPE_CP = 0x5043,
	BMP_IMAGETYPE_IC = 0x4349,
	BMP_IMAGETYPE_PT = 0x5450,
	BMP_IMAGETYPE_BA = 0x4142
};
typedef enum BmpImagetype BMPIMAGETYPE;

struct BmpArrayInfo {
	BMPHANDLE    handle;
	BMPIMAGETYPE type;
	int          width, height;
	int          ncolors;                   /* 0 = RGB */
	int          screenwidth, screenheight; /* typically 0, or 1024x768 for 'hi-res' */
};



APIDECL BMPHANDLE bmpread_new(FILE *file);

APIDECL BMPRESULT bmpread_load_info(BMPHANDLE h);
APIDECL BMPIMAGETYPE bmpread_image_type(BMPHANDLE h);

APIDECL BMPRESULT bmpread_dimensions(BMPHANDLE  h,
                                     int       *width,
                                     int       *height,
                                     int       *channels,
                                     int       *bitsperchannel,
                                     BMPORIENT *orientation);

APIDECL int       bmpread_width(BMPHANDLE h);
APIDECL int       bmpread_height(BMPHANDLE h);
APIDECL int       bmpread_channels(BMPHANDLE h);
APIDECL int       bmpread_bitsperchannel(BMPHANDLE h);
APIDECL BMPORIENT bmpread_orientation(BMPHANDLE h);

APIDECL int       bmpread_resolution_xdpi(BMPHANDLE h);
APIDECL int       bmpread_resolution_ydpi(BMPHANDLE h);

APIDECL size_t    bmpread_buffersize(BMPHANDLE h);

APIDECL BMPRESULT bmpread_load_image(BMPHANDLE h, unsigned char **buffer);
APIDECL BMPRESULT bmpread_load_line(BMPHANDLE h, unsigned char **buffer);

APIDECL int       bmpread_num_palette_colors(BMPHANDLE h);
APIDECL BMPRESULT bmpread_load_palette(BMPHANDLE h, unsigned char **palette);

APIDECL void      bmpread_set_undefined(BMPHANDLE h, BMPUNDEFINED mode);
APIDECL void      bmpread_set_insanity_limit(BMPHANDLE h, size_t limit);

APIDECL int       bmpread_is_64bit(BMPHANDLE h);
APIDECL BMPRESULT bmpread_set_64bit_conv(BMPHANDLE h, BMPCONV64 conv);

APIDECL size_t    bmpread_iccprofile_size(BMPHANDLE h);
APIDECL BMPRESULT bmpread_load_iccprofile(BMPHANDLE h, unsigned char **profile);


APIDECL int bmpread_array_num(BMPHANDLE h);
APIDECL BMPRESULT bmpread_array_info(BMPHANDLE h, struct BmpArrayInfo *ai, int idx);


APIDECL BMPINFOVER  bmpread_info_header_version(BMPHANDLE h);
APIDECL const char* bmpread_info_header_name(BMPHANDLE h);
APIDECL int         bmpread_info_header_size(BMPHANDLE h);
APIDECL int         bmpread_info_compression(BMPHANDLE h);
APIDECL const char* bmpread_info_compression_name(BMPHANDLE h);
APIDECL int         bmpread_info_bitcount(BMPHANDLE h);
APIDECL BMPRESULT   bmpread_info_channel_bits(BMPHANDLE h, int *r, int *g, int *b, int *a);




APIDECL BMPHANDLE bmpwrite_new(FILE *file);

APIDECL BMPRESULT bmpwrite_set_dimensions(BMPHANDLE h,
                                          unsigned  width,
                                          unsigned  height,
                                          unsigned  channels,
                                          unsigned  bitsperchannel);
APIDECL BMPRESULT bmpwrite_set_resolution(BMPHANDLE h, int xdpi, int ydpi);
APIDECL BMPRESULT bmpwrite_set_output_bits(BMPHANDLE h, int red, int green, int blue, int alpha);
APIDECL BMPRESULT bmpwrite_set_palette(BMPHANDLE h, int numcolors, const unsigned char *palette);
APIDECL BMPRESULT bmpwrite_allow_2bit(BMPHANDLE h);
APIDECL BMPRESULT bmpwrite_allow_huffman(BMPHANDLE h);
APIDECL BMPRESULT bmpwrite_allow_rle24(BMPHANDLE h);
APIDECL BMPRESULT bmpwrite_set_rle(BMPHANDLE h, BMPRLETYPE type);
APIDECL BMPRESULT bmpwrite_set_orientation(BMPHANDLE h, BMPORIENT orientation);
APIDECL BMPRESULT bmpwrite_set_64bit(BMPHANDLE h);
APIDECL BMPRESULT bmpwrite_set_huffman_img_fg_idx(BMPHANDLE h, int idx);

APIDECL BMPRESULT bmpwrite_set_iccprofile(BMPHANDLE h, size_t size,
                                          const unsigned char *iccprofile);
APIDECL BMPRESULT bmpwrite_set_rendering_intent(BMPHANDLE h, BMPINTENT intent);

APIDECL BMPRESULT bmpwrite_save_image(BMPHANDLE h, const unsigned char *image);
APIDECL BMPRESULT bmpwrite_save_line(BMPHANDLE h, const unsigned char *line);



APIDECL BMPRESULT bmp_set_number_format(BMPHANDLE h, BMPFORMAT format);
APIDECL BMPRESULT bmp_set_huffman_t4black_value(BMPHANDLE h, int blackidx);

APIDECL void        bmp_free(BMPHANDLE h);

APIDECL const char* bmp_errmsg(BMPHANDLE h);

APIDECL const char* bmp_version(void);


/* these errorcodes aren't part of the API yet, but will be.
 * currently only used internally by bmplib.
 */

#define BMP_ERRTYPE_HARD    0x0000000f
#define BMP_ERR_FILEIO      0x00000001
#define BMP_ERR_MEMORY      0x00000002
#define BMP_ERR_INTERNAL    0x00000004

#define BMP_ERRTYPE_DATA    0x0000fff0
#define BMP_ERR_PIXEL       0x00000010
#define BMP_ERR_TRUNCATED   0x00000020
#define BMP_ERR_HEADER      0x00000040
#define BMP_ERR_INSANE      0x00000080
#define BMP_ERR_UNSUPPORTED 0x00000100
#define BMP_ERR_JPEG        0x00000200
#define BMP_ERR_PNG         0x00000400
#define BMP_ERR_DIMENSIONS  0x00000800
#define BMP_ERR_INVALID     0x00001000

#define BMP_ERRTYPE_USER    0x0fff0000
#define BMP_ERR_CONV64      0x00010000
#define BMP_ERR_FORMAT      0x00020000
#define BMP_ERR_NULL        0x00040000
#define BMP_ERR_PALETTE     0x00080000
#define BMP_ERR_NOINFO      0x00100000
#define BMP_ERR_UNDEFMODE   0x00200000



#ifdef __cplusplus
}
#endif

#endif /* BMPLIB_H */
