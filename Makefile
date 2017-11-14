CC = gcc
CC_MINGW = i686-w64-mingw32-gcc
ZLIB_HEADERS = zlib_src/inffast.h zlib_src/crc32.h zlib_src/inflate.h zlib_src/trees.h zlib_src/inffixed.h zlib_src/zutil.h zlib_src/deflate.h zlib_src/zlib.h zlib_src/zconf.h zlib_src/inftrees.h
ZLIB_SRCS = zlib_src/deflate.c zlib_src/trees.c zlib_src/adler32.c zlib_src/inftrees.c zlib_src/zall.c zlib_src/inflate.c zlib_src/crc32.c zlib_src/inffast.c zlib_src/zutil.c
DOCKER_CROSSBUILD = docker run -v "$$PWD:/workdir" -u "$$(id -u):$$(id -g)" --rm -it multiarch/crossbuild 

.PHONY: all clean
all: imgdataopt
imgdataopt: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(CC) -Izlib_src -DNO_VIZ -ansi -pedantic -s -O2 -W -Wall -Wextra $(CFLAGS) -o imgdataopt imgdataopt.c zlib_src/zall.c
imgdataopt.xstatic: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	xstatic $(CC) -Izlib_src -DNO_VIZ -ansi -pedantic -s -O2 -W -Wall -Wextra $(CFLAGS) -o imgdataopt.xstatic imgdataopt.c zlib_src/zall.c
# Using -O3 so that it will be faster in pdfsizeopt.
imgdataopt.xstatico3: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	xstatic $(CC) -Izlib_src -DNO_VIZ -ansi -pedantic -s -O3 -W -Wall -Wextra $(CFLAGS) -o imgdataopt.xstatic imgdataopt.c zlib_src/zall.c
# Like imgdataopt, but with the system's zlib (-lz) instead of the bundled zlib.
imgdataopt.lz: imgdataopt.c
	$(CC) -ansi -pedantic -s -O2 -W -Wall -Wextra $(CFLAGS) -o imgdataopt.lz imgdataopt.c -lz
imgdataopt.exe: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(CC_MINGW) -Izlib_src -DNO_VIZ -ansi -pedantic -s -O2 -W -Wall -Wextra $(CFLAGS) -o imgdataopt.exe imgdataopt.c zlib_src/zall.c
# Without -DNO_COMBINE64 we'd get this error: Undefined symbols for architecture i386: "___moddi3", referenced from: _adler32_combine in zall-....o _adler32_combine64 in zall-....o
# Another solution is adding -lgcc with libgcc.a taken from somewhere else.
imgdataopt.darwinc32: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(DOCKER_CROSSBUILD) /usr/osxcross/bin/o32-clang -mmacosx-version-min=10.5 -lSystem -lcrt1.10.5.o -nostdlib -DNO_VIZ -DNO_COMBINE64 -ansi -pedantic -O2 -W -Wall -Wextra $(CFLAGS) -o imgdataopt.darwinc32 imgdataopt.c zlib_src/zall.c
	$(DOCKER_CROSSBUILD) /usr/osxcross/bin/i386-apple-darwin14-strip imgdataopt.darwinc32
imgdataopt.darwinc64: imgdataopt.c $(ZLIB_HEADERS) $(ZLIB_SRCS)
	$(DOCKER_CROSSBUILD) /usr/osxcross/bin/o64-clang -mmacosx-version-min=10.5 -lSystem -lcrt1.10.5.o -nostdlib -DNO_VIZ -DNO_COMBINE64 -ansi -pedantic -O2 -W -Wall -Wextra $(CFLAGS) -o imgdataopt.darwinc64 imgdataopt.c zlib_src/zall.c
	$(DOCKER_CROSSBUILD) /usr/osxcross/bin/x86_64-apple-darwin14-strip imgdataopt.darwinc64
clean:
	rm -f core imgdataopt imgdataopt.lz imgdataopt.exe imgdataopt.xstatic imgdataopt.xstatico3 imgdataopt.darwinc32 imgdataopt.darwinc64 *.o zlib_src/*.o
