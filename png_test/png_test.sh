#! /bin/bash --
# by pts@fazekas.hu at Wed Oct  4 18:02:10 CEST 2017
#
# This tests that imgdataopt can load various PNG files correctly, especially
# with all kinds of predictors. Please note that it's not a comprehensive
# test, for example it doesn't contain interlaced PDF. It is also checked
# that imgdataopt is able to load its own output.
#

function cleanup() {
  rm -f -- png_test.tmp.pbm png_test.tmp.pgm png_test.tmp.ppm png_test.tmp.png
}

function do_png_test() {
  local INPUT_PNG="$1" TMP_PNM="$2" EXPECTED_PNM="$3" TMP_PNG=png_test.tmp.png

  $PREFIX "$IMGDATAOPT" -j:quiet -- "$EXPECTED_PNM" "$TMP_PNM"
  # Remove comment from "$TMP_PNM".
  #perl -pi -0777 -e 's@\A(P\d\n)#.*\n@$1@' "$TMP_PNM"
  cmp "$EXPECTED_PNM" "$TMP_PNM"

  $PREFIX "$IMGDATAOPT" -j:quiet -- "$INPUT_PNG" "$TMP_PNM"
  #perl -pi -0777 -e 's@\A(P\d\n)#.*\n@$1@' "$TMP_PNM"
  cmp "$EXPECTED_PNM" "$TMP_PNM"

  # -c:zip:15:9 makes a difference, it makes imgdataopt choose per-row
  # predictors differently.
  $PREFIX "$IMGDATAOPT" -j:quiet -c:zip:15:9 -- "$INPUT_PNG" "$TMP_PNG"
  $PREFIX "$IMGDATAOPT" -j:quiet -- "$TMP_PNG" "$TMP_PNM"
  #perl -pi -0777 -e 's@\A(P\d\n)#.*\n@$1@' "$TMP_PNM"
  cmp "$EXPECTED_PNM" "$TMP_PNM"

  $PREFIX "$IMGDATAOPT" -j:quiet -- "$INPUT_PNG" "$TMP_PNG"
  $PREFIX "$IMGDATAOPT" -j:quiet -- "$TMP_PNG" "$TMP_PNM"
  #perl -pi -0777 -e 's@\A(P\d\n)#.*\n@$1@' "$TMP_PNM"
  cmp "$EXPECTED_PNM" "$TMP_PNM"

  rm -f -- "$TMP_PNG" "$TMP_PNM"
}

set -ex
cd "${0%/*}"
PREFIX=
#PREFIX="valgrind -q --error-exitcode=124"
if test "$1"; then
  IMGDATAOPT="$1"
else
  IMGDATAOPT="../imgdataopt"
fi
if test -d "${SAM2P%/*}/pppdir"; then
  export PATH="${SAM2P%/*}/pppdir"
fi

cleanup
do_png_test hello.gray1allpreds.png png_test.tmp.pbm hello.gray1.pbm
do_png_test hello.gray2allpreds.png png_test.tmp.pgm hello.gray2.pgm
do_png_test hello.indexed4allpreds.png png_test.tmp.ppm hello.rgb8.ppm
do_png_test hello.indexed4orig.png png_test.tmp.ppm hello.rgb8.ppm
do_png_test hello.indexed4pngout.png png_test.tmp.ppm hello.rgb8.ppm
do_png_test hello.rgb8allpreds.png png_test.tmp.ppm hello.rgb8.ppm

do_png_test chess.gray1.png png_test.tmp.pbm chess.gray1.pbm
do_png_test chess.gray2.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.gray4.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.gray8.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.grayb1.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.grayb2.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.grayb4.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.grayb8.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexed1.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexed1w.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexed2.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexed4.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexed8.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexed8u.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexedb8u.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexedc2.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexedc4.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexedc8.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexedd8.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.indexede8.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.rgb1.png png_test.tmp.pgm chess.gray1.pgm
do_png_test chess.rgb2.png png_test.tmp.pbm chess.gray1.pbm
do_png_test chess.rgb4.png png_test.tmp.ppm chess.gray1.ppm
do_png_test chess.rgb8.png png_test.tmp.pbm chess.gray1.pbm

do_png_test square.indexed2.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.indexed4.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.indexed8.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.indexedb2.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.indexedb8.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.indexedc8.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.rgb1.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.rgb2.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.rgb4.png png_test.tmp.ppm square.rgb1.ppm
do_png_test square.rgb8.png png_test.tmp.ppm square.rgb1.ppm

cleanup  # Clean up only on success.

: png_test.sh OK.
