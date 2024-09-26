
# Rupert's bmplib

## Goals for bmplib 1.x:
- Successfully read any half-way sane BMP
- Write any sensible BMP
- Robustness! Don't let malformed BMPs bother us

## Current status (v1.4.3):
### Reading BMP files:
  - 16/24/32 bit RGB(A) with any bits/channel combination (BI_RGB, BI_BITFIELDS, BI_ALPHABITFIELDS).
  - 64 bit RGBA (caveat see below)
  - 1/2/4/8 bit indexed (palette), including RLE4 and RLE8 compressed.
  - RLE24 compressed (OS/2).
  - optional line-by-line reading of BMPs, even RLE.

  successful results from reading sample images from Jason Summers'
  fantastic [BMP Suite](https://entropymine.com/jason/bmpsuite/):
   - all 'good' files
   - most 'questionable' files (see below)
   - some 'bad' files

  Questionable files that failed:
  - embedded JPEG and PNG. Not really a fail. We return BMP_RESULT_JPEG or
    BMP_RESULT_PNG and leave the file pointer in the correct state
    to be passed on to either libpng or libjpeg. Works as designed. Don't
    want to create dependency on those libs.
  - Huffman-encoded OS/2 BMPs: see TODO.
  - We currently ignore icc-profiles and chromaticity/gamma
    values. See TODO.


### Writing BMP files:
  - RGB(A) 16/24/32 bit.
  - any bit-depth combination for the RGBA channels.
  - Indexed 1/2/4/8 bit, no RLE-compression.
  - write BI_RGB when possible, BI_(ALPHA)BITFIELDS only when
    necessary.
  - optional line-by-line writing of BMPs.


## Installation

### Download and compile bmplib library
To install the latest development version of the library under the default `/usr/local` prefix:
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

see API.md for the API documentation


### 64bit BMPs
64bit BMPs are a special breed. First of all, there is very little information about
the format out there, let alone an 'official' spec from MS.
It seems that
the RGBA components are (always?) stored as s2.13 fixed-point
numbers. And according to Jason Summers' BMP Suite the
RGB components are encoded in linear light. As that's the only
sample of a 64-bit BMP I have, that's what I am going with
for now.
But that also means that there is no one obvious format in
which to return the data.
Possible choices are:
1. return the values untouched, which means the caller has to
   be aware of the s2.13 format. (BMP_CONV64_NONE)
2. return the values as normal 16bit values, left in linear
   light (BMP_CONV64_16BIT)
3. return the values as normal 16bit values, converted to sRGB
   gamma. (BMP_CONV64_16BIT_SRGB)

Choice 3 (16bit sRGB gamma) seems to be the most sensible default
(and I made it the default),
as it will work well for all callers which are not aware/don't
care about 64bit BMPs and just want to use/diplay them like any
other BMP. (Even though this goes against my original intent to
not have bmplib do any color conversions.)

Note: the s2.13 format allows for negative values and values
greater than 1! When converting to normal 16bit (BMP_CONV64_16BIT and
BMP_CONV64_16BIT_SRGB), these values will be clipped to 0...1.

In case the default (BMP_CONV64_16BIT_SRGB) doesn't work for you,
bmplib now provides these two functions to check if
a BMP is 64bit and to set the conversion:
- `bmpread_is_64bit()`
- `bmpread_set_64bit_conv()`


## TODOs:
### Definitely:
   - [x] write indexed images.
   - [ ] write RLE-compressed images (RLE4/RLE8 only. No OS/2 v2 BMPs).
   - [x] read RLE24-encoded BMPs.
   - [ ] read Huffman-encoded BMPs.
   - [x] line-by-line reading/writing. ~~Right now, the image can only be
     passed as a whole to/from bmplib.~~
   - [ ] read/write icc-profile and chromaticity/gamma values
   - [x] sanity checks for size of of image / palette. Require confirmation
     above a certain size (~ 500MB?)
   - [x] store undefined pixels (RLE delta and early EOL/EOF) as alpha


### Maybe:
   - [x] passing indexed data and palette to user (optionally) instead of RGB-data.
   - [ ] interpret icc-profile, to enable giving at least sRGB/not-sRGB info.
     (Like sRGB / probably-sRGB / maybe-sRGB). Torn on that one, would
     need dependency on liblcms2.
   - [ ] "BA"-files (bitmap-arrays). Either return the first bitmap only
     (which is the 'official' default) or let user pick one/multiple/all
     to be read in sequence.
   - [ ] Add a 'not-a-BMP-file' return type instead of just returning error.
   - [ ] icon- and pointer-files ("CI", "CP", "IC", "PT").
   - [x] 64-bits BMPs. (I changed my mind)

### Unclear:
   - [ ] platforms: I am writing bmplib on Linux/intel (Ubuntu) using meson.
     Suggestions welcome on what is necessary to build on other
     platforms/cpus. And Windows?


### Non-feature (internal):
   - [x] complete API description (see API.md)
   - [x] bmp-read.c is getting too big, split into several files




## Misc:
- [x] License: probably LPGL3? That's what I'm going with for now.



Cheers,

Rupert

bmplibinfo@gmail.com

(be sure to include 'BMP' anywhere in the subject line, otherwise mail will
go straight into spam folder)
