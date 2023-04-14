
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "ppm_rw.h"
#include "bmp_rw.h"
#include "crt_core.h"

#if !CMD_LINE_VERSION
#include "fw.h"
#define XMAX 832
#define YMAX 624
#endif

#include "crt_core.h"

#include <stdlib.h>
#include <string.h>
