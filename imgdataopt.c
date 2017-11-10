/*
 * imgdataopt: raster (bitmap) image data size optimizer
 * by pts@fazekas.hu at Mon Nov  6 18:50:40 CET 2017
 *
 * Compilation:
 * * xstatic gcc -ansi -pedantic -s -O2 -W -Wall -Wextra -Werror -o imgdataopt imgdataopt.c -lz
 * Also works with g++ -ansi -pedantic ...
 * Also works with gcc-4.4 -ansi -pedantic ...
 * Also works with g++-4.4 -ansi -pedantic ...
 * Also works with gcc-4.6 -ansi -pedantic ...
 * Also works with g++-4.6 -ansi -pedantic ...
 * Also works with gcc-4.8 -ansi -pedantic ...
 * Also works with g++-4.8 -ansi -pedantic ...
 * Also works with clang-3.4 -ansi -pedantic ...
 * Also works with tcc 0.9.25 (and pts-tcc 0.9.25): tcc -c -O2 -W -Wall -Wextra -Werror imgdataopt.c && gcc -m32 -o imgdataopt imgdataopt.o -lz
 */

/* !! compile the final version with g++ */
/* !! check: ignore: Extra compressed data https://github.com/pts/pdfsizeopt/issues/51 */
/* !! check: nonvalidating PNG parser: properly ignore checksums */

#ifdef __TINYC__   /* tcc: https://bellard.org/tcc/ */
#define USE_GCC_ALTERNATE_KEYWORDS 1
#else
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>  /* crc32(), adler32(), deflateInit(), deflate(), deflateEnd(), inflateInit(), inflate(), inflateEnd(). */
#endif

/* Disable some GCC alternate keywords
 * (https://gcc.gnu.org/onlinedocs/gcc/Alternate-Keywords.html) if not
 * compiling with GCC (or Clang). `gcc -ansi -pedantic' has no effect on
 * these.
 */
#if defined(__GNUC__) || defined(USE_GCC_ALTERNATE_KEYWORDS)
#define ATTRIBUTE_NORETURN __attribute__((noreturn))
#define ATTRIBUTE_USED __attribute__((used))
#define INLINE __inline__
#else
#define ATTRIBUTE_NORETURN
#define ATTRIBUTE_USED
#define INLINE
#endif

#ifdef __TINYC__
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint32_t;
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int32_t;
typedef unsigned int size_t;  /* TODO(pts): 64-bit tcc. */
#define NULL ((void*)0)
void ATTRIBUTE_NORETURN exit(int status);
/* string.h */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
/* stdio.h */
#define SEEK_SET 0
typedef struct FILE FILE;
extern FILE *stderr;
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
FILE *fopen(const char *path, const char *mode);
int fprintf(FILE *stream, const char *format, ...);
int putc(int c, FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);
int ferror(FILE *stream);
int fclose(FILE *stream);
/* zlib.h */
#define Z_NO_FLUSH 0
#define Z_FINISH 4
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_DATA_ERROR (-3)
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef unsigned char Bytef;
typedef struct z_stream_s {
  const Bytef *next_in;
  uInt     avail_in;
  uLong    total_in;
  Bytef    *next_out;
  uInt     avail_out;
  uLong    total_out;
  const char *msg;
  struct internal_state *state;
  void*/*alloc_func*/ zalloc;
  void*/*free_func*/  zfree;
  void    *opaque;
  int     data_type;
  uLong   adler;
  uLong   reserved;
} z_stream;
#define ZLIB_VERSION "1.0"  /* Doesn't matter, "1.2.8" also works. */
int deflateInit_(z_stream *strm, int level, const char *version, int stream_size);
#define deflateInit(strm, level) deflateInit_((strm), (level), ZLIB_VERSION, (int)sizeof(z_stream))
int deflate(z_stream *strm, int flush);
int deflateEnd(z_stream *strm);
int inflateInit_(z_stream *strm, const char *version, int stream_size);
#define inflateInit(strm) inflateInit_((strm), ZLIB_VERSION, (int)sizeof(z_stream))
int inflate(z_stream *strm, int flush);
int inflateEnd(z_stream *strm);
uLong crc32(uLong crc, const Bytef *buf, uInt len);
#endif

typedef char xbool_t;

static ATTRIBUTE_NORETURN void die(const char *msg) {
  fprintf(stderr, "fatal: %s\n", msg);  /* !! get rid of printf */
  exit(120);
}

static uint32_t add_check(uint32_t a, uint32_t b) {
  /* Check for overflow. Works only if everything is unsigned. */
  if (b > (uint32_t)-1 - a) die("integer overflow");
  return a + b;
}

/* Checks that a + b doesn't overflow, then returns a. */
static uint32_t add0_check(uint32_t a, uint32_t b) {
  /* Check for overflow. Works only if everything is unsigned. */
  if (b > (uint32_t)-1 - a) die("integer overflow");
  return a;
}

static uint32_t multiply_check(uint32_t a, uint32_t b) {
  const uint32_t result = a * b;
  /* Check for overflow. Works only if everything is unsigned. */
  if (result / a != b) die("integer overflow");
  return result;
}

static void *xmalloc(size_t size) {
  void *result;
  if (size == 0) return NULL;
  if (!(result = malloc(size))) die("out of memory");
  return result;
}

/* --- */

/* color_type constants. Must be same as PNG. */
#define CT_GRAY 0
#define CT_RGB 2
#define CT_INDEXED_RGB 3
#define CT_GRAY_ALPHA 4  /* Not supported by imgdataopt. */
#define CT_RGB_ALPHA 6   /* Not supported by imgdataopt. */

typedef struct Image {
  uint32_t width;
  uint32_t height;
  /* Computed from width, bpc and cpp.
   * It's guaranteed that rlen * height first to an uint32_t.
   */
  uint32_t rlen;
  /* data[:alloced] is available as a buffer.
   * alloced >= rlen * height.
   */
  uint32_t alloced;
  /* data[:rlen * height] contains image data. */
  char *data;
  /* At most 3 * 256 bytes, RGB... format. */
  char *palette;
  /* Number of bytes in the palette (between 3 and 256 * 3), or
   * 0 if no palette.
   */
  uint32_t palette_size;
  /* Bits per color component. At most 85, typically 1, 2, 4 or 8. */
  uint8_t bpc;
  /* CT_... */
  uint8_t color_type;
  /* Computed from color_type. */
  uint8_t cpp;
} Image;

static void noalloc_image(Image *img) ATTRIBUTE_USED;  /* !! */
static void noalloc_image(Image *img) {
  img->data = img->palette = NULL;
}

static void alloc_image(
    Image *img, uint32_t width, uint32_t height, uint8_t bpc,
    uint8_t color_type, uint32_t palette_size) {
  const uint8_t cpp = (color_type == CT_RGB ? 3 : 1);
  const uint32_t samples_per_row = add0_check(multiply_check(width, cpp), 2);
  /* Number of bytes in a row. */
  const uint32_t rlen = bpc == 8 ? samples_per_row :
      bpc == 4 ? ((samples_per_row + 1) >> 1) :
      bpc == 2 ? ((samples_per_row + 3) >> 2) :
      bpc == 1 ? ((samples_per_row + 7) >> 3) : 0;
  const uint32_t alloced = multiply_check(rlen, height);
  add_check(multiply_check(rlen, 7), 1);  /* Early upper limit. */
  img->width = width;
  img->height = height;
  img->rlen = rlen;
  img->alloced = alloced;
  img->bpc = bpc;
  img->color_type = color_type;
  img->cpp = cpp;
  if (palette_size == 0 && color_type == CT_INDEXED_RGB) palette_size = 3 * 256;
  if (palette_size != 0 && color_type != CT_INDEXED_RGB) palette_size = 0;
  img->palette_size = palette_size;
  img->data = (char*)xmalloc(alloced);
  img->palette = (char*)xmalloc(palette_size);
}

static INLINE void dealloc_image(Image *img) {
  free(img->data); img->data = NULL;
  free(img->palette); img->palette = NULL;
}

/* --- PNM */

/* !! Remove PNM functionality */

/* We relax: we don't check img->color_type == GRAY. */
static void write_pnm(const char *filename, const Image *img) {
  const uint32_t width = img->width;
  const uint32_t height = img->height;
  const char *p, *pend;
  FILE *f;
  if (img->bpc != 8) die("need bpc=8 for writing pgm/ppm");
  if (img->cpp != 1 && img->cpp != 3) die("need cpp=1 or =3 for writing pgm/ppm");
  if (!(f = fopen(filename, "wb"))) die("error writing pgm");
  fprintf(f, "%s%lu %lu 255\n",
         img->cpp == 3 ? "P6 " : "P5 ",
         (unsigned long)width, (unsigned long)height);
  p = img->data;
  pend = p + multiply_check(width, height);
  if (pend < p) die("image too large");
  for (; p != pend; ++p) {
    const char c = *p ? '\xff' : '\0';
    putc(c, f);
  }
  fflush(f);
  if (ferror(f)) die("error writing pgm");
  fclose(f);
}

/* --- PNG */

#define PNG_COMPRESSION_DEFAULT 0
#define PNG_FILTER_DEFAULT 0
#define PNG_INTERLACE_NONE 0

/* PNG predictors. */
#define PNG_PR_NONE 0
#define PNG_PR_SUB 1
#define PNG_PR_UP 2
#define PNG_PR_AVERAGE 3
#define PNG_PR_PAETH 4

/* Predictor modes. Same as sam2p -c:zip:... */
#define PM_NONE 1
#define PM_TIFF2 2
#define PM_PNGNONE 10
#define PM_PNGAUTO 15
#define PM_SMART 25  /* Default of sam2p. */

uint32_t get_u32be(char *p) {
  register unsigned char *pu = (unsigned char *)p;
  register uint32_t result = *pu++;
  result = result << 8 | *pu++;
  result = result << 8 | *pu++;
  result = result << 8 | *pu;
  return result;
}

char *put_u32be(char *p, uint32_t k) {
  *p++ = k >> 24;
  *p++ = k >> 16;
  *p++ = k >> 8;
  *p++ = k;
  return p;
}

char *put_u32le(char *p, uint32_t k) {
  *p++ = k; k >>= 8;
  *p++ = k; k >>= 8;
  *p++ = k; k >>= 8;
  *p++ = k;
  return p;
}

char *put_u16le(char *p, uint16_t k) {
  *p++ = k; k >>= 8;
  *p++ = k;
  return p;
}

/* Using i * -1 instead of -i because egcs-2.91.60 is buggy. */
static INLINE unsigned absu(unsigned i) {
  return ((signed)i)<0 ? (i * -1) : i;
}

static INLINE unsigned paeth_predictor(unsigned a, unsigned b, unsigned c) {
  /* Code ripped from RFC 2083 (PNG specification), which also says:
   * The calculations within the PaethPredictor function must be
   * performed exactly, without overflow.  Arithmetic modulo 256 is to
   * be used only for the final step of subtracting the function result
   * from the target byte value.
   */
  /* a = left, b = above, c = upper left */
  unsigned p  = a + b - c;       /* initial estimate */
  unsigned pa = absu(p - a);     /* distances to a, b, c */
  unsigned pb = absu(p - b);
  unsigned pc = absu(p - c);
  /* return nearest of a,b,c, breaking ties in order a,b,c. */
  return (pa <= pb && pa <= pc) ? a
       : pb <= pc ? b
       : c;
}

static void* xzalloc(void *opaque, uInt items, uInt size) {
  (void)opaque;
  return calloc(items, size);
}

/* Returns the payload size of the IDAT chunk.
 * flate_level: 0 is uncompressed, 1..9 is compressed, 9 is maximum compression
 *   (slow, but produces slow output).
 */
static uint32_t write_png_img_data(
    FILE *f, const char *img_data, register uint32_t rlen, uint32_t height,
    uint8_t predictor_mode, uint8_t bpc_cpp, uint8_t flate_level) {
  const uint32_t rlen1 = rlen + 1;
  char obuf[8192];
  uint32_t crc32v = 900662814UL;  /* zlib.crc32("IDAT"). */
  z_stream zs;
  int zr;
  uInt zoutsize;
  /* If more than 24 bits, then rowsum would overflow. */
  if (rlen >> 24) die("image rlen too large");
  zs.zalloc = xzalloc;  /* calloc to pacify valgrind. */
  zs.zfree = NULL;
  zs.opaque = NULL;
  /* !! Preallocate buffers in 1 big chunk, see deflateInit in sam2p. Everywhere. */
  if (deflateInit(&zs, flate_level)) die("error in deflateInit");
  fwrite("\0\0\0\0IDAT", 1, 8, f);
  zs.next_in = (Bytef*)img_data;
  zs.avail_in = 0;
  if (predictor_mode == PM_NONE) {
    const uint32_t usize = multiply_check(rlen, height);
    zs.avail_in = usize;  /* TODO(pts): Check for overflow. */
    /* Z_FINISH below will do all the compression. */
  } else if (predictor_mode == PM_TIFF2) {
    /* Implemented in TIFFPredictor2::vi_write in encoder.cpp in sam2p. */
    die("TIFF2 predictor not supported");
  } else if (predictor_mode == PM_PNGAUTO) {
    /* 1 for the predictor identifier in the row, 6 for the 5 predictors + copy
     * of the previous row.
     */
    char *tmp = (char*)xmalloc(add_check(multiply_check(rlen, 6), 1));
    const int32_t left_delta = -((bpc_cpp + 7) >> 3);
    /* Since 1 <= bpc_cpp <= 24, so -3 <= left_delta <= -1. */
    memset(tmp + 1 + rlen * 5, '\0', rlen);  /* Previous row. */
    for (; height > 0; img_data += rlen, --height) {
      char *p, *pend, *best_predicted;
      uint32_t best_rowsum, rowsum, pi;
      pend = (p = tmp + 1) + rlen;
      memcpy(p, img_data, rlen);  /* PNG_PR_NONE */
      /* 1, 2 or 3 iterations of this loop. */
      for (; p != pend && tmp - p >= left_delta; ++p) {
        const unsigned char v = *p, vpr = p[rlen * 5];  /* Sign is important. */
        p += rlen; *p = v;  /* PNG_PR_SUB */
        p += rlen; *p = v - vpr;  /* PNG_PR_UP */
        p += rlen; *p = v - (vpr >> 1);  /* PNG_PR_AVERAGE */
        /* After the -, same as paeth_predictor(0, vpr, 0); .*/
        p += rlen; *p = v - vpr;  /* PNG_PR_PAETH */
        p -= rlen * 4;
      }
      for (; p != pend; ++p) {
        const unsigned char v = *p, vpr = p[rlen * 5];  /* Sign is important. */
        const unsigned char vpc = p[left_delta];  /* Sign is important. */
        p += rlen; *p = v - vpc;  /* PNG_PR_SUB */
        p += rlen; *p = v - vpr;  /* PNG_PR_UP */
        /* It's important to use tmp (not tmp) here. */
        p += rlen; *p = v - ((vpc + vpr) >> 1);  /* PNG_PR_AVERAGE */
        /* It's important to use tmp (not tmp) here. */
        p += rlen; *p = v - paeth_predictor(vpc, vpr, ((unsigned char*)p)[(int32_t)rlen + left_delta]);  /* PNG_PR_PAETH */
        p -= rlen * 4;
      }

      best_predicted = tmp + 1;
      /* Copy the current row as the previous row for the next iteration. */
      memcpy(p + rlen * 4, best_predicted, rlen);
      for (best_rowsum = 0, p = best_predicted; p != pend; ++p) {
        const signed char c = *p;  /* Sign is important. */
        best_rowsum += (signed char)c < 0 ? (c * -1) : c;
      }
      for (pi = 1; pi <= 4; ++pi) {
        for (pend = p + rlen, rowsum = 0; p != pend; ++p) {
          const signed char c = *p;  /* Sign is important. */
          rowsum += (signed char)c < 0 ? (c * -1) : c;
        }
        if (rowsum < best_rowsum) {
          best_rowsum = rowsum;
          best_predicted = p - rlen;
        }
      }

      --best_predicted;
      best_predicted[0] = (best_predicted - tmp) / rlen;
      /* fprintf(stderr, "best_predictor=%d min_weight=%d\n", *best_predicted, best_rowsum); */
      zs.next_in = (Bytef*)best_predicted;
      zs.avail_in = rlen1;
      do {
        zs.next_out = (Bytef*)obuf;
        zs.avail_out = sizeof(obuf);
        if (deflate(&zs, Z_NO_FLUSH) != Z_OK) die("deflate failed");
        zoutsize = zs.next_out - (Bytef*)obuf;
        crc32v = crc32(crc32v, (const Bytef*)obuf, zoutsize);
        fwrite(obuf, 1, zoutsize, f);
      } while (zs.avail_out == 0);
      if (zs.avail_in != 0) die("deflate has not processed all input");
    }
    free(tmp);
  } else if (predictor_mode == PM_PNGNONE) {
    for (; height > 0; img_data += rlen, --height) {
      char predictor = PNG_PR_NONE;
      zs.next_in = (Bytef*)&predictor;
      zs.avail_in = 1;
      for (;;) {  /* Write the predictor; write rlen bytes from img_data. */
        do {
          zs.next_out = (Bytef*)obuf;
          zs.avail_out = sizeof(obuf);
          if (deflate(&zs, Z_NO_FLUSH) != Z_OK) die("deflate failed");
          zoutsize = zs.next_out - (Bytef*)obuf;
          crc32v = crc32(crc32v, (const Bytef*)obuf, zoutsize);
          fwrite(obuf, 1, zoutsize, f);
        } while (zs.avail_out == 0);
        if (zs.avail_in != 0) die("deflate has not processed all input");
        if (predictor != PNG_PR_NONE) break;
        ++predictor;
        zs.next_in = (Bytef*)img_data;
        zs.avail_in = rlen;  /* TODO(pts): Check for overflow. */
      }
    }
  } else {
    die("unknown predictor");
  }
  do {  /* Flush deflate output. */
    zs.next_out = (Bytef*)obuf;
    zs.avail_out = sizeof(obuf);
    if ((zr = deflate(&zs, Z_FINISH)) != Z_STREAM_END && zr != Z_OK) {
      die("deflate failed");
    }
    zoutsize = zs.next_out - (Bytef*)obuf;
    crc32v = crc32(crc32v, (const Bytef*)obuf, zoutsize);
    fwrite(obuf, 1, zoutsize, f);
  } while (zr == Z_OK && zs.avail_out == 0);
  if (zs.avail_in != 0) die("deflate has not processed all input");
  deflateEnd(&zs);
  /* No need to append zs.adler, deflate() does it for us. */
  put_u32be(obuf, crc32v);
  fwrite(obuf, 1, 4, f);
  return zs.total_out;
}

static const char kPngHeader[16 + 1] = "\x89PNG\r\n\x1a\n\0\0\0\rIHDR";

static void write_png_header(
    FILE *f,
    uint32_t width, uint32_t height, uint8_t bpc, uint8_t color_type,
    uint8_t filter) {
  char buf[33], *p = buf;
  memcpy(p, kPngHeader, 16); p += 16;
  p = put_u32be(p, width);
  p = put_u32be(p, height);
  *p++ = bpc;
  *p++ = color_type;
  *p++ = PNG_COMPRESSION_DEFAULT;
  *p++ = filter;
  *p++ = PNG_INTERLACE_NONE;
  put_u32be(p, crc32(0, (const Bytef*)(buf + 12), 17));
  fwrite(buf, 1, 33, f);
}

/* size is the number of bytes in the palette, typically 3 * color_count.
 * data[:size] looks like RGBRGBRGB...
 */
static void write_png_palette(
    FILE *f, const char *data, uint32_t size) {
  const uint32_t crc32v_plte = 1269336405UL;  /* crc32(0, "PLTE", 4). */
  char buf[8];
  put_u32be(buf, size);
  memcpy(buf + 4, "PLTE", 4);
  fwrite(buf, 1, 8, f);
  fwrite(data, 1, size, f);
  put_u32be(buf, crc32(crc32v_plte, (const Bytef*)data, size));
  fwrite(buf, 1, 4, f);
}

static void write_png_end(FILE *f) {
  fwrite("\0\0\0\0IEND\xae""B`\x82", 1, 12, f);
}

/* tmp is preallocated to rlen * 6 + 1 bytes.
 * If is_extended is true, that can produce an invalid PNG (e.g. with PM_NONE).
 */
static void write_png(const char *filename, const Image *img,
                      xbool_t is_extended, uint8_t predictor_mode,
                      uint8_t flate_level) {
  const uint8_t bpc = img->bpc;
  const uint8_t color_type = img->color_type;
  uint8_t filter;
  xbool_t do_palette = color_type == CT_INDEXED_RGB;
  const uint32_t idat_size_ofs = do_palette ?
      add_check(45, img->palette_size) : 33;
  uint32_t idat_size;
  FILE *f;
  char buf[4];

  if (predictor_mode < PM_NONE) {
    predictor_mode = PM_NONE;
  } else if (predictor_mode == PM_SMART) {
    predictor_mode =
        /* This condition comes from the is_predictor_recommended = ...
         * assignment in sam2p 0.49.4, which comes from the do_filter = ...
         * assignment in png_write_IHDR() in pngwutil.c of libpng-1.2.15 .
         */
        bpc == 8 && (color_type == CT_RGB || color_type == CT_GRAY) ?
        PM_PNGAUTO : PM_NONE;
  }
  if (!is_extended && predictor_mode != PM_PNGAUTO) {
    predictor_mode = PM_PNGNONE;
  }
  /* Only PNG_FILTER_DEFAULT (0) is standard PNG. 1 is PM_NONE, 2 is PM_TIFF2.
   */
  filter = predictor_mode < PM_PNGNONE ? predictor_mode : PNG_FILTER_DEFAULT;

  if (!(f = fopen(filename, "wb"))) die("error writing png");
  write_png_header(f, img->width, img->height, bpc, color_type, filter);
  if (do_palette) {
    write_png_palette(f, img->palette, img->palette_size);
  }
  idat_size = write_png_img_data(
      f, img->data, img->rlen, img->height, predictor_mode,
      img->bpc * img->cpp, flate_level);
  write_png_end(f);
  if (fseek(f, idat_size_ofs, SEEK_SET)) die("error seeking to idat_size_ofs");
  put_u32be(buf, idat_size);
  fwrite(buf, 1, 4, f);
  fflush(f);
  if (ferror(f)) die("error writing png");
  fclose(f);
}

/* img must be initialized (at least noalloc_image). */
static void read_png(const char *filename, Image *img) {
  uint32_t width, height, palette_size = 0;
  uint8_t bpc, color_type, filter;
  char right_and_byte = 0;
  /* Must be large enough for the PNG header (33 bytes), for the palette (3
   * * 256 bytes), and must be fast enough for inflate, and must be at least
   * 4 bytes (adler32 checksum size) for inflate.
   */
  char buf[8192], *p;
  unsigned char *dp0 = 0;
  register unsigned char *dp = NULL;
  char predictor;
  uint32_t d_remaining = (uint32_t)-1, rlen = 0;
  int32_t left_delta_inv = 0;
  FILE *f;
  z_stream zs;
  int zr = Z_OK;
  xbool_t do_one_more_inflate = 1;
  if (!(f = fopen(filename, "rb"))) die("error reading png");
  if (33 != fread(buf, 1, 33, f)) die("png too short");
  /* https://tools.ietf.org/rfc/rfc2083.txt */
  if (0 != memcmp(buf, kPngHeader, 16)) die("bad signature in png");
  if (crc32(0, (const Bytef*)buf + 12, 17) != get_u32be(buf + 29)) {
    die("crc error in png ihdr");
  }
  dealloc_image(img);
  width = get_u32be(buf + 16);
  if ((int32_t)width <= 0) die("bad png width");
  height = get_u32be(buf + 20);
  if ((int32_t)height <= 0) die("bad png height");
  p = buf + 24;
  bpc = *p++;
  if (bpc == 16) die("not supported png bpc");
  if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8) die("bad png bpc");
  color_type = *p++;
  if (color_type != CT_GRAY && color_type != CT_RGB &&
      color_type != CT_INDEXED_RGB) die("bad png color_type");
  if (*p++ != PNG_COMPRESSION_DEFAULT) die("bad png compression");
  filter = *p++;
  /* PNG supports filter == 0 only; 1 and 2 are imgdataopt extensions. */
  if (filter != PNG_FILTER_DEFAULT && filter != PM_NONE && filter != PM_TIFF2) die("bad png filter");
  if (filter == PM_TIFF2) die("TIFF2 predictor not supported");
  if (*p++ != PNG_INTERLACE_NONE) die("not supported png interlace");
  for (;;) {
    uint32_t chunk_size;
    if (8 != fread(buf, 1, 8, f)) die("eof in png chunk header");
    chunk_size = get_u32be(buf);
    p = buf + 4;
    {
      /* We ignore every other chunk (such as gamma correction with gAMA and
       * transparency in tRNS), so this code is not suitable as a
       * general-purpose PNG renderer.
       */
      const xbool_t is_plte = 0 == memcmp(p, "PLTE", 4);
      const xbool_t is_idat = 0 == memcmp(p, "IDAT", 4);
      const xbool_t is_iend = 0 == memcmp(p, "IEND", 4);
      uint32_t crc32v = crc32(0, (const Bytef*)p, 4);
      if (is_plte) {
        if (img->data) die("png palette too late");
        if (chunk_size == 0 || chunk_size > 3 * 256 || chunk_size % 3 != 0) {
          die("bad png palette size");
        }
        if (palette_size != 0) die("dupicate png palette");
        palette_size = chunk_size;
        goto do_alloc_image;
      }
      if (is_idat && !img->data) {
        /* PNG requires that PLTE is appears after IDAT. */
        if (color_type == CT_INDEXED_RGB) die("missing png palette");
        palette_size = 0;
       do_alloc_image:
        alloc_image(img, width, height, bpc, color_type, palette_size);
        left_delta_inv = bpc * img->cpp;
        /* 0: 0xff, 1: 0x80, 2: 0xc0, 3: 0xe0, 4: 0xf0, 5: 0xf8, 6: 0xfc, 7: 0xfe. */
        right_and_byte = (width & 7) == 0 ? 0xff :
            (uint16_t)0x7f00 >> (((width & 7) * left_delta_inv) & 7);
        /* fprintf(stderr, "!! width=%d rlen=%d right_and_byte=0x%x bpc=%d\n", width, rlen, (unsigned char)right_and_byte, bpc); */
        left_delta_inv = ((left_delta_inv + 7) >> 3);
      }
      while (chunk_size > 0) {
        const uint32_t want = chunk_size < sizeof(buf) ?
            chunk_size : sizeof(buf);
        if (want != fread(buf, 1, want, f)) die("eof in png chunk");
        if (is_plte) {
          if (want != palette_size) die("ASSERT: png palette buf too small");
          memcpy(img->palette, buf, palette_size);
        } else if (is_idat) {
          if (!dp) {
            zs.zalloc = xzalloc;  /* calloc to pacify valgrind. */
            zs.zfree = NULL;
            zs.opaque = NULL;
            if (inflateInit(&zs)) die("error in deflateInit");
            dp = dp0 = (unsigned char*)img->data;
            /* Overflow already checked by alloc_image. */
            rlen = img->rlen;
            d_remaining = rlen * img->height;  /* Not 0, checked by alloc_image. */
            if (filter == PNG_FILTER_DEFAULT) {
              zs.next_out = (Bytef*)&predictor;
              zs.avail_out = 1;
            } else {
              zs.next_out = (Bytef*)dp;
              zs.avail_out = d_remaining;  /* TODO(pts): Check for overflow. */
            }
          }
          /* There was an error or EOF before, we can't inflate anymore. */
          zs.next_in = (Bytef*)buf;
          zs.avail_in = want;
          if (d_remaining == 0 && zr == Z_OK && do_one_more_inflate) {
            /* Do one more inflate, so that it can process the adler32 checksum. */
            do_one_more_inflate = 0;
            zs.next_out = (Bytef*)&predictor;
            zs.avail_out = 1;
            zr = inflate(&zs, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END && zr != Z_DATA_ERROR) {
              die("inflate failed");
            }
          }
          while (zr == Z_OK && zs.avail_in != 0 && d_remaining != 0) {
            zr = inflate(&zs, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END && zr != Z_DATA_ERROR) {
              die("inflate failed");
            }
            /* TODO(pts): Process a row partially if there is an EOD. */
            if (zs.avail_out != 0) {
            } else if (zs.next_out == (Bytef*)(&predictor + 1)) {
              if ((unsigned char)predictor > 4) die("bad png predictor");
              zs.next_out = (Bytef*)dp;
              zs.avail_out = rlen;
            } else if (filter != PNG_FILTER_DEFAULT) {  /* PM_NONE */
              uint32_t y = height;
              for (; y > 0; --y) {
                dp += rlen;
                dp[-1] &= right_and_byte;
              }
              d_remaining = 0;
            } else {
              /* Now we've predictor and dp[:rlen] as the current row ready. */
              unsigned char *dpleft = dp + left_delta_inv;
              unsigned char *dpend = dp + rlen;
              register unsigned char *dr, *dc;
              switch (predictor) {
               case PNG_PR_SUB: do_sub:
                if ((uint32_t)left_delta_inv < rlen) {
                  for (dc = dp, dp += left_delta_inv; dp != dpend; *dp++ += *dc++) {}
                }
                break;
               case PNG_PR_UP:
                if (dp != dp0) {  /* Skip it in the first row. */
                  for (dr = dp - rlen; dp != dpend; *dp++ += *dr++) {}
                }
                break;
               case PNG_PR_AVERAGE:
                /* It's important here that dr and dc are _unsigned_ char* */
                if (dp == dp0) {  /* First row. */
                  for (; dp != dpend && dp != dpleft; ++dp) {}
                  for (dc = dp - left_delta_inv; dp != dpend; *dp++ += *dc++ >> 1) {}
                } else {
                  for (dr = dp - rlen; dp != dpend && dp != dpleft; *dp++ += *dr++ >> 1) {}
                  for (dc = dp - left_delta_inv; dp != dpend; *dp++ += (*dc++ + *dr++) >> 1) {}
                }
                break;
               case PNG_PR_PAETH:
                /* It's important here that dr and dc are _unsigned_ char* */
                if (dp == dp0) goto do_sub;  /* First row. */
                for (dr = dp - rlen; dp != dpend && dp != dpleft; *dp++ += *dr++) {}
                for (dc = dp - left_delta_inv; dp != dpend; *dp++ += paeth_predictor(*dc++, *dr, *(dr - left_delta_inv)), ++dr) {}
                break;
               default: ; /* No special action needed for PNG_PR_NONE. */
                dp = dpend;
              }
              dp[-1] &= right_and_byte;
              d_remaining -= rlen;
              zs.next_out = (Bytef*)&predictor;
              zs.avail_out = d_remaining != 0;
            }
          }
        }
        crc32v = crc32(crc32v, (const Bytef*)buf, want);
        chunk_size -= want;
      }
      if (4 != fread(buf, 1, 4, f)) die("eof in png chunk crc");
      if (crc32v != get_u32be(buf)) die("crc error in png chunk");
      if (is_iend) break;
    }
  }
  if (!img->data) die("missing png image data");
  if (zr == Z_DATA_ERROR) {
    fprintf(stderr, "warning: bad png image data or bad adler32\n");
  }
  if (d_remaining == 0) {
    if (dp && zr == Z_OK && (zs.avail_in != 0 || zs.avail_out != 0)) {  /* Not Z_STREAM_END. */
      fprintf(stderr, "warning: png image data too long\n");
    }
  } else {
    fprintf(stderr, "warning: png image data too short\n");
    /* TODO(pts): Make it white instead on RGB and gray. */
    memset(dp, '\0', d_remaining);
  }
  if (dp) inflateEnd(&zs);
  if (ferror(f)) die("error reading png");
  fclose(f);
}

/* --- */

static xbool_t is_gray_ok(const Image *img) {
  const char *p, *pend;
  if (img->bpc != 8) die("ASSERT: is_gray_ok needs bpc=8");
  if (img->color_type == CT_GRAY) {
    return 1;
  } else if (img->color_type == CT_INDEXED_RGB) {
    p = img->palette;
    pend = p + img->palette_size;
  } else {
    p = img->data;
    pend = img->data + img->rlen * img->height;
  }
  /* assert((p - pend) % 3 == 0); */
  for (; p != pend; p += 2) {
    const char v = *p++;
    if (v != p[0] || v != p[1]) return 0;
  }
  return 1;
}

/* Converts the image to bpc=to_bpc in place.
 *
 * Assumes (but doesn't check) that the conversion is lossless.
 */
static void convert_to_bpc(Image *img, uint8_t to_bpc) {
  /* alloc_image guarantees that there is no overflow here. */
  const uint32_t spr = img->width * img->cpp;
  const uint8_t bpc = img->bpc;
  uint32_t height = img->height;
  char *op = img->data;
  const char *p = op, *pend;
  register unsigned char v;
  if (bpc == to_bpc) return;
  if (bpc != 8 || to_bpc == 8) {  /* Convert to bpc=8 first. */
    const uint32_t rlen = img->rlen;
    const uint32_t new_size = multiply_check(spr, height);
    const uint32_t rlen_height = rlen * height;
    uint32_t h1 = height;
    if (img->alloced < new_size) {
      p = img->data = op = (char*)realloc(op, new_size);
      if (!op) die("out of memory");
      img->alloced = new_size;
    }
    p += new_size - rlen_height;
    memmove((char*)p, op, rlen_height);
    if (img->color_type == CT_INDEXED_RGB) {
      if (bpc == 4) {
        for (; h1 > 0; --h1) {
          uint8_t i = spr & 1;
          for (pend = p + (rlen - (i != 0)); p != pend;) {
            v = *p++;
            *op++ = v >> 4;
            *op++ = v & 15;
          }
          if (i != 0) *op++ = *p++ >> 4;
        }
      } else if (bpc == 2) {
        for (; h1 > 0; --h1) {
          uint8_t i = spr & 3;
          for (pend = p + (rlen - (i != 0)); p != pend;) {
            v = *p++;
            *op++ = v >> 6;
            *op++ = (v >> 4) & 3;
            *op++ = (v >> 2) & 3;
            *op++ = v & 3;
          }
          if (i != 0) {
            for (v = *p++; i > 0; *op++ = v >> 6, v <<= 2, --i) {}
          }
        }
      } else if (bpc == 1) {
        for (; h1 > 0; --h1) {
          uint8_t i = spr & 7;
          for (pend = p + (rlen - (i != 0)); p != pend;) {
            v = *p++;
            *op++ = v >> 7;
            *op++ = (v >> 6) & 1;
            *op++ = (v >> 5) & 1;
            *op++ = (v >> 4) & 1;
            *op++ = (v >> 3) & 1;
            *op++ = (v >> 2) & 1;
            *op++ = (v >> 1) & 1;
            *op++ = v & 1;
          }
          if (i != 0) {
            for (v = *p++; i > 0; *op++ = v >> 7, v <<= 1, --i) {}
          }
        }
      }
    } else {  /* Non-indexed. */
      if (bpc == 4) {
        for (; h1 > 0; --h1) {
          uint8_t i = spr & 1;
          unsigned char w;
          for (pend = p + (rlen - (i != 0)); p != pend;) {
            v = *p++;
            /* (w * 0x11) is optimized by gcc -O2 to (w | w << 4), but not by
             * gcc -Om.
             */
            w = (v >> 4); *op++ = w | (w << 4);
            w = (v & 15); *op++ = w | (w << 4);
          }
          if (i != 0) {
            w = (*p++ >> 4); *op++ = w | (w << 4);
          }
        }
      } else if (bpc == 2) {
        for (; h1 > 0; --h1) {
          uint8_t i = spr & 3;
          unsigned char w;
          for (pend = p + (rlen - (i != 0)); p != pend;) {
            v = *p++;
            w = v >> 6; w |= (w << 2); *op++ = w | (w << 4);
            w = (v >> 4) & 3; w |= (w << 2); *op++ = w | (w << 4);
            w = (v >> 2) & 3; w |= (w << 2); *op++ = w | (w << 4);
            w = v & 3; w |= (w << 2); *op++ = w | (w << 4);
          }
          if (i != 0) {
            for (v = *p++; i > 0; w = v >> 6, w |= (w << 2), *op++ = w | (w << 4), v <<= 2, --i) {}
          }
        }
      } else if (bpc == 1) {
        for (; h1 > 0; --h1) {
          uint8_t i = spr & 7;
          for (pend = p + (rlen - (i != 0)); p != pend;) {
            v = *p++;
            *op++ = -(v >> 7);
            *op++ = -((v >> 6) & 1);
            *op++ = -((v >> 5) & 1);
            *op++ = -((v >> 4) & 1);
            *op++ = -((v >> 3) & 1);
            *op++ = -((v >> 2) & 1);
            *op++ = -((v >> 1) & 1);
            *op++ = -(v & 1);
          }
          if (i != 0) {
            for (v = *p++; i > 0; *op++ = -(v >> 7), v <<= 1, --i) {}
          }
        }
      }
    }
    img->bpc = 8;
    img->rlen = spr;
    p = op = img->data;
  }
  if (img->color_type == CT_INDEXED_RGB) {
    if (to_bpc == 1) {
      for (; height > 0; --height) {
        for (pend = p + (spr & ~(uint32_t)7); p != pend;) {
          v = *p++ & 1;
          v <<= 1; v |= *p++ & 1;
          v <<= 1; v |= *p++ & 1;
          v <<= 1; v |= *p++ & 1;
          v <<= 1; v |= *p++ & 1;
          v <<= 1; v |= *p++ & 1;
          v <<= 1; v |= *p++ & 1;
          *op++ = (v << 1) | (*p++ & 1);
        }
        pend = p + (spr & 7);
        if (p != pend) {
          v = *p++ & 1;
          while (p != pend) {
            v <<= 1; v |= *p++ & 1;
          }
          *op++ = v << (8 - (spr & 7));
        }
      }
      img->rlen = (spr + 7) >> 3;
    } else if (to_bpc == 2) {
      for (; height > 0; --height) {
        for (pend = p + (spr & ~(uint32_t)3); p != pend;) {
          v = *p++ & 3;
          v <<= 2; v |= *p++ & 3;
          v <<= 2; v |= *p++ & 3;
          *op++ = (v << 2) | (*p++ & 3);
        }
        pend = p + (spr & 3);
        if (p != pend) {
          v = *p++ & 3;
          while (p != pend) {
            v <<= 2; v |= *p++ & 3;
          }
          *op++ = v << (8 - 2 * (spr & 3));
        }
      }
      img->rlen = (spr + 3) >> 2;
    } else if (to_bpc == 4) {
      for (; height > 0; --height) {
        for (pend = p + (spr & ~(uint32_t)1); p != pend;) {
          v = *p++ & 15;
          *op++ = (v << 4) | (*p++ & 15);
        }
        if ((spr & 1) != 0) {
          *op++ = (*p++ & 15) << 4;
        }
      }
      img->rlen = (spr + 1) >> 1;
    } else if (to_bpc == 8) {
    } else {
      die("ASSERT: bad to_bpc");
    }
  } else {  /* Non-indexed. */
    if (to_bpc == 1) {
      for (; height > 0; --height) {
        for (pend = p + (spr & ~(uint32_t)7); p != pend;) {
          v = *(unsigned char*)p++ >> 7;
          v <<= 1; v |= *(unsigned char*)p++ >> 7;
          v <<= 1; v |= *(unsigned char*)p++ >> 7;
          v <<= 1; v |= *(unsigned char*)p++ >> 7;
          v <<= 1; v |= *(unsigned char*)p++ >> 7;
          v <<= 1; v |= *(unsigned char*)p++ >> 7;
          v <<= 1; v |= *(unsigned char*)p++ >> 7;
          *op++ = (v << 1) | (*(unsigned char*)p++ >> 7);
        }
        pend = p + (spr & 7);
        if (p != pend) {
          v = *(unsigned char*)p++ >> 7;
          while (p != pend) {
            v <<= 1; v |= *(unsigned char*)p++ >> 7;
          }
          *op++ = v << (8 - (spr & 7));
        }
      }
      img->rlen = (spr + 7) >> 3;
    } else if (to_bpc == 2) {
      for (; height > 0; --height) {
        for (pend = p + (spr & ~(uint32_t)3); p != pend;) {
          v = *(unsigned char*)p++ >> 6;
          v <<= 2; v |= *(unsigned char*)p++ >> 6;
          v <<= 2; v |= *(unsigned char*)p++ >> 6;
          *op++ = (v << 2) | (*(unsigned char*)p++ >> 6);
        }
        pend = p + (spr & 3);
        if (p != pend) {
          v = *(unsigned char*)p++ >> 6;
          while (p != pend) {
            v <<= 2; v |= *(unsigned char*)p++ >> 6;
          }
          *op++ = v << (8 - 2 * (spr & 3));
        }
      }
      img->rlen = (spr + 3) >> 2;
    } else if (to_bpc == 4) {
      for (; height > 0; --height) {
        for (pend = p + (spr & ~(uint32_t)1); p != pend;) {
          v = *(unsigned char*)p++ & 0xf0;
          *op++ = v | (*(unsigned char*)p++ >> 4);
        }
        if ((spr & 1) != 0) {
          *op++ = *(unsigned char*)p++ & 0xf0;
        }
      }
      img->rlen = (spr + 1) >> 1;
    } else if (to_bpc == 8) {
    } else {
      die("ASSERT: bad to_bpc");
    }
  }
  img->bpc = to_bpc;
}

/* --- */

static void init_image_chess(Image *img) {
  enum { kWidth = 91, kHeight = 84 };  /* For -ansi -pedantic. */
  const uint32_t width = 91, height = 84;
  static const char palette[] = "\0\0\0\xff\xff\xff";
  uint32_t x, y;
  char (*img_data)[kHeight][kWidth];
  alloc_image(img, width, height, 8, CT_INDEXED_RGB, sizeof(palette) - 1);
  img_data = (char(*)[kHeight][kWidth])img->data;
  memcpy(img->palette, palette, sizeof(palette) - 1);
  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      (*img_data)[y][x] = (x == 1 || x == 82 || y == 1 || y == 82) ||
                           (x >= 2 && x < 82 && y >= 2 && y < 82 &&
                           ((x + 8) / 10 + (y + 8) / 10) % 2);
    }
  }
}

int main(int argc, char **argv) {
  Image img;
  const uint8_t predictor_mode = PM_PNGAUTO;
  const xbool_t is_extended = 0;
  const uint8_t flate_level = 0;

  (void)argc; (void)argv;
  init_image_chess(&img);
  fprintf(stderr, "is_gray_ok=%d\n", is_gray_ok(&img));  /* : 1 */
  write_pnm("chess2.pgm", &img);
  write_png("chess2.png", &img, is_extended, predictor_mode, flate_level);
  write_png("chess2n.png", &img, 1, PM_NONE, 9);
  convert_to_bpc(&img, 1);
  write_png("chess2i1.png", &img, is_extended, PM_NONE, 9);
  convert_to_bpc(&img, 2);
  write_png("chess2i2.png", &img, is_extended, PM_NONE, 9);
  convert_to_bpc(&img, 4);
  write_png("chess2i4.png", &img, is_extended, PM_NONE, 9);
  convert_to_bpc(&img, 1);
  img.color_type = CT_GRAY;  /* This only works if bpc=1. */
  write_png("chess2g1.png", &img, is_extended, PM_NONE, 9);
  convert_to_bpc(&img, 2);
  write_png("chess2g2.png", &img, is_extended, PM_NONE, 9);
  convert_to_bpc(&img, 4);
  write_png("chess2g4.png", &img, is_extended, PM_NONE, 9);
  convert_to_bpc(&img, 8);
  fprintf(stderr, "is_gray_ok=%d\n", is_gray_ok(&img));  /* : 1 */
  write_png("chess2g8.png", &img, is_extended, PM_NONE, 9);
  read_png("chess2i1.png", &img);
  write_png("chess2i1w.png", &img, 1, predictor_mode, 9);
  read_png("chess2n.png", &img);
  write_png("chess2nr.png", &img, is_extended, PM_NONE, 9);
  read_png("chess2.png", &img);
  write_png("chess3.png", &img, is_extended, PM_PNGNONE, 9);
  read_png("beach.png", &img);
  write_png("beach3.png", &img, is_extended, PM_PNGNONE, 9);
  dealloc_image(&img);

  return 0;
}
