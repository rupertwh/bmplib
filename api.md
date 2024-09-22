# bmplib API

## Data types and constants

### Data types

`BMPHANDLE`

Returned by `bmpread_new()` and `bmpwrite_new()`.
Identifies the current operation for all subsequent
calls to bmplib-functions.

`BMPRESULT`

Many bmplib functions return the success/failure of an operation as a `BMPRESULT`. It can have one
of the following values:

- `BMP_RESULT_OK`
- `BMP_RESULT_ERROR`
- `BMP_RESULT_TRUNCATED`
- `BMP_RESULT_INVALID`
- `BMP_RESULT_PNG`
- `BMP_RESULT_JPEG`
- `BMP_RESULT_INSANE`

`BMP_RESULT_OK` will always have the vaue 0, all other result codes should only be referred to by name, their values might change in the future.


## Functions for reading BMP files

### Getting a handle: bmpread_new()
```
BMPHANDLE bmpread_new(FILE *file)
```
`bmpread_new()` returns a handle which is used for all subsequent calls to bmplib. When you are done with the file, call `bmp_free()` to release this handle.

### Read the file header: bmpread_load_info()
```
BMPRESULT bmpread_load_info(BMPHANDLE h)
```
bmplib reads the file header and checks validity. Possible return values are:
- `BMP_RESULT_OK`: All is good, you can proceed to read the file.
- `BMP_INSANE`: The file is valid, but huge. The default limit is 500MB. If you want to read the file anyway, use `bmpread_set_insanity_limit()` to increase the allowed file size. Otherwise, `bmpread_load_image()` will refuse to load the image.
 (You can build the library with a different default limit, using the meson option '-Dinsanity_limit_mb=nnn')
- `BMP_RESULT_PNG` / `BMP_RESULT_JPEG`: It's not really a BMP file, but a wrapped PNG or JPEG. The file pointer is left in the correct state to be passed on to e.g. libpng or libjpeg.
- `BMP_RESULT_ERROR`: The file cannot be read. Either it's not a valid or an unsupported BMP or there was some file error. Use `bmp_errmsg()` to get an idea what went wrong.

Calling `bmpread_load_info()` is optional when you use `bmpread_dimensions()` (see below).
### Getting information about image dimensions: bmpread_dimensions() et al.
```
BMPRESULT bmpread_dimensions(BMPHANDLE h,
                             int      *width,
                             int      *height,
                             int      *channels,
                             int      *bitsperchannel,
                             int      *topdown)
```
Use `bmpread_dimensions()` to get all dimensions with one call. It is not necessary to call `bmpread_load_info()` first. The return value will be the same as for `bmpread_load_info()`.

The dimensions describe the image returned by bmplib, *not* necesarily the original BMP file.

Alternatively, you can use the following functions to receive the values one at a time, each returned as an `int`.

Note, in order to use these functions, -- unlike with `bmpread_dimensions()` -- you must first (successfully) call `bmpread_load_info()`, otherwise they will all return 0!

```
int       bmpread_width(BMPHANDLE h)
int       bmpread_height(BMPHANDLE h)
int       bmpread_channels(BMPHANDLE h)
int       bmpread_bits_per_channel(BMPHANDLE h)
int       bmpread_topdown(BMPHANDLE h)
```
#### top-down / bottom-up
`bmpread_topdown()` or the topdown value returned by `bmpread_dimensions()` is only relevant if you load the BMP file line-by-line. In line-by-line mode (using `bmpread_load_line()`), the image data is always delivered in the order it is in the BMP file. The topdown value will tell you if it's top-down or bottom-up. On the other hand, when the whole image is loaded at once (using `bmpread_load_image()`), bmplib will *always* return the image top-down, regardless of how the BMP file is oriented. The topdown value will still indicate the orientation of the original BMP.


### Required size for buffer to receive image: bmpread_buffersize():
```
size_t    bmpread_buffersize(BMPHANDLE h)
```
Returns the buffer size you have to allocate for the whole image.


### bmpread_load_image()
```
BMPRESULT bmpread_load_image(BMPHANDLE h, char **pbuffer)
```

Loads the complete image from the BMP file into the buffer pointed to by `pbuffer`.
You can either allocate a buffer yourself or let `pbuffer` point to a NULL-pointer in which case bmplib will allocate an appropriate buffer. In the latter case, you will have to call `free()` on the buffer, once you are done with it.
If you allocate the buffer yourself, the buffer must be at least as large as the size returned by `bmpread_buffersize()`.
```
char *buffer;

/* either: */
buffer = NULL;
bmpread_load_image(h, &buffer);   /* bmplib will allocate the buffer */

/* or: */
buffer = malloc(bmpread_buffersize(h));
bmpread_load_image(h, &buffer);   /* bmplib will use the provided buffer */

```

The image data is written to the buffer according to the returned dimensions (see above). Multi-byte values are always written in host byte order, in the order R-G-B or R-G-B-A. The returned image is always top-down, i.e. data starts in the top left corner. Unlike BMPs which are (almost always) bottom-up.


### Huge files: bmpread_set_insanity_limit()

bmplib will refuse to load images beyond a certain size (default 500MB) and instead return BMP_RESULT_INSANE. If you want to load the image anyway, call `bmpread_set_insanity_limit()` at any time before
calling `bmpread_load_image()`. `limit` is the new allowed size in bytes. (not MB!)
```
void bmpread_set_insanity_limit(BMPHANDLE h, size_t limit)
```



### Undefined and invalid pixels: bmpread_set_undefined_to_alpha()
RLE-encoded BMP files may have undefined pixels, either by using early end-of-line or end-of-file codes, or by using delta codes to skip part of the image. bmplib default is to make such pixels transparent. RLE-encoded BMPs will therefore always be returned with an alpha channel by default, wether the file has such undefined pixels or not (because bmplib doesn't know beforehand if there are any undefined pixels). You can change this behaviour by calling
`bmpread_set_undefined_to_alpha()`, with the second argument `yes` set to 0. In that case, the returned image will have no alpha channel, and undefined pixels will be whatever was in the buffer you passed to `bmpread_load_image()`. This function has no effect on non-RLE BMPs.
```
void      bmpread_set_undefined_to_alpha(BMPHANDLE h, int yes)
```

### Query info about the BMP file
Note: these functions return information about the original BMP file being read. They do *not* describe the format of the returned image data, which may be different!
```
int       bmpread_info_header_version(BMPHANDLE h)
int       bmpread_info_header_size(BMPHANDLE h)
int       bmpread_info_compression(BMPHANDLE h)
int       bmpread_info_bitcount(BMPHANDLE h)
const char* bmpread_info_header_name(BMPHANDLE h)
const char* bmpread_info_compression_name(BMPHANDLE h)
BMPRESULT bmpread_info_channel_bits(BMPHANDLE h, int *r, int *g, int *b, int *a)
```


### bmp_free()
```
void bmp_free(BMPHANDLE h);
```


## Writing BMP files
### bmpwrite_new()
```
BMPHANDLE bmpwrite_new(FILE *file);
```

### bmpwrite_set_dimensions()
```
BMPRESULT bmpwrite_set_dimensions(BMPHANDLE h,
                                  unsigned  width,
                                  unsigned  height,
                                  unsigned  channels,
                                  unsigned  bits_per_channel);
```

### bmpwrite_set_output_bits()
```
BMPRESULT bmpwrite_set_output_bits(BMPHANDLE h, int red, int green, int blue, int alpha);
```

### bmpwrite_save_image()
```
BMPRESULT bmpwrite_save_image(BMPHANDLE h, void *image);
```




### bmp_free()
```
void        bmp_free(BMPHANDLE h);
```

### bmp_errmsg()
```
const char* bmp_errmsg(BMPHANDLE h);
```

### bmp_version()
```
const char* bmp_version(void);
```


## Sample code

### Reading BMPs

```
    /* (all error checking left out for clarity) */

    BMPHANDLE read_handle;
    FILE     *file;
    int       width, height, channels, bitsperchannel, topdown;
    char     *image_data;
    size_t    alloc_size;


    /* open a file and call bmpread_new() to get a BMPHANDLE,
     * which will be used on all subsequent calls.
     */

    file = fopen("someimage.bmp", "rb");
    read_handle = bmpread_new(file);


    /* get image dimensions
     * the color information (channels/bits) describes the data
     * that bmplib will return, NOT necessarily the BMP file.
     */

    bmpread_load_info(read_handle);

    width = bmpread_width(read_handle);
    height = bmpread_height(read_handle);
    channels = bmpread_channels(read_handle);
    bitsperchannel = bmpread_bits_per_channel(read_handle);


    /* get required size for memory buffer and allocate memory */

    alloc_size = bmpread_buffersize(read_handle);
    image_data = malloc(alloc_size);


    /* load the image and clean up: */

    bmpread_load_image(read_handle, image_data);

    bmp_free(read_handle);
    fclose(file);

    /* ready to use the image written to image_data */

    /* image data is always returned in host byte order as
     * 8, 16, or 32 bits per channel RGB or RGBA data.
     * No padding.
     */
```


### Writing BMPs

```
    BMPHANDLE write_handle;
    FILE     *file;
    char     *image_data;
    int       width, height, channel, bitsperchannel;

    /* 'image_data' contains the image to be saved as either
     * 8, 16, or 32 bits per channel RGBA or RGBA data in
     * host byte order without any padding
     */


    /* open a file for writing and get a BMPHANDLE */

    file = fopen("image.bmp", "wb");
    write_handle = bmpwrite_new(file);


    /* inform bmplib of the image dimensions.
     * The color information (channels, bits) refer to the format
     * your image buffer is in, not the format the BMP file should
     * be written in.
     */

    bmpwrite_set_dimensions(write_handle, width, height,
                                          channels, bits_per_channel);



   /* Optional: choose bit-depths (independantly for each channel)
    * for the BMP file. bmplib will choose an appropriate BMP file
    * format to accomodate those bitdepths.
    */

    bmpwrite_set_output_bits(write_handle, 5, 6, 5, 0);


    /* save data to file */

    bmpwrite_save_image(write_handle, image_data);

    bmp_free(write_handle);
    fclose(file);
```

