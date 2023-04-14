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

#ifndef CMD_LINE_VERSION
#define CMD_LINE_VERSION 1
#endif

#include "ntsc.h"

static int
cmpsuf(char *s, char *suf, int nc)
{
    return strcmp(s + strlen(s) - nc, suf);
}

#define DRV_HEADER "NTSC/CRT v%d.%d.%d by EMMIR 2018-2023\n",\
                    CRT_MAJOR, CRT_MINOR, CRT_PATCH

#if CMD_LINE_VERSION

static int dooverwrite = 1;
static int docolor = 1;
static int field = 0;
static int progressive = 0;
static int raw = 0;
static int hue = 0;
static int save_analog = 0;

static int
stoint(char *s, int *err)
{
    char *tail;
    long val;

    errno = 0;
    *err = 0;
    val = strtol(s, &tail, 10);
    if (errno == ERANGE) {
        printf("integer out of integer range\n");
        *err = 1;
    } else if (errno != 0) {
        printf("bad string: %s\n", strerror(errno));
        *err = 1;
    } else if (*tail != '\0') {
        printf("integer contained non-numeric characters\n");
        *err = 1;
    }
    return val;
}

static void
usage(char *p)
{
    printf(DRV_HEADER);
    printf("usage: %s -m|o|f|p|r|h|a outwidth outheight noise artifact_hue infile outfile\n", p);
    printf("sample usage: %s -op 640 480 24 0 in.ppm out.ppm\n", p);
    printf("sample usage: %s - 832 624 0 90 in.ppm out.ppm\n", p);
    printf("-- NOTE: the - after the program name is required\n");
    printf("\tartifact_hue is [0, 359]\n");
    printf("------------------------------------------------------------\n");
    printf("\tm : monochrome\n");
    printf("\to : do not prompt when overwriting files\n");
    printf("\tf : odd field (only meaningful in progressive mode)\n");
    printf("\tp : progressive scan (rather than interlaced)\n");
    printf("\tr : raw image (needed for images that use artifact colors)\n");
    printf("\ta : save analog signal as image instead of decoded image\n");
    printf("\th : print help\n");
    printf("\n");
    printf("by default, the image will be full color, interlaced, and scaled to the output dimensions\n");
}

static int
process_args(int argc, char **argv)
{
    char *flags;

    flags = argv[1];
    if (*flags == '-') {
        flags++;
    }
    for (; *flags != '\0'; flags++) {
        switch (*flags) {
            case 'm': docolor = 0;     break;
            case 'o': dooverwrite = 0; break;
            case 'f': field = 1;       break;
            case 'p': progressive = 1; break;
            case 'r': raw = 1;         break;
            case 'a': save_analog = 1; break;
            case 'h': usage(argv[0]); return 0;
            default:
                fprintf(stderr, "Unrecognized flag '%c'\n", *flags);
                return 0;
        }
    }
    return 1;
}

static int
fileexist(char *n)
{
    FILE *fp = fopen(n, "r");
    if (fp) {
        fclose(fp);
        return 1;
    }
    return 0;
}

static int
promptoverwrite(char *fn)
{
    if (dooverwrite && fileexist(fn)) {
        do {
            char c = 0;
            printf("\n--- file (%s) already exists, overwrite? (y/n)\n", fn);
            scanf(" %c", &c);
            if (c == 'y' || c == 'Y') {
                return 1;
            }
            if (c == 'n' || c == 'N') {
                return 0;
            }
        } while (1);
    }
    return 1;
}

int
main(int argc, char **argv)
{
    struct NTSC_SETTINGS ntsc;
    struct CRT crt;
    int *img;
    int imgw, imgh;
    int *output = NULL;
    int outw = 832;
    int outh = 624;
    int noise = 24;
    char *input_file;
    char *output_file;
    int err = 0;

    if (argc < 8) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!process_args(argc, argv)) {
        return EXIT_FAILURE;
    }

    printf(DRV_HEADER);

    outw = stoint(argv[2], &err);
    if (err) {
        return EXIT_FAILURE;
    }

    outh = stoint(argv[3], &err);
    if (err) {
        return EXIT_FAILURE;
    }

    noise = stoint(argv[4], &err);
    if (err) {
        return EXIT_FAILURE;
    }

    if (noise < 0) noise = 0;

    hue = stoint(argv[5], &err);
    if (err) {
        return EXIT_FAILURE;
    }
    hue %= 360;
    
    output = calloc(outw * outh, sizeof(int));
    if (output == NULL) {
        printf("out of memory\n");
        return EXIT_FAILURE;
    }

    input_file = argv[6];
    output_file = argv[7];

    if (cmpsuf(input_file, ".ppm", 4) == 0) {
        if (!ppm_read24(input_file, &img, &imgw, &imgh, calloc)) {
            printf("unable to read image\n");
            return EXIT_FAILURE;
        }
    } else {
        if (!bmp_read24(input_file, &img, &imgw, &imgh, calloc)) {
            printf("unable to read image\n");
            return EXIT_FAILURE;
        }
    }
    printf("loaded %d %d\n", imgw, imgh);

    if (!promptoverwrite(output_file)) {
        return EXIT_FAILURE;
    }

    crt_init(&crt, outw, outh, CRT_PIX_FORMAT_BGRA, output);

    ntsc.data = img;
    ntsc.format = CRT_PIX_FORMAT_BGRA;
    ntsc.w = imgw;
    ntsc.h = imgh;
    ntsc.as_color = docolor;
    ntsc.field = field & 1;
    ntsc.raw = raw;
    ntsc.hue = hue;
    ntsc.frame = 0;
    
    crt.blend = 1;
    crt.scanlines = 1;

    printf("converting to %dx%d...\n", outw, outh);
    err = 0;
   
    /* accumulate 4 frames */
    while (err < 4) {
        crt_modulate(&crt, &ntsc);
        crt_demodulate(&crt, noise);
        if (!progressive) {
            ntsc.field ^= 1;
            crt_modulate(&crt, &ntsc);
            crt_demodulate(&crt, noise);
            if ((err & 1) == 0) {
                /* a frame is two fields */
                ntsc.frame ^= 1;
            }
        }
        err++;
    }
        
    if (save_analog) {
        int i, norm;
        
        free(output);
        output = calloc(CRT_HRES * CRT_VRES, sizeof(int));
        for (i = 0; i < (CRT_HRES * CRT_VRES); i++) {
            norm = crt.analog[i] + 128;
            output[i] = norm << 16 | norm << 8 | norm;
        }
        outw = CRT_HRES;
        outh = CRT_VRES;
    }
    
    if (cmpsuf(output_file, ".ppm", 4) == 0) {
        if (!ppm_write24(output_file, output, outw, outh)) {
            printf("unable to write image\n");
            return EXIT_FAILURE;
        }
    } else {
        if (!bmp_write24(output_file, output, outw, outh)) {
            printf("unable to write image\n");
            return EXIT_FAILURE;
        }
    }
    printf("done\n");
    return EXIT_SUCCESS;
}
#else
static int *video = NULL;
static VIDINFO *info;

static struct CRT crt;

static int *img;
static int imgw;
static int imgh;

static int color = 1;
static int noise = 12;
static int field = 0;
static int progressive = 0;
static int raw = 0;
static int hue = 0;
static int fadephos = 1; /* fade phosphors each frame */

static void
updatecb(void)
{
    if (pkb_key_pressed(FW_KEY_ESCAPE)) {
        sys_shutdown();
    }

    if (pkb_key_held('q')) {
        crt.black_point += 1;
        printf("crt.black_point   %d\n", crt.black_point);
    }
    if (pkb_key_held('a')) {
        crt.black_point -= 1;
        printf("crt.black_point   %d\n", crt.black_point);
    }

    if (pkb_key_held('w')) {
        crt.white_point += 1;
        printf("crt.white_point   %d\n", crt.white_point);
    }
    if (pkb_key_held('s')) {
        crt.white_point -= 1;
        printf("crt.white_point   %d\n", crt.white_point);
    }

    if (pkb_key_held(FW_KEY_ARROW_UP)) {
        crt.brightness += 1;
        printf("%d\n", crt.brightness);
    }
    if (pkb_key_held(FW_KEY_ARROW_DOWN)) {
        crt.brightness -= 1;
        printf("%d\n", crt.brightness);
    }
    if (pkb_key_held(FW_KEY_ARROW_LEFT)) {
        crt.contrast -= 1;
        printf("%d\n", crt.contrast);
    }
    if (pkb_key_held(FW_KEY_ARROW_RIGHT)) {
        crt.contrast += 1;
        printf("%d\n", crt.contrast);
    }
    if (pkb_key_held('1')) {
        crt.saturation -= 1;
        printf("%d\n", crt.saturation);
    }
    if (pkb_key_held('2')) {
        crt.saturation += 1;
        printf("%d\n", crt.saturation);
    }
    if (pkb_key_held('3')) {
        noise -= 1;
        if (noise < 0) {
            noise = 0;
        }
        printf("%d\n", noise);
    }
    if (pkb_key_held('4')) {
        noise += 1;
        printf("%d\n", noise);
    }
    if (pkb_key_held('5')) {
        hue--;
        if (hue < 0) {
            hue = 359;
        }
        printf("%d\n", hue);
    }
    if (pkb_key_held('6')) {
        hue++;
        if (hue > 359) {
            hue = 0;
        }
        printf("%d\n", hue);
    }
    if (pkb_key_held('7')) {
        crt.hue -= 1;
        printf("%d\n", crt.hue);
    }
    if (pkb_key_held('8')) {
        crt.hue += 1;
        printf("%d\n", crt.hue);
    }
  
    if (pkb_key_pressed(FW_KEY_SPACE)) {
        color ^= 1;
    }
    
    if (pkb_key_pressed('m')) {
        fadephos ^= 1;
        printf("fadephos: %d\n", fadephos);
    }
    if (pkb_key_pressed('r')) {
        crt_reset(&crt);
    }
    if (pkb_key_pressed('g')) {
        crt.scanlines ^= 1;
        printf("crt.scanlines: %d\n", crt.scanlines);
    }
    if (pkb_key_pressed('b')) {
        crt.blend ^= 1;
        printf("crt.blend: %d\n", crt.blend);
    }
    if (pkb_key_pressed('f')) {
        field ^= 1;
        printf("field: %d\n", field);
    }
    if (pkb_key_pressed('e')) {
        progressive ^= 1;
        printf("progressive: %d\n", progressive);
    }
    if (pkb_key_pressed('t')) {
        /* Analog array must be cleared since it normally doesn't get zeroed each frame
         * so active video portions that were written to in non-raw mode will not lose
         * their values resulting in the previous image being
         * displayed where the new, smaller image is not
         */
#if (CRT_SYSTEM == CRT_SYSTEM_NTSC)
        /* clearing the analog buffer with optimized NES mode will cause the
         * image to break since the field does not get repopulated
         */
        memset(crt.analog, 0, sizeof(crt.analog));
#endif
        raw ^= 1;
        printf("raw: %d\n", raw);
    }
}

static void
fade_phosphors(void)
{
    int i, *v;
    unsigned int c;

    v = video;

    for (i = 0; i < info->width * info->height; i++) {
        c = v[i] & 0xffffff;
        v[i] = (c >> 1 & 0x7f7f7f) +
               (c >> 2 & 0x3f3f3f) +
               (c >> 3 & 0x1f1f1f) +
               (c >> 4 & 0x0f0f0f);
    }
}

static void
displaycb(void)
{
    static struct NTSC_SETTINGS ntsc;
  
    if (fadephos) {
        fade_phosphors();
    } else {
        memset(video, 0, info->width * info->height * sizeof(int));
    }
    /* not necessary to clear if you're rendering on a constant region of the display */
    /* memset(crt.analog, 0, sizeof(crt.analog)); */
    ntsc.data = img;
    ntsc.format = CRT_PIX_FORMAT_BGRA;
    ntsc.w = imgw;
    ntsc.h = imgh;
    ntsc.as_color = color;
    ntsc.field = field & 1;
    ntsc.raw = raw;
    ntsc.hue = hue;
    if (ntsc.field == 0) {
        /* a frame is two fields */
        ntsc.frame ^= 1;
    }
    crt_modulate(&crt, &ntsc);
    crt_demodulate(&crt, noise);
    if (!progressive) {
        field ^= 1;
    }
    vid_blit();
    vid_sync();
}

int
main(int argc, char **argv)
{
    int werr;
    char *input_file;

    sys_init();
    sys_updatefunc(updatecb);
    sys_displayfunc(displaycb);
    sys_keybfunc(pkb_keyboard);
    sys_keybupfunc(pkb_keyboardup);

    clk_mode(FW_CLK_MODE_HIRES);
    pkb_reset();
    sys_sethz(60);
    sys_capfps(1);

    werr = vid_open("crt", XMAX, YMAX, 1, FW_VFLAG_VIDFAST);
    if (werr != FW_VERR_OK) {
        FW_error("unable to create window\n");
        return EXIT_FAILURE;
    }

    info = vid_getinfo();
    video = info->video;
    
    printf(DRV_HEADER);

    crt_init(&crt, info->width, info->height, CRT_PIX_FORMAT_BGRA, video);
    crt.blend = 1;
    crt.scanlines = 1;

    if (argc == 1) {
        fprintf(stderr, "Please specify PPM or BMP image input file.\n");
        return EXIT_FAILURE;
    }
    input_file = argv[1];
    
    if (cmpsuf(input_file, ".ppm", 4) == 0) {
        if (!ppm_read24(input_file, &img, &imgw, &imgh, calloc)) {
            fprintf(stderr, "unable to read image\n");
            return EXIT_FAILURE;
        }
    } else {
        if (!bmp_read24(input_file, &img, &imgw, &imgh, calloc)) {
            fprintf(stderr, "unable to read image\n");
            return EXIT_FAILURE;
        }
    }

    printf("loaded %d %d\n", imgw, imgh);

    sys_start();

    sys_shutdown();
    return EXIT_SUCCESS;
}

#endif
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

/* ensure negative values for x get properly modulo'd */
#define POSMOD(x, n)     (((x) % (n) + (n)) % (n))

static int sigpsin15[18] = { /* significant points on sine wave (15-bit) */
    0x0000,
    0x0c88,0x18f8,0x2528,0x30f8,0x3c50,0x4718,0x5130,0x5a80,
    0x62f0,0x6a68,0x70e0,0x7640,0x7a78,0x7d88,0x7f60,0x8000,
    0x7f60
};

static int
sintabil8(int n)
{
    int f, i, a, b;
    
    /* looks scary but if you don't change T14_2PI
     * it won't cause out of bounds memory reads
     */
    f = n >> 0 & 0xff;
    i = n >> 8 & 0xff;
    a = sigpsin15[i];
    b = sigpsin15[i + 1];
    return (a + ((b - a) * f >> 8));
}

/* 14-bit interpolated sine/cosine */
extern void
crt_sincos14(int *s, int *c, int n)
{
    int h;
    
    n &= T14_MASK;
    h = n & ((T14_2PI >> 1) - 1);
    
    if (h > ((T14_2PI >> 2) - 1)) {
        *c = -sintabil8(h - (T14_2PI >> 2));
        *s = sintabil8((T14_2PI >> 1) - h);
    } else {
        *c = sintabil8((T14_2PI >> 2) - h);
        *s = sintabil8(h);
    }
    if (n > ((T14_2PI >> 1) - 1)) {
        *c = -*c;
        *s = -*s;
    }
}

extern int
crt_bpp4fmt(int format)
{
    switch (format) {
        case CRT_PIX_FORMAT_RGB: 
        case CRT_PIX_FORMAT_BGR: 
            return 3;
        case CRT_PIX_FORMAT_ARGB:
        case CRT_PIX_FORMAT_RGBA:
        case CRT_PIX_FORMAT_ABGR:
        case CRT_PIX_FORMAT_BGRA:
            return 4;
        default:
            return 0;
    }
}

/*****************************************************************************/
/********************************* FILTERS ***********************************/
/*****************************************************************************/

/* convolution is much faster but the EQ looks softer, more authentic, and more analog */
#define USE_CONVOLUTION 0
#define USE_7_SAMPLE_KERNEL 1
#define USE_6_SAMPLE_KERNEL 0
#define USE_5_SAMPLE_KERNEL 0

#if (CRT_CC_SAMPLES != 4)
/* the current convolutions do not filter properly at > 4 samples */
#undef USE_CONVOLUTION
#define USE_CONVOLUTION 0
#endif

#if USE_CONVOLUTION

/* NOT 3 band equalizer, faster convolution instead.
 * eq function names preserved to keep code clean
 */
static struct EQF {
    int h[7];
} eqY, eqI, eqQ;

/* params unused to keep the function the same */
static void
init_eq(struct EQF *f,
        int f_lo, int f_hi, int rate,
        int g_lo, int g_mid, int g_hi)
{    
    memset(f, 0, sizeof(struct EQF));
}

static void
reset_eq(struct EQF *f)
{
    memset(f->h, 0, sizeof(f->h));
}

static int
eqf(struct EQF *f, int s)
{
    int i;
    int *h = f->h;

    for (i = 6; i > 0; i--) {
        h[i] = h[i - 1];
    }
    h[0] = s;
#if USE_7_SAMPLE_KERNEL
    /* index : 0 1 2 3 4 5 6 */
    /* weight: 1 4 7 8 7 4 1 */
    return (s + h[6] + ((h[1] + h[5]) * 4) + ((h[2] + h[4]) * 7) + (h[3] * 8)) >> 5;
#elif USE_6_SAMPLE_KERNEL
    /* index : 0 1 2 3 4 5 */
    /* weight: 1 3 4 4 3 1 */
    return (s + h[5] + 3 * (h[1] + h[4]) + 4 * (h[2] + h[3])) >> 4;
#elif USE_5_SAMPLE_KERNEL
    /* index : 0 1 2 3 4 */
    /* weight: 1 2 2 2 1 */
    return (s + h[4] + ((h[1] + h[2] + h[3]) << 1)) >> 3;
#else
    /* index : 0 1 2 3 */
    /* weight: 1 1 1 1*/
    return (s + h[3] + h[1] + h[2]) >> 2;
#endif
}

#else

#define HISTLEN     3
#define HISTOLD     (HISTLEN - 1) /* oldest entry */
#define HISTNEW     0             /* newest entry */

#define EQ_P        16 /* if changed, the gains will need to be adjusted */
#define EQ_R        (1 << (EQ_P - 1)) /* rounding */
/* three band equalizer */
static struct EQF {
    int lf, hf; /* fractions */
    int g[3]; /* gains */
    int fL[4];
    int fH[4];
    int h[HISTLEN]; /* history */
} eqY, eqI, eqQ;

/* f_lo - low cutoff frequency
 * f_hi - high cutoff frequency
 * rate - sampling rate
 * g_lo, g_mid, g_hi - gains
 */
static void
init_eq(struct EQF *f,
        int f_lo, int f_hi, int rate,
        int g_lo, int g_mid, int g_hi)
{
    int sn, cs;
    
    memset(f, 0, sizeof(struct EQF));
        
    f->g[0] = g_lo;
    f->g[1] = g_mid;
    f->g[2] = g_hi;
    
    crt_sincos14(&sn, &cs, T14_PI * f_lo / rate);
    if (EQ_P >= 15) {
        f->lf = 2 * (sn << (EQ_P - 15));
    } else {
        f->lf = 2 * (sn >> (15 - EQ_P));
    }
    crt_sincos14(&sn, &cs, T14_PI * f_hi / rate);
    if (EQ_P >= 15) {
        f->hf = 2 * (sn << (EQ_P - 15));
    } else {
        f->hf = 2 * (sn >> (15 - EQ_P));
    }
}

static void
reset_eq(struct EQF *f)
{
    memset(f->fL, 0, sizeof(f->fL));
    memset(f->fH, 0, sizeof(f->fH));
    memset(f->h, 0, sizeof(f->h));
}

static int
eqf(struct EQF *f, int s)
{    
    int i, r[3];

    f->fL[0] += (f->lf * (s - f->fL[0]) + EQ_R) >> EQ_P;
    f->fH[0] += (f->hf * (s - f->fH[0]) + EQ_R) >> EQ_P;
    
    for (i = 1; i < 4; i++) {
        f->fL[i] += (f->lf * (f->fL[i - 1] - f->fL[i]) + EQ_R) >> EQ_P;
        f->fH[i] += (f->hf * (f->fH[i - 1] - f->fH[i]) + EQ_R) >> EQ_P;
    }
    
    r[0] = f->fL[3];
    r[1] = f->fH[3] - f->fL[3];
    r[2] = f->h[HISTOLD] - f->fH[3];

    for (i = 0; i < 3; i++) {
        r[i] = (r[i] * f->g[i]) >> EQ_P;
    }
  
    for (i = HISTOLD; i > 0; i--) {
        f->h[i] = f->h[i - 1];
    }
    f->h[HISTNEW] = s;
    
    return (r[0] + r[1] + r[2]);
}

#endif

/*****************************************************************************/
/***************************** PUBLIC FUNCTIONS ******************************/
/*****************************************************************************/

extern void
crt_resize(struct CRT *v, int w, int h, int f, unsigned char *out)
{    
    v->outw = w;
    v->outh = h;
    v->out_format = f;
    v->out = out;
}

extern void
crt_reset(struct CRT *v)
{
    v->hue = 0;
    v->saturation = 10;
    v->brightness = 0;
    v->contrast = 180;
    v->black_point = 0;
    v->white_point = 100;
    v->hsync = 0;
    v->vsync = 0;
}

extern void
crt_init(struct CRT *v, int w, int h, int f, unsigned char *out)
{
    memset(v, 0, sizeof(struct CRT));
    crt_resize(v, w, h, f, out);
    crt_reset(v);
    v->rn = 194;
    
    /* kilohertz to line sample conversion */
#define kHz2L(kHz) (CRT_HRES * (kHz * 100) / L_FREQ)
    
    /* band gains are pre-scaled as 16-bit fixed point
     * if you change the EQ_P define, you'll need to update these gains too
     */
#if (CRT_CC_SAMPLES == 4)
    init_eq(&eqY, kHz2L(1500), kHz2L(3000), CRT_HRES, 65536, 8192, 9175);  
    init_eq(&eqI, kHz2L(80),   kHz2L(1150), CRT_HRES, 65536, 65536, 1311);
    init_eq(&eqQ, kHz2L(80),   kHz2L(1000), CRT_HRES, 65536, 65536, 0);
#elif (CRT_CC_SAMPLES == 5)
    init_eq(&eqY, kHz2L(1500), kHz2L(3000), CRT_HRES, 65536, 12192, 7775);
    init_eq(&eqI, kHz2L(80),   kHz2L(1150), CRT_HRES, 65536, 65536, 1311);
    init_eq(&eqQ, kHz2L(80),   kHz2L(1000), CRT_HRES, 65536, 65536, 0);
#else
#error "NTSC-CRT currently only supports 4 or 5 samples per chroma period."
#endif

}

extern void
crt_demodulate(struct CRT *v, int noise)
{
    /* made static so all this data does not go on the stack */
    static struct {
        int y, i, q;
    } out[AV_LEN + 1], *yiqA, *yiqB;
    int i, j, line, rn;
    signed char *sig;
    int s = 0;
    int field, ratio;
    int *ccr; /* color carrier signal */
    int huesn, huecs;
    int xnudge = -3, ynudge = 3;
    int bright = v->brightness - (BLACK_LEVEL + v->black_point);
    int bpp, pitch;
#if CRT_DO_BLOOM
    int prev_e; /* filtered beam energy per scan line */
    int max_e; /* approx maximum energy in a scan line */
#endif
    
    bpp = crt_bpp4fmt(v->out_format);
    if (bpp == 0) {
        return;
    }
    pitch = v->outw * bpp;
    
    crt_sincos14(&huesn, &huecs, ((v->hue % 360) + 33) * 8192 / 180);
    huesn >>= 11; /* make 4-bit */
    huecs >>= 11;

    rn = v->rn;
    for (i = 0; i < CRT_INPUT_SIZE; i++) {
        rn = (214019 * rn + 140327895);

        /* signal + noise */
        s = v->analog[i] + (((((rn >> 16) & 0xff) - 0x7f) * noise) >> 8);
        if (s >  127) { s =  127; }
        if (s < -127) { s = -127; }
        v->inp[i] = s;
    }
    v->rn = rn;

    /* Look for vertical sync.
     * 
     * This is done by integrating the signal and
     * seeing if it exceeds a threshold. The threshold of
     * the vertical sync pulse is much higher because the
     * vsync pulse is a lot longer than the hsync pulse.
     * The signal needs to be integrated to lessen
     * the noise in the signal.
     */
    for (i = -CRT_VSYNC_WINDOW; i < CRT_VSYNC_WINDOW; i++) {
        line = POSMOD(v->vsync + i, CRT_VRES);
        sig = v->inp + line * CRT_HRES;
        s = 0;
        for (j = 0; j < CRT_HRES; j++) {
            s += sig[j];
            /* increase the multiplier to make the vsync
             * more stable when there is a lot of noise
             */
            if (s <= (CRT_VSYNC_THRESH * SYNC_LEVEL)) {
                goto vsync_found;
            }
        }
    }
vsync_found:
#if CRT_DO_VSYNC
    v->vsync = line; /* vsync found (or gave up) at this line */
#else
    v->vsync = -3;
#endif
    /* if vsync signal was in second half of line, odd field */
    field = (j > (CRT_HRES / 2));
#if CRT_DO_BLOOM
    max_e = (128 + (noise / 2)) * AV_LEN;
    prev_e = (16384 / 8);
#endif
    /* ratio of output height to active video lines in the signal */
    ratio = (v->outh << 16) / CRT_LINES;
    ratio = (ratio + 32768) >> 16;
    
    field = (field * (ratio / 2));

    for (line = CRT_TOP; line < CRT_BOT; line++) {
        unsigned pos, ln;
        int scanL, scanR, dx;
        int L, R;
        unsigned char *cL, *cR;
#if (CRT_CC_SAMPLES == 4)
        int wave[CRT_CC_SAMPLES];
#else
        int waveI[CRT_CC_SAMPLES];
        int waveQ[CRT_CC_SAMPLES];
#endif
        int dci, dcq; /* decoded I, Q */
        int xpos, ypos;
        int beg, end;
        int phasealign;
#if CRT_DO_BLOOM
        int line_w;
#endif
  
        beg = (line - CRT_TOP + 0) * (v->outh + v->v_fac) / CRT_LINES + field;
        end = (line - CRT_TOP + 1) * (v->outh + v->v_fac) / CRT_LINES + field;

        if (beg >= v->outh) { continue; }
        if (end > v->outh) { end = v->outh; }

        /* Look for horizontal sync.
         * See comment above regarding vertical sync.
         */
        ln = (POSMOD(line + v->vsync, CRT_VRES)) * CRT_HRES;
        sig = v->inp + ln + v->hsync;
        s = 0;
        for (i = -CRT_HSYNC_WINDOW; i < CRT_HSYNC_WINDOW; i++) {
            s += sig[SYNC_BEG + i];
            if (s <= (CRT_HSYNC_THRESH * SYNC_LEVEL)) {
                break;
            }
        }
#if CRT_DO_HSYNC
        v->hsync = POSMOD(i + v->hsync, CRT_HRES);
#else
        v->hsync = 0;
#endif
        
        xpos = POSMOD(AV_BEG + v->hsync + xnudge, CRT_HRES);
        ypos = POSMOD(line + v->vsync + ynudge, CRT_VRES);
        pos = xpos + ypos * CRT_HRES;
        
        ccr = v->ccf[ypos % CRT_CC_VPER];
#if (CRT_CC_SAMPLES == 4)
        sig = v->inp + ln + (v->hsync & ~3); /* faster */
#else
        sig = v->inp + ln + (v->hsync - (v->hsync % CRT_CC_SAMPLES));
#endif
        for (i = CB_BEG; i < CB_BEG + (CB_CYCLES * CRT_CB_FREQ); i++) {
            int p, n;
            p = ccr[i % CRT_CC_SAMPLES] * 127 / 128; /* fraction of the previous */
            n = sig[i];                 /* mixed with the new sample */
            ccr[i % CRT_CC_SAMPLES] = p + n;
        }

        phasealign = POSMOD(v->hsync, CRT_CC_SAMPLES);
        
#if (CRT_CC_SAMPLES == 4)
        /* amplitude of carrier = saturation, phase difference = hue */
        dci = ccr[(phasealign + 1) & 3] - ccr[(phasealign + 3) & 3];
        dcq = ccr[(phasealign + 2) & 3] - ccr[(phasealign + 0) & 3];

        wave[0] = ((dci * huecs - dcq * huesn) >> 4) * v->saturation;
        wave[1] = ((dcq * huecs + dci * huesn) >> 4) * v->saturation;
        wave[2] = -wave[0];
        wave[3] = -wave[1];
#elif (CRT_CC_SAMPLES == 5)
        {
            int dciA, dciB;
            int dcqA, dcqB;
            int ang = (v->hue % 360);
            int off180 = CRT_CC_SAMPLES / 2;
            int off90 = CRT_CC_SAMPLES / 4;
            int peakA = phasealign + off90;
            int peakB = phasealign + 0;
            dciA = dciB = dcqA = dcqB = 0;
            /* amplitude of carrier = saturation, phase difference = hue */
            dciA = ccr[(peakA) % CRT_CC_SAMPLES];
            /* average */
            dciB = (ccr[(peakA + off180) % CRT_CC_SAMPLES]
                  + ccr[(peakA + off180 + 1) % CRT_CC_SAMPLES]) / 2;
            dcqA = ccr[(peakB + off180) % CRT_CC_SAMPLES];
            dcqB = ccr[(peakB) % CRT_CC_SAMPLES];
            dci = dciA - dciB;
            dcq = dcqA - dcqB;
            /* create wave tables and rotate them by the hue adjustment angle */
            for (i = 0; i < CRT_CC_SAMPLES; i++) {
                int sn, cs;
                crt_sincos14(&sn, &cs, ang * 8192 / 180);
                waveI[i] = ((dci * cs + dcq * sn) >> 15) * v->saturation;
                /* Q is offset by 90 */
                crt_sincos14(&sn, &cs, (ang + 90) * 8192 / 180);
                waveQ[i] = ((dci * cs + dcq * sn) >> 15) * v->saturation;
                ang += (360 / CRT_CC_SAMPLES);
            }
        }
#endif
        sig = v->inp + pos;
#if CRT_DO_BLOOM
        s = 0;
        for (i = 0; i < AV_LEN; i++) {
            s += sig[i]; /* sum up the scan line */
        }
        /* bloom emulation */
        prev_e = (prev_e * 123 / 128) + ((((max_e >> 1) - s) << 10) / max_e);
        line_w = (AV_LEN * 112 / 128) + (prev_e >> 9);

        dx = (line_w << 12) / v->outw;
        scanL = ((AV_LEN / 2) - (line_w >> 1) + 8) << 12;
        scanR = (AV_LEN - 1) << 12;
        
        L = (scanL >> 12);
        R = (scanR >> 12);
#else
        dx = ((AV_LEN - 1) << 12) / v->outw;
        scanL = 0;
        scanR = (AV_LEN - 1) << 12;
        L = 0;
        R = AV_LEN;
#endif
        reset_eq(&eqY);
        reset_eq(&eqI);
        reset_eq(&eqQ);
        
#if (CRT_CC_SAMPLES == 4)
        for (i = L; i < R; i++) {
            out[i].y = eqf(&eqY, sig[i] + bright) << 4;
            out[i].i = eqf(&eqI, sig[i] * wave[(i + 0) & 3] >> 9) >> 3;
            out[i].q = eqf(&eqQ, sig[i] * wave[(i + 3) & 3] >> 9) >> 3;
        }
#else
        for (i = L; i < R; i++) {
            out[i].y = eqf(&eqY, sig[i] + bright) << 4;
            out[i].i = eqf(&eqI, sig[i] * waveI[i % CRT_CC_SAMPLES] >> 9) >> 3;
            out[i].q = eqf(&eqQ, sig[i] * waveQ[i % CRT_CC_SAMPLES] >> 9) >> 3;
        } 
#endif

        cL = v->out + (beg * pitch);
        cR = cL + pitch;

        for (pos = scanL; pos < scanR && cL < cR; pos += dx) {
            int y, i, q;
            int r, g, b;
            int aa, bb;

            R = pos & 0xfff;
            L = 0xfff - R;
            s = pos >> 12;
            
            yiqA = out + s;
            yiqB = out + s + 1;
            
            /* interpolate between samples if needed */
            y = ((yiqA->y * L) >>  2) + ((yiqB->y * R) >>  2);
            i = ((yiqA->i * L) >> 14) + ((yiqB->i * R) >> 14);
            q = ((yiqA->q * L) >> 14) + ((yiqB->q * R) >> 14);
            
            /* YIQ to RGB */
            r = (((y + 3879 * i + 2556 * q) >> 12) * v->contrast) >> 8;
            g = (((y - 1126 * i - 2605 * q) >> 12) * v->contrast) >> 8;
            b = (((y - 4530 * i + 7021 * q) >> 12) * v->contrast) >> 8;
          
            if (r < 0) r = 0;
            if (g < 0) g = 0;
            if (b < 0) b = 0;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;

            if (v->blend) {
                aa = (r << 16 | g << 8 | b);

                switch (v->out_format) {
                    case CRT_PIX_FORMAT_RGB:
                    case CRT_PIX_FORMAT_RGBA:
                        bb = cL[0] << 16 | cL[1] << 8 | cL[2];
                        break;
                    case CRT_PIX_FORMAT_BGR: 
                    case CRT_PIX_FORMAT_BGRA:
                        bb = cL[2] << 16 | cL[1] << 8 | cL[0];
                        break;
                    case CRT_PIX_FORMAT_ARGB:
                        bb = cL[1] << 16 | cL[2] << 8 | cL[3];
                        break;
                    case CRT_PIX_FORMAT_ABGR:
                        bb = cL[3] << 16 | cL[2] << 8 | cL[1];
                        break;
                    default:
                        bb = 0;
                        break;
                }

                /* blend with previous color there */
                bb = (((aa & 0xfefeff) >> 1) + ((bb & 0xfefeff) >> 1));
            } else {
                bb = (r << 16 | g << 8 | b);
            }

            switch (v->out_format) {
                case CRT_PIX_FORMAT_RGB:
                case CRT_PIX_FORMAT_RGBA:
                    cL[0] = bb >> 16 & 0xff;
                    cL[1] = bb >>  8 & 0xff;
                    cL[2] = bb >>  0 & 0xff;
                    break;
                case CRT_PIX_FORMAT_BGR: 
                case CRT_PIX_FORMAT_BGRA:
                    cL[0] = bb >>  0 & 0xff;
                    cL[1] = bb >>  8 & 0xff;
                    cL[2] = bb >> 16 & 0xff;
                    break;
                case CRT_PIX_FORMAT_ARGB:
                    cL[1] = bb >> 16 & 0xff;
                    cL[2] = bb >>  8 & 0xff;
                    cL[3] = bb >>  0 & 0xff;
                    break;
                case CRT_PIX_FORMAT_ABGR:
                    cL[1] = bb >>  0 & 0xff;
                    cL[2] = bb >>  8 & 0xff;
                    cL[3] = bb >> 16 & 0xff;
                    break;
                default:
                    break;
            }

            cL += bpp;
        }
        
        /* duplicate extra lines */
        for (s = beg + 1; s < (end - v->scanlines); s++) {
            memcpy(v->out + s * pitch, v->out + (s - 1) * pitch, pitch);
        }
    }
}
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

#if (CRT_SYSTEM == CRT_SYSTEM_NTSC)

#if (CRT_CHROMA_PATTERN == 1)
/* 227.5 subcarrier cycles per line means every other line has reversed phase */
#define CC_PHASE(ln)     (((ln) & 1) ? -1 : 1)
#else
#define CC_PHASE(ln)     (1)
#endif

#define EXP_P         11
#define EXP_ONE       (1 << EXP_P)
#define EXP_MASK      (EXP_ONE - 1)
#define EXP_PI        6434
#define EXP_MUL(x, y) (((x) * (y)) >> EXP_P)
#define EXP_DIV(x, y) (((x) << EXP_P) / (y))

static int e11[] = {
    EXP_ONE,
    5567,  /* e   */
    15133, /* e^2 */
    41135, /* e^3 */
    111817 /* e^4 */
}; 

/* fixed point e^x */
static int
expx(int n)
{
    int neg, idx, res;
    int nxt, acc, del;
    int i;

    if (n == 0) {
        return EXP_ONE;
    }
    neg = n < 0;
    if (neg) {
        n = -n;
    }
    idx = n >> EXP_P;
    res = EXP_ONE;
    for (i = 0; i < idx / 4; i++) {
        res = EXP_MUL(res, e11[4]);
    }
    idx &= 3;
    if (idx > 0) {
        res = EXP_MUL(res, e11[idx]);
    }
    
    n &= EXP_MASK;
    nxt = EXP_ONE;
    acc = 0;
    del = 1;
    for (i = 1; i < 17; i++) {
        acc += nxt / del;
        nxt = EXP_MUL(nxt, n);
        del *= i;
        if (del > nxt || nxt <= 0 || del <= 0) {
            break;
        }
    }
    res = EXP_MUL(res, acc);

    if (neg) {
        res = EXP_DIV(EXP_ONE, res);
    }
    return res;
}

/*****************************************************************************/
/********************************* FILTERS ***********************************/
/*****************************************************************************/

/* infinite impulse response low pass filter for bandlimiting YIQ */
static struct IIRLP {
    int c;
    int h; /* history */
} iirY, iirI, iirQ;

/* freq  - total bandwidth
 * limit - max frequency
 */
static void
init_iir(struct IIRLP *f, int freq, int limit)
{
    int rate; /* cycles/pixel rate */
    
    memset(f, 0, sizeof(struct IIRLP));
    rate = (freq << 9) / limit;
    f->c = EXP_ONE - expx(-((EXP_PI << 9) / rate));
}

static void
reset_iir(struct IIRLP *f)
{
    f->h = 0;
}

/* hi-pass for debugging */
#define HIPASS 0

static int
iirf(struct IIRLP *f, int s)
{
    f->h += EXP_MUL(s - f->h, f->c);
#if HIPASS
    return s - f->h;
#else
    return f->h;
#endif
}

extern void
crt_modulate(struct CRT *v, struct NTSC_SETTINGS *s)
{
    int x, y, xo, yo;
    int destw = AV_LEN;
    int desth = ((CRT_LINES * 64500) >> 16);
    int iccf[CRT_CC_SAMPLES];
    int ccmodI[CRT_CC_SAMPLES]; /* color phase for mod */
    int ccmodQ[CRT_CC_SAMPLES]; /* color phase for mod */
    int ccburst[CRT_CC_SAMPLES]; /* color phase for burst */
    int sn, cs, n, ph;
    int inv_phase = 0;
    int bpp;

    if (!s->iirs_initialized) {
        init_iir(&iirY, L_FREQ, Y_FREQ);
        init_iir(&iirI, L_FREQ, I_FREQ);
        init_iir(&iirQ, L_FREQ, Q_FREQ);
        s->iirs_initialized = 1;
    }
#if CRT_DO_BLOOM
    if (s->raw) {
        destw = s->w;
        desth = s->h;
        if (destw > ((AV_LEN * 55500) >> 16)) {
            destw = ((AV_LEN * 55500) >> 16);
        }
        if (desth > ((CRT_LINES * 63500) >> 16)) {
            desth = ((CRT_LINES * 63500) >> 16);
        }
    } else {
        destw = (AV_LEN * 55500) >> 16;
        desth = (CRT_LINES * 63500) >> 16;
    }
#else
    if (s->raw) {
        destw = s->w;
        desth = s->h;
        if (destw > AV_LEN) {
            destw = AV_LEN;
        }
        if (desth > ((CRT_LINES * 64500) >> 16)) {
            desth = ((CRT_LINES * 64500) >> 16);
        }
    }
#endif
    if (s->as_color) {
        for (x = 0; x < CRT_CC_SAMPLES; x++) {
            n = s->hue + x * (360 / CRT_CC_SAMPLES);
            crt_sincos14(&sn, &cs, (n + 33) * 8192 / 180);
            ccburst[x] = sn >> 10;
            crt_sincos14(&sn, &cs, n * 8192 / 180);
            ccmodI[x] = sn >> 10;
            crt_sincos14(&sn, &cs, (n - 90) * 8192 / 180);
            ccmodQ[x] = sn >> 10;
        }
    } else {
        memset(ccburst, 0, sizeof(ccburst));
        memset(ccmodI, 0, sizeof(ccmodI));
        memset(ccmodQ, 0, sizeof(ccmodQ));
    }
    
    bpp = crt_bpp4fmt(s->format);
    if (bpp == 0) {
        return; /* just to be safe */
    }
    xo = AV_BEG  + s->xoffset + (AV_LEN    - destw) / 2;
    yo = CRT_TOP + s->yoffset + (CRT_LINES - desth) / 2;
    
    s->field &= 1;
    s->frame &= 1;
    inv_phase = (s->field == s->frame);
    ph = CC_PHASE(inv_phase);

    /* align signal */
    xo = (xo & ~3);
    
    for (n = 0; n < CRT_VRES; n++) {
        int t; /* time */
        signed char *line = &v->analog[n * CRT_HRES];
        
        t = LINE_BEG;

        if (n <= 3 || (n >= 7 && n <= 9)) {
            /* equalizing pulses - small blips of sync, mostly blank */
            while (t < (4   * CRT_HRES / 100)) line[t++] = SYNC_LEVEL;
            while (t < (50  * CRT_HRES / 100)) line[t++] = BLANK_LEVEL;
            while (t < (54  * CRT_HRES / 100)) line[t++] = SYNC_LEVEL;
            while (t < (100 * CRT_HRES / 100)) line[t++] = BLANK_LEVEL;
        } else if (n >= 4 && n <= 6) {
            int even[4] = { 46, 50, 96, 100 };
            int odd[4] =  { 4, 50, 96, 100 };
            int *offs = even;
            if (s->field == 1) {
                offs = odd;
            }
            /* vertical sync pulse - small blips of blank, mostly sync */
            while (t < (offs[0] * CRT_HRES / 100)) line[t++] = SYNC_LEVEL;
            while (t < (offs[1] * CRT_HRES / 100)) line[t++] = BLANK_LEVEL;
            while (t < (offs[2] * CRT_HRES / 100)) line[t++] = SYNC_LEVEL;
            while (t < (offs[3] * CRT_HRES / 100)) line[t++] = BLANK_LEVEL;
        } else {
            int cb;

            /* video line */
            while (t < SYNC_BEG) line[t++] = BLANK_LEVEL; /* FP */
            while (t < BW_BEG)   line[t++] = SYNC_LEVEL;  /* SYNC */
            while (t < AV_BEG)   line[t++] = BLANK_LEVEL; /* BW + CB + BP */
            if (n < CRT_TOP) {
                while (t < CRT_HRES) line[t++] = BLANK_LEVEL;
            }

            /* CB_CYCLES of color burst at 3.579545 Mhz */
            for (t = CB_BEG; t < CB_BEG + (CB_CYCLES * CRT_CB_FREQ); t++) {
#if (CRT_CHROMA_PATTERN == 1)
                int off180 = CRT_CC_SAMPLES / 2;
                cb = ccburst[(t + inv_phase * off180) % CRT_CC_SAMPLES];
#else
                cb = ccburst[t % CRT_CC_SAMPLES];
#endif
                line[t] = (BLANK_LEVEL + (cb * BURST_LEVEL)) >> 5;
                iccf[t % CRT_CC_SAMPLES] = line[t];
            }
        }
    }

    for (y = 0; y < desth; y++) {
        int field_offset;
        int sy;
        
        field_offset = (s->field * s->h + desth) / desth / 2;
        sy = (y * s->h) / desth;
    
        sy += field_offset;

        if (sy >= s->h) sy = s->h;
        
        sy *= s->w;
        
        reset_iir(&iirY);
        reset_iir(&iirI);
        reset_iir(&iirQ);
        
        for (x = 0; x < destw; x++) {
            int fy, fi, fq;
            int rA, gA, bA;
            const unsigned char *pix;
            int ire; /* composite signal */
            int xoff;
            
            pix = s->data + ((((x * s->w) / destw) + sy) * bpp);
            switch (s->format) {
                case CRT_PIX_FORMAT_RGB:
                case CRT_PIX_FORMAT_RGBA:
                    rA = pix[0];
                    gA = pix[1];
                    bA = pix[2];
                    break;
                case CRT_PIX_FORMAT_BGR: 
                case CRT_PIX_FORMAT_BGRA:
                    rA = pix[2];
                    gA = pix[1];
                    bA = pix[0];
                    break;
                case CRT_PIX_FORMAT_ARGB:
                    rA = pix[1];
                    gA = pix[2];
                    bA = pix[3];
                    break;
                case CRT_PIX_FORMAT_ABGR:
                    rA = pix[3];
                    gA = pix[2];
                    bA = pix[1];
                    break;
                default:
                    rA = gA = bA = 0;
                    break;
            }

            /* RGB to YIQ */
            fy = (19595 * rA + 38470 * gA +  7471 * bA) >> 14;
            fi = (39059 * rA - 18022 * gA - 21103 * bA) >> 14;
            fq = (13894 * rA - 34275 * gA + 20382 * bA) >> 14;
            ire = BLACK_LEVEL + v->black_point;
            
            xoff = (x + xo) % CRT_CC_SAMPLES;
            /* bandlimit Y,I,Q */
            fy = iirf(&iirY, fy);
            fi = iirf(&iirI, fi) * ph * ccmodI[xoff] >> 4;
            fq = iirf(&iirQ, fq) * ph * ccmodQ[xoff] >> 4;
            ire += (fy + fi + fq) * (WHITE_LEVEL * v->white_point / 100) >> 10;
            if (ire < 0)   ire = 0;
            if (ire > 110) ire = 110;

            v->analog[(x + xo) + (y + yo) * CRT_HRES] = ire;
        }
    }
    for (n = 0; n < CRT_CC_VPER; n++) {
        for (x = 0; x < CRT_CC_SAMPLES; x++) {
            v->ccf[n][x] = iccf[x] << 7;
        }
    }
}
#endif
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

/*
 * BMP image reader/writer kindly provided by 'deqmega' https://github.com/DEQ2000-cyber
 */

static unsigned char *
loadBMP(char *file, unsigned int *w, unsigned int *h, unsigned char *bpp,
        void *(*calloc_func)(size_t, size_t))
{
    FILE *f;
    unsigned char header[54];
    unsigned char pad[3];
    unsigned char *data = NULL;
    unsigned int width, height, size;
    unsigned char BPP, padding;
    unsigned int X;
    int Y;
    
    f = fopen(file, "rb");
    if (f == NULL) {
        return NULL;
    }
    fread(header, sizeof(unsigned char), 54, f);
    width = *(int*) &header[18];
    height = *(int*) &header[22];
    BPP = *(int*) &header[28];
    size = (width * height * (BPP / 8));
    padding = ((4 - (width * (BPP / 8)) % 4) % 4);
    data = calloc_func(size, (BPP / 8));
    if (data == NULL) {
        return NULL;
    }
    fseek(f, 54, SEEK_SET);
    for (Y = height - 1; Y >= 0; Y--) {
        for (X = 0; X < width; X++) {
            fread(&data[(Y * width + X) * (BPP / 8)], (BPP / 8), 1, f);
        }
        fread(pad, padding, 1, f);
    }
    fclose(f), f = NULL;
    *w = width;
    *h = height;
    *bpp = BPP;
    return data;
}

static void *
loadBMPconverter(char *file, int *w, int *h,
        void *(*calloc_func)(size_t, size_t))
{
    unsigned int *data = NULL;
    unsigned int x, y, i;
    unsigned char n;
    unsigned char *p;
    unsigned int *pix;
    
    p = loadBMP(file, &x, &y, &n, calloc_func);
    if (p == NULL) {
        return NULL;
    }
    *w = x;
    *h = y;
    data = calloc_func(x * y, sizeof(unsigned int));
    if (data == NULL) {
        return NULL;
    }
    pix = data;
    if ((n / 8) == 4) {
        memcpy(pix, p, (x * y * sizeof(unsigned int)));
        free(p);
        return data;
    }
    for (i = 0; i < (x * y * (n / 8)); i += (n / 8)) {
        *(pix++) = (p[i] << 0) | (p[i + 1] << 8) | (p[i + 2] << 16) | (255 << 24);
    }
    free(p);
    return data;
}

static int
saveBMP(char *file, int *data, unsigned int w,
        unsigned int h)
{
    FILE *f;
    unsigned int filesize;
    unsigned char pad[3], header[14], info[40];
    unsigned char padding;
    unsigned int X;
    int Y, bpp = 4;
    
    if (data == NULL) {
        return 0;
    }
    padding = ((4 - (w * bpp) % 4) % 4);
    memset(header, 0, sizeof(header));
    memset(info, 0, sizeof(info));
    filesize = 14 + 40 + w * h * bpp + padding * w;
    header[0] = 'B';
    header[1] = 'M';
    header[2] = filesize;
    header[3] = filesize >> 8;
    header[4] = filesize >> 16;
    header[5] = filesize >> 24;
    header[10] = 14 + 40;
    info[0] = 40;
    info[4]  = (w >>  0) & 0xff;
    info[5]  = (w >>  8) & 0xff;
    info[6]  = (w >> 16) & 0xff;
    info[7]  = (w >> 24) & 0xff;
    info[8]  = (h >>  0) & 0xff;
    info[9]  = (h >>  8) & 0xff;
    info[10] = (h >> 16) & 0xff;
    info[11] = (h >> 24) & 0xff;
    info[12] = 1;
    info[14] = bpp * 8;
    f = fopen(file, "wb");
    if (f == NULL) {
        return 0;
    }
    fwrite(header, 14, 1, f);
    fwrite(info, 40, 1, f);
    for (Y = h - 1; Y >= 0; Y--) {
        for (X = 0; X < w; X++) {
            fwrite(&data[Y * w + X], sizeof(int), 1, f);
        }
        fwrite(pad, padding, 1, f);
    }
    fclose(f);
    return 1;
}

extern int
bmp_read24(char *file, int **out_color, int *out_w, int *out_h,
        void *(*calloc_func)(size_t, size_t))
{
    *out_color = loadBMPconverter(file, out_w, out_h, calloc_func);
    return (*out_color != NULL);
}

extern int
bmp_write24(char *name, int *color, int w, int h)
{
    return saveBMP(name, color, (unsigned int) w, (unsigned int) h);
}
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

extern int
ppm_read24(char *file,
           int **out_color, int *out_w, int *out_h,
           void *(*calloc_func)(size_t, size_t))
 {
    FILE *f;
    long beg;
    int *out;
    int i, npix;
    int r, g, b;
    int header = 0;
    char buf[64];
    int maxc = 0xff;

    f = fopen(file, "rb");
    if (f == NULL) {
        printf("[ppm_rw] unable to open ppm: %s\n", file);
        return 0;
    }
    while (header < 3) {
        if (!fgets(buf, sizeof(buf), f)) {
            printf("[ppm_rw] invalid ppm [no data]: %s\n", file);
            goto err;
        }
        if (buf[0] == '#') {
            continue;
        }
        switch (header) {
            case 0:
                if (buf[0] != 'P' || buf[1] != '6') {
                    printf("[ppm_rw] invalid ppm [not P6]: %s\n", file);
                    goto err;
                }
                break;
            case 1:
                if (sscanf(buf, "%d %d", out_w, out_h) != 2) {
                    printf("[ppm_rw] invalid ppm [no dim]: %s\n", file);
                    goto err;
                }
                break;
            case 2:
                maxc = atoi(buf);
                if (maxc > 0xff) {
                    printf("[ppm_rw] invalid ppm [>255]: %s\n", file);
                    goto err;
                }
                break;
            default:
                break;
        }
        header++;
    }
           
    beg = ftell(f);
    npix = *out_w * *out_h;
    *out_color = calloc_func(npix, sizeof(int));
    if (*out_color == NULL) {
        printf("[ppm_rw] out of memory loading ppm: %s\n", file);
        goto err;
    }
    out = *out_color;
    /*printf("ppm 24-bit w: %d, h: %d, s: %d\n", *out_w, *out_h, npix);*/
    for (i = 0; i < npix; i++) {
#define TO_8_BIT(x) (((x) * 255 + (maxc) / 2) / (maxc))
        r = TO_8_BIT(fgetc(f));
        g = TO_8_BIT(fgetc(f));
        b = TO_8_BIT(fgetc(f));
        if (feof(f)) {
            printf("[ppm_rw] early eof: %s\n", file);
            goto err;
        }
        out[i] = (r << 16 | g << 8 | b);
    }
    fseek(f, beg, SEEK_SET);
    fclose(f);
    return 1;
err:
    fclose(f);
    return 0;
}

extern int
ppm_write24(char *name, int *color, int w, int h)
{
    FILE *f;
    int i, npix, c;

    f = fopen(name, "wb");
    if (f == NULL) {
        printf("[ppm_rw] failed to write file: %s\n", name);
        return 0;
    }

    fprintf(f, "P6\n%d %d\n255\n", w, h);

    npix = w * h;
    for (i = 0; i < npix; i++) {
        c = *color++;
        fputc((c >> 16 & 0xff), f);
        fputc((c >> 8  & 0xff), f);
        fputc((c >> 0  & 0xff), f);
    }
    fclose(f);
    return 1;
}
