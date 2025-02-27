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

#define CMD_LINE_VERSION 1
#define NTSC_C
#include "ntsc.h"

static int
cmpsuf(char *s, char *suf, int nc)
{
    return strcmp(s + strlen(s) - nc, suf);
}

#define DRV_HEADER "NTSC/CRT v%d.%d.%d by EMMIR 2018-2023\n",\
                    CRT_MAJOR, CRT_MINOR, CRT_PATCH

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

