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

struct event {
	int min;
	int maj;
	enum action action;
	char *devpath;
	char *devname;
	struct rule *rule;
};

/* Simple cache for regcomp() */
static struct pregentry {
	regex_t preg;
	int cached;
} pregcache[LEN(rules)];

static int dohotplug(void);
static int matchrule(int ruleidx, char *devname);
static void runrule(struct rule *rule);
static void parsepath(struct rule *rule, char *devpath,
		      size_t devpathsz, char *devname);
static int createdev(struct event *ev);
static int doevent(struct event *ev);
static int craftev(struct event *ev, enum action action,
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

/* Handle hotplugging events */
static int
dohotplug(void)
{
	char *min, *maj;
	char *action;
	struct event ev;

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

/*
 * `ruleidx' indexes into the rules[] table.  We assume
 * pregcache[] is mapped 1-1 with the rules[] table.
 */
static int
matchrule(int ruleidx, char *devname)
{
	struct rule *rule = &rules[ruleidx];
	regex_t *match;
	regmatch_t off;
	int ret;

	if (!pregcache[ruleidx].cached) {
		ret = regcomp(&pregcache[ruleidx].preg,
			      rule->devregex, REG_EXTENDED);
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
runrule(struct rule *rule)
{
	if (!rule->cmd)
		return;

	system(&rule->cmd[1]);
}

/*
 * Parse rule->path[] and set `devpath' to the absolute
 * path of the device node.  If we have to rename the
 * device node then set `devname' to the new device name.
 */
static void
parsepath(struct rule *rule, char *devpath, size_t devpathsz,
	  char *devname)
{
	char buf[BUFSIZ], *p;
	char *dirc, *basec;

	if (rule->path[0] != '=' && rule->path[0] != '>')
		eprintf("Invalid path '%s'\n", rule->path);

	p = strchr(&rule->path[1], '/');
	if (p) {
		if (rule->path[strlen(rule->path) - 1] == '/') {
			snprintf(devpath, devpathsz, "/dev/%s%s",
				 &rule->path[1], devname);
			return;
		}
		dirc = strdup(&rule->path[1]);
		if (!dirc)
			eprintf("strdup:");
		snprintf(buf, sizeof(buf), "/dev/%s", dirname(dirc));
		free(dirc);
		basec = strdup(&rule->path[1]);
		if (!basec)
			eprintf("strdup:");
		strlcpy(devname, basename(basec), sizeof(devname));
		free(basec);
		snprintf(devpath, devpathsz, "%s/%s",
			 buf, devname);
	} else {
		strlcpy(devname, &rule->path[1], sizeof(devname));
		snprintf(devpath, devpathsz, "/dev/%s", devname);
	}
}

static int
createdev(struct event *ev)
{
	struct rule *rule;
	struct passwd *pw;
	struct group *gr;
	char *dirc;
	char devpath[PATH_MAX];
	char devname[PATH_MAX];
	char buf[BUFSIZ];
	int type;

	rule = ev->rule;

	snprintf(buf, sizeof(buf), "%d:%d", ev->maj, ev->min);
	type = devtype(buf);
	if (type < 0)
		return -1;

	strlcpy(devname, ev->devname, sizeof(devname));
	snprintf(devpath, sizeof(devpath), "/dev/%s", devname);

	/* Parse path and create the directory tree */
	if (rule->path) {
		parsepath(rule, devpath, sizeof(devpath),
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

	/* Create the actual dev node */
	if (mknod(devpath, rule->mode | type,
		  makedev(ev->maj, ev->min)) < 0 &&
	    errno != EEXIST)
		eprintf("mknod %s:", devpath);

	errno = 0;
	pw = getpwnam(rule->user);
	if (errno)
		eprintf("getpwnam %s:", rule->user);
	else if (!pw)
		enprintf(1, "getpwnam %s: no such user\n",
			 rule->user);

	errno = 0;
	gr = getgrnam(rule->group);
	if (errno)
		eprintf("getgrnam %s:", rule->group);
	else if (!gr)
		enprintf(1, "getgrnam %s: no such group\n",
			 rule->group);

	if (chown(devpath, pw->pw_uid, gr->gr_gid) < 0)
		eprintf("chown %s:", devpath);

	/* Create symlink */
	if (rule->path && rule->path[0] == '>') {
		snprintf(buf, sizeof(buf), "/dev/%s", ev->devname);
		if (symlink(devpath, buf))
			eprintf("symlink %s -> %s:",
				ev->devname, devpath);
	}

	snprintf(buf, sizeof(buf), "SMDEV=%s", devpath);
	if (putenv(buf) < 0)
		eprintf("putenv:");

	runrule(rule);

	return 0;
}

/* Event dispatcher */
static int
doevent(struct event *ev)
{
	int i;

	for (i = 0; i < LEN(rules); i++) {
		if (matchrule(i, ev->devname) < 0)
			continue;
		ev->rule = &rules[i];
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

/* Craft a fake event so the rest of the code can cope */
static int
craftev(struct event *ev, enum action action, char *sysfspath)
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
	struct event ev;

	recurse(path, populatedev);
	if (!strcmp(path, "dev")) {
		cwd = agetcwd();
		if (!craftev(&ev, ADD_ACTION, cwd))
			doevent(&ev);
		free(cwd);
	}
}
