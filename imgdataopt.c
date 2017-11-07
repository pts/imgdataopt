/* by pts@fazekas.hu at Mon Nov  6 18:50:40 CET 2017 */
/* xstatic gcc -s -O2 -W -Wall -Wextra -Werror -o write_png write_png.c -lz */
/* !! compile the final version with g++ */
/* !! ignore: Extra compressed data https://github.com/pts/pdfsizeopt/issues/51 */
/* !! nonvalidating PNG parser: ignore checksums, including zlib adler32 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>  /* crc32(), adler32(), deflateInit(), deflate(), deflateEnd(). */

typedef char xbool_t;

static void die(const char *msg) {
  fprintf(stderr, "fatal: %s\n", msg);
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

/* --- PNM */

static void write_pgm(const char *filename, const char *img_data,
                      uint32_t width, uint32_t height) {
  const char *p, *pend;
  FILE *f;
  if (!(f = fopen(filename, "wb"))) die("error writing pgm");
  fprintf(f, "P5 %lu %lu 255\n", (unsigned long)width, (unsigned long)height);
  p = img_data;
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

/* color_type constants. */
#define CT_GRAY 0
#define CT_RGB 2
#define CT_INDEXED_RGB 3
#define CT_GRAY_ALPHA 4  /* Not supported by imgdataopt. */
#define CT_RGB_ALPHA 6   /* Not supported by imgdataopt. */

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

static void write_png_header(
    FILE *f,
    uint32_t width, uint32_t height, uint8_t bpc, uint8_t color_type,
    uint8_t filter) {
  char buf[33], *p = buf;
  memcpy(p, "\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16); p += 16;
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

/* Using i * -1 instead of -i because egcs-2.91.60 is buggy. */
static __inline__ unsigned absu(unsigned i) {
  return ((signed)i)<0 ? (i * -1) : i;
}

static __inline unsigned paeth_predictor(unsigned a, unsigned b, unsigned c) {
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

/* Returns the payload size of the IDAT chunk.
 * zip_level: 0 is uncompressed, 1..9 is compressed, 9 is maximum compression
 *   (slow, but produces slow output).
 */
static uint32_t write_png_img_data(
    FILE *f, const char *img_data, register uint32_t rlen, uint32_t height,
    char *tmp, uint8_t predictor_mode, uint8_t bpc_cpp, uint8_t zip_level) {
  const uint32_t rlen1 = rlen + 1;
  char obuf[8192];
  uint32_t crc32v = 900662814UL;  /* zlib.crc32("IDAT"). */
  z_stream zs;
  int zr;
  uInt zoutsize;
  /* If more than 24 bits, then rowsum would overflow. */
  if (rlen >> 24) die("image rlen too large");
  zs.zalloc = Z_NULL;
  zs.zfree = Z_NULL;
  zs.opaque = Z_NULL;
  /* !! Preallocate buffers in 1 big chunk, see deflateInit in sam2p. */
  if (deflateInit(&zs, zip_level)) die("error in deflateInit");
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

      /* !! Check that predictor selection and output is same as
       *    sam2p PNGPredictorAuto.
       * !! Check RGB4 against Ghostscript, Evince etc.
       */
      --best_predicted;
      best_predicted[0] = (best_predicted - tmp) / rlen;
      /* fprintf(stderr, "best_predictor=%ld\n", (long)((best_predicted - tmp) / rlen)); */
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

static void write_png_end(FILE *f) {
  fwrite("\0\0\0\0IEND\xae""B`\x82", 1, 12, f);
}

/* tmp is preallocated to rlen * 6 + 1 bytes.
 * If is_extended is true, that can produce an invalid PNG (e.g. with PM_NONE).
 */
static void write_png(const char *filename, const char *img_data,
                      uint32_t width, uint32_t height,
                      char *tmp, xbool_t is_extended, uint8_t predictor_mode,
                      uint8_t zip_level) {
  const uint8_t bpc = 8;
  const uint8_t color_type = CT_INDEXED_RGB;
  uint8_t filter;
  const uint8_t bpc_cpp = bpc * (color_type == CT_RGB ? 3 : 1);
  const uint32_t samples_per_row =
      add0_check(color_type == CT_RGB ? multiply_check(width, 3) : width, 2);
  /* Number of bytes in a row. */
  const uint32_t rlen = bpc == 8 ? samples_per_row :
      bpc == 4 ? ((samples_per_row + 1) >> 1) :
      bpc == 2 ? ((samples_per_row + 3) >> 2) :
      bpc == 1 ? ((samples_per_row + 7) >> 3) : 0;
  const char * const palette = "\0\0\0\xff\xff\xff";
  const uint32_t palette_size = 6;  /* Must be (uint32_t)-1 if no palette. */
  const uint32_t idat_size_ofs = palette_size == (uint32_t)-1 ?
      33 : add_check(45, palette_size);
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
  write_png_header(f, width, height, bpc, color_type, filter);
  write_png_palette(f, palette, palette_size);
  idat_size = write_png_img_data(
      f, img_data, rlen, height, tmp, predictor_mode, bpc_cpp, zip_level);
  write_png_end(f);
  if (fseek(f, idat_size_ofs, SEEK_SET)) die("error seeking to idat_size_ofs");
  put_u32be(buf, idat_size);
  fwrite(buf, 1, 4, f);
  fflush(f);
  if (ferror(f)) die("error writing png");
  fclose(f);
}

/* --- */

int main(int argc, char **argv) {
  const uint32_t width = 91, height = 84;
  const uint32_t rlen = width;  /* !! */
  /* 1 for the predictor identifier in the row, 6 for the 5 predictors + copy
   * of the previous row.
   */
  const uint32_t tmp_size = add_check(multiply_check(rlen, 6), 1);
  const uint8_t predictor_mode = PM_PNGAUTO;
  const xbool_t is_extended = 0;
  const uint8_t zip_level = 0;
  char tmp[tmp_size];
  uint32_t x, y;
  char img_data2[height][width];
  const char *img_data;

  (void)argc; (void)argv;

  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      img_data2[y][x] = (x == 1 || x == 82 || y == 1 || y == 82) ||
                        (x >= 2 && x < 82 && y >= 2 && y < 82 &&
                         ((x + 8) / 10 + (y + 8) / 10) % 2);
    }
  }
  img_data = (const char*)(void*)img_data2;
  write_pgm("chess2.pgm", img_data, width, height);
  write_png("chess2.png", img_data, width, height, tmp,
            is_extended, predictor_mode, zip_level);
  return 0;
}
