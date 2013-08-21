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

static int matchrule(const struct Rule *Rule, const char *devname);
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
matchrule(const struct Rule *Rule, const char *devname)
{
	regex_t match;
	regmatch_t off;
	int ret;

	ret = regcomp(&match, Rule->devregex, REG_EXTENDED);
	if (ret < 0)
		eprintf("regcomp:");

	ret = regexec(&match, devname, 1, &off, 0);
	regfree(&match);

	if (ret || off.rm_so || off.rm_eo != strlen(devname))
		return -1;
	return 0;
}

static int
create_dev(const char *path)
{
	struct Rule *Rule;
	struct passwd *pw;
	struct group *gr;
	char buf[BUFSIZ], *p;
	const char *devname;
	char origdevname[PATH_MAX];
	int maj, min, type;
	int i, ret;

	p = strrchr(path, '/');
	if (!p)
		return -1;
	p++;
	devname = p;
	snprintf(origdevname, sizeof(origdevname), "/dev/%s", devname);

	snprintf(buf, sizeof(buf), "%s/dev", path);
	ret = devtomajmin(buf, &maj, &min);
	if (ret < 0)
		return -1;

	snprintf(buf, sizeof(buf), "%d:%d", maj, min);
	type = devtype(buf);
	if (type < 0)
		return -1;

	if (chdir("/dev") < 0)
		eprintf("chdir /dev:");

	for (i = 0; i < LEN(Rules); i++) {
		Rule = &Rules[i];

		if (matchrule(Rule, devname) < 0)
			continue;

		if (Rule->path) {
			if (Rule->path[strlen(Rule->path) - 1] == '/') {
				umask(022);
				if (mkpath(&Rule->path[1], 0755) < 0)
					eprintf("mkdir %s:", &Rule->path[1]);
				umask(0);
				if (chdir(&Rule->path[1]) < 0)
					eprintf("chdir %s:", &Rule->path[1]);
			} else {
				devname = &Rule->path[1];
			}
		}

		/* Create the actual dev nodes */
		ret = mknod(devname, Rules[i].mode | type, makedev(maj, min));
		if (ret < 0 && errno != EEXIST)
			eprintf("mknod %s:", devname);
		pw = getpwnam(Rules[i].user);
		if (!pw)
			eprintf("getpwnam %s:", Rules[i].user);
		gr = getgrnam(Rules[i].group);
		if (!gr)
			eprintf("getgrnam %s:", Rules[i].group);
		ret = chown(devname, pw->pw_uid, gr->gr_gid);
		if (ret < 0)
			eprintf("chown %s:", devname);

		/* Create the symlinks */
		if (Rule->path && Rule->path[0] == '>') {
			snprintf(buf, sizeof(buf), "%s%s", &Rule->path[1], devname);
			if (symlink(buf, origdevname))
				eprintf("symlink %s -> %s:",
					origdevname, buf);
		}

		/* Export the needed environment */
		snprintf(buf, sizeof(buf), "SMDEV=%s", devname);
		if (putenv(buf) < 0)
			eprintf("putenv:");

		/* Run the command hooks for this rule */
		if (Rule->cmd) {
			switch (Rule->cmd[0]) {
			case '@':
				system(&Rule->cmd[1]);
				break;
			case '$':
			case '*':
			default:
				fprintf(stderr, "Unsupported command '%s' for target '%s'\n",
					Rule->cmd, Rule->devregex);
				break;
			}
		}

		break;
	}

	if (chdir(path) < 0)
		eprintf("chdir %s:", path);

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
