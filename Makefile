# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.  This file is offered as-is,
# without any warranty.


# The package path prefix, if you want to install to another root, set DESTDIR to that root.
PREFIX ?= /usr
# The binary path excluding prefix.
BIN ?= /bin
# The library binary path excluding prefix.
LIBEXEC ?= /libexec
# The resource path excluding prefix.
DATA ?= /share
# The binary path including prefix.
BINDIR ?= $(PREFIX)$(BIN)
# The library binary path including prefix.
LIBEXECDIR ?= $(PREFIX)$(LIBEXEC)
# The resource path including prefix.
DATADIR ?= $(PREFIX)$(DATA)
# The license base path including prefix.
LICENSEDIR ?= $(DATADIR)/licenses


# The name of the package as it should be installed.
PKGNAME ?= daemond


OPTIMISE = -Og -g
STD = gnu99
LFLAGS = -lrt
WARN = -Wall -Wextra -pedantic -Wdouble-promotion -Wformat=2 -Winit-self -Wmissing-include-dirs      \
       -Wtrampolines -Wmissing-prototypes -Wmissing-declarations -Wnested-externs                    \
       -Wno-variadic-macros -Wsync-nand -Wunsafe-loop-optimizations -Wcast-align                     \
       -Wdeclaration-after-statement -Wundef -Wbad-function-cast -Wwrite-strings -Wlogical-op        \
       -Wstrict-prototypes -Wold-style-definition -Wpacked -Wvector-operation-performance            \
       -Wunsuffixed-float-constants -Wsuggest-attribute=const -Wsuggest-attribute=noreturn           \
       -Wsuggest-attribute=format -Wnormalized=nfkc -fstrict-aliasing -fipa-pure-const -ftree-vrp    \
       -fstack-usage -funsafe-loop-optimizations -Wshadow -Wredundant-decls -Winline -Wcast-qual     \
       -Wsign-conversion -Wstrict-overflow=5 -Wconversion -Wsuggest-attribute=pure -Wswitch-default  \
       -Wstrict-aliasing=1 -fstrict-overflow -Wfloat-equal -Waggregate-return
FLAGS = $(OPTIMISE) -std=$(STD) $(LFLAGS) $(WARN) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS)



# Build rules.

.PHONY: all
all: bin/daemond bin/daemond-resurrectd bin/start-daemond


bin/%: src/%.c src/*.h
	@mkdir -p bin
	$(CC) $(FLAGS) -o $@ $<
	-@rm $*.su


# Clean rules.

.PHONY: clean
clean:
	-rm -rf obj bin *.su

