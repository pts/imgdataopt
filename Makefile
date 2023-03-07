CC = gcc  # g++, clang and clang++ also works
TCC = tcc  # Tiny C Compiler, https://bellard.org/tcc/ ; also try TCC=pts-tcc at http://ptspts.blogspot.com/2009/11/tiny-self-contained-c-compiler-using.html
CC_MINGW = i686-w64-mingw32-gcc
ZLIB_HEADERS = zlib_src/inffast.h zlib_src/crc32.h zlib_src/inflate.h zlib_src/trees.h zlib_src/inffixed.h zlib_src/zutil.h zlib_src/deflate.h zlib_src/zlib.h zlib_src/zconf.h zlib_src/inftrees.h
ZLIB_SRCS = zlib_src/deflate.c zlib_src/trees.c zlib_src/adler32.c zlib_src/inftrees.c zlib_src/zall.c zlib_src/inflate.c zlib_src/crc32.c zlib_src/inffast.c zlib_src/zutil.c
DOCKER_CROSSBUILD = docker run -v "$$PWD:/workdir" -u "$$(id -u):$$(id -g)" --rm -it multiarch/crossbuild 

WFLAGS = -W -Wall -Wextra -Werror=implicit-function-declaration -Wno-array-bounds -Wno-variadic-macros
ZLIB_SRC_FLAGS = -Izlib_src -DNO_VIZ -DNO_COMBINE64

.PHONY: all clean
all: imgdataopt

# -Werror=implicit-function-declaration works with gcc-4.4, but not with
# gcc-4.1. For earlier versions of gcc, it can be safely dropped.
imgdataopt: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(CC) $(ZLIB_SRC_FLAGS) -ansi -pedantic -s -O2 $(WFLAGS) $(CFLAGS) -o imgdataopt imgdataopt.c zlib_src/zall.c
# Debug mode.
imgdataopt.yes: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(CC) $(ZLIB_SRC_FLAGS) -ansi -pedantic -g -O2 $(WFLAGS) $(CFLAGS) -o imgdataopt.yes imgdataopt.c zlib_src/zall.c
# Like imgdataopt, but with the system's zlib (-lz) instead of the bundled zlib.
imgdataopt.lz: imgdataopt.c
	$(CC) -ansi -pedantic -s -O2 $(WFLAGS) $(CFLAGS) -o imgdataopt.lz imgdataopt.c -lz

imgdataopt.xstatic: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	xstatic $(CC) -Wl,--gc-sections -ffunction-sections -fdata-sections $(ZLIB_SRC_FLAGS) -ansi -pedantic -s -O2 $(WFLAGS) $(CFLAGS) -o $@ imgdataopt.c zlib_src/zall.c
# Good for pdfsizept, -DNO_PMTIFF is also OK, because that just removes reading of nonstandard PNG (with the TIFF2 predictor), and pdfsizeopt doesn't pass it as input.
# We don't use -Os instead of -O2, because we appreciate the speed benefit of -O2: -Os is 82164 bytes, -O2 is 95312 bytes, -O3 is 114000 bytes with gcc-7.3.
imgdataopt.xstaticmini: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	xstatic $(CC) -Wl,--gc-sections -ffunction-sections -fdata-sections $(ZLIB_SRC_FLAGS) -DNO_PMTIFF -DNO_PNM -DNO_REGTEST -ansi -pedantic -s -O2 $(WFLAGS) $(CFLAGS) -o $@ imgdataopt.c zlib_src/zall.c
# Using -O3 so that it will be faster in pdfsizeopt.
imgdataopt.xstatico3: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	xstatic $(CC) -Wl,--gc-sections -ffunction-sections -fdata-sections $(ZLIB_SRC_FLAGS) -ansi -pedantic -s -O3 $(WFLAGS) $(CFLAGS) -o imgdataopt.xstatico3 imgdataopt.c zlib_src/zall.c
imgdataopt.exe: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(CC_MINGW) -Wl,--gc-sections -ffunction-sections -fdata-sections $(ZLIB_SRC_FLAGS) -ansi -pedantic -s -O2 $(WFLAGS) $(CFLAGS) -o imgdataopt.exe imgdataopt.c zlib_src/zall.c
# Without -DNO_COMBINE64 we'd get this error: Undefined symbols for architecture i386: "___moddi3", referenced from: _adler32_combine in zall-....o _adler32_combine64 in zall-....o
# Another solution is adding -lgcc with libgcc.a taken from somewhere else.
imgdataopt.darwinc32: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(DOCKER_CROSSBUILD) /usr/osxcross/bin/o32-clang -mmacosx-version-min=10.5 -Wl,-dead_strip -ffunction-sections -fdata-sections -lSystem -lcrt1.10.5.o -nostdlib $(ZLIB_SRC_FLAGS) -ansi -pedantic -O2 $(WFLAGS) $(CFLAGS) -o imgdataopt.darwinc32 imgdataopt.c zlib_src/zall.c
	$(DOCKER_CROSSBUILD) /usr/osxcross/bin/i386-apple-darwin14-strip imgdataopt.darwinc32
imgdataopt.darwinc64: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(DOCKER_CROSSBUILD) /usr/osxcross/bin/o64-clang -mmacosx-version-min=10.5 -Wl,-dead_strip -ffunction-sections -fdata-sections -lSystem -lcrt1.10.5.o -nostdlib $(ZLIB_SRC_FLAGS) -ansi -pedantic -O2 $(WFLAGS) $(CFLAGS) -o imgdataopt.darwinc64 imgdataopt.c zlib_src/zall.c
	$(DOCKER_CROSSBUILD) /usr/osxcross/bin/x86_64-apple-darwin14-strip imgdataopt.darwinc64

imgdataopt.tcclz: imgdataopt.c
	$(TCC) -m32 -c $(WFLAGS) -o imgdataopt.tcclz.o imgdataopt.c
	gcc -m32 -s -o imgdataopt.tcclz imgdataopt.tcclz.o -lz
imgdataopt.tcc: imgdataopt.c $(ZLIB_SRCS)
	$(TCC) -m32 $(ZLIB_SRC_FLAGS) $(WFLAGS) -o imgdataopt.tcc imgdataopt.c zlib_src/zall.c
	strip imgdataopt.tcc

clean:
	rm -f core imgdataopt imgdataopt.yes imgdataopt.lz imgdataopt.tcclz imgdataopt.exe imgdataopt.xstatic imgdataopt.xstaticmini imgdataopt.xstatico3 imgdataopt.darwinc32 imgdataopt.darwinc64 *.o zlib_src/*.o
