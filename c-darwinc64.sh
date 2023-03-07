#! /bin/sh --
set -ex

if type x86_64-apple-darwin14-gcc >/dev/null 2>&1; then
  PREFIX=  # https://github.com/pts/pts-osxcross
else
  PREFIX="docker run -v --rm -it $PWD:/workdir multiarch/crossbuild /usr/osxcross/bin/"
fi


${PREFIX}x86_64-apple-darwin14-gcc -mmacosx-version-min=10.5 -Wl,-dead_strip -ffunction-sections -fdata-sections -lSystem -lcrt1.10.5.o -nostdlib -Izlib_src -DNO_VIZ -DNO_COMBINE64 -ansi -pedantic -O2 -W -Wall -Wextra -Werror=implicit-function-declaration -Wno-variadic-macros -o imgdataopt.darwinc64 imgdataopt.c zlib_src/zall.c
${PREFIX}x86_64-apple-darwin14-strip imgdataopt.darwinc64
${PREFIX}x86_64-apple-darwin14-gcc -mmacosx-version-min=10.5 -Wl,-dead_strip -ffunction-sections -fdata-sections -lSystem -lcrt1.10.5.o -nostdlib -Izlib_src -DNO_VIZ -DNO_COMBINE64 -DNO_PMTIFF -DNO_PNM -DNO_REGTEST -ansi -pedantic -O2 -W -Wall -Wextra -Werror=implicit-function-declaration -Wno-variadic-macros -o imgdataopt.darwinc64mini imgdataopt.c zlib_src/zall.c
${PREFIX}x86_64-apple-darwin14-strip imgdataopt.darwinc64mini

: "$0" OK.
