#!/usr/bin/make -f

# Plain Text Archive Tool Installation Script
# Written in 2013 by Jordan Vaughan
#
# To the extent possible under law, the author(s) have dedicated all copyright
# and related and neighboring rights to this software to the public domain
# worldwide. This software is distributed without any warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication along
# with this software. If not, see
# <http://creativecommons.org/publicdomain/zero/1.0/>.


# CONFIGURATION VARIABLES
# Feel free to set these at the command line.

# the name of the generated binary
BINFILE = ptar

# compilation flags
CFLAGS ?= -D_XOPEN_SOURCE=700 -D_BSD_SOURCE -O2 -g -Wall -Werror
CPPFLAGS ?=

# the installation program (install(1))
INSTALL ?= install

# the user and group for the installed binary
INSTALL_USER ?= root
INSTALL_GROUP ?= root

# the installation prefix
PREFIXDIR ?= /usr

# the directory that will hold the installed binary
BINDIR ?= $(PREFIXDIR)/bin

# the name of the ptar archive that the 'dist' target builds
DISTARCHIVE = $(BINFILE).ptar


# INTERNAL VARIABLES
# Do not modify these from the command line.
SRC = ptar.c
OBJ = $(SRC:.c=.o)
INSTALL_PROGRAM = $(INSTALL) -p -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 755 -s
DISTCONTENTS = COPYING README.md FORMAT.md $(BINFILE)


# TARGETS
all: options $(BINFILE)

options:
	@echo "ptar build options:"
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
	@echo "CC      = $(CC)"
	@echo

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $<

$(BINFILE): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f $(BINFILE) $(OBJ) $(DISTARCHIVE)

install: $(BINFILE)
	$(INSTALL_PROGRAM) $(BINFILE) $(BINDIR)/$(BINFILE)

dist: $(DISTARCHIVE)

$(DISTARCHIVE): all
	$(PWD)/$(BINFILE) c $(DISTCONTENTS) >$@

