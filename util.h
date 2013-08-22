/* See LICENSE file for copyright and license details. */
#include "arg.h"

#define LEN(x) (sizeof (x) / sizeof *(x))

extern char *argv0;

char *agetcwd(void);
void apathmax(char **, long *);
int devtomajmin(const char *path, int *maj, int *min);
int devtype(const char *majmin);
void enprintf(int, const char *, ...);
void eprintf(const char *, ...);
long estrtol(const char *, int);
void recurse(const char *, void (*)(const char *));
size_t strlcpy(char *dest, const char *src, size_t size);
