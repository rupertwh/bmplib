# Rupert's bmplib -- Full API Description (v1.7.1)

Refer to the *Quick Start Guide* (API-quick-start.md) for a quick intro to bmplib which describes only the minimal set of functions needed to read/write BMP files.


## 1. Functions for reading BMP files

### Get a handle
```
BMPHANDLE bmpread_new(FILE *file)
```

`bmpread_new()` returns a handle which is used for all subsequent calls to
bmplib. When you are done with the file, call `bmp_free()` to release this
handle.

The handle cannot be reused to read multiple files.



### Read the file header
```
BMPRESULT bmpread_load_info(BMPHANDLE h)
```

bmplib reads the file header and checks validity. Possible return values are:
- `BMP_RESULT_OK`: All is good, you can proceed to read the file.
- `BMP_INSANE`: The file is valid, but huge. The default limit is 500MB
  (relevant is the required buffer size to hold the complete image, not the
  file size. If you want to read the file anyway, use
  `bmpread_set_insanity_limit()` to increase the allowed file size.
  Otherwise, `bmpread_load_image()` will refuse to load the image. (You can
  build the library with a different default limit, using the meson
  option '-Dinsanity_limit_mb=nnn')
- `BMP_RESULT_PNG` / `BMP_RESULT_JPEG`: It's not really a BMP file, but a
  wrapped PNG or JPEG. The file pointer is left in the correct state to be
  passed on to e.g. libpng or libjpeg.
- `BMP_RESULT_ERROR`: The file cannot be read. Either it's not a valid or an
  unsupported BMP or there was some file error. Use `bmp_errmsg()` to get an
  idea what went wrong.

Calling `bmpread_load_info()` is optional when you use `bmpread_dimensions()`
(see below).


### Get image dimensions
```
BMPRESULT bmpread_dimensions(BMPHANDLE  h,
                             int       *width,
                             int       *height,
                             int       *channels,
                             int       *bitsperchannel,
                             BMPORIENT *orientation)
```

Use `bmpread_dimensions()` to get all dimensions with one call. It is not
necessary to call `bmpread_load_info()` first. The return value will be the
same as for `bmpread_load_info()`.

The dimensions describe the image returned by bmplib, *not* necessarily the
original BMP file.

Alternatively, you can use the following functions to receive the values one
at a time, each returned as an `int`. Getting the horizontal and vertical
resolutions in DPI is only available via these single functions.

Note, in order to use these functions, -- unlike with `bmpread_dimensions
()` -- you must first (successfully) call `bmpread_load_info()`, otherwise
they will all return 0!

```
int       bmpread_width(BMPHANDLE h)
int       bmpread_height(BMPHANDLE h)
int       bmpread_channels(BMPHANDLE h)
int       bmpread_bitsperchannel(BMPHANDLE h)
BMPORIENT bmpread_orientation(BMPHANDLE h)

int       bmpread_resolution_xdpi(BMPHANDLE h)
int       bmpread_resolution_ydpi(BMPHANDLE h)

```

#### top-down / bottom-up
`*orientation` is one of:
 - `BMP_ORIENT_BOTTOMUP`
 - `BMP_ORIENT_TOPDOWN`

 `bmpread_orientation()` or the `orientation` value returned by
 `bmpread_dimensions()` **is only relevant if you load the BMP file
 line-by-line**. In line-by-line mode (using `bmpread_load_line()`), the
 image data is always delivered in the order it is in the BMP file. The
 `orientation` value will tell you if it's top-down or bottom-up. On the
 other hand, when the whole image is loaded at once (using `bmpread_load_image
 ()`), bmplib will **always** return the image top-down, regardless of how
 the BMP file is oriented. The `orientation` value will still indicate the
 orientation of the original BMP.

#### Required size for buffer to receive image
```
size_t    bmpread_buffersize(BMPHANDLE h)
```
Returns the buffer size you have to allocate for the whole image.


### Indexed BMPs

By default, bmplib will interpret indexed (color palette) BMPs and return the
image as 24-bit RGB data, same as non-indexed (RGB) BMPs.

If instead you want to keep the image as indexed, you have the option do so
with these two functions:

```
int       bmpread_num_palette_colors(BMPHANDLE h)
BMPRESULT bmpread_load_palette(BMPHANDLE h, unsigned char **palette)
```

`bmpread_num_palette_colors()` will return 0 for non-indexed images, otherwise
it will return the number of entries in the color palette.

`bmpread_load_palette()` will retrieve the color palette and store it in the
character buffer pointed to by `palette`.

The colors are stored 4 bytes per entry: The first three bytes are the red,
green, and blue values in that order, the 4th byte is always set to zero. So
the palette buffer will contain "rgb0rgb0rgb0...".

As with the main image buffer, you can either provide one for the palette or
let bmplib allocate it for you (and then `free()` it, once you are done):

```
unsigned char *palette;
int            numcolors;

numcolors = bmpread_num_palette_colors(h);

/* either: */
palette = NULL;
bmpread_load_palette(h, &palette);          /* bmplib will allocate the palette buffer */

/* or: */
palette = malloc(4 * numcolors);
bmpread_load_palette(h, &palette);          /* bmplib will use the provided buffer */

```

Note: Once you have called `bmpread_load_palette()`, both `bmpread_load_image
()` and `bmpread_load_line()` will return the image as 8-bit indexes into the
palette, you *cannot go back* to the default of loading the image as 24-bit
RGB data. After you loaded the palette, calls to `bmpread_dimensions
()`, `bmpread_buffersize()`, etc. will reflect that change. Also,
`bmpread_set_undefined()` will have no effect, as indexed images cannot have
an alpha channel (see below).

#### Undefined pixels

RLE-encoded BMP files may have undefined pixels, either by using early
end-of-line or end-of-file codes, or by using delta codes to skip part of the
image. That is not an error, but a feature of RLE. bmplib default is to make
such pixels transparent. RLE-encoded BMPs will therefore always be returned
with an alpha channel by default, whether the file has such undefined pixels
or not (because bmplib doesn't know beforehand if there will be any undefined
pixels).

You can change this behaviour by calling `bmpread_set_undefined()`, with
`mode` set to `BMP_UNDEFINED_LEAVE`. In that case, the returned image will
have no alpha channel, and undefined pixels will not change the image buffer.
So whatever was in the image buffer before loading the image will remain
untouched by undefined pixels. (Note: if you let bmplib allocate the image
buffer, it will always be initialized to zero before loading the image). This
function has no effect on non-RLE BMPs.

```
void bmpread_set_undefined(BMPHANDLE h, BMPUNDEFINED mode)
```

`mode` can be one of:
- `BMP_UNDEFINED_LEAVE`
- `BMP_UNDEFINED_TO_ALPHA` (default)

Note: If you use `bmpread_load_palette()` to switch to loading the index data
instead of RGB data, this setting will have no effect and undefined pixels
will always be left alone! (see above)


### Optional settings for 64bit BMPs

```
int bmpread_is_64bit(BMPHANDLE h)
BMPRESULT bmpread_set_64bit_conv(BMPHANDLE h, BMPCONV64 conv)
```

If you don't do anything, 64bit BMPs will be read like any other BMP and the
data will be returned as 16bit/channel sRGB RGBA.

But if you want to access the original s2.13 fixed-point components, or you
don't want the linear-to-sRGB conversion, you can use `bmpread_set_64bit_conv
()` and `bmp_set_number_format()` to control how the image is returned:

Options for `bmpread_set_64bit()` are:

- `BMP_CONV64_SRGB`: the default, original data is assumed to be s2.13
  fixed-point linear and converted to sRGB-gamma.
- `BMP_CONV64_LINEAR`: no gamma-conversion is applied to the image data.
- `BMP_CONV64_NONE`: this option is just a shorthand for setting
  BMP_CONV64_LINEAR *and* BMP_FORMAT_S2_13. Image values are returned exactly
  as they are in the BMP file, without any conversion or attempt at
  interpretation.


### Setting a number format

By default, bmplib will always return the image data as 8-,16-, or 32-bit integer values. You can instead set the number format to floating point or fixed using:

```
BMPRESULT bmp_set_number_format(BMPHANDLE h, BMPFORMAT format)
```

(see below, *3. General functions for both reading/writing BMPs*)



### Huge files: bmpread_set_insanity_limit()

bmplib will refuse to load images beyond a certain size (default 500MB) and
instead return BMP_RESULT_INSANE. If you want to load the image anyway, call
`bmpread_set_insanity_limit()` at any time before calling `bmpread_load_image
()`. `limit` is the new allowed size in bytes. (not MB!)

```
void
bmpread_set_insanity_limit(BMPHANDLE h, size_t limit)
```


### Load the image
#### bmpread_load_image()

```
BMPRESULT bmpread_load_image(BMPHANDLE h, unsigned char **pbuffer)
```

Loads the complete image from the BMP file into the buffer pointed to by
`pbuffer`. You can either allocate a buffer yourself or let `pbuffer` point
to a NULL-pointer in which case bmplib will allocate an appropriate buffer.
In the latter case, you will have to `free()` the buffer, once you are done
with it.

If you allocate the buffer yourself, the buffer must be at least as large as
the size returned by `bmpread_buffersize()`.

```
unsigned char *buffer;

/* either: */
buffer = NULL;
bmpread_load_image(h, &buffer);   /* bmplib will allocate the buffer */

/* or: */
buffer = malloc(bmpread_buffersize(h));
bmpread_load_image(h, &buffer);   /* bmplib will use the provided buffer */

```

The image data is written to the buffer according to the returned dimensions
(see above). Multi-byte values are always written in host byte order, in the
order R-G-B or R-G-B-A. The returned image is always top-down, i.e. data
starts in the top left corner. Unlike BMPs which are (almost always)
bottom-up. (See above, "Getting information...")

If `bmpread_load_image()` returns BMP_RESULT_TRUNCATED or BMP_RESULT_INVALID,
the file may have been damaged or simply contains invalid image data. Image
data is loaded anyway as far as possible and may be partially usable.

#### bmpread_load_line()
```
BMPRESULT bmpread_load_line(BMPHANDLE h, unsigned char **pbuffer)
```

Loads a single scan line from the BMP file into the buffer pointed to by
`pbuffer`. You can either allocate a buffer yourself or let `pbuffer` point
to a NULL-pointer in which case bmplib will allocate an appropriate buffer.
In the latter case, you will have to call `free()` on the buffer, once you
are done with it.

To determine the required buffer size, either divide the value from
`bmpread_buffersize()` by the number of scanlines (= `bmpread_height()`), or
calculate from the image dimensions returned by bmplib as width * channels *
bitsperchannel / 8.

```
single_line_buffersize = bmpread_buffersize(h) / bmpread_height(h);
/* or */
single_line_buffersize = bmpread_width(h) * bmpread_channels(h) * bmpread_bitsperchannel(h) / 8;
```

Repeated calls to `bmpread_load_line()` will return each scan line, one after
the other.

Important: when reading the image this way, line by line, the orientation
(`bmpread_orientation()`) of the original BMP matters! The lines will be
returned in whichever order they are stored in the BMP. Use the value
returned by `bmpread_orientation()` to determine if it is top-down or
bottom-up. Almost all BMPs will be bottom-up. (see above)


### Invalid pixels

Invalid pixels may occur in indexed BMPs, both RLE and non-RLE. Invalid pixels
either point beyond the given color palette, or they try to set pixels
outside the image dimensions. Pixels containing an invalid color value will
be set to the maximum allowed value, and attempts to point outside the image
will be ignored.

In both cases, `bmpread_load_image()` and `bmpread_load_line()` will return
BMP_RESULT_INVALID, unless the image is also truncated, then
BMP_RESULT_TRUNCATED is returned.


### Query info about the BMP file

Note: these functions return information about the original BMP file being
read. They do *not* describe the format of the returned image data, which may
be different!

```
BMPINFOVER  bmpread_info_header_version(BMPHANDLE h)
int         bmpread_info_header_size(BMPHANDLE h)
int         bmpread_info_compression(BMPHANDLE h)
int         bmpread_info_bitcount(BMPHANDLE h)
const char* bmpread_info_header_name(BMPHANDLE h)
const char* bmpread_info_compression_name(BMPHANDLE h)
BMPRESULT   bmpread_info_channel_bits(BMPHANDLE h, int *r, int *g, int *b, int *a)
```


### Release the handle

```
void bmp_free(BMPHANDLE h)
```

Frees all resources associated with the handle `h`. **Image data is not
affected**, so you can call bmp_free() immediately after `bmpread_load_image
()` and still use the returned image data.

Note: Any error message strings returned by `bmp_errmsg()` are invalidated by
`bmp_free()` and must not be used anymore!




## 2. Functions for writing BMP files

### Get a handle
```
BMPHANDLE bmpwrite_new(FILE *file)
```

### Set image dimensions
```
BMPRESULT bmpwrite_set_dimensions(BMPHANDLE h,
                                  unsigned  width,
                                  unsigned  height,
                                  unsigned  channels,
                                  unsigned  bitsperchannel)

BMPRESULT bmpwrite_set_resolution(BMPHANDLE h, int xdpi, int ydpi)

```

Note: the dimensions set with `bmpwrite_set_dimensions()` describe the source
data that you pass to bmplib, *not* the output BMP format. Use
`bmpwrite_set_output_bits()`, `bmpwrite_set_palette()`, and
`bmpwrite_set_64bit()` to modify the format written to the BMP file.


### Set the output format

Optional: set the bit-depth for each output channel. bmplib will otherwise
choose appropriate bit-depths for your image. The bit-depth per channel can
be anywhere between 0 and 32, inclusive. In sum, the bits must be at least 1
and must not exceed 32.

```
BMPRESULT bmpwrite_set_output_bits(BMPHANDLE h, int red, int green, int blue, int alpha)
```

### Indexed images

```
BMPRESULT bmpwrite_set_palette(BMPHANDLE h, int numcolors, unsigned char *palette)
BMPRESULT bmpwrite_allow_2bit(BMPHANDLE h)
```

You can write 1/2/4/8-bit indexed images by providing a color palette with
`bmpwrite_set_palette()`. The palette entries must be 4 bytes each, the first
three bytes are the red, green, and blue values in that order, the 4th byte
is padding and will be ignored.

If you provide a palette, the image data you provide has to be 1 channel, 8
bits per pixel, regardless of the palette size. The pixel values must be
indexing the color palette. Invalid index values (which go beyond the color
palette) will be silently clipped. bmplib will choose the appropriate
bit-depth for the BMP according to the number of color-entries in your
palette.

By default, bmplib will not write 2-bit indexed BMPs (supposedly a Windows CE
relict), as many readers will refuse to open these. If you do want a 2-bit
BMP for 3- or 4-color images, call `bmpwrite_allow_2bit()` before calling
`bmpwrite_save_image()`.

#### RLE

```
BMPRESULT bmpwrite_set_rle(BMPHANDLE h, BMPRLETYPE type)
BMPRESULT bmpwrite_allow_huffman(BMPHANDLE h)
```

Indexed images may optionally be written run-lenght-encoded (RLE) bitmaps.
Images with 16 or fewer colors can be written as either RLE4 or RLE8
(default is RLE4), images with more than 16 colors only as RLE8.

Images with only 2 colors can also be written with 1-D Huffman encoding, but
only after explicitly allowing it by calling `bmpwrite_allow_huffman()`
(very few programs will be able to read Huffman encoded BMPs).

To activate RLE compression, call `bmpwrite_set_rle()` with `type` set to one
of the following values:
- `BMP_RLE_NONE` no RLE compression, same as not calling `bmpwrite_set_rle()`
  at all
- `BMP_RLE_AUTO` choose RLE4, RLE8, or 1-D Huffman based on number of colors
  in palette
- `BMP_RLE_RLE8` use RLE8, regardless of number of colors in palette

In order to write 1-D Huffman encoded bitmpas, the provided palette must have
2 colors, RLE type must be set to `BMP_RLE_AUTO`, and `bmpwrite_allow_huffman
()` must be called. Be aware that *very* few programs will be able to read
Huffman encoded BMPs!

#### RLE24

RLE24 is an old OS/2 compression method. As the name suggests, it's 24-bit RLE
for non-indexed images. Like Huffman encoding, it's very uncommon and only
very few programs will be able to read these BMPs. Writing RLE24 bitmaps must
be explicitly allowed by first calling `bmpwrite_allow_rle24()`.

In order to save an image as RLE24, the data must be provided as 8 bits per
channel RGB (no alpha channel). Call `bmpwrite_set_rle()` with type set to
`BMP_RLE_AUTO` and also call `bmpwrite_allow_rle24()` (in any order).


### top-down / bottom-up

By default, bmplib will write BMP files bottom-up, which is how BMP files are
usually orientated.

For non-RLE files, you have the option to change the orientation to top-down.
(RLE files always have to be written in the default bottom-up orientation.)

```
BMPRESULT bmpwrite_set_orientation(BMPHANDLE h, BMPORIENT orientation)
```

with `orientation` set to one of the following values:
 - `BMPORIENT_BOTTOMUP`
 - `BMPORIENT_TOPDOWN`

Note: When writing the whole image at once using `bmpwrite_save_image()`, the
image buffer you provide must **always** be in top-down orientation,
regardless of the orientation chosen for the BMP file.

When writing the image line-by-line using `bmpwrite_save_line()`, you must
provide the image lines in the order according to the orientation you have
chosen for the BMP file.




### 64-bit RGBA BMPs

By default, bmplib will not write 64-bit BMPs because they are rather exotic and hardly any
software can open them.

If you do want to write 64-bit BMPs, call

```
BMPRESULT bmpwrite_set_64bit(BMPHANDLE h)
```

In order to make use of the extended range available in 64-bit BMPs (-4.0 to +3.999...), you will probably want to provide the image buffer either as 32-bit float or as 16-bit s2.13 (and call `bmp_set_number_format()` accordingly).

Note: 64-bit BMPs store pixel values in *linear light*. Unlike when *reading* 64-bit BMPs, bmplib will not make any gamma/linear conversion while writing BMPs. You have to provide the proper linear values in the image buffer.




### Write the image

```
BMPRESULT bmpwrite_save_image(BMPHANDLE h, const unsigned char *image)
BMPRESULT bmpwrite_save_line(BMPHANDLE h, const unsigned char *line)
```

Write either the whole image at once with `bmpwrite_save_image()` or one line
at a time with `bmpwrite_save_line()`.

The image data pointed to by `image` or `line` must be in the format described
by `bmpwrite_set_dimensions()`. Multi-byte values (16 or 32 bit) are expected
in host byte order, the channels in the order R-G-B-(A). Indexed data must be
supplied as 8 bit per pixel, even when writing lower bit (1/2/4) BMPs
(see above).

Important: When writing the whole image at once using `bmpwrite_save_image
()`, the image data must be provided top-down (same as is returned by
`bmpread_load_image()`). When using `bmpwrite_save_line()` to write the image
line-by-line, the image data must be provided according to the orientation
set with `bmpwrite_set_orientation()` (see above).






## 3. General functions for both reading/writing BMPs

### bmp_free()

```
void bmp_free(BMPHANDLE h)
```

Frees all resources associated with the handle `h`. Image data is not
affected, so you can call bmp_free() immediately after bmpread_load_image
() and still use the returned image data. Note: Any error messages returned
by `bmp_errmsg()` are invalidated by `bmp_free()` and cannot be used
anymore.


### bmp_errmsg()

```
const char* bmp_errmsg(BMPHANDLE h)
```

Returns a zero-terminated character string containing the last error
description(s). The returned string is safe to use until any other
bmplib-function is called with the same handle.


### bmp_set_number_format()

```
BMPRESULT bmp_set_number_format(BMPHANDLE h, BMPFORMAT format)
```

sets the number format of the image buffer received from / passed to bmplib. `format` can be one of

- `BMP_FORMAT_INT` image buffer values are expected/returned as 8-, 16-, or 32-bit integers. (this is the default)
- `BMP_FORMAT_FLOAT` image buffer values are expected/returned as 32-bit floating point numbers (C `float`).
- `BMP_FORMAT_S2_13` image buffer values are expected/returned as s2.13 fixed point numbers. s2.13 is a 16-bit format with one sign bit, 2 integer bits, and 13 bits for the fractional part. Range is from -4.0 to +3.999...

For indexed images, `BMP_FORMAT_INT` is the only valid format.


### bmp_version()

```
const char* bmp_version(void)
```

Returns a zero-terminated character string containing the version of bmplib.




## 4. Data types and constants

#### `BMPHANDLE`

Returned by `bmpread_new()` and `bmpwrite_new()`.
Identifies the current operation for all subsequent
calls to bmplib-functions.

#### `BMPRESULT`

Many bmplib functions return the success/failure of an operation as a
`BMPRESULT`. It can have one of the following values:

- `BMP_RESULT_OK`
- `BMP_RESULT_INVALID`
- `BMP_RESULT_TRUNCATED`
- `BMP_RESULT_INSANE`
- `BMP_RESULT_PNG`
- `BMP_RESULT_JPEG`
- `BMP_RESULT_ERROR`

Can safely be cast from/to int. `BMP_RESULT_OK` will always have the vaue 0.
The rest will have values in increasing order. So it would be possible to do
something like:

```
if (bmpread_load_image(h, &buffer) <= BMP_RESULT_TRUNCATED) {
    /* use image - might be ok or (partially) corrupt */
}
else {
    printf("Couldn't get any image data: %s\n", bmp_errmsg(h));
}
```


#### `BMPINFOVER`

Returned by `bmpread_info_header_version()`. Possible values are:
- `BMPINFO_CORE_OS21` BITMAPCOREHEADER aka OS21XBITMAPHEADER (12 bytes)
- `BMPINFO_OS22`      OS22XBITMAPHEADER (16/40/64 bytes)
- `BMPINFO_V3`        BITMAPINFOHEADER (40 bytes)
- `BMPINFO_V3_ADOBE1` BITMAPINFOHEADER with additional RGB masks (52 bytes)
- `BMPINFO_V3_ADOBE2` BITMAPINFOHEADER with additional RGBA masks (56 bytes)
- `BMPINFO_V4,`       BITMAPV4HEADER (108 bytes)
- `BMPINFO_V5,`       BITMAPV5HEADER (124 bytes)
- `BMPINFO_FUTURE`    possible future info header (> 124 bytes)

Can safely be cast from/to int. The values will always be strictly increasing
from `BMPINFO_CORE_OS21` to `BMPINFO_FUTURE`.

#### `BMPRLETYPE`

Used in `bmpwrite_set_rle()`. Possible values are:
- `BMP_RLE_NONE` No RLE
- `BMP_RLE_AUTO` RLE4 or RLE8, chosen based on number of colors in palette
- `BMP_RLE_RLE8` Use RLE8 for any number of colors in palette

Can safely be cast from/to int.

#### `BMPUNDEFINED`

Used in `bmpread_set_undefined()`. Possible values are:
- `BMP_UNDEFINED_TO_ALPHA` (default)
- `BMP_UNDEFINED_TO_ZERO`

Can safely be cast from/to int.

#### `BMPCONV64`

Used in `bmpread_set_64bit_conv()`. Possible values are:
- `BMP_CONV64_SRGB` (default)
- `BMP_CONV64_LINEAR`
- `BMP_CONV64_NONE`

Can safely be cast from/to int.

#### `BMPFORMAT`

Used in `bmp_set_number_format()`. Possible values are:
- `BMP_FORMAT_INT` (default)
- `BMP_FORMAT_FLOAT` 32-bit floating point
- `BMP_FORMAT_S2_13` s2.13 fixed point



## 5. Sample code

### Reading BMPs

```
    /* (all error checking left out for clarity) */

    BMPHANDLE h;
    FILE     *file;
    int       width, height, channels, bitsperchannel, orientation;
    uint8_t  *image_buffer;


    /* Open a file and call bmpread_new() to get a BMPHANDLE,
     * which will be used on all subsequent calls.
     */

    file = fopen("someimage.bmp", "rb");
    h = bmpread_new(file);


    /* Get image dimensions
     * The color information (channels/bits) describes the data
     * that bmplib will return, NOT necessarily the BMP file.
     */

    bmpread_load_info(h);

    width          = bmpread_width(h);
    height         = bmpread_height(h);
    channels       = bmpread_channels(h);
    bitsperchannel = bmpread_bitsperchannel(h);


    /* Get required size for memory buffer and allocate memory */

    image_buffer = malloc(bmpread_buffersize(h));


    /* Load the image and clean up: */

    bmpread_load_image(h, &image_buffer);

    bmp_free(h);
    fclose(file);

    /* Ready to use the image written to image_buffer */

    /* Image data is always returned in host byte order as
     * 8, 16, or 32 bits per channel RGB or RGBA data.
     * No padding.
     */
```


### Writing BMPs

```
    /* (all error checking left out for clarity) */

    BMPHANDLE h;
    FILE     *file;
    uint8_t  *image_buffer;
    int       width, height, channels, bitsperchannel;

    /* 'image_buffer' contains the image to be saved as either
     * 8, 16, or 32 bits per channel RGB or RGBA data in
     * host byte order without any padding
     */


    /* Open a file for writing and get a BMPHANDLE */

    file = fopen("image.bmp", "wb");
    h = bmpwrite_new(file);


    /* Inform bmplib of the image dimensions.
     * The color information (channels, bits) refer to the format
     * your image buffer is in, not the format the BMP file should
     * be written in.
     */

    bmpwrite_set_dimensions(h, width, height, channels, bitsperchannel);


   /* Optional: choose bit-depths (independently for each channel,
    * in the order R,G,B,A) for the BMP file. bmplib will choose
    * an appropriate BMP file format to accomodate those bitdepths.
    */

    bmpwrite_set_output_bits(h, 5, 6, 5, 0);


    /* Save data to file */

    bmpwrite_save_image(h, image_buffer);

    bmp_free(h);
    fclose(file);
```
