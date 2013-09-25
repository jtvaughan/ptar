#!/bin/sh

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

if [ -z "$CC" ]; then
  echo "ERROR: The environment CC is not defined.  Please define it before running this installation script." >&2
  exit 1
fi

if [ $# -eq 0 ]; then
  echo "ERROR: No installation directory specified.  Please specify it as the first command-line argument for this script." >&2
  exit 1
fi
if [ ! -d "$1" ]; then
  echo "ERROR: $1 is not a directory." >&2
  exit 1
fi

set -x
CFLAGS=${CFLAGS:--flto -O3 -g0}
$CC $CPPFLAGS $CFLAGS -D_XOPEN_SOURCE=700 -D_BSD_SOURCE -I. -o $1/ptar ptar.c
