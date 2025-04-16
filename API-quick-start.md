# Rupert's bmplib -- Quick Start Guide

The API has grown quite a bit more than I had originally anticipated. But most
of the provided functions are optional.

So, to not scare you off right away, I wrote this quick start guide version of
the API description which only covers the basic functions. These basic
functions will be sufficient for many/most use cases.  (see also the two
examples at the end of this document):

For the complete API, refer to the *Full API Description* (API-full.md).

## 1. Reading BMP files:

```c
bmpread_new();
bmpread_dimensions();
bmpread_load_image();
bmp_free();
```

### Get a handle

```c
BMPHANDLE bmpread_new(FILE *file)
```

`bmpread_new()` returns a handle which is used for all subsequent calls to
bmplib. When you are done with the file, call `bmp_free()` to release this
handle.

The handle cannot be reused to read multiple files.

### Get image dimensions

```c
BMPRESULT bmpread_dimensions(BMPHANDLE  h,
                             int       *width,
                             int       *height,
                             int       *channels,
                             int       *bitsperchannel,
                             BMPORIENT *orientation);
```

Use `bmpread_dimensions()` to get all dimensions with one call. The return
value will be the same as for `bmpread_load_info()` (see "3. Result codes"
below and *Full API Description*).

The dimensions describe the image returned by bmplib, *not* necessarily the
original BMP file.

`orientation` can be ignored, it is only relevant when loading the image
line-by-line. Can be set to NULL. (see *Full API Description*)


### Load the image

```c
BMPRESULT bmpread_load_image(BMPHANDLE h, unsigned char **pbuffer);
```

Loads the complete image from the BMP file into the buffer pointed to by
`pbuffer`. You can either allocate a buffer yourself or let `pbuffer` point
to a NULL-pointer in which case bmplib will allocate an appropriate buffer.
In the latter case, you will have to `free()` the buffer, once you are done
with it.

If you allocate the buffer yourself, the buffer must be at least as large as
the size returned by `bmpread_buffersize()` (see *Full API description*).

```c
unsigned char *buffer;

/* either: */
buffer = NULL;
bmpread_load_image(h, &buffer);   /* bmplib will allocate the buffer */

/* or: */
buffer = malloc(bmpread_buffersize(h));
bmpread_load_image(h, &buffer);   /* bmplib will use the provided buffer */

```

The image data is written to the buffer according to the returned dimensions
(see above). 16-bit and 32-bit values are always written in host byte order,
in the order R-G-B or R-G-B-A. The returned image is always top-down, i.e.
data starts in the top left corner. Unlike BMPs which are (almost always)
bottom-up.

If `bmpread_load_image()` returns `BMP_RESULT_TRUNCATED` or `BMP_RESULT_INVALID`,
the file may have been damaged or simply contains invalid image data. Image
data is loaded anyway as far as possible and may be partially usable.



### Release the handle

```c
void bmp_free(BMPHANDLE h);
```

Frees all resources associated with the handle `h`. **Image data is not
affected**, so you can call `bmp_free()` immediately after `bmpread_load_image()`
and still use the returned image data.

Note: Any error message strings returned by `bmp_errmsg()` are invalidated by
`bmp_free()` and must not be used anymore!



## 2. Writing BMP files:

```c
bmpwrite_new();
bmpwrite_set_dimensions();
bmpwrite_save_image();
bmp_free();
```

### Get a handle

```c
BMPHANDLE bmpwrite_new(FILE *file);
```

### Set image dimensions

```c
BMPRESULT bmpwrite_set_dimensions(BMPHANDLE h,
                                  unsigned  width,
                                  unsigned  height,
                                  unsigned  channels,
                                  unsigned  bitsperchannel);
```

Note: the dimensions set with `bmpwrite_set_dimensions()` describe the source
data that you pass to bmplib, *not* the output BMP format. Use
`bmpwrite_set_output_bits()`, `bmpwrite_set_palette()`, and
`bmpwrite_set_64bit()` to modify the format written to the BMP file. (see *Full
API description*)



### Write the image

```c
BMPRESULT bmpwrite_save_image(BMPHANDLE h, const unsigned char *image);
```

Write the whole image at once with `bmpwrite_save_image()`.

The image data pointed to by `image` must be in the format described
by `bmpwrite_set_dimensions()`. Multi-byte values (16 or 32 bit) are expected
in host byte order, the channels in the order R-G-B-(A).

Important: Image data must be provided top-down. (Even though the created BMP
file will be bottom-up.)

### bmp_free()

```c
void bmp_free(BMPHANDLE h);
```

Frees all resources associated with the handle `h`.

Note: Any error messages returned by `bmp_errmsg()` are invalidated by
`bmp_free()` and cannot be used anymore.




## 3. Result codes

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

Can safely be cast from/to int. `BMP_RESULT_OK` is guaranteed to have the value 0.



## 5. Sample code

### Reading BMPs

```c
    /* (all error checking left out for clarity) */

    BMPHANDLE      h;
    FILE          *file;
    int            width, height, channels, bitsperchannel;
    unsigned char *image_buffer;


    /* Open a file and call bmpread_new() to get a BMPHANDLE,
     * which will be used on all subsequent calls.
     */

    file = fopen("someimage.bmp", "rb");
    h    = bmpread_new(file);


    /* Get image dimensions
     * The color information (channels/bits) describes the data
     * that bmplib will return, NOT necessarily the BMP file.
     * Setting orientation to NULL, image is always returned top-down.
     */

    bmpread_dimensions(h, &width, &height, &channels, &bitsperchannel, NULL);


    /* Load the image and clean up: */

    image_buffer = NULL;
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

```c
    /* (all error checking left out for clarity) */

    BMPHANDLE      h;
    FILE          *file;
    unsigned char *image_buffer;
    int            width, height, channels, bitsperchannel;

    /* 'image_buffer' contains the image to be saved as either
     * 8, 16, or 32 bits per channel RGB or RGBA data in
     * host byte order without any padding
     */


    /* Open a file for writing and get a BMPHANDLE */

    file = fopen("image.bmp", "wb");
    h    = bmpwrite_new(file);


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
