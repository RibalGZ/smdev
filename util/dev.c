/* See LICENSE file for copyright and license details. */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include "../util.h"

/* Example `path' is /sys/devices/virtual/tty/tty0/dev */
int
devtomajmin(const char *path, int *maj, int *min)
{
	char buf[BUFSIZ];
	int fd;
	ssize_t n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		eprintf("open %s:", path);
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0)
		eprintf("%s: read error:", path);
	if (!n)
		return -1;
	if (buf[n - 1] == '\n')
		buf[n - 1] = '\0';
	buf[n] = '\0';
	sscanf(buf, "%d:%d", maj, min);
	return 0;
}

/* `majmin' format is maj:min */
int
devtype(const char *majmin)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "/sys/dev/block/%s", majmin);
	if (!access(path, F_OK))
		return S_IFBLK;
	snprintf(path, sizeof(path), "/sys/dev/char/%s", majmin);
	if (!access(path, F_OK))
		return S_IFCHR;
	return -1;
}
