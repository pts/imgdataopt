#! /usr/bin/python
# by pts@fazekas.hu at Mon Nov  6 18:08:44 CET 2017

import struct
import zlib


def write_pgm(filename, img_data):
  f = open(filename, 'wb')
  try:
    f.write('P5 %d %d 255\n' % (len(img_data[0]), len(img_data)))
    for line in img_data:
      f.write(line.replace('\1', '\xff'))
  finally:
    f.close()


def write_png(filename, img_data):
  # https://tools.ietf.org/rfc/rfc2083.txt

  def compress(data):
    #return zlib.compress(data)
    # No compression below, same as zlib.compress(data, 0).
    # https://www.ietf.org/rfc/rfc1951.txt
    data = str(data)
    output = ['x\1']
    max_block_size = 0xfb00  # zlib uses 0xfb00, we could use at most 65535.
    for i in xrange(0, len(data), 65535):
      size = len(data) - i
      is_final = size <= 65535
      if not is_final:
        size = 65535
      output.append(struct.pack('<BHH', is_final, size, 65535 & ~size))
      output.append(data[i : i + size])  # TODO(pts): Don't copy slice.
    output.append(struct.pack('>l', zlib.adler32(data)))
    return ''.join(output)

  def write_chunk(chunk_type, chunk_data):
    f.write(struct.pack('>L', len(chunk_data)))
    # This wastes memory on the string concatenation.
    # TODO(pts): Optimize memory use.
    f.write(chunk_type)
    chunk_data = str(chunk_data)
    f.write(chunk_data)
    f.write(struct.pack('>l', zlib.crc32(
        chunk_data, zlib.crc32(chunk_type, 0))))

  f = open(filename, 'wb')
  try:
    f.write('\x89PNG\r\n\x1A\n')  # PNG signature.
    width, height = len(img_data[0]), len(img_data)
    bpc = 8
    # 0: 'gray',
    # 2: 'rgb',
    # 3: 'indexed-rgb',
    # 4: 'gray-alpha',
    # 6: 'rgb-alpha',
    color_type = 3
    compression = 0
    filter = 0
    is_interlaced = 0
    plte = '\0\0\0\xff\xff\xff'
    output = []
    for line in img_data:
      output.append('\0')  # Predictor value for the specified line.
      output.append(line)
    write_chunk(
        'IHDR', struct.pack(
            '>LL5B', width, height, bpc, color_type,
            compression, filter, is_interlaced))
    #assert 0, f.tell()  # 33.
    if plte is not None:
      write_chunk('PLTE', plte)
    write_chunk('IDAT', compress(''.join(output)))
    # "\0\0\0\0IEND\xae""B`\x82".
    write_chunk('IEND', '')
  finally:
    f.close()


def work():
  # A chessboard in a frame.
  img_data = tuple(''.join(
      '\0\1'[((x in (1, 82) or y in (1, 82)) and
              1) or  # not (x in (0, 83) or y in (0, 83))) or
             (2 <= x < 82 and 2 <= y < 82 and
              ((x - 2) // 10 + (y - 2) // 10) % 2)]
      for x in xrange(91)) for y in xrange(84))
  write_pgm('chess.pgm', img_data)
  write_png('chess.png', img_data)


if __name__ == '__main__':
  work()
