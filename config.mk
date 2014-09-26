# smdev version
VERSION = 0.2.2

# paths
PREFIX = /usr/local

#CC = gcc
#CC = musl-gcc
LD = $(CC)
CPPFLAGS = -D_BSD_SOURCE -D_GNU_SOURCE
CFLAGS   = -g -std=c99 -Wall -pedantic $(CPPFLAGS)
LDFLAGS  = -g
