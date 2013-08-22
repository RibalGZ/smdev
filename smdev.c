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

struct Event {
	int min;
	int maj;
	char *action;
	char *devpath;
	char *devname;
};

static int dohotplug(void);
static int matchrule(struct Rule *Rule, char *devname);
static int createdev(struct Event *ev);
static void populatedev(const char *path);

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

	umask(0);
	if (sflag)
		recurse("/sys/devices", populatedev);
	else
		if (dohotplug() < 0)
			return 1;
	return 0;
}

static int
dohotplug(void)
{
	char *min, *maj;
	struct Event ev;

	min = getenv("MINOR");
	maj = getenv("MAJOR");
	ev.action = getenv("ACTION");
	ev.devpath = getenv("DEVPATH");
	ev.devname = getenv("DEVNAME");
	if (!min || !maj || !ev.action || !ev.devpath ||
	    !ev.devname)
		return -1;

	ev.min = estrtol(min, 10);
	ev.maj = estrtol(maj, 10);

	if (!strcmp(ev.action, "add"))
		return createdev(&ev);
	else
		eprintf("Unsupported action '%s'\n", ev.action);

	return 0;
}

static int
matchrule(struct Rule *Rule, char *devname)
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
createdev(struct Event *ev)
{
	struct Rule *Rule;
	struct passwd *pw;
	struct group *gr;
	char devpath[PATH_MAX], *devname;
	char buf[BUFSIZ];
	int type;
	int i;

	snprintf(buf, sizeof(buf), "%d:%d", ev->maj, ev->min);
	type = devtype(buf);
	if (type < 0)
		return -1;

	devname = ev->devname;
	snprintf(devpath, sizeof(devpath), "/dev/%s", devname);
	for (i = 0; i < LEN(Rules); i++) {
		Rule = &Rules[i];

		if (matchrule(Rule, devname) < 0)
			continue;

		if (Rule->path) {
			if (Rule->path[0] != '=' && Rule->path[0] != '>')
				eprintf("Invalid path '%s'\n", Rule->path);
			if (Rule->path[strlen(Rule->path) - 1] == '/') {
				snprintf(buf, sizeof(buf), "/dev/%s", &Rule->path[1]);
				umask(022);
				if (mkpath(buf, 0755) < 0)
					eprintf("mkdir %s:", buf);
				umask(0);
				snprintf(devpath, sizeof(devpath), "/dev/%s%s",
					 &Rule->path[1], devname);
			} else {
				devname = &Rule->path[1];
				snprintf(devpath, sizeof(devpath), "/dev/%s", devname);
			}
		}

		/* Create the actual dev nodes */
		if (mknod(devpath, Rules[i].mode | type,
			  makedev(ev->maj, ev->min)) < 0 &&
		    errno != EEXIST)
			eprintf("mknod %s:", devpath);

		errno = 0;
		pw = getpwnam(Rules[i].user);
		if (errno)
			eprintf("getpwnam %s:", Rules[i].user);
		else if (!pw)
			enprintf(1, "getpwnam %s: no such user\n",
				 Rules[i].user);

		errno = 0;
		gr = getgrnam(Rules[i].group);
		if (errno)
			eprintf("getgrnam %s:", Rules[i].group);
		else if (!gr)
			enprintf(1, "getgrnam %s: no such group\n",
				 Rules[i].group);

		if (chown(devpath, pw->pw_uid, gr->gr_gid) < 0)
			eprintf("chown %s:", devpath);

		/* Create symlinks */
		if (Rule->path && Rule->path[0] == '>') {
			snprintf(buf, sizeof(buf), "/dev/%s", ev->devname);
			if (symlink(devpath, buf))
				eprintf("symlink %s -> %s:",
					ev->devname, devpath);
		}

		snprintf(buf, sizeof(buf), "SMDEV=%s", devpath);
		if (putenv(buf) < 0)
			eprintf("putenv:");

		/* Run command hooks for this rule */
		if (Rule->cmd) {
			switch (Rule->cmd[0]) {
			case '*':
			case '@':
				system(&Rule->cmd[1]);
				break;
			case '$':
				break;
			default:
				eprintf("Invalid command '%s'\n", Rule->cmd);
			}
		}

		break;
	}

	return 0;
}

static void
populatedev(const char *path)
{
	char tmppath[PATH_MAX];
	char *cwd;
	struct Event ev;

	recurse(path, populatedev);
	if (!strcmp(path, "dev")) {
		cwd = agetcwd();
		ev.action = "add";
		ev.devpath = cwd + strlen("/sys");
		ev.devname = basename(cwd);
		snprintf(tmppath, sizeof(tmppath), "/sys%s/dev",
			 ev.devpath);
		if (devtomajmin(tmppath, &ev.maj, &ev.min) < 0)
			return;
		createdev(&ev);
		free(cwd);
	}
}
