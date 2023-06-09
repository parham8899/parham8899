/* +-------------------------------------------------------------------+ */
/* | Copyright 1990, 1991, 1993, David Koblas.  (koblas@netcom.com)    | */
/* |   Permission to use, copy, modify, and distribute this software   | */
/* |   and its documentation for any purpose and without fee is hereby | */
/* |   granted, provided that the above copyright notice appear in all | */
/* |   copies and that both that copyright notice and this permission  | */
/* |   notice appear in supporting documentation.  This software is    | */
/* |   provided "as is" without express or implied warranty.           | */
/* +-------------------------------------------------------------------+ */

/* There is a copy of the GIF89 specification, as defined by its
   inventor, Compuserve, in 1989, at http://members.aol.com/royalef/gif89a.txt

   This covers the high level format, but does not cover how the "data"
   contents of a GIF image represent the raster of color table indices.
   An appendix describes extensions to Lempel-Ziv that GIF makes (variable
   length compression codes and the clear and end codes), but does not
   describe the Lempel-Ziv base.
*/

#define _BSD_SOURCE   /* Make sure strcasecmp() is in string.h */

#include <string.h>
#include <assert.h>

#include "pnm.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"

#define GIFMAXVAL 255
#define MAXCOLORMAPSIZE 256

#define CM_RED 0
#define CM_GRN 1
#define CM_BLU 2

#define MAX_LZW_BITS  12

#define INTERLACE      0x40
#define LOCALCOLORMAP  0x80
#define BitSet(byte, bit)      (((byte) & (bit)) == (bit))

static __inline__ bool
ReadOK(FILE *          const fileP,
       unsigned char * const buffer,
       size_t          const len) {

    size_t bytesRead;

    bytesRead = fread(buffer, len, 1, fileP);

    return (bytesRead != 0);
}


#define LM_to_uint(a,b)                        (((b)<<8)|(a))

static int const maxnum_lzwCode = (1<<MAX_LZW_BITS);

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * input_filespec;  /* Filespecs of input files */
    unsigned int verbose;    /* -verbose option */
    unsigned int comments;   /* -comments option */
    bool all_images;  /* He wants all the images */
    unsigned int image_no;
        /* image number he wants from input, starting at 0.  Undefined
           if all_images is TRUE
        */
    const char * alpha_filename;
    unsigned int quitearly;
};



static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to optParseOptions3 on how to parse our options.
         */
    
    optStruct3 opt;

    unsigned int alphaSpec, imageSpec;
    const char * image;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);
    
    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "verbose",     OPT_FLAG, NULL, 
            &cmdlineP->verbose,         0);
    OPTENT3(0, "comments",    OPT_FLAG, NULL,
            &cmdlineP->comments,        0);
    OPTENT3(0, "quitearly",    OPT_FLAG, NULL,
            &cmdlineP->quitearly,       0);
    OPTENT3(0, "image",       OPT_STRING, &image,
            &imageSpec,                 0);
    OPTENT3(0, "alphaout",    OPT_STRING, &cmdlineP->alpha_filename, 
            &alphaSpec,                 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    optParseOptions3( &argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!imageSpec) {
        cmdlineP->image_no = 0;
        cmdlineP->all_images = FALSE;
    } else {
        if (strcasecmp(image, "all") == 0) 
            cmdlineP->all_images = TRUE;
        else {
            char * tailptr;

            long const imageNo = strtol(image, &tailptr, 10);

            if (*tailptr != '\0')
                pm_error("Invalid value for '-image' option.  Must be either "
                         "a number or 'all'.  You specified '%s'", image);
            else if (imageNo < 0)
                pm_error("Invalid value for '-image' option.  Must be "
                         "positive.  You specified %ld", imageNo);
            else if (imageNo == 0)
                pm_error("Invalid value for 'image' option.  You specified "
                         "zero.  The first image is 1.");

            cmdlineP->all_images = FALSE;
            cmdlineP->image_no = (unsigned int) imageNo - 1;
        }
    }
    
    if (argc-1 == 0) 
        cmdlineP->input_filespec = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->input_filespec = argv[1];

    if (!alphaSpec) 
        cmdlineP->alpha_filename = NULL;
}


typedef unsigned char gifColorMap[3][MAXCOLORMAPSIZE];

struct gifScreen {
    unsigned int    Width;
    unsigned int    Height;
    gifColorMap     ColorMap;
    unsigned int    ColorMapSize;
        /* Number of colors in the color map. */
    unsigned int    ColorResolution;
    unsigned int    Background;
    unsigned int    AspectRatio;
        /* Aspect ratio of each pixel, times 64, minus 15.  (i.e. 1 => 1:4).
           But Zero means 1:1.
        */
    int      hasGray;  
        /* Boolean: global colormap has at least one gray color
           (not counting black and white) 
        */
    int      hasColor;
        /* Boolean: global colormap has at least one non-gray,
           non-black, non-white color 
        */
};

struct gif89 {
       int     transparent;
       int     delayTime;
       int     inputFlag;
       int     disposal;
};

static void
initGif89(struct gif89 * const gif89P) {
    gif89P->transparent = -1;
    gif89P->delayTime = -1;
    gif89P->inputFlag = -1;
    gif89P->disposal = -1;
}       


static int verbose;
int    showComment;



static void
readColorMap(FILE *ifP, const int colormapsize, 
             unsigned char colormap[3][MAXCOLORMAPSIZE],
             int *hasGrayP, int * const hasColorP) {

    int             i;
    unsigned char   rgb[3];

    assert(colormapsize <= MAXCOLORMAPSIZE);

    *hasGrayP = FALSE;  /* initial assumption */
    *hasColorP = FALSE;  /* initial assumption */

    for (i = 0; i < colormapsize; ++i) {
        if (! ReadOK(ifP, rgb, sizeof(rgb)))
            pm_error("Unable to read Color %d from colormap", i);

        colormap[CM_RED][i] = rgb[0] ;
        colormap[CM_GRN][i] = rgb[1] ;
        colormap[CM_BLU][i] = rgb[2] ;

        if (rgb[0] == rgb[1] && rgb[1] == rgb[2]) {
            if (rgb[0] != 0 && rgb[0] != GIFMAXVAL)
                *hasGrayP = TRUE;
        } else
            *hasColorP = TRUE;
    }
}



static bool zeroDataBlock = FALSE;
    /* the most recently read DataBlock was an EOD marker, i.e. had
       zero length */

static void
getDataBlock(FILE *          const ifP, 
             unsigned char * const buf, 
             bool *          const eofP,
             unsigned int *  const lengthP) {
/*----------------------------------------------------------------------------
   Read a DataBlock from file 'ifP', return it at 'buf'.

   The first byte of the datablock is the length, in pure binary, of the
   rest of the datablock.  We return the data portion (not the length byte)
   of the datablock at 'buf', and its length as *lengthP.

   Except that if we hit EOF or have an I/O error reading the first
   byte (size field) of the DataBlock, we return *eofP == TRUE and
   *lengthP == 0.

   We return *eofP == FALSE if we don't hit EOF or have an I/O error.

   If we hit EOF or have an I/O error reading the data portion of the
   DataBlock, we exit the program with pm_error().
-----------------------------------------------------------------------------*/
    unsigned char count;
    bool successfulRead;
    
    long const pos=ftell(ifP);
    successfulRead = ReadOK(ifP, &count, 1);
    if (!successfulRead) {
        pm_message("EOF or error in reading DataBlock size from file" );
        *eofP = TRUE;
        *lengthP = 0;
    } else {
        if (verbose)
            pm_message("%d byte block at Position %ld", count, pos);
        *eofP = FALSE;
        *lengthP = count;

        if (count == 0) 
            zeroDataBlock = TRUE;
        else {
            bool successfulRead;

            zeroDataBlock = FALSE;
            successfulRead = ReadOK(ifP, buf, count); 
            
            if (!successfulRead) 
                pm_error("EOF or error reading data portion of %d byte "
                         "DataBlock from file", count);
        }
    }
}



static void
readThroughEod(FILE * const ifP) {
/*----------------------------------------------------------------------------
  Read the file 'ifP' through the next EOD marker.  An EOD marker is a
  a zero length data block.

  If there is no EOD marker between the present file position and EOF,
  we read to EOF and issue warning message about a missing EOD marker.
-----------------------------------------------------------------------------*/
    unsigned char buf[260];
    bool eod;

    eod = FALSE;  /* initial value */
    while (!eod) {
        bool eof;
        unsigned int count;

        getDataBlock(ifP, buf, &eof, &count);
        if (eof)
            pm_message("EOF encountered before EOD marker.  The GIF "
                       "file is malformed, but we are proceeding "
                       "anyway as if an EOD marker were at the end "
                       "of the file.");
        if (eof || count == 0)
            eod = TRUE;
    }
}



static void
doCommentExtension(FILE * const ifP) {
/*----------------------------------------------------------------------------
   Read the rest of a comment extension from the input file 'ifP' and handle
   it.
   
   We ought to deal with the possibility that the comment is not text.  I.e.
   it could have nonprintable characters or embedded nulls.  I don't know if
   the GIF spec requires regular text or not.
-----------------------------------------------------------------------------*/
    char buf[255+1];
    unsigned int blocklen;  
    bool done;

    done = FALSE;
    while (!done) {
        bool eof;
        getDataBlock(ifP, (unsigned char*) buf, &eof, &blocklen); 
        if (blocklen == 0 || eof)
            done = TRUE;
        else {
            buf[blocklen] = '\0';
            if (showComment) {
                pm_message("gif comment: %s", buf);
            }
        }
    }
}



static void 
doGraphicControlExtension(FILE *         const ifP,
                          struct gif89 * const gif89P) {

    bool eof;
    unsigned int length;
    static unsigned char buf[256];

    getDataBlock(ifP, buf, &eof, &length);
    if (eof)
        pm_error("EOF/error encountered reading "
                 "1st DataBlock of Graphic Control Extension.");
    else if (length < 4) 
        pm_error("graphic control extension 1st DataBlock too short.  "
                 "It must be at least 4 bytes; it is %d bytes.",
                 length);
    else {
        gif89P->disposal = (buf[0] >> 2) & 0x7;
        gif89P->inputFlag = (buf[0] >> 1) & 0x1;
        gif89P->delayTime = LM_to_uint(buf[1],buf[2]);
        if ((buf[0] & 0x1) != 0)
            gif89P->transparent = buf[3];
        readThroughEod(ifP);
    }
}



static void
doExtension(FILE * const ifP, int const label, struct gif89 * const gif89P) {
    const char * str;
    
    switch (label) {
    case 0x01:              /* Plain Text Extension */
        str = "Plain Text";
#ifdef notdef
        GetDataBlock(ifP, (unsigned char*) buf, &eof, &length);
        
        lpos   = LM_to_uint(buf[0], buf[1]);
        tpos   = LM_to_uint(buf[2], buf[3]);
        width  = LM_to_uint(buf[4], buf[5]);
        height = LM_to_uint(buf[6], buf[7]);
        cellw  = buf[8];
        cellh  = buf[9];
        foreground = buf[10];
        background = buf[11];
        
        while (GetDataBlock(ifP, (unsigned char*) buf) != 0) {
            PPM_ASSIGN(xels[ypos][xpos],
                       cmap[CM_RED][v],
                       cmap[CM_GRN][v],
                       cmap[CM_BLU][v]);
            ++index;
        }
#else
        readThroughEod(ifP);
#endif
        break;
    case 0xff:              /* Application Extension */
        str = "Application";
        readThroughEod(ifP);
        break;
    case 0xfe:              /* Comment Extension */
        str = "Comment";
        doCommentExtension(ifP);
        break;
    case 0xf9:              /* Graphic Control Extension */
        str = "Graphic Control";
        doGraphicControlExtension(ifP, gif89P);
        break;
    default: {
        static char buf[256];
        str = buf;
        sprintf(buf, "UNKNOWN (0x%02x)", label);
        pm_message("Ignoring unrecognized extension (type 0x%02x)", label);
        readThroughEod(ifP);
        }
        break;
    }
    if (verbose)
        pm_message(" got a '%s' extension", str );
}



struct getCodeState {
    unsigned char buf[280];
        /* This is the buffer through which we read the data from the 
           stream.  We must buffer it because we have to read whole data
           blocks at a time, but our client wants one code at a time.
           The buffer typically contains the contents of one data block
           plus two bytes from the previous data block.
        */
    int bufCount;
        /* This is the number of bytes of contents in buf[]. */
    int curbit;
        /* The bit number (starting at 0) within buf[] of the next bit
           to be returned.  If the next bit to be returned is not yet in
           buf[] (we've already returned everything in there), this points
           one beyond the end of the buffer contents.
        */
    bool streamExhausted;
        /* The last time we read from the input stream, we got an EOD marker
           or EOF
        */
};



static void
initGetCode(struct getCodeState * const getCodeStateP) {
    
    /* Fake a previous data block */
    getCodeStateP->buf[0] = 0;
    getCodeStateP->buf[1] = 0;
    getCodeStateP->bufCount = 2;
    getCodeStateP->curbit = getCodeStateP->bufCount * 8;
    getCodeStateP->streamExhausted = FALSE;
}



static void
getAnotherBlock(FILE * const ifP, 
                struct getCodeState * const gsP) {

    unsigned int count;
    unsigned int assumed_count;
    bool eof;

    /* Shift buffer down so last two bytes are now the
       first two bytes.  Shift 'curbit' cursor, which must
       be somewhere in or immediately after those two
       bytes, accordingly.
    */
    gsP->buf[0] = gsP->buf[gsP->bufCount-2];
    gsP->buf[1] = gsP->buf[gsP->bufCount-1];

    gsP->curbit -= (gsP->bufCount-2)*8;
    gsP->bufCount = 2;
        
    /* Add the next block to the buffer */
    getDataBlock(ifP, &gsP->buf[gsP->bufCount], &eof, &count);
    if (eof) {
        pm_message("EOF encountered in image "
                   "before EOD marker.  The GIF "
                   "file is malformed, but we are proceeding "
                   "anyway as if an EOD marker were at the end "
                   "of the file.");
        assumed_count = 0;
    } else
        assumed_count = count;

    gsP->streamExhausted = (assumed_count == 0);

    gsP->bufCount += assumed_count;
}



static void
doGetCode(FILE *                const ifP, 
          int                   const codeSize,
          struct getCodeState * const gsP,
          int *                 const retvalP) {

    if ((gsP->curbit+codeSize) > gsP->bufCount*8 && !gsP->streamExhausted) 
        /* Not enough left in buffer to satisfy request.  Get the next
           data block into the buffer.
        */
        getAnotherBlock(ifP, gsP);

    if ((gsP->curbit+codeSize) > gsP->bufCount*8) {
        /* If the buffer still doesn't have enough bits in it, that means
           there were no data blocks left to read.
        */
        *retvalP = -1;  /* EOF */

        {
            int const bitsUnused = gsP->bufCount*8 - gsP->curbit;
            if (bitsUnused > 0)
                pm_message("Stream ends with a partial code "
                           "(%d bits left in file; "
                           "expected a %d bit code).  Ignoring.",
                           bitsUnused, codeSize);
        }
    } else {
        int i, j;
        int code;
        unsigned char * const buf = gsP->buf;

        code = 0;  /* initial value */
        for (i = gsP->curbit, j = 0; j < codeSize; ++i, ++j)
            code |= ((buf[ i / 8 ] & (1 << (i % 8))) != 0) << j;
        gsP->curbit += codeSize;
        *retvalP = code;
    }
}



static int
getCode(FILE * const ifP, 
        int    const codeSize, 
        bool   const init)
{
/*----------------------------------------------------------------------------
   If 'init', initialize the code getter.

   Otherwise, read and return the next lzw code from the file *ifP.

   'codeSize' is the number of bits in the code we are to get.

   Return -1 instead of a code if we encounter the end of the file.
-----------------------------------------------------------------------------*/
    static struct getCodeState getCodeState;

    int retval;

    if (init) {
        initGetCode(&getCodeState);
        retval = 0;
    } else 
        doGetCode(ifP, codeSize, &getCodeState, &retval);

    return retval;
}


struct stack {
    /* Stack grows from low addresses to high addresses */
    int * stack;  /* malloc'ed array */
    int * sp;     /* stack pointer */
    int * top;    /* next word above top of stack */
};



static void 
initStack(struct stack * const stackP, unsigned int const size) {

    MALLOCARRAY(stackP->stack, size);
    if (stackP->stack == NULL)
        pm_error("Unable to allocate %d-word stack.", size);
    stackP->sp = stackP->stack;
    stackP->top = stackP->stack + size;
}



static void
pushStack(struct stack * const stackP, int const value) {

    if (stackP->sp >= stackP->top)
        pm_error("stack overflow");

    *(stackP->sp++) = value;
}



static bool
stackIsEmpty(const struct stack * const stackP) {
    return stackP->sp == stackP->stack;
}



static int
popStack(struct stack * const stackP) {

    if (stackP->sp <= stackP->stack)
        pm_error("stack underflow");
    
    return *(--stackP->sp);
}



static void
termStack(struct stack * const stackP) {
    free(stackP->stack);
    stackP->stack = NULL;
}


/*----------------------------------------------------------------------------
   Some notes on LZW.

   LZW is an extension of Limpel-Ziv.  The two extensions are:

     1) in Limpel-Ziv, codes are all the same number of bits.  In
        LZW, they start out small and increase as the stream progresses.

     2) LZW has a clear code that resets the string table and code
        size.

   The LZW code space is allocated as follows:

   The true data elements are dataWidth bits wide, so the maximum
   value of a true data element is 2**dataWidth-1.  We call that
   max_dataVal.  The first byte in the stream tells you what dataWidth
   is.

   LZW codes 0 - max_dataVal are direct codes.  Each on represents
   the true data element whose value is that of the LZW code itself.
   No decompression is required.

   max_dataVal + 1 and up are compression codes.  They encode
   true data elements:

   max_dataVal + 1 is the clear code.
         
   max_dataVal + 2 is the end code.

   max_dataVal + 3 and up are string codes.  Each string code 
   represents a string of true data elements.  The translation from a
   string code to the string of true data elements varies as the stream
   progresses.  In the beginning and after every clear code, the
   translation table is empty, so no string codes are valid.  As the
   stream progresses, the table gets filled and more string codes 
   become valid.

-----------------------------------------------------------------------------*/


struct decompressor {
    struct stack stack;
    int      fresh;
        /* The stream is right after a clear code or at the very beginning */
    int      codeSize;
        /* The current code size -- each LZW code in this part of the image
           is this many bits.  Ergo, we read this many bits at a time from
           the stream.
        */
    int      maxnum_code;
        /* The maximum number of LZW codes that can be represented with the 
           current code size.  (1 << codeSize)
        */
    int      next_tableSlot;
        /* Index in the code translation table of the next free entry */
    int      firstcode;
        /* This is always a true data element code */
    int      prevcode;
        /* The code just before, in the image, the one we're processing now */
    int      table[2][(1 << MAX_LZW_BITS)];

    /* The following are constant for the life of the decompressor */
    FILE * ifP;
    int init_codeSize;
    int max_dataVal;
    int clear_code;
    int end_code; 
};



static void
resetDecompressor(struct decompressor * const decompP) {

    decompP->codeSize = decompP->init_codeSize+1;
    decompP->maxnum_code = 1 << decompP->codeSize;
    decompP->next_tableSlot = decompP->max_dataVal + 3;
    decompP->fresh = 1;
}



static void
lzwInit(struct decompressor * const decompP, 
        FILE *                const ifP,
        int                   const init_codeSize) {

    if (verbose)
        pm_message("Image says the initial compression code size is "
                   "%d bits", 
                   init_codeSize);
    
    decompP->ifP = ifP;
    decompP->init_codeSize = init_codeSize;

    assert(decompP->init_codeSize < sizeof(decompP->max_dataVal) * 8);

    decompP->max_dataVal = (1 << init_codeSize) - 1;
    decompP->clear_code = decompP->max_dataVal + 1;
    decompP->end_code = decompP->max_dataVal + 2;

    if (verbose)
        pm_message("Initial code size is %u bits; clear code = 0x%x, "
                   "end code = 0x%x",
                   decompP->init_codeSize, 
                   decompP->clear_code, decompP->end_code);
    
    /* The entries in the translation table for true data codes are
       constant throughout the stream.  We set them now and they never
       change.
    */
    {
        unsigned int i;
        for (i = 0; i <= decompP->max_dataVal; ++i) {
            decompP->table[0][i] = 0;
            decompP->table[1][i] = i;
        }
    }
    resetDecompressor(decompP);

    getCode(decompP->ifP, 0, TRUE);
    
    decompP->fresh = TRUE;
    
    initStack(&decompP->stack, maxnum_lzwCode * 2);
}



static void
lzwTerm(struct decompressor * const decompP) {

    termStack(&decompP->stack);
}



static void
expandCodeOntoStack(struct decompressor * const decompP,
                    int                   const incode,
                    bool *                const errorP) {
/*----------------------------------------------------------------------------
   'incode' is an LZW string code.  It represents a string of true data
   elements, as defined by the string translation table in *decompP.

   Expand the code to a string of LZW direct codes and push them onto the
   stack such that the leftmost code is on top.

   Also add to the translation table where appropriate.

   Iff the translation table contains a cycle (which means the LZW stream
   from which it was built is invalid), return *errorP == TRUE.
-----------------------------------------------------------------------------*/
    int code;
    bool error;

    error = FALSE;

    if (incode < decompP->next_tableSlot) 
        code = incode;
    else {
        /* It's a code that isn't in our translation table yet */
        pushStack(&decompP->stack, decompP->firstcode);
        code = decompP->prevcode;
    }

    {
        /* Get the whole string that this compression code
           represents and push it onto the code stack so the
           leftmost code is on top.  Set decompP->firstcode to the
           first (leftmost) code in that string.
            */

        unsigned int stringCount;
        stringCount = 0;

        while (code > decompP->max_dataVal && !error) {
            if (stringCount > maxnum_lzwCode) {
                pm_message("Error in GIF image: contains LZW string loop");
                error = TRUE;
            } else {
                ++stringCount;
                pushStack(&decompP->stack, decompP->table[1][code]);
                code = decompP->table[0][code];
            }
        }
        decompP->firstcode = decompP->table[1][code];
        pushStack(&decompP->stack, decompP->firstcode);
    }

    if (decompP->next_tableSlot < maxnum_lzwCode) {
        decompP->table[0][decompP->next_tableSlot] = decompP->prevcode;
        decompP->table[1][decompP->next_tableSlot] = decompP->firstcode;
        ++decompP->next_tableSlot;
        if (decompP->next_tableSlot >= decompP->maxnum_code) {
            /* We've used up all the codes of the current code size.
               Future codes in the stream will have codes one bit longer.
               But there's an exception if we're already at the LZW
               maximum, in which case the codes will simply continue
               the same size.
            */
            if (decompP->codeSize < MAX_LZW_BITS) {
                ++decompP->codeSize;
                decompP->maxnum_code = 1 << decompP->codeSize;
            }
        }
    }

    decompP->prevcode = incode;
    *errorP = error;
}



static int
lzwReadByte(struct decompressor * const decompP) {
/*----------------------------------------------------------------------------
  Return the next data element of the decompressed image.  In the context
  of a GIF, a data element is the color table index of one pixel.

  We read and return the next byte of the decompressed image, or:

    Return -1 if we hit EOF prematurely (i.e. before an "end" code.  We
    forgive the case that the "end" code is followed by EOF instead of
    an EOD marker (zero length DataBlock)).

    Return -2 if there are no more bytes in the image.  In that case,
    make sure the file is positioned immediately after the image (i.e.
    after the EOD marker that marks the end of the image or EOF).

    Return -3 if we encounter errors in the LZW stream.
-----------------------------------------------------------------------------*/
    int retval;

    if (!stackIsEmpty(&decompP->stack))
        retval = popStack(&decompP->stack);
    else if (decompP->fresh) {
        decompP->fresh = FALSE;
        /* Read off all initial clear codes, read the first non-clear code,
           and return it.  There are no strings in the table yet, so the next
           code must be a direct true data code.
        */
        do {
            decompP->firstcode =
                getCode(decompP->ifP, decompP->codeSize, FALSE);
            decompP->prevcode = decompP->firstcode;
        } while (decompP->firstcode == decompP->clear_code);
        if (decompP->firstcode == decompP->end_code) {
            if (!zeroDataBlock)
                readThroughEod(decompP->ifP);
            retval = -2;
        } else
            retval = decompP->firstcode;
    } else {
        int code;
        code = getCode(decompP->ifP, decompP->codeSize, FALSE);
        if (code == -1)
            retval = -1;
        else {
            assert(code >= 0);  /* -1 is only possible error return */
            if (code == decompP->clear_code) {
                resetDecompressor(decompP);
                retval = lzwReadByte(decompP);
            } else {
                if (code == decompP->end_code) {
                    if (!zeroDataBlock)
                        readThroughEod(decompP->ifP);
                    retval = -2;
                } else {
                    bool error;
                    expandCodeOntoStack(decompP, code, &error);
                    if (error)
                        retval = -3;
                    else
                        retval = popStack(&decompP->stack);
                }
            }
        }
    }
    return retval;
}



enum pass {MULT8PLUS0, MULT8PLUS4, MULT4PLUS2, MULT2PLUS1};

static void
bumpRowInterlace(unsigned int * const rowP,
                 unsigned int   const rows,
                 enum pass *    const passP) {
/*----------------------------------------------------------------------------
   Move *pixelCursorP to the next row in the interlace pattern.
-----------------------------------------------------------------------------*/
    /* There are 4 passes:
       MULT8PLUS0: Rows 8, 16, 24, 32, etc.
       MULT8PLUS4: Rows 4, 12, 20, 28, etc.
       MULT4PLUS2: Rows 2, 6, 10, 14, etc.
       MULT2PLUS1: Rows 1, 3, 5, 7, 9, etc.
    */
    
    switch (*passP) {
    case MULT8PLUS0:
        *rowP += 8;
        break;
    case MULT8PLUS4:
        *rowP += 8;
        break;
    case MULT4PLUS2:
        *rowP += 4;
        break;
    case MULT2PLUS1:
        *rowP += 2;
        break;
    }
    /* Set the proper pass for the next read.  Note that if there are
       more than 4 rows, the sequence of passes is sequential, but
       when there are fewer than 4, we may skip e.g. from MULT8PLUS0
       to MULT4PLUS2.
    */
    while (*rowP >= rows && *passP != MULT2PLUS1) {
        switch (*passP) {
        case MULT8PLUS0:
            *passP = MULT8PLUS4;
            *rowP = 4;
            break;
        case MULT8PLUS4:
            *passP = MULT4PLUS2;
            *rowP = 2;
            break;
        case MULT4PLUS2:
            *passP = MULT2PLUS1;
            *rowP = 1;
            break;
        case MULT2PLUS1:
            /* We've read the entire image */
            break;
        }
    }
}



struct pnmBuffer {
    xel ** xels;
    unsigned int col;
    unsigned int row;
};

static void
addPixelToRaster(unsigned int       const cmapIndex,
                 struct pnmBuffer * const pnmBufferP,
                 unsigned int       const cols,
                 unsigned int       const rows,
                 gifColorMap              cmap, 
                 unsigned int       const cmapSize,
                 bool               const interlace,
                 int                const transparentIndex,
                 bit **             const alphabits,
                 enum pass *        const passP) {

    if (cmapIndex >= cmapSize)
        pm_error("Invalid color index %u in an image that has only "
                 "%u colors in the color map.", cmapIndex, cmapSize);
    
    assert(cmapIndex < MAXCOLORMAPSIZE);
    
    PPM_ASSIGN(pnmBufferP->xels[pnmBufferP->row][pnmBufferP->col], 
               cmap[CM_RED][cmapIndex],
               cmap[CM_GRN][cmapIndex],
               cmap[CM_BLU][cmapIndex]);
    
    if (alphabits) 
        alphabits[pnmBufferP->row][pnmBufferP->col] =
            (cmapIndex == transparentIndex) ? PBM_BLACK : PBM_WHITE;
    
    ++pnmBufferP->col;
    if (pnmBufferP->col == cols) {
        pnmBufferP->col = 0;
        if (interlace)
            bumpRowInterlace(&pnmBufferP->row, rows, passP);
        else
            ++pnmBufferP->row;
    }
}



static void
readImageData(FILE *       const ifP, 
              xel **       const xels, 
              unsigned int const cols,
              unsigned int const rows,
              gifColorMap        cmap, 
              unsigned int const cmapSize,
              bool         const interlace,
              int          const transparentIndex,
              bit **       const alphabits) {

    unsigned char lzwMinCodeSize;      
    enum pass pass;
    struct decompressor decomp;
    struct pnmBuffer pnmBuffer;
    bool gotMinCodeSize;

    pass = MULT8PLUS0;

    pnmBuffer.xels = xels;
    pnmBuffer.col  = 0;
    pnmBuffer.row  = 0;

    gotMinCodeSize =  ReadOK(ifP, &lzwMinCodeSize, 1);
    if (!gotMinCodeSize)
        pm_error("GIF stream ends (or read error) "
                 "right after an image separator; no "
                 "image data follows.");

    if (lzwMinCodeSize > MAX_LZW_BITS)
        pm_error("Invalid minimum code size value in image data: %u.  "
                 "Maximum allowable code size in GIF is %u", 
                 lzwMinCodeSize, MAX_LZW_BITS);

    lzwInit(&decomp, ifP, lzwMinCodeSize);

    while (pnmBuffer.row < rows) {
        int const rc = lzwReadByte(&decomp);

        switch (rc) {
        case -3:
            pm_error("Error in GIF input stream");
            break;
        case -2:
            pm_error("Error in GIF image: Not enough raster data to fill "
                     "%u x %u dimensions.  Ran out of raster data in "
                     "row %u", cols, rows, pnmBuffer.row);
            break;
        case -1:
            pm_error("Premature end of file; no proper GIF closing");
            break;
        default:
            addPixelToRaster(rc, &pnmBuffer, cols, rows, cmap, cmapSize,
                             interlace, transparentIndex, alphabits, &pass);
        }
    }
    if (lzwReadByte(&decomp) >= 0)
        pm_message("Extraneous data at end of image.  "
                   "Skipped to end of image");

    lzwTerm(&decomp);
}



static void
writePnm(FILE *outfile, xel ** const xels, 
         const int cols, const int rows,
         const int hasGray, const int hasColor) {
/*----------------------------------------------------------------------------
   Write a PNM image to the current position of file 'outfile' with
   dimensions 'cols' x 'rows' and raster 'xels'.
   
   Make it PBM, PGM, or PBM according to 'hasGray' and 'hasColor'.
-----------------------------------------------------------------------------*/
    int format;
    const char *format_name;
           
    if (hasColor) {
        format = PPM_FORMAT;
        format_name = "PPM";
    } else if (hasGray) {
        format = PGM_FORMAT;
        format_name = "PGM";
    } else {
        format = PBM_FORMAT;
        format_name = "PBM";
    }
    if (verbose) 
        pm_message("writing a %s file", format_name);
    
    if (outfile) 
        pnm_writepnm(outfile, xels, cols, rows,
                     (xelval) GIFMAXVAL, format, FALSE);
}



static void
transparencyMessage(int const transparentIndex, 
                    gifColorMap cmap) {
/*----------------------------------------------------------------------------
   If user wants verbose output, tell him that the color with index
   'transparentIndex' is supposed to be a transparent background color.
   
   If transparentIndex == -1, tell him there is no transparent background
   color.
-----------------------------------------------------------------------------*/
    if (verbose) {
        if (transparentIndex == -1)
            pm_message("no transparency");
        else
            pm_message("transparent background color: rgb:%02x/%02x/%02x "
                       "Index %d",
                       cmap[CM_RED][transparentIndex],
                       cmap[CM_GRN][transparentIndex],
                       cmap[CM_BLU][transparentIndex],
                       transparentIndex
                );
    }
}

static void
readGifHeader(FILE * const gifFile, struct gifScreen * const gifScreenP) {
/*----------------------------------------------------------------------------
   Read the GIF stream header off the file gifFile, which is present
   positioned to the beginning of a GIF stream.  Return the info from it
   as *gifScreenP.
-----------------------------------------------------------------------------*/
    unsigned char   buf[16];
    char     version[4];


    if (! ReadOK(gifFile,buf,6))
        pm_error("error reading magic number" );
    
    if (strncmp((char *)buf,"GIF",3) != 0)
        pm_error("File does not contain a GIF stream.  It does not start "
                 "with 'GIF'.");
    
    strncpy(version, (char *)buf + 3, 3);
    version[3] = '\0';
    
    if (verbose)
        pm_message("GIF format version is '%s'", version);
    
    if ((!STREQ(version, "87a")) && (!STREQ(version, "89a")))
        pm_error("bad version number, not '87a' or '89a'" );
    
    if (! ReadOK(gifFile,buf,7))
        pm_error("failed to read screen descriptor" );
    
    gifScreenP->Width           = LM_to_uint(buf[0],buf[1]);
    gifScreenP->Height          = LM_to_uint(buf[2],buf[3]);
    gifScreenP->ColorMapSize    = 1 << ((buf[4] & 0x07) + 1);
    gifScreenP->ColorResolution = (buf[4] & 0x70 >> 3) + 1;
    gifScreenP->Background      = buf[5];
    gifScreenP->AspectRatio     = buf[6];

    if (verbose) {
        pm_message("GIF Width = %d GIF Height = %d "
                   "Pixel aspect ratio = %d (%f:1)",
                   gifScreenP->Width, gifScreenP->Height, 
                   gifScreenP->AspectRatio, 
                   gifScreenP->AspectRatio == 0 ? 
                   1 : (gifScreenP->AspectRatio + 15) / 64.0);
        pm_message("Colors = %d   Color Resolution = %d",
                   gifScreenP->ColorMapSize, gifScreenP->ColorResolution);
    }           
    if (BitSet(buf[4], LOCALCOLORMAP)) {    /* Global Colormap */
        readColorMap(gifFile, gifScreenP->ColorMapSize, gifScreenP->ColorMap,
                     &gifScreenP->hasGray, &gifScreenP->hasColor);
        if (verbose) {
            pm_message("Color map %s grays, %s colors", 
                       gifScreenP->hasGray ? "contains" : "doesn't contain",
                       gifScreenP->hasColor ? "contains" : "doesn't contain");
        }
    }
    
    if (gifScreenP->AspectRatio != 0 && gifScreenP->AspectRatio != 49) {
        float   r;
        r = ( (float) gifScreenP->AspectRatio + 15.0 ) / 64.0;
        pm_message("warning - input pixels are not square, "
                   "but we are rendering them as square pixels "
                   "in the output.  "
                   "To fix the output, run it through "
                   "'pnmscale -%cscale %g'",
                   r < 1.0 ? 'x' : 'y',
                   r < 1.0 ? 1.0 / r : r );
    }
}



static void
readExtensions(FILE*          const ifP, 
               struct gif89 * const gif89P,
               bool *         const eodP) {
/*----------------------------------------------------------------------------
   Read extension blocks from the GIF stream to which the file *ifP is
   positioned.  Read up through the image separator that begins the
   next image or GIF stream terminator.

   If we encounter EOD (end of GIF stream) before we find an image 
   separator, we return *eodP == TRUE.  Else *eodP == FALSE.

   If we hit end of file before an EOD marker, we abort the program with
   an error message.
-----------------------------------------------------------------------------*/
    bool imageStart;
    bool eod;

    eod = FALSE;
    imageStart = FALSE;

    /* Read the image descriptor */
    while (!imageStart && !eod) {
        unsigned char c;

        if (! ReadOK(ifP,&c,1))
            pm_error("EOF / read error on image data" );

        if (c == ';') {         /* GIF terminator */
            eod = TRUE;
        } else if (c == '!') {         /* Extension */
            if (! ReadOK(ifP,&c,1))
                pm_error("EOF / "
                         "read error on extension function code");
            doExtension(ifP, c, gif89P);
        } else if (c == ',') 
            imageStart = TRUE;
        else 
            pm_message("bogus character 0x%02x, ignoring", (int) c );
    }
    *eodP = eod;
}



static void
reportImageInfo(unsigned int const cols,
                unsigned int const rows,
                bool         const useGlobalColormap,
                unsigned int const localColorMapSize,
                bool         const interlaced) {


    pm_message("reading %u by %u%s GIF image",
               cols, rows, interlaced ? " interlaced" : "" );
    if (useGlobalColormap)
        pm_message("  Uses global colormap");
    else
        pm_message("  Uses local colormap of %u colors", localColorMapSize);
}



static void
convertImage(FILE *           const ifP, 
             bool             const skipIt, 
             FILE *           const imageout_file, 
             FILE *           const alphafile, 
             struct gifScreen       gifScreen,
             struct gif89     const gif89) {
/*----------------------------------------------------------------------------
   Read a single GIF image from the current position of file 'ifP'.

   If 'skipIt' is TRUE, don't do anything else.  Otherwise, write the
   image to the current position of files 'imageout_file' and 'alphafile'.
   If 'alphafile' is NULL, though, don't write any alpha information.
-----------------------------------------------------------------------------*/
    unsigned char buf[16];
    bool useGlobalColormap;
    xel **xels;  /* The image raster, in libpnm format */
    bit **alphabits;  
        /* The image alpha mask, in libpbm format.  NULL if we aren't computing
           an alpha mask.
        */
    unsigned int cols, rows;  /* Dimensions of the image */
    gifColorMap localColorMap;
    unsigned int localColorMapSize;
    bool interlaced;

    if (! ReadOK(ifP,buf,9))
        pm_error("couldn't read left/top/width/height");

    useGlobalColormap = ! BitSet(buf[8], LOCALCOLORMAP);
    localColorMapSize = 1u << ((buf[8] & 0x07) + 1);
    cols = LM_to_uint(buf[4], buf[5]);
    rows = LM_to_uint(buf[6], buf[7]);
    interlaced = !!BitSet(buf[8], INTERLACE);

    if (verbose)
        reportImageInfo(cols, rows, useGlobalColormap, localColorMapSize,
                        interlaced);
        
    xels = pnm_allocarray(cols, rows);
    if (!xels)
        pm_error("couldn't alloc space for image" );

    if (alphafile) {
        alphabits = pbm_allocarray(cols, rows);
        if (!alphabits)
            pm_error("couldn't alloc space for alpha image" );
    } else
        alphabits = NULL;
    
    if (!useGlobalColormap) {
        int hasGray, hasColor;

        readColorMap(ifP, localColorMapSize, localColorMap, 
                     &hasGray, &hasColor);
        transparencyMessage(gif89.transparent, localColorMap);
        readImageData(ifP, xels, cols, rows, localColorMap, localColorMapSize,
                      interlaced, gif89.transparent, alphabits);
        if (!skipIt) {
            writePnm(imageout_file, xels, cols, rows,
                     hasGray, hasColor);
        }
    } else {
        transparencyMessage(gif89.transparent, gifScreen.ColorMap);
        readImageData(ifP, xels, cols, rows, 
                      gifScreen.ColorMap, gifScreen.ColorMapSize,
                      interlaced, gif89.transparent, alphabits);
        if (!skipIt) {
            writePnm(imageout_file, xels, cols, rows,
                     gifScreen.hasGray, gifScreen.hasColor);
        }
    }

    if (!skipIt && alphafile && alphabits)
        pbm_writepbm(alphafile, alphabits, cols, rows, FALSE);

    pnm_freearray(xels, rows);
    if (alphabits)
        pbm_freearray(alphabits, rows);
}



static void
convertImages(FILE * const ifP, 
              bool   const allImages,
              int    const requestedImageSeq, 
              bool   const drainStream,
              FILE * const imageout_file, 
              FILE * const alphafile) {
/*----------------------------------------------------------------------------
   Read a GIF stream from file 'ifP' and write one or more images from
   it as PNM images to file 'imageout_file'.  If the images have transparency
   and 'alphafile' is non-NULL, write PGM alpha masks to file 'alphafile'.

   'allImages' means Caller wants all the images in the stream.  

   'requestedImageSeq' is meaningful only when 'allImages' is FALSE.  It 
   is the sequence number of the one image Caller wants from the stream,
   with the first image being 0.

   'drainInput' means to read the entire GIF stream, even after
   reading the image Caller asked for.  We read the stream, not just
   the file it's in, so we still recognize certain errors in the GIF
   format in the tail of the stream and there may yet be more stuff in
   the file when we return.
-----------------------------------------------------------------------------*/
    int imageSeq;
        /* Sequence within GIF stream of image we are currently processing.
           First is 0.
        */
    struct gifScreen gifScreen;
    struct gif89 gif89;
    bool eod;
        /* We've read through the GIF terminator character */

    initGif89(&gif89);

    readGifHeader(ifP, &gifScreen);

    for (imageSeq = 0, eod = FALSE;
         !eod && (imageSeq <= requestedImageSeq || allImages || drainStream);
         ++imageSeq) {

        readExtensions(ifP, &gif89, &eod);

        if (eod) {
            /* GIF stream ends before image with sequence imageSeq */
            if (!allImages && (imageSeq <= requestedImageSeq))
                pm_error("You requested Image %d, but "
                         "only %d image%s found in GIF stream",
                         requestedImageSeq+1,
                         imageSeq, imageSeq>1?"s":"" );
        } else {
            if (verbose)
                pm_message("Reading Image Sequence %d", imageSeq);
            convertImage(ifP, !allImages && (imageSeq != requestedImageSeq), 
                         imageout_file, alphafile, gifScreen, gif89);
        }
    }
}



int
main(int argc, char **argv) {

    struct cmdlineInfo cmdline;
    FILE *ifP;
    FILE *alpha_file, *imageout_file;

    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);
    verbose = cmdline.verbose;
    showComment = cmdline.comments;
   
    ifP = pm_openr(cmdline.input_filespec);

    if (cmdline.alpha_filename == NULL)
        alpha_file = NULL;
    else
        alpha_file = pm_openw(cmdline.alpha_filename);

    if (alpha_file && STREQ(cmdline.alpha_filename, "-"))
        imageout_file = NULL;
    else
        imageout_file = stdout;

    convertImages(ifP, cmdline.all_images, cmdline.image_no, 
                  !cmdline.quitearly, imageout_file, alpha_file);

    pm_close(ifP);
    if (imageout_file != NULL) 
        pm_close( imageout_file );
    if (alpha_file != NULL)
        pm_close( alpha_file );

    return 0;
}
