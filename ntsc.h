
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/*****************************************************************************/
/*
 * NTSC/CRT - integer-only NTSC video signal encoding / decoding emulation
 *
 *   by EMMIR 2018-2023
 *
 *   YouTube: https://www.youtube.com/@EMMIR_KC/videos
 *   Discord: https://discord.com/invite/hdYctSmyQJ
 */
/*****************************************************************************/
#ifndef _CRT_CORE_H_
#define _CRT_CORE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* crt_core.h
 *
 * The demodulator. This is also where you can define which system to emulate.
 * 
 */
    
/* library version */
#define CRT_MAJOR 2
#define CRT_MINOR 2
#define CRT_PATCH 0

    
#define CRT_SYSTEM_NTSC 0 /* standard NTSC */

/* the system to be compiled */
#define CRT_SYSTEM CRT_SYSTEM_NTSC

/* 0 = vertical  chroma (228 chroma clocks per line) */
/* 1 = checkered chroma (227.5 chroma clocks per line) */
#define CRT_CHROMA_PATTERN 1

/* chroma clocks (subcarrier cycles) per line */
#if (CRT_CHROMA_PATTERN == 1)
#define CRT_CC_LINE 2275
#else
/* this will give the 'rainbow' effect in the famous waterfall scene */
#define CRT_CC_LINE 2280
#endif

/* NOTE, in general, increasing CRT_CB_FREQ reduces blur and bleed */
#define CRT_CB_FREQ     4 /* carrier frequency relative to sample rate */
#define CRT_HRES        (CRT_CC_LINE * CRT_CB_FREQ / 10) /* horizontal res */
#define CRT_VRES        262                       /* vertical resolution */
#define CRT_INPUT_SIZE  (CRT_HRES * CRT_VRES)

#define CRT_TOP         21     /* first line with active video */
#define CRT_BOT         261    /* final line with active video */
#define CRT_LINES       (CRT_BOT - CRT_TOP) /* number of active video lines */
    
#define CRT_CC_SAMPLES  4 /* samples per chroma period (samples per 360 deg) */
#define CRT_CC_VPER     1 /* vertical period in which the artifacts repeat */

/* search windows, in samples */
#define CRT_HSYNC_WINDOW 8
#define CRT_VSYNC_WINDOW 8

/* accumulated signal threshold required for sync detection.
 * Larger = more stable, until it's so large that it is never reached in which
 *          case the CRT won't be able to sync
 */
#define CRT_HSYNC_THRESH 4
#define CRT_VSYNC_THRESH 94

/*
 *                      FULL HORIZONTAL LINE SIGNAL (~63500 ns)
 * |---------------------------------------------------------------------------|
 *   HBLANK (~10900 ns)                 ACTIVE VIDEO (~52600 ns)
 * |-------------------||------------------------------------------------------|
 *   
 *   
 *   WITHIN HBLANK PERIOD:
 *   
 *   FP (~1500 ns)  SYNC (~4700 ns)  BW (~600 ns)  CB (~2500 ns)  BP (~1600 ns)
 * |--------------||---------------||------------||-------------||-------------|
 *      BLANK            SYNC           BLANK          BLANK          BLANK
 * 
 */
#define LINE_BEG         0
#define FP_ns            1500      /* front porch */
#define SYNC_ns          4700      /* sync tip */
#define BW_ns            600       /* breezeway */
#define CB_ns            2500      /* color burst */
#define BP_ns            1600      /* back porch */
#define AV_ns            52600     /* active video */
#define HB_ns            (FP_ns + SYNC_ns + BW_ns + CB_ns + BP_ns) /* h blank */
/* line duration should be ~63500 ns */
#define LINE_ns          (FP_ns + SYNC_ns + BW_ns + CB_ns + BP_ns + AV_ns)

/* convert nanosecond offset to its corresponding point on the sampled line */
#define ns2pos(ns)       ((ns) * CRT_HRES / LINE_ns)
/* starting points for all the different pulses */
#define FP_BEG           ns2pos(0)
#define SYNC_BEG         ns2pos(FP_ns)
#define BW_BEG           ns2pos(FP_ns + SYNC_ns)
#define CB_BEG           ns2pos(FP_ns + SYNC_ns + BW_ns)
#define BP_BEG           ns2pos(FP_ns + SYNC_ns + BW_ns + CB_ns)
#define AV_BEG           ns2pos(HB_ns)
#define AV_LEN           ns2pos(AV_ns)

/* somewhere between 7 and 12 cycles */
#define CB_CYCLES   10

/* frequencies for bandlimiting */
#define L_FREQ           1431818 /* full line */
#define Y_FREQ           420000  /* Luma   (Y) 4.2  MHz of the 14.31818 MHz */
#define I_FREQ           150000  /* Chroma (I) 1.5  MHz of the 14.31818 MHz */
#define Q_FREQ           55000   /* Chroma (Q) 0.55 MHz of the 14.31818 MHz */

/* IRE units (100 = 1.0V, -40 = 0.0V) */
#define WHITE_LEVEL      100
#define BURST_LEVEL      20
#define BLACK_LEVEL      7
#define BLANK_LEVEL      0
#define SYNC_LEVEL      -40

struct NTSC_SETTINGS {
    const unsigned char *data; /* image data */
    int format;     /* pix format (one of the CRT_PIX_FORMATs in crt_core.h) */
    int w, h;       /* width and height of image */
    int raw;        /* 0 = scale image to fit monitor, 1 = don't scale */
    int as_color;   /* 0 = monochrome, 1 = full color */
    int field;      /* 0 = even, 1 = odd */
    int frame;      /* 0 = even, 1 = odd */
    int hue;        /* 0-359 */
    int xoffset;    /* x offset in sample space. 0 is minimum value */
    int yoffset;    /* y offset in # of lines. 0 is minimum value */
    /* make sure your NTSC_SETTINGS struct is zeroed out before you do anything */
    int iirs_initialized; /* internal state */
};



/* NOTE: this library does not use the alpha channel at all */
#define CRT_PIX_FORMAT_RGB  0  /* 3 bytes per pixel [R,G,B,R,G,B,R,G,B...] */
#define CRT_PIX_FORMAT_BGR  1  /* 3 bytes per pixel [B,G,R,B,G,R,B,G,R...] */
#define CRT_PIX_FORMAT_ARGB 2  /* 4 bytes per pixel [A,R,G,B,A,R,G,B...]   */
#define CRT_PIX_FORMAT_RGBA 3  /* 4 bytes per pixel [R,G,B,A,R,G,B,A...]   */
#define CRT_PIX_FORMAT_ABGR 4  /* 4 bytes per pixel [A,B,G,R,A,B,G,R...]   */
#define CRT_PIX_FORMAT_BGRA 5  /* 4 bytes per pixel [B,G,R,A,B,G,R,A...]   */

/* do bloom emulation (side effect: makes screen have black borders) */
#define CRT_DO_BLOOM    0  /* does not work for NES */
#define CRT_DO_VSYNC    1  /* look for VSYNC */
#define CRT_DO_HSYNC    1  /* look for HSYNC */

struct CRT {
    signed char analog[CRT_INPUT_SIZE];
    signed char inp[CRT_INPUT_SIZE]; /* CRT input, can be noisy */

    int outw, outh; /* output width/height */
    int out_format; /* output pixel format (one of the CRT_PIX_FORMATs) */
    unsigned char *out; /* output image */

    int hue, brightness, contrast, saturation; /* common monitor settings */
    int black_point, white_point; /* user-adjustable */
    int scanlines; /* leave gaps between lines if necessary */
    int blend; /* blend new field onto previous image */
    unsigned v_fac; /* factor to stretch img vertically onto the output img */

    /* internal data */
    int ccf[CRT_CC_VPER][CRT_CC_SAMPLES]; /* faster color carrier convergence */
    int hsync, vsync; /* keep track of sync over frames */
    int rn; /* seed for the 'random' noise */
};

/* Initializes the library. Sets up filters.
 *   w   - width of the output image
 *   h   - height of the output image
 *   f   - format of the output image
 *   out - pointer to output image data
 */
extern void crt_init(struct CRT *v, int w, int h, int f, unsigned char *out);

/* Updates the output image parameters
 *   w   - width of the output image
 *   h   - height of the output image
 *   f   - format of the output image
 *   out - pointer to output image data
 */
extern void crt_resize(struct CRT *v, int w, int h, int f, unsigned char *out);

/* Resets the CRT settings back to their defaults */
extern void crt_reset(struct CRT *v);

/* Modulates RGB image into an analog NTSC signal
 *   s - struct containing settings to apply to this field
 */
extern void crt_modulate(struct CRT *v, struct NTSC_SETTINGS *s);
    
/* Demodulates the NTSC signal generated by crt_modulate()
 *   noise - the amount of noise added to the signal (0 - inf)
 */
extern void crt_demodulate(struct CRT *v, int noise);

/* Get the bytes per pixel for a certain CRT_PIX_FORMAT_
 * 
 *   format - the format to get the bytes per pixel for
 *   
 * returns 0 if the specified format does not exist
 */
extern int crt_bpp4fmt(int format);

/*****************************************************************************/
/*************************** FIXED POINT SIN/COS *****************************/
/*****************************************************************************/

#define T14_2PI           16384
#define T14_MASK          (T14_2PI - 1)
#define T14_PI            (T14_2PI / 2)

extern void crt_sincos14(int *s, int *c, int n);

#ifdef __cplusplus
}
#endif

#endif

#if !CMD_LINE_VERSION
#include "fw.h"
#define XMAX 832
#define YMAX 624
#else
#include "ppm_rw.h"
#include "bmp_rw.h"
#endif

