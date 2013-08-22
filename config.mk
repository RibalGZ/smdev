# smdev version
VERSION = 0.0

# paths
PREFIX = /usr/local

#CC = gcc
#CC = musl-gcc
LD = $(CC)
CPPFLAGS = -D_BSD_SOURCE -D_GNU_SOURCE
CFLAGS   = -g -ansi -Wall -pedantic $(CPPFLAGS)
LDFLAGS  = -g
