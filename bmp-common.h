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

#if defined(__GNUC__)
	#define ATTR_CONST __attribute__((const))
#else
	#define ATTR_CONST
#endif

#ifdef WIN32
        #define API
#else
	#define API __attribute__ ((visibility ("default")))
#endif

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
		unsigned long long value[4];
		struct {
			unsigned long long red;
			unsigned long long green;
			unsigned long long blue;
			unsigned long long alpha;
		};
	} mask;
	union {
		int value[4];
		struct {
			int red;
			int green;
			int blue;
			int alpha;
		};
	} shift;
	union {
		int value[4];
		struct {
			int red;
			int green;
			int blue;
			int alpha;
		};
	} bits;
	union {
		double val[4];
		struct {
			double red;
			double green;
			double blue;
			double alpha;
		};
	} maxval;
};

typedef struct Bmpread  *BMPREAD;
typedef struct Bmpwrite *BMPWRITE;
typedef struct Bmpread * restrict BMPREAD_R;
typedef struct Bmpwrite * restrict BMPWRITE_R;

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
	size_t            bytes_read;  /* number of bytes we have read from the file */
	struct Bmpfile   *fh;
	struct Bmpinfo   *ih;
	unsigned int      insanity_limit;
	int               width;
	unsigned          height;
	enum BmpOrient    orientation;
	int               has_alpha;   /* original BMP has alpha channel */
	enum BmpUndefined undefined_mode;
	int               we_allocated_buffer;
	int               line_by_line;
	struct Palette   *palette;
	struct Colormask  cmask;
	/* result image dimensions */
	enum Bmpconv64    conv64;
	int               conv64_explicit;
	int               result_channels;
	int               result_indexed;
	int               result_bits_per_pixel;
	int               result_bytes_per_pixel;
	int               result_bitsperchannel;
	enum BmpFormat    result_format;
	int               result_format_explicit;
	size_t            result_size;
	/* state */
	unsigned long     lasterr;
	int               getinfo_called;
	int               getinfo_return;
	int               jpeg;
	int               png;
	int               dimensions_queried;
	int               dim_queried_width;
	int               dim_queried_height;
	int               dim_queried_channels;
	int               dim_queried_bitsperchannel;
	int               image_loaded;
	int               rle;
	int               rle_eol;
	int               rle_eof;
	int               lbl_x;  /* remember where we are in the image  */
	int               lbl_y;  /* for line by line reading            */
	int               lbl_file_y;  /* RLE files may be ahead of the image y */
	uint32_t          hufbuf;
	int               hufbuf_len;
	int               truncated;
	int               invalid_index;
	int               invalid_delta;
	int               invalid_overrun;
	int               file_err;
	int               file_eof;
	int               panic;

};


struct Bmpwrite {
	struct {
		uint32_t magic;
		LOG      log;
	};
	FILE            *file;
	struct Bmpfile  *fh;
	struct Bmpinfo  *ih;
	int              width;
	int              height;
	/* input */
	int              source_channels;
	int              source_bitsperchannel;
	int              source_bytes_per_pixel;
	int              source_format;
	struct Palette  *palette;
	int              palette_size; /* sizeof palette in bytes */
	/* output */
	size_t           bytes_written;
	size_t           bytes_written_before_bitdata;
	int              has_alpha;
	enum BmpOrient   outorientation;
	struct Colormask cmask;
	int              rle_requested;
	int              rle;
	int              allow_2bit; /* Windows CE, but many will not read it */
	int              allow_huffman;
	int              allow_rle24;
	int              out64bit;
	int              outbytes_per_pixel;
	int              padding;
	int             *group;
	int              group_count;
	/* state */
	int              outbits_set;
	int              dimensions_set;
	int              saveimage_done;
	int              line_by_line;
	int              lbl_y;
	uint32_t         hufbuf;
	int              hufbuf_len;
};



int cm_all_lessoreq_int(int limit, int n, ...);
int cm_all_equal_int(int n, ...);
int cm_all_positive_int(int n, ...);
int cm_is_one_of(int n, int candidate, ...);

#define cm_align4size(a)     ((((a) + 3) >> 2) << 2)
#define cm_align2size(a)     ((((a) + 1) >> 1) << 1)
int cm_align4padding(unsigned long long a);
int cm_align2padding(unsigned long long a);
int cm_count_bits(unsigned long v);

int cm_gobble_up(BMPREAD_R rp, int count);
int cm_check_is_read_handle(BMPHANDLE h);
int cm_check_is_write_handle(BMPHANDLE h);

const char* cm_conv64_name(enum Bmpconv64 conv);
const char* cm_format_name(enum BmpFormat format);

int write_u16_le(FILE *file, uint16_t val);
int write_u32_le(FILE *file, uint32_t val);
int read_u16_le(FILE *file, uint16_t *val);
int read_u32_le(FILE *file, uint32_t *val);

int write_s16_le(FILE *file, int16_t val);
int write_s32_le(FILE *file, int32_t val);
int read_s16_le(FILE *file, int16_t *val);
int read_s32_le(FILE *file, int32_t *val);



#define HMAGIC_READ  0x44414552UL
#define HMAGIC_WRITE 0x54495257UL

#define BMPFILE_BM 0x4d42
#define BMPFILE_BA 0x4142
#define BMPFILE_CI 0x4943
#define BMPFILE_CP 0x5043
#define BMPFILE_IC 0x4349
#define BMPFILE_PT 0x5450


#define BMPFHSIZE           14
#define BMPIHSIZE_V3        40
#define BMPIHSIZE_V4       108
#define BMPIHSIZE_OS22      64

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


#define LCS_CALIBRATED_RGB      0
#define LCS_sRGB                0x73524742 /* 'sRGB' */
#define LCS_WINDOWS_COLOR_SPACE 0x57696e20 /* 'Win ' */
#define PROFILE_LINKED          0x4c494e4b /* 'LINK' */
#define PROFILE_EMBEDDED        0x4d424544 /* 'MBED' */
