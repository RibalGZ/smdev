/* See LICENSE file for copyright and license details. */
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "mkpath.h"
#include "util.h"

enum action {
	ADD_ACTION,
	REMOVE_ACTION,
	UNKNOWN_ACTION
};

struct Event {
	int min;
	int maj;
	enum action action;
	char *devpath;
	char *devname;
	struct Rule *Rule;
};

static struct pregentry {
	regex_t preg;
	int cached;
} pregcache[LEN(Rules)];

static int dohotplug(void);
static int matchrule(int ruleidx, char *devname);
static void runrule(struct Rule *Rule);
static void parsepath(struct Rule *Rule, char *devpath, size_t sz,
		      char *devname);
static int createdev(struct Event *ev);
static int doevent(struct Event *ev);
static int craftev(struct Event *ev, enum action action,
		   char *sysfspath);
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
	int i;

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
			eprintf("Environment not set up correctly for hotplugging\n");

	for (i = 0; i < LEN(pregcache); i++)
		if (pregcache[i].cached)
			regfree(&pregcache[i].preg);

	return 0;
}

static enum action
mapaction(const char *action)
{
	if (!strcmp(action, "add"))
		return ADD_ACTION;
	if (!strcmp(action, "remove"))
		return REMOVE_ACTION;
	return UNKNOWN_ACTION;
}

static int
dohotplug(void)
{
	char *min, *maj;
	char *action;
	struct Event ev;

	min = getenv("MINOR");
	maj = getenv("MAJOR");
	action = getenv("ACTION");
	ev.devpath = getenv("DEVPATH");
	ev.devname = getenv("DEVNAME");
	if (!min || !maj || !action || !ev.devpath ||
	    !ev.devname)
		return -1;

	ev.min = estrtol(min, 10);
	ev.maj = estrtol(maj, 10);
	ev.action = mapaction(action);

	return doevent(&ev);
}

static int
matchrule(int ruleidx, char *devname)
{
	struct Rule *Rule = &Rules[ruleidx];
	regex_t *match;
	regmatch_t off;
	int ret;

	if (!pregcache[ruleidx].cached) {
		ret = regcomp(&pregcache[ruleidx].preg,
			      Rule->devregex, REG_EXTENDED);
		if (ret < 0)
			eprintf("regcomp:");
		pregcache[ruleidx].cached = 1;
	}
	match = &pregcache[ruleidx].preg;
	ret = regexec(match, devname, 1, &off, 0);
	if (ret == REG_NOMATCH || off.rm_so ||
	    off.rm_eo != strlen(devname))
		return -1;
	return 0;
}

static void
runrule(struct Rule *Rule)
{
	if (!Rule->cmd)
		return;

	system(&Rule->cmd[1]);
}

static void
parsepath(struct Rule *Rule, char *devpath, size_t sz,
	  char *devname)
{
	char buf[BUFSIZ], *p;
	char *dirc, *basec;

	if (Rule->path[0] != '=' && Rule->path[0] != '>')
		eprintf("Invalid path '%s'\n", Rule->path);

	p = strchr(&Rule->path[1], '/');
	if (p) {
		if (Rule->path[strlen(Rule->path) - 1] == '/') {
			snprintf(devpath, sz, "/dev/%s%s",
				 &Rule->path[1], devname);
		} else {
			dirc = strdup(&Rule->path[1]);
			if (!dirc)
				eprintf("strdup:");
			snprintf(buf, sizeof(buf), "/dev/%s", dirname(dirc));
			free(dirc);
			basec = strdup(&Rule->path[1]);
			if (!basec)
				eprintf("strdup:");
			strlcpy(devname, basename(basec), sizeof(devname));
			free(basec);
			snprintf(devpath, sz, "%s/%s",
				 buf, devname);
		}
	} else {
		strlcpy(devname, &Rule->path[1], sizeof(devname));
		snprintf(devpath, sz, "/dev/%s", devname);
	}
}

static int
createdev(struct Event *ev)
{
	struct Rule *Rule;
	struct passwd *pw;
	struct group *gr;
	char *dirc;
	char devpath[PATH_MAX];
	char devname[PATH_MAX];
	char buf[BUFSIZ];
	int type;

	Rule = ev->Rule;

	snprintf(buf, sizeof(buf), "%d:%d", ev->maj, ev->min);
	type = devtype(buf);
	if (type < 0)
		return -1;

	strlcpy(devname, ev->devname, sizeof(devname));
	snprintf(devpath, sizeof(devpath), "/dev/%s", devname);

	if (Rule->path) {
		parsepath(Rule, devpath, sizeof(devpath),
			  devname);
		dirc = strdup(devpath);
		if (!dirc)
			eprintf("strdup:");
		strlcpy(buf, dirname(dirc), sizeof(buf));
		free(dirc);
		umask(022);
		if (mkpath(buf, 0755) < 0)
			eprintf("mkdir %s:", buf);
		umask(0);
	}

	/* Create the actual dev nodes */
	if (mknod(devpath, Rule->mode | type,
		  makedev(ev->maj, ev->min)) < 0 &&
	    errno != EEXIST)
		eprintf("mknod %s:", devpath);

	errno = 0;
	pw = getpwnam(Rule->user);
	if (errno)
		eprintf("getpwnam %s:", Rule->user);
	else if (!pw)
		enprintf(1, "getpwnam %s: no such user\n",
			 Rule->user);

	errno = 0;
	gr = getgrnam(Rule->group);
	if (errno)
		eprintf("getgrnam %s:", Rule->group);
	else if (!gr)
		enprintf(1, "getgrnam %s: no such group\n",
			 Rule->group);

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

	runrule(Rule);

	return 0;
}

static int
doevent(struct Event *ev)
{
	int i;

	for (i = 0; i < LEN(Rules); i++) {
		if (matchrule(i, ev->devname) < 0)
			continue;
		ev->Rule = &Rules[i];
		switch (ev->action) {
		case ADD_ACTION:
			return createdev(ev);
		default:
			break;
		}
		break;
	}
	return 0;
}

static int
craftev(struct Event *ev, enum action action, char *sysfspath)
{
	char path[PATH_MAX];

	ev->action = action;
	ev->devpath = sysfspath + strlen("/sys");
	ev->devname = basename(sysfspath);
	snprintf(path, sizeof(path), "/sys%s/dev",
		 ev->devpath);
	if (devtomajmin(path, &ev->maj, &ev->min) < 0)
		return -1;
	return 0;
}

static void
populatedev(const char *path)
{
	char *cwd;
	struct Event ev;

	recurse(path, populatedev);
	if (!strcmp(path, "dev")) {
		cwd = agetcwd();
		if (!craftev(&ev, ADD_ACTION, cwd))
			doevent(&ev);
		free(cwd);
	}
}
