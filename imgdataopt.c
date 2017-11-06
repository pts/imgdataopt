/* by pts@fazekas.hu at Mon Nov  6 18:50:40 CET 2017 */
/* xstatic gcc -s -O2 -W -Wall -Wextra -Werror -o write_png write_png.c -lz */
/* !! g++ */
/* !! ignore: Extra compressed data https://github.com/pts/pdfsizeopt/issues/51 */
/* !! nonvalidating PNG parser: ignore checksums, including zlib adler32 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>  /* crc32(), adler32(). */

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
  pend =  p + multiply_check(width, height);
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

/* Predictors. */
#define PNG_PR_NONE 0
#define PNG_PR_SUB 1
#define PNG_PR_UP 2
#define PNG_PR_AVERAGE 3
#define PNG_PR_PAETH 4

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

/* Returns the payload size of the IDAT chunk. */
static uint32_t write_png_img_data(
    FILE *f, const char *img_data, uint32_t rlen, uint32_t height,
    char *tmp, xbool_t is_predictor, uint8_t bpc_cpp) {
  /* !! do it compressed instead. */
  const uint32_t rlen1 = rlen + 1;
  const uint32_t usize = multiply_check(rlen + 1, height);
  uint32_t size = 11;  /* "IDAT" "x\1" "\1" ... 4(adler32) */
  uint32_t adler32v = 1;
  uint32_t crc32v;
  uint32_t i;
  char buf[17];
  /* If more than 24 bits, then rowsum would overflow. */
  if (rlen >> 24) die("image rlen too large");
  if (usize > 0xfb00) die("uncompressed image too large for flate block");
  put_u32be(buf, 0);
  memcpy(buf + 4, "IDATx\1\1", 7);
  put_u16le(buf + 11, usize);
  put_u16le(buf + 13, ~usize);
  crc32v = crc32(0, (const Bytef*)(buf + 4), 11);
  fwrite(buf, 1, 15, f);
  if (is_predictor) {
    /* Since 1 <= bpc_cpp <= 24, so 1 <= left_delta <= 3. */
    const uint32_t left_delta = (bpc_cpp + 7) >> 3;
    unsigned char *tmpu = (unsigned char*)tmp;
    unsigned char *prev_rowu = tmpu + rlen1 * 5;
    const unsigned char *pu, *puend;
    memset(prev_rowu, '\0', rlen1);
    tmpu[rlen1 * PNG_PR_NONE] = PNG_PR_NONE;
    tmpu[rlen1 * PNG_PR_SUB] = PNG_PR_SUB;
    tmpu[rlen1 * PNG_PR_UP] = PNG_PR_UP;
    tmpu[rlen1 * PNG_PR_AVERAGE] = PNG_PR_AVERAGE;
    tmpu[rlen1 * PNG_PR_PAETH] = PNG_PR_PAETH;
    for (; height > 0; img_data += rlen, --height) {
      uint32_t best_rowsum, rowsum, pi;
      const unsigned char *best_predicted;
      memcpy(tmpu + 1, img_data, rlen);
      /* !! fewer multiplications and deltas */
      for (i = 1; i < rlen1 && i <= left_delta; ++i) {
        const unsigned char v = tmpu[i], vpr = prev_rowu[i];
        tmpu[rlen1 * PNG_PR_SUB + i] = v;
        /* It's important to use tmpu (not tmp) here. */
        tmpu[rlen1 * PNG_PR_AVERAGE + i] = v - (vpr >> 1);
        /* It's important to use tmpu (not tmp) here. */
        /* After the -, same as paeth_predictor(0, prev_rowu[i], 0); .*/
        tmpu[rlen1 * PNG_PR_UP + i] =
            tmpu[rlen1 * PNG_PR_PAETH + i] = v - vpr;
      }
      for (; i < rlen1; ++i) {
        const unsigned char v = tmpu[i], vpr = prev_rowu[i];
        const unsigned char vpc = tmpu[i - left_delta];
        tmpu[rlen1 * PNG_PR_UP + i] = v - vpr;
        tmpu[rlen1 * PNG_PR_SUB + i] = v - vpc;
        /* It's important to use tmpu (not tmp) here. */
        tmpu[rlen1 * PNG_PR_AVERAGE + i] = v - ((vpc + vpr) >> 1);
        /* It's important to use tmpu (not tmp) here. */
        tmpu[rlen1 * PNG_PR_PAETH + i] = v - paeth_predictor(vpc, vpr, prev_rowu[i - left_delta]);
      }
      memcpy(prev_rowu, tmpu, rlen1);  /* !! Do it without this. Is it possible? */

      puend = (best_predicted = pu = tmpu) + rlen1;
      for (++pu, best_rowsum = 0; pu != puend;) {
        const signed char c = *pu++;
        /* It's important to use tmpu (not tmp) here. */
        best_rowsum += (signed char)c < 0 ? (c * -1) : c;
      }
      for (pi = 1; pi <= 4; ++pi) {
        for (puend = pu + rlen1, rowsum = 0, ++pu; pu != puend;) {
          const signed char c = *pu++;
          /* It's important to use tmpu (not tmp) here. */
          rowsum += (signed char)c < 0 ? (c * -1) : c;
        }
        if (rowsum < best_rowsum) {
          best_rowsum = rowsum;
          best_predicted = pu - rlen1;
        }
      }

      /* fprintf(stderr, "best_predictor=%d\n", best_predictor); */
      crc32v = crc32(crc32v, (const Bytef*)best_predicted, rlen1);
      adler32v = adler32(adler32v, (const Bytef*)best_predicted, rlen1);
      fwrite((const char *)best_predicted, 1, rlen1, f);
      size = add_check(size, rlen1);
    }
  } else {  /* No predictor (always PNG_PR_NONE). */
    buf[0] = PNG_PR_NONE;
    for (; height > 0; img_data += rlen, --height) {
      fwrite(buf, 1, 1, f);
      crc32v = crc32(crc32v, (const Bytef*)buf, 1);
      adler32v = adler32(adler32v, (const Bytef*)buf, 1);
      fwrite(img_data, 1, rlen, f);
      crc32v = crc32(crc32v, (const Bytef*)img_data, rlen);
      adler32v = adler32(adler32v, (const Bytef*)img_data, rlen);
      size = add_check(size, rlen1);
    }
  }
  put_u32be(buf, adler32v);
  crc32v = crc32(crc32v, (const Bytef*)buf, 4);
  put_u32be(buf + 4, crc32v);
  fwrite(buf, 1, 8, f);
  return size;
}

static void write_png_end(FILE *f) {
  fwrite("\0\0\0\0IEND\xae""B`\x82", 1, 12, f);
}

/* tmp is preallocated to (rlen + 1) * 5 bytes. */
static void write_png(const char *filename, const char *img_data,
                      uint32_t width, uint32_t height,
                      char *tmp) {
  const uint8_t bpc = 8;
  const uint8_t color_type = CT_INDEXED_RGB;
  const uint8_t filter = PNG_FILTER_DEFAULT;
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
  const xbool_t is_predictor = 1;
  uint32_t idat_size;
  FILE *f;
  char buf[4];
  if (!(f = fopen(filename, "wb"))) die("error writing png");
  write_png_header(f, width, height, bpc, color_type, filter);
  write_png_palette(f, palette, palette_size);
  idat_size = write_png_img_data(
      f, img_data, rlen, height, tmp, is_predictor, bpc_cpp);
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
  const uint32_t tmp_size = multiply_check(rlen + 1, 6);
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
  write_png("chess2.png", img_data, width, height, tmp);
  return 0;
}
