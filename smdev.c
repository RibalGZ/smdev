/* See LICENSE file for copyright and license details. */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <regex.h>
#include "config.h"
#include "mkpath.h"
#include "util.h"

static int create_dev(const char *path);
static void sysrecurse(const char *path);

static void
usage(void)
{
	eprintf("usage: %s [-s]\n", argv0);
}

int
main(int argc, char *argv[])
{
	int sflag = 0;

	ARGBEGIN {
	case 's':
		sflag = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (!sflag)
		usage();

	umask(0);
	recurse("/sys/devices", sysrecurse);

	return 0;
}

static int
create_dev(const char *path)
{
	struct passwd *pw;
	struct group *gr;
	regex_t match;
	regmatch_t off;
	char *regex;
	char buf[64], *p;
	char tmppath[PATH_MAX], devpath[PATH_MAX], *dev;
	char *devname;
	int maj, min, type;
	int i, ret;

	p = strrchr(path, '/');
	if (!p)
		return -1;
	p++;
	devname = strdup(p);
	if (!devname)
		eprintf("strdup:");
	snprintf(devpath, sizeof(devpath), "/dev/%s", devname);

	snprintf(tmppath, sizeof(tmppath), "%s/dev", path);
	ret = devtomajmin(tmppath, &maj, &min);
	if (ret < 0) {
		free(devname);
		return -1;
	}

	snprintf(buf, sizeof(buf), "%d:%d", maj, min);
	type = devtype(buf);
	if (type < 0) {
		free(devname);
		return -1;
	}

	for (i = 0; i < LEN(Rules); i++) {
		regex = strdup(Rules[i].devregex);
		if (!regex)
			eprintf("strdup:");

		ret = regcomp(&match, regex, REG_EXTENDED);
		if (ret < 0)
			eprintf("regcomp:");

		ret = regexec(&match, devname, 1, &off, 0);
		regfree(&match);
		free(regex);

		if (ret || off.rm_so || off.rm_eo != strlen(devname))
			continue;

		if (Rules[i].cmd) {
			switch (Rules[i].cmd[0]) {
			case '@':
			case '$':
			case '*':
				fprintf(stderr, "Unsupported command '%s' for target '%s'\n",
					Rules[i].cmd, Rules[i].devregex);
				break;
			default:
				eprintf("Invalid command '%s'\n", Rules[i].cmd);
			}
		}

		dev = devpath;
		if (Rules[i].path) {
			switch (Rules[i].path[0]) {
			case '=':
				if (Rules[i].path[strlen(Rules[i].path) - 1] == '/') {
					snprintf(devpath, sizeof(devpath), "/dev/%s", &Rules[i].path[1]);
					if (mkpath(devpath, 0755) < 0)
						eprintf("mkdir %s:", devpath);
					strcat(devpath, devname);
				} else {
					snprintf(devpath, sizeof(devpath),
						 "/dev/%s", &Rules[i].path[1]);
				}
				break;
			case '>':
				fprintf(stderr, "Unsupported path '%s' for target '%s'\n",
					Rules[i].path, Rules[i].devregex);
				break;
			default:
				eprintf("Invalid path '%s'\n", Rules[i].path);
			}
		}

		ret = mknod(dev, Rules[i].mode | type, makedev(maj, min));
		if (ret < 0 && errno != EEXIST)
			eprintf("mknod %s:", dev);

		pw = getpwnam(Rules[i].user);
		if (!pw)
			eprintf("getpwnam %s:", Rules[i].user);

		gr = getgrnam(Rules[i].group);
		if (!gr)
			eprintf("getgrnam %s:", Rules[i].group);

		ret = chown(dev, pw->pw_uid, gr->gr_gid);
		if (ret < 0)
			eprintf("chown %s:", dev);
		break;
	}

	free(devname);
	return 0;
}

static void
sysrecurse(const char *path)
{
	char *cwd;

	recurse(path, sysrecurse);
	if (!strcmp(path, "dev")) {
		cwd = agetcwd();
		create_dev(cwd);
		free(cwd);
	}
}
