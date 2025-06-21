
# Rupert's bmplib

## Goals for bmplib 1.x:
- Successfully read any half-way sane BMP
- Write any sensible BMP
- Robustness! Don't let malformed BMPs bother us

Download [bmplib on github](https://github.com/rupertwh/bmplib).

## Current status (v1.8.0):
### Reading BMP files:
  - 16/24/32 bit RGB(A) with any bits/channel combination
    (BI_RGB, BI_BITFIELDS, BI_ALPHABITFIELDS).
  - 64 bit RGBA (caveat see below)
  - 1/2/4/8 bit indexed (palette), including RLE4 and RLE8 compressed.
  - RLE24 compressed (OS/2).
  - Huffman encoded (OS/2).
  - OS/2 bitmap arrays.
  - OS/2 icons and pointers.
  - optional line-by-line reading of BMPs.
  - optionally return image data as float or s2.13 fixed point.

  successful results from reading sample images from Jason Summers'
  fantastic [BMP Suite](https://entropymine.com/jason/bmpsuite/):
   - all 'good' files
   - most 'questionable' files (see below)
   - some 'bad' files

  Questionable files that fail:
  - embedded JPEG and PNG. Not really a fail. We return BMP_RESULT_JPEG or
    BMP_RESULT_PNG and leave the file pointer in the correct state to be
    passed on to either libpng or libjpeg. Works as designed. Don't want to
    create dependency on those libs.
  - We currently ignore chromaticity/gamma values from V4+ headers. See TODO.


### Writing BMP files:
  - RGB(A) 16/24/32 bit.
  - 64-bit RGBA
  - any bit-depth combination for the RGBA channels.
  - Indexed 1/2/4/8 bit, optional RLE4, RLE8, and 1-D Huffman compression.
  - RLE24 compression.
  - write BI_RGB when possible, BI_BITFIELDS only when
    necessary.
  - optional line-by-line writing of BMPs.
  - optionally supply image data as float or s2.13 fixed point.


## Installation

### Download and compile bmplib library

To install the latest development version of the library under the default
`/usr/local` prefix on debian-like Linux:

```
sudo apt install build-essential git meson pkg-config
git clone https://github.com/rupertwh/bmplib.git
cd bmplib/
meson setup --buildtype=release mybuilddir
cd mybuilddir/
ninja
ninja install
```
Receive updates:
```
cd bmplib/
git pull --rebase
cd mybuilddir/
ninja
ninja install
```


### Use bmplib in your program

A minimalistic `meson.build` for a program that uses bmplib:

```
project('mytest', 'c')
bmpdep = dependency('libbmp')
executable('mytest', 'main.c', dependencies: [bmpdep])
```

Includes:

```
#include <bmplib.h>
```

see API-quick-start.md and API-full.md for the API documentation


### 64bit BMPs

64bit BMPs are a special breed. First of all, there is very little information
about the format out there, let alone an 'official' spec from MS. It seems
that the RGBA components are stored as s2.13 fixed-point numbers. And
according to Jason Summers' BMP Suite the RGB components are encoded in
linear light. As that's the only sample of a 64-bit BMP I have, that's what I
am going with for now. But that also means that there is no one obvious
format in which to return the data.

Possible choices are:
1. return the values untouched, which means the caller has to
   be aware of the s2.13 format and linear gamma. (BMP_CONV64_NONE)
2. return the values converted to 16-bit integers (or other selected
   number format), left in linear light (BMP_CONV64_LINEAR)
3. return the values converted to 16-bit integers (or other selected
   numer format), converted to sRGB gamma. (BMP_CONV64_SRGB)

Choice 3 (16bit sRGB gamma) seems to be the most sensible default (and I made
it the default), as it will work well for all callers which are not
aware/don't care about 64bit BMPs and just want to use/diplay them like any
other BMP. (Even though this goes against my original intent to not have
bmplib do any color conversions.)

Note: the s2.13 format allows for negative values and values greater than 1!
When converting to 16bit integers, these values will be clipped to 0...1. In
order to preserve the full possible range of 64bit BMP pixel values, the
number format should be set to either BMP_FORMAT_FLOAT or BMP_FORMAT_S2_13.

bmplib provides these functions to check if a BMP is 64bit and to set the
conversion:
- `bmpread_is_64bit()`
- `bmpread_set_64bit_conv()`
- `bmp_set_number_format()`

As to writing BMPs, by default bmplib will not write 64bit BMPs, as they are
so exotic that only few applications will read them (other than native
Microsoft tools, the new GIMP 3.0 is the only one I am aware of). Use
`bmpwrite_set_64bit()` in order to write 64bit BMPs.


## TODOs:
### Definitely:

   - [ ] read/write chromaticity/gamma values


### Maybe:

   - [ ] interpret icc-profile, to enable giving at least sRGB/not-sRGB info.
     (Like sRGB / probably-sRGB / maybe-sRGB). Torn on that one, would need
     dependency on liblcms2.

### Unclear:

   - [ ] platforms: I am writing bmplib on Linux/intel (Ubuntu) using meson.
     Suggestions welcome on what is necessary to build on other
     platforms/cpus. And Windows?


Cheers,

Rupert

bmplibinfo@gmail.com

(be sure to include 'BMP' anywhere in the subject line, otherwise mail will go
straight into spam folder)
