/* bmplib - bmp-common.h
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

#undef TRUE
#define TRUE  (1)

#undef FALSE
#define FALSE (0)

#undef MAX
#undef MIN
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARR_SIZE(a) ((int) (sizeof a / sizeof (a)[0]))


union Pixel {
	unsigned int value[4];
	struct {
		unsigned int red;
		unsigned int green;
		unsigned int blue;
		unsigned int alpha;
	};
};

struct Colormask {
	union {
		unsigned long value[4];
		struct {
			unsigned long	red;
			unsigned long	green;
			unsigned long	blue;
			unsigned long	alpha;
		};
	} mask;
	union {
		unsigned long value[4];
		struct {
			unsigned long	red;
			unsigned long	green;
			unsigned long	blue;
			unsigned long	alpha;
		};
	} shift;
	union {
		int value[4];
		struct {
			int	red;
			int	green;
			int	blue;
			int	alpha;
		};
	} bits;
};

typedef struct Bmpread * restrict BMPREAD_R;

struct Palette {
	int         numcolors;
	union Pixel color[1];
};

struct Bmpread {
	struct {
		uint32_t magic;
		LOG      log;
	};
	FILE             *file;
	unsigned long     bytes_read;  /* number of bytes we have read from the file */
	struct Bmpfile   *fh;
	struct Bmpinfo   *ih;
	unsigned int      insanity_limit;
	int               width;
	int               height;
	int               topdown;
	int               has_alpha;   /* original BMP has alpha channel */
	int               undefined_to_alpha;
	int               wipe_buffer;
	int               we_allocated_buffer;
	int               line_by_line;
	struct Palette   *palette;
	struct Colormask  colormask;
	/* result image dimensions */
	int               result_channels;
	int               result_bits_per_pixel;
	int               result_bytes_per_pixel;
	int               result_bits_per_channel;
	size_t            result_size;
	/* state */
	int               getinfo_called;
	int               getinfo_return;
	int               jpeg;
	int               png;
	int               dimensions_queried;
	int               dim_queried_width;
	int               dim_queried_height;
	int               dim_queried_channels;
	int               dim_queried_bits_per_channel;
	int               image_loaded;
	int               rle;
	int               rle_eol;
	int               rle_eof;
	int               lbl_x;  /* remember where we are in the image  */
	int               lbl_y;  /* for line by line reading            */
	int               lbl_file_y;  /* RLE files may be ahead of the image y */
	int               truncated;
	int               invalid_pixels;
	int               invalid_delta;
	int               file_err;
	int               file_eof;
	int               panic;

};


#define cm_align4size(a)     ((((a) + 3) >> 2) << 2)
#define cm_align2size(a)     ((((a) + 1) >> 1) << 1)
int cm_align4padding(unsigned long a);
int cm_align2padding(unsigned long a);
int cm_count_bits(unsigned long v);

int cm_gobble_up(FILE *file, int count, LOG log);
int cm_check_is_read_handle(BMPHANDLE h);

int write_u16_le(FILE *file, uint16_t val);
int write_u32_le(FILE *file, uint32_t val);
int read_u16_le(FILE *file, uint16_t *val);
int read_u32_le(FILE *file, uint32_t *val);

int write_s16_le(FILE *file, int16_t val);
int write_s32_le(FILE *file, int32_t val);
int read_s16_le(FILE *file, int16_t *val);
int read_s32_le(FILE *file, int32_t *val);

#define EXPORT_VIS __attribute__ ((visibility ("default")))


#define HMAGIC_READ	 (0x44414552UL)
#define HMAGIC_WRITE (0x54495257UL)

typedef struct Bmpread  *BMPREAD;
typedef struct Bmpwrite *BMPWRITE;





/* There seems to be ambiguity about wether the 40-byte
 * BITMAPINFOHEADER is version 1 or version 3. Both make
 * sense, depending on wether you consider the
 * BITMAPCORE/OS2 versions v1 and v2, or if you consider
 * the Adobe-extensions (supposedly at one time
 * MS-'official') v2 and v3.
 * I am going with BITMAPINFOHEADER = v3.
 */


#define BMPFILE_BM (0x4d42)
#define BMPFILE_BA (0x4142)
#define BMPFILE_CI (0x4943)
#define BMPFILE_CP (0x5043)
#define BMPFILE_IC (0x4349)
#define BMPFILE_PT (0x5450)


#define BMPFHSIZE     (14)
#define BMPIHSIZE_V3  (40)
#define BMPIHSIZE_V4 (108)

typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t  LONG;
typedef uint8_t  BYTE;

struct Bmpfile {
	WORD  type; /* "BM" */
	DWORD size; /* bytes in file */
	WORD  reserved1;
	WORD  reserved2;
	DWORD offbits;
};

struct Bmpinfo {
	/* BITMAPINFOHEADER (40 bytes) */
	DWORD size; /* sizof struct */
	LONG  width;
	LONG  height;
	WORD  planes;
	WORD  bitcount;
	DWORD compression;
	DWORD sizeimage; /* 0 ok for uncompressed */
	LONG  xpelspermeter;
	LONG  ypelspermeter;
	DWORD clrused;
	DWORD clrimportant;
	/* BITMAPV4INFOHEADER (108 bytes) */
	DWORD redmask;
	DWORD greenmask;
	DWORD bluemask;
	DWORD alphamask;
	DWORD cstype;
	LONG  redX;
	LONG  redY;
	LONG  redZ;
	LONG  greenX;
	LONG  greenY;
	LONG  greenZ;
	LONG  blueX;
	LONG  blueY;
	LONG  blueZ;
	DWORD gammared;
	DWORD gammagreen;
	DWORD gammablue;
	/* BITMAPV5INFOHEADER (124 bytes) */
	DWORD intent;
	DWORD profiledata;
	DWORD profilesize;
	DWORD reserved;

	/* OS22XBITMAPHEADER */
	WORD resolution;      /* = 0   */
	WORD orientation;     /* = 0   */
	WORD halftone_alg;
	DWORD halftone_parm1;
	DWORD halftone_parm2;
	DWORD color_encoding;  /* = 0 (RGB) */
	DWORD app_id;

  /* internal only, not from file: */
  enum BmpInfoVer   version;
};




#define BI_RGB             0
#define BI_RLE8            1
#define BI_RLE4            2
#define BI_BITFIELDS       3
#define BI_JPEG            4
#define BI_PNG             5
#define BI_ALPHABITFIELDS  6
#define BI_CMYK           11
#define BI_CMYKRLE8       12
#define BI_CMYKRLE4       13

#define BI_OS2_HUFFMAN_DUP  3 /* same as BI_BITFIELDS in Win BMP */
#define BI_OS2_RLE24_DUP    4 /* same as BI_JPEG in Win BMP      */

/* we set our own unique values: */
#define BI_OS2_HUFFMAN  1001
#define BI_OS2_RLE24    1002
