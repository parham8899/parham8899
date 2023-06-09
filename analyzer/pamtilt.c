/*=============================================================================
                             pgmtilt
===============================================================================
  Print the tilt angle of a PGM file

  Based on pgmskew by Gregg Townsend, August 2005.

  Adapted to Netpbm by Bryan Henderson, August 2005.

  All work has been contributed to the public domain by its authors.
=============================================================================*/

#include <math.h>

#include "pam.h"
#include "mallocvar.h"
#include "shhopt.h"

/* program constants */
#define DIFFMAX 255                     /* maximum scaling for differences */
#define BARLENGTH 60                    /* length of bars printed with -v */

struct cmdlineInfo {
    /* command parameters */
    const char * inputFilename;
    float maxangle;     /* maximum angle attempted */
    float astep;        /* initial angle increment */
    float qmin;         /* minimum quality of solution */
    unsigned int hstep; /* horizontal step size */
    unsigned int vstep; /* vertical step size */
    unsigned int dstep; /* difference distance */
    unsigned int fast;    /* skip third iteration */
    unsigned int verbose; /* generate commentary */
};

static void
abandon(void) {

    printf("00.00\n");
    exit(0);
}



static void
parseCommandLine(int argc, char *argv[],
                 struct cmdlineInfo * const cmdlineP) {

    static optEntry option_def[50];
    static optStruct3 opt;
    unsigned int option_def_index;

    /* set defaults */
    cmdlineP->hstep = 11;        /* read only every 11th column */
    cmdlineP->vstep = 5;         /* calc differences every 5th row */
    cmdlineP->dstep = 2;         /* check for differences two rows down */
    cmdlineP->maxangle = 10.0;   /* assume skew is less than +/- ten degrees */
    cmdlineP->astep = 1.0;       /* initially check by one-degree increments */
    cmdlineP->qmin = 1.0;        /* don't require S/N better than 1.0 */

    /* initialize option table */
    option_def_index = 0;       /* incremented by OPTENT3 */
    OPTENT3(0, "fast",     OPT_FLAG,  NULL, &cmdlineP->fast,     0);
    OPTENT3(0, "verbose",  OPT_FLAG,  NULL, &cmdlineP->verbose,  0);
    OPTENT3(0, "angle",    OPT_FLOAT, &cmdlineP->maxangle, NULL, 0);
    OPTENT3(0, "quality",  OPT_FLOAT, &cmdlineP->qmin,     NULL, 0);
    OPTENT3(0, "astep",    OPT_FLOAT, &cmdlineP->astep,    NULL, 0);
    OPTENT3(0, "hstep",    OPT_UINT,  &cmdlineP->hstep,    NULL, 0);
    OPTENT3(0, "vstep",    OPT_UINT,  &cmdlineP->vstep,    NULL, 0);
    OPTENT3(0, "dstep",    OPT_UINT,  &cmdlineP->dstep,    NULL, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;          /* no short options used */
    opt.allowNegNum = FALSE;            /* don't allow negative values */
    optParseOptions3(&argc, argv, opt, sizeof(opt), 0);

    if (cmdlineP->hstep < 1)
        pm_error("-hstep must be at least 1 column.");
    if (cmdlineP->vstep < 1)
        pm_error("-vstep must be at least 1 row.");
    if (cmdlineP->dstep < 1)
        pm_error("-dstep must be at least 1 row.");
    if (cmdlineP->maxangle < 1 || cmdlineP->maxangle > 45)
        pm_error("-maxangle must be between 1 and 45 degrees.");

    if (argc-1 < 1)                      /* if input file name given */
        cmdlineP->inputFilename = "-";
    else {
        cmdlineP->inputFilename = argv[1];

        if (argc-1 > 1)
            pm_error("There is at most one argument.  You specified %d",
                     argc-1);
    }
}



static void
computeSteps(const struct pam * const pamP,
             unsigned int       const hstepReq,
             unsigned int       const vstepReq,
             unsigned int *     const hstepP,
             unsigned int *     const vstepP) {
/*----------------------------------------------------------------------------
  Adjust parameters if necessary now that we have the image size
-----------------------------------------------------------------------------*/
    if (pamP->width < 10 || pamP->height < 10)
        abandon();

    if (pamP->width < 10 * hstepReq)
        *hstepP = pamP->width / 10;
    else
        *hstepP = hstepReq;

    if (pamP->height < 10 * vstepReq)
        *vstepP = pamP->height / 10;
    else
        *vstepP = vstepReq;
}



static void
load(const struct pam * const pamP,
     unsigned int       const hstep,
     sample ***         const pixelsP,
     unsigned int *     const hsamplesP) {
/*----------------------------------------------------------------------------
  read file into memory, returning array of rows
-----------------------------------------------------------------------------*/
    unsigned int const hsamples = 1 + (pamP->width - 1) / hstep;
        /* use only this many cols */

    unsigned int row;
    tuple *  tuplerow;
    sample ** pixels;

    tuplerow = pnm_allocpamrow(pamP);
    
    MALLOCARRAY(pixels, pamP->height);
    if (pixels == NULL)
        pm_error("Unable to allocate array of %u pixel rows",
                 pamP->height);

    if (pixels == NULL)
        pm_error("Unable to allocate array of %u rows", pamP->height);

    for (row = 0; row < pamP->height; ++row) {
        unsigned int i;
        unsigned int col;

        pnm_readpamrow(pamP, tuplerow);

        MALLOCARRAY(pixels[row], hsamples);
        if (pixels[row] == NULL)
            pm_error("Unable to allocate %u-sample array for Row %u",
                     hsamples, row);

        /* save every hstep'th column */
        for (i = col = 0; i < hsamples; ++i, col += hstep)
            pixels[row][i] = tuplerow[col][0];
    }

    pnm_freepamrow(tuplerow);

    *hsamplesP = hsamples;
    *pixelsP   = pixels;
}



static void
freePixels(sample **    const samples,
           unsigned int const rows) {

    unsigned int row;

    for (row = 0; row < rows; ++row)
        free(samples[row]);

    free(samples);
}



static void
replacePixelValuesWithScaledDiffs(
    const struct pam * const pamP,
    sample **          const pixels,
    unsigned int       const hsamples,
    unsigned int       const dstep) {
/*----------------------------------------------------------------------------
  Replace pixel values with scaled differences used for all
  calculations
-----------------------------------------------------------------------------*/
    float const m = DIFFMAX / (float) pamP->maxval;  /* scale multiplier */

    unsigned int row;

    for (row = dstep; row < pamP->height; ++row) {
        unsigned int col;
        for (col = 0; col < hsamples; ++col) {
            int const d = pixels[row - dstep][col] - pixels[row][col];
            unsigned int const absd = d < 0 ? -d : d;
            pixels[row - dstep][col] = m * absd;  /* scale the difference */
        }
    }
}



static void
scoreAngle(const struct pam * const pamP,
           sample **          const pixels,
           unsigned int       const hstep,
           unsigned int       const vstep,
           unsigned int       const hsamples,
           float              const deg,
           float *            const scoreP) {
/*----------------------------------------------------------------------------
  calculate score for a given angle
-----------------------------------------------------------------------------*/
    float  const radians = (float)deg/360 * 2 * M_PI;
    float  const dy      = hstep * tan(radians);
    int    const dtotal  = pamP->width * tan(radians);
    double const tscale  = 1.0 / hsamples;

    double total;
    int first;
    int last;
    
    unsigned int row;

    total = 0.0; /* initial value */

    if (dtotal > 0) {
        first = 0;
        last = pamP->height - 1 - (dtotal + 1);
    } else {
        first = -(dtotal - 1);
        last = pamP->height - 1;
    }
    for (row = first; row < last; row += vstep) {
        float o;
        long t;
        double dt;

        unsigned int i;

        for (i = 0, t = 0, o = 0.5;
             i < hsamples;
             ++i, t += pixels[(int)(row + o)][i], o += dy) {
        }
        dt = tscale * t;
        total += dt * dt;
    }
    *scoreP = total / (last - first);
}



static void
getBestAngleLocal(
    const struct pam * const pamP,
    sample **          const pixels,
    unsigned int       const hstep,
    unsigned int       const vstep,
    unsigned int       const hsamples,
    float              const minangle,
    float              const maxangle,
    float              const incr,
    bool               const verbose,
    float *            const bestAngleP,
    float *            const qualityP) {
/*----------------------------------------------------------------------------
  find angle of highest score within a range
-----------------------------------------------------------------------------*/
    int const nsamples = ((maxangle - minangle) / incr + 1.5);

    float score;
    float quality;  /* signal/noise ratio of the best angle */
    float angle;
    float bestangle;
    float bestscore;
    float total;
    float others;
    float * results;
    int i;

    MALLOCARRAY_NOFAIL(results, nsamples);      /* allocate array of results */

    /* try all angles within the given range, stepping by incr */
    bestangle = minangle;
    bestscore = 0;
    total = 0;
    for (i = 0; i < nsamples; i++) {
        angle = minangle + i * incr;
        scoreAngle(pamP, pixels, hstep, vstep, hsamples, angle, &score);
        results[i] = score;
        if (score > bestscore ||
            (score == bestscore && fabs(angle) < fabs(bestangle))) {
            bestscore = score;
            bestangle = angle;
        }
        total += score;
    }

    others = (total-bestscore) / (nsamples-1);  /* get mean of other scores */
    quality = bestscore / others;

    if (verbose) {
        fprintf(stderr,
                "\n%2d angles from %6.2f to %5.2f by %4.2f:  "
                "best = %6.2f, S:N = %4.2f\n",
                nsamples, minangle, maxangle, incr, bestangle, quality);
        for (i = 0; i < nsamples; ++i) {
            float const angle = minangle + i * incr;
            int const n = (int) (BARLENGTH * results[i] / bestscore + 0.5);
            fprintf(stderr, "%6.2f: %8.2f %0*d\n", angle, results[i], n, 0);
        }
    }
    free(results);

    *qualityP   = quality;
    *bestAngleP = bestangle;
}



static void
readRelevantPixels(const char *   const inputFilename,
                   unsigned int   const hstepReq,
                   unsigned int   const vstepReq,
                   unsigned int * const hstepP,
                   unsigned int * const vstepP,
                   sample ***     const pixelsP,
                   struct pam *   const pamP,
                   unsigned int * const hsamplesP) {
/*----------------------------------------------------------------------------
  load the image, saving only the pixels we might actually inspect
-----------------------------------------------------------------------------*/
    FILE * ifP;
    unsigned int hstep;
    unsigned int vstep;

    ifP = pm_openr(inputFilename);
    pnm_readpaminit(ifP, pamP, PAM_STRUCT_SIZE(tuple_type));
    computeSteps(pamP, hstepReq, vstepReq, &hstep, &vstep);

    load(pamP, hstep, pixelsP, hsamplesP);

    *hstepP = hstep;
    *vstepP = vstep;
    
    pm_close(ifP);
}



static void
getAngle(const struct pam * const pamP,
         sample **          const pixels,
         unsigned int       const hstep,
         unsigned int       const vstep,
         unsigned int       const hsamples,
         float              const maxangle,
         float              const astep,
         float              const qmin,
         bool               const fast,
         bool               const verbose,
         float *            const angleP) {

    float a;
    float da;
    float lastq;        /* quality (s/n ratio) of last measurement */
    
    getBestAngleLocal(pamP, pixels, hstep, vstep, hsamples,
                      -maxangle, maxangle, astep, verbose,
                      &a, &lastq);

    if ((a < -maxangle + astep / 2) || (a > maxangle - astep / 2))
        /* extreme val almost certainly wrong */
        abandon();
    if (lastq < qmin)
        /* insufficient s/n ratio */
        abandon();

    /* make a finer search in the neighborhood */
    da = astep / 10;
    getBestAngleLocal(pamP, pixels, hstep, vstep, hsamples,
                      a - 9 * da, a + 9 * da, da, verbose,
                      &a, &lastq);

    /* iterate once more unless we don't need that much accuracy */
    if (!fast) {
        da /= 10;
        getBestAngleLocal(pamP, pixels, hstep, vstep, hsamples,
                          a - 9 * da, a + 9 * da, da, verbose,
                          &a, &lastq);
    }
    *angleP = a;
}



int
main(int argc, char *argv[]) {

    struct cmdlineInfo cmdline;
    struct pam pam;
    sample ** pixels;       /* pixel data */
    unsigned int hsamples; /* horizontal samples used */
    unsigned int hstep;    /* horizontal step size */
    unsigned int vstep;    /* vertical step size */
    float angle;

    pgm_init(&argc, argv);              /* initialize netpbm system */

    parseCommandLine(argc, argv, &cmdline);

    readRelevantPixels(cmdline.inputFilename, cmdline.hstep, cmdline.vstep,
                       &hstep, &vstep, &pixels, &pam, &hsamples);

    replacePixelValuesWithScaledDiffs(&pam, pixels, hsamples, cmdline.dstep);

    getAngle(&pam, pixels, hstep, vstep, hsamples,
             cmdline.maxangle, cmdline.astep, cmdline.qmin,
             cmdline.fast, cmdline.verbose, &angle);

    /* report the result on stdout */
    printf("%.2f\n", angle);

    freePixels(pixels, pam.height);

    return 0;
}
