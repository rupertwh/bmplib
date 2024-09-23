
# Rupert's bmplib

## Goals for bmplib 1.x:
- Successfully read any half-way sane BMP
- Write any sensible BMP
- Robustness! Don't let malformed BMPs bother us

## Current status (v1.3.1):
### Reading BMP files:
  - 16/24/32 bit RGB(A) with any bits/channel combination (BI_RGB, BI_BITFIELDS, BI_ALPHABITFIELDS)
  - 1/2/4/8 bit indexed (palette), including RLE4 and RLE8 compressed
  - RLE24 compressed (OS/2)
  - optional line-by-line reading of BMPs, even RLE

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
  - 64bit: No plans to implement until officially established.
  - We currently ignore icc-profiles and chromaticity/gamma
    values. See TODO.


### Writing BMP files:
  - RGB(A) 16/24/32, no indexed files.
  - any bit-depth combination for the RGBA channels.
  - write BI_RGB when possible, BI_(ALPHA)BITFIELDS only when
    necessary.


## Installation

### Download and compile bmplib library
To install the library under the default `/usr/local` prefix:
```
sudo apt install build-essential git meson pkg-config
git clone https://github.com/rupertwh/bmplib.git
cd bmplib/
meson setup _build
cd _build/
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

see api.md for the API documentation

## TODOs:
### Definitely:
   - [ ] write indexed RGBs. (RLE4/RLE8 only. No OS/2 v2 BMPs)
   - [x] read RLE24-encoded BMPs.
   - [ ] read Huffman-encoded BMPs.
   - [x] line-by-line reading/writing. ~~Right now, the image can only be
     passed as a whole to/from bmplib.~~
   - [ ] read/write icc-profile and chromaticity/gamma values
   - [x] sanity checks for size of of image / palette. Require confirmation
     above a certain size (~ 500MB?)
   - [x] store undefined pixels (RLE delta and early EOL/EOF) as alpha


### Maybe:
   - [ ] passing indexed data and palette to user (optionally) instead of
     RGB-data.
   - [ ] interpret icc-profile, to enable giving at least sRGB/not-sRGB info.
     (Like sRGB / probably-sRGB / maybe-sRGB). Torn on that one, would
     need dependency on liblcms2.
   - [ ] "BA"-files (bitmap-arrays). Either return the first bitmap only
     (which is the 'official' default) or let user pick one/multiple/all
     to be read in sequence.
   - [ ] Add a 'not-a-BMP-file' return type instead of just returning error.
   - [ ] icon- and pointer-files ("CI", "CP", "IC", "PT").
   - [ ] 64-bits BMPs.

### Unclear:
   - [ ] platforms: I am writing bmplib on Linux/intel (Ubuntu) using meson.
     Suggestions welcome on what is necessary to build on other
     platforms/cpus. And Windows?


### Non-feature (internal):
   - [x] complete API description (api.md)
   - [x] bmp-read.c is getting too big, split into several files




## Misc:
- [x] License: probably LPGL3? That's what I'm going with for now.



Cheers,

Rupert

bmplibinfo@gmail.com

(be sure to include 'BMP' anywhere in the subject line, otherwise mail will
go straight into spam folder)
