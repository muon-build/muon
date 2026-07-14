#!/bin/sh
# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-FileCopyrightText: kzc <kzc@users.noreply.github.com>
# SPDX-License-Identifier: GPL-3.0-only

set -e

BUILD=t
TCC=tcc
export CC=$TCC
export NINJA=samu  # Avoid ninja on msys2 - incompatible with muon.
DEST_INC=$BUILD/include
MINGW=$MINGW_PREFIX
[ -d "$MINGW" ] && echo "Using MINGW_PREFIX=$MINGW" || "MINGW_PREFIX directory $MINGW not found."
export CFLAGS="$CFLAGS -I $(cygpath -wam $DEST_INC)"
rm -rf $BUILD && \
mkdir -p $BUILD && \
mkdir -p $DEST_INC/sys && \
touch    $DEST_INC/sys/socket.h && \
/usr/bin/echo -e "#ifndef _INC_VADEFS\n#define _INC_VADEFS\n#include <stdarg.h> // tcc\n#endif" > $DEST_INC/vadefs.h && \
cp -v -rp $MINGW/include/curl            $DEST_INC/curl && \
cp -v     $MINGW/include/archive.h       $DEST_INC/ && \
cp -v     $MINGW/include/archive_entry.h $DEST_INC/ && \
cp -v     $MINGW/include/zlib.h          $DEST_INC/ && \
cp -v     $MINGW/include/zconf.h         $DEST_INC/ && \
./bootstrap.sh $BUILD && \
./$BUILD/muon-bootstrap build -Ddisable-test-languages=cpp,objc $BUILD && \
./$BUILD/muon -C $BUILD test -v -R -o term
