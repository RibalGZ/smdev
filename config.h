/* See LICENSE file for copyright and license details. */
struct {
        const char *devregex;
        const char *user;
        const char *group;
        int mode;
        const char *path;
        const char *cmd;
} Rules[] = {
        { "null",       "root", "root",  0666, NULL, "@chmod 666 $SMDEV"    },
        { "zero",       "root", "root",  0666, NULL, NULL                   },
        { "full",       "root", "root",  0666, NULL, NULL                   },
        { "random",     "root", "root",  0666, NULL, NULL                   },
        { "urandom",    "root", "root",  0666, NULL, NULL                   },
        { "mem",        "root", "root",  0640, NULL, NULL                   },
        { "kmem",       "root", "root",  0640, NULL, NULL                   },
        { "console",    "root", "tty",   0600, NULL, "@chmod 600 $SMDEV"    },
        { "ptmx",       "root", "tty",   0666, NULL, NULL                   },
        { "pty.*",      "root", "tty",   0660, NULL, NULL                   },
        { "tty",        "root", "tty",   0666, NULL, NULL                   },
        { "tty[0-9]*",  "root", "tty",   0660, NULL, NULL                   },
        { "vcsa*[0-9]*","root", "tty",   0660, NULL, NULL                   },
        { "sd[a-z].*",  "root", "disk",  0660, NULL, NULL                   },
        { "sr[0-9]*",   "root", "cdrom", 0660, NULL, "@ln -sf $SMDEV cdrom" },
        { ".*",         "root", "root",  0777, NULL, NULL                   },
};
