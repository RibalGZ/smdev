include config.mk

.POSIX:
.SUFFIXES: .c .o

LIB = \
	util/agetcwd.o      \
	util/apathmax.o     \
	util/dev.o          \
	util/eprintf.o      \
	util/estrtol.o      \
	util/recurse.o

SRC = smdev.c

OBJ = $(SRC:.c=.o) $(LIB)
BIN = $(SRC:.c=)
MAN = $(SRC:.c=.1)

all: options binlib

options:
	@echo mdev build options:
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "LDFLAGS  = $(LDFLAGS)"
	@echo "CC       = $(CC)"

binlib: util.a
	$(MAKE) bin

bin: $(BIN)

$(OBJ): util.h config.mk

.o:
	@echo LD $@
	@$(LD) -o $@ $< util.a $(LDFLAGS)

.c.o:
	@echo CC $<
	@$(CC) -c -o $@ $< $(CFLAGS)

util.a: $(LIB)
	@echo AR $@
	@$(AR) -r -c $@ $(LIB)
	@ranlib $@

install: all
	@echo installing executable to $(DESTDIR)$(PREFIX)/sbin
	@mkdir -p $(DESTDIR)$(PREFIX)/sbin
	@cp -f $(BIN) $(DESTDIR)$(PREFIX)/sbin
	@cd $(DESTDIR)$(PREFIX)/sbin && chmod 755 $(BIN)

uninstall:
	@echo removing executable from $(DESTDIR)$(PREFIX)/sbin
	@cd $(DESTDIR)$(PREFIX)/sbin && rm -f $(BIN)

clean:
	@echo cleaning
	@rm -f $(BIN) $(OBJ) $(LIB) util.a
