/* gcc -c -O2 -DNO_VIZ -W -Wall -Wextra -Werror -ansi -pedantic zall.c
 * g++ -c -O2 -DNO_VIZ -W -Wall -Wextra -Werror -ansi -pedantic zall.c
 *
 * Compiles without warnings with gcc-4.4, g++-4.4, gcc-4.8, g++-4.8.
 */

/* Also useful: #define NO_VIZ 1 */
#define NO_DUMMY_DECL 1

#ifdef __TINYC__
typedef unsigned int size_t;  /* TODO(pts): 64-bit tcc. */
/* stdlib.h */
void *calloc(size_t nmemb, size_t size);
void *malloc(size_t size);
void free(void *ptr);
/* string.h */
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
#endif

#include "inflate.c"
#include "crc32.c"
#include "adler32.c"
#include "inffast.c"
#include "inftrees.c"
#include "zutil.c"

#include "deflate.c"
#include "trees.c"
