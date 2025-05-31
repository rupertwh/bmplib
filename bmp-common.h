/* bmplib - bmp-common.h
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

#undef MAX
#undef MIN
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARR_SIZE(a) ((int) (sizeof a / sizeof (a)[0]))

#if defined(__GNUC__)
	#define ATTR_CONST __attribute__((const))
	#define API __attribute__ ((visibility ("default")))
#else
	#define ATTR_CONST
	#define API
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


struct Palette {
	int         numcolors;
	union Pixel color[1];
};


struct Bmpcommon {
	uint32_t magic;
	LOG      log;
	bool     huffman_black_is_zero; /* defaults to false */
};

enum ReadState {
	RS_INIT,
	RS_EXPECT_ICON_MASK,
	RS_HEADER_OK,
	RS_DIMENSIONS_QUERIED,
	RS_LOAD_STARTED,
	RS_LOAD_DONE,
	RS_ARRAY,
	RS_FATAL,
};

struct Bmpread {
	struct Bmpcommon  c;
	FILE             *file;
	size_t            bytes_read;  /* number of bytes we have read from the file */
	struct Bmpfile   *fh;
	struct Bmpinfo   *ih;
	struct Arraylist *arrayimgs;
	int               narrayimgs;
	bool              is_arrayimg;
	unsigned int      insanity_limit;
	int               width;
	int               height;
	enum BmpOrient    orientation;
	bool              is_icon;
	bool              icon_is_mono;
	bool              has_alpha;   /* original BMP has alpha channel */
	enum BmpUndefined undefined_mode;
	bool              we_allocated_buffer;
	struct Palette   *palette;
	struct Colormask  cmask;
	unsigned char    *icon_mono_and;
	unsigned char    *icon_mono_xor;
	int               icon_mono_width;
	int               icon_mono_height;
	/* result image dimensions */
	enum Bmpconv64    conv64;
	int               result_channels;
	int               result_bits_per_pixel;
	int               result_bytes_per_pixel;
	int               result_bitsperchannel;
	enum BmpFormat    result_format;
	size_t            result_size;
	bool              conv64_explicit;
	bool              result_indexed;
	bool              result_format_explicit;
	/* state */
	unsigned long     lasterr;
	enum ReadState    read_state;
	int               getinfo_return;
	bool              jpeg;
	bool              png;
	bool              dim_queried_width;
	bool              dim_queried_height;
	bool              dim_queried_channels;
	bool              dim_queried_bitsperchannel;
	bool              iccprofile_size_queried;
	bool              rle;
	bool              rle_eol;
	bool              rle_eof;
	int               lbl_x;  /* remember where we are in the image  */
	int               lbl_y;  /* for line by line reading            */
	int               lbl_file_y;  /* RLE files may be ahead of the image y */
	uint32_t          hufbuf;
	int               hufbuf_len;
	bool              truncated;
	bool              invalid_index;
	bool              invalid_delta;
	bool              invalid_overrun;
	bool              file_err;
	bool              file_eof;
	bool              panic;
};

enum WriteState {
	WS_INIT,
	WS_DIMENSIONS_SET,
	WS_SAVE_STARTED,
	WS_SAVE_DONE,
	WS_FATAL,
};

struct Bmpwrite {
	struct Bmpcommon c;
	FILE            *file;
	struct Bmpfile  *fh;
	struct Bmpinfo  *ih;
	int              width;
	int              height;
	/* input */
	int              source_channels;
	int              source_bitsperchannel;
	int              source_bytes_per_pixel;
	enum BmpFormat   source_format;
	bool             source_has_alpha;
	struct Palette  *palette;
	int              palette_size; /* sizeof palette in bytes */
	unsigned char   *iccprofile;
	int              iccprofile_size;
	/* output */
	uint64_t         bytes_written;
	size_t           bytes_written_before_bitdata;
	enum BmpOrient   outorientation;
	bool             huffman_fg_idx;
	struct Colormask cmask;
	enum BmpRLEtype  rle_requested;
	int              rle; /* 1, 4, 8, or 24 */
	bool             allow_2bit;     /* Windows CE */
	bool             allow_huffman;  /* OS/2 */
	bool             allow_rle24;    /* OS/2 */
	bool             out64bit;
	int              outbytes_per_pixel;
	int              padding;
	int             *group;
	int              group_count;
	/* state */
	enum WriteState  write_state;
	bool             outbits_set;
	int              lbl_y;
	uint32_t         hufbuf;
	int              hufbuf_len;
};


union Bmphandle {
	struct Bmpcommon  common;
	struct Bmpread    read;
	struct Bmpwrite   write;
};


typedef struct Bmpread  *BMPREAD;
typedef struct Bmpwrite *BMPWRITE;
typedef struct Bmpread  *restrict BMPREAD_R;
typedef struct Bmpwrite *restrict BMPWRITE_R;


bool cm_all_lessoreq_int(int limit, int n, ...);
bool cm_all_equal_int(int n, ...);
bool cm_all_positive_int(int n, ...);
bool cm_is_one_of(int n, int candidate, ...);

#define cm_align4size(a)     (((a) + 3) & ~3ULL)
int cm_align4padding(unsigned long long a);
int cm_count_bits(unsigned long v);

bool cm_gobble_up(BMPREAD_R rp, int count);
BMPREAD cm_read_handle(BMPHANDLE h);
BMPWRITE cm_write_handle(BMPHANDLE h);

const char* cm_conv64_name(enum Bmpconv64 conv);
const char* cm_format_name(enum BmpFormat format);

bool write_u16_le(FILE *file, uint16_t val);
bool write_u32_le(FILE *file, uint32_t val);
bool read_u16_le(FILE *file, uint16_t *val);
bool read_u32_le(FILE *file, uint32_t *val);

bool write_s16_le(FILE *file, int16_t val);
bool write_s32_le(FILE *file, int32_t val);
bool read_s16_le(FILE *file, int16_t *val);
bool read_s32_le(FILE *file, int32_t *val);

uint32_t u32_from_le(const unsigned char *buf);
int32_t  s32_from_le(const unsigned char *buf);
uint16_t u16_from_le(const unsigned char *buf);
int16_t  s16_from_le(const unsigned char *buf);

const char* cm_infoheader_name(enum BmpInfoVer infoversion);


#define HMAGIC_READ  0x44414552UL
#define HMAGIC_WRITE 0x54495257UL

#define BMPFILE_BM 0x4d42
#define BMPFILE_BA 0x4142
#define BMPFILE_CI 0x4943
#define BMPFILE_CP 0x5043
#define BMPFILE_IC 0x4349
#define BMPFILE_PT 0x5450


#define BMPFHSIZE       14
#define BMPIHSIZE_V3    40
#define BMPIHSIZE_V4   108
#define BMPIHSIZE_OS22  64
#define BMPIHSIZE_V5   124

struct Bmpfile {
	uint16_t type; /* "BM" */
	uint32_t size; /* bytes in file */
	uint16_t reserved1;
	uint16_t reserved2;
	uint32_t offbits;
};

struct Bmpinfo {
	/* BITMAPINFOHEADER (40 bytes) */
	uint32_t size; /* sizof struct */
	int32_t  width;
	int32_t  height;
	uint16_t planes;
	uint16_t bitcount;
	uint32_t compression;
	uint32_t sizeimage; /* 0 ok for uncompressed */
	int32_t  xpelspermeter;
	int32_t  ypelspermeter;
	uint32_t clrused;
	uint32_t clrimportant;
	/* BITMAPV4INFOHEADER (108 bytes) */
	uint32_t redmask;
	uint32_t greenmask;
	uint32_t bluemask;
	uint32_t alphamask;
	uint32_t cstype;
	int32_t  redX;
	int32_t  redY;
	int32_t  redZ;
	int32_t  greenX;
	int32_t  greenY;
	int32_t  greenZ;
	int32_t  blueX;
	int32_t  blueY;
	int32_t  blueZ;
	uint32_t gammared;
	uint32_t gammagreen;
	uint32_t gammablue;
	/* BITMAPV5INFOHEADER (124 bytes) */
	uint32_t intent;
	uint32_t profiledata;
	uint32_t profilesize;
	uint32_t reserved;

	/* OS22XBITMAPHEADER */
	uint16_t resolution;      /* = 0   */
	uint16_t orientation;     /* = 0   */
	uint16_t halftone_alg;
	uint32_t halftone_parm1;
	uint32_t halftone_parm2;
	uint32_t color_encoding;  /* = 0 (RGB) */
	uint32_t app_id;

	/* internal only, not from file: */
	enum BmpInfoVer version;
};

struct Bmparray {
	uint16_t type;
	uint32_t size;
	uint32_t offsetnext;
	uint16_t screenwidth;
	uint16_t screenheight;
};

#define IH_PROFILEDATA_OFFSET (14L + 112L)

#define MAX_ICCPROFILE_SIZE (1UL << 20)


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


#define LCS_GM_BUSINESS         1
#define LCS_GM_GRAPHICS         2
#define LCS_GM_IMAGES           4
#define LCS_GM_ABS_COLORIMETRIC 8
