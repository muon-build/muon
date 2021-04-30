outdir=${OUTDIR:-.build}
srcdir=${SRCDIR:-$(dirname "$0")}
AR=${AR:-ar}
AS=${AS:-as}
CC=${CC:-cc}
CFLAGS=${CFLAGS:-}
LDFLAGS=${LDFLAGS:-}
LD=${LD:-ld}
QBE=${QBE:-qbe}
SCDOC=${SCDOC:-scdoc}

for arg
do
	case "$arg" in
		--bindir=*)
			BINDIR=${arg#*=}
			;;
		--libdir=*)
			LIBDIR=${arg#*=}
			;;
		--mandir=*)
			MANDIR=${arg#*=}
			;;
		--prefix=*)
			PREFIX=${arg#*=}
			if [ "$PREFIX" = "/usr" ]
			then
				SYSCONFDIR=/etc
			fi
			;;
		--sharedir=*)
			SHAREDIR=${arg#*=}
			;;
		--sysconfdir=*)
			SYSCONFDIR=${arg#*=}
			;;
		*)
			option "$arg"
			;;
	esac
done

subdir() {
	eval ". $srcdir/$1/configure"
}

genrules() {
	target="$1"
	shift
	printf '# Begin generated rules for %s\n' "$target"
	for file in "$@"
	do
		ext="${file#*.}"
		file="${file%.*}"
		deps=
		printf '%s.o: %s.%s%s\n' "$file" "$file" "$ext" "$deps"
	done
	printf '%s_objects=\\\n' "$target"
	n=0
	for file in "$@"
	do
		file="${file%.*}"
		n=$((n+1))
		if [ $n -eq $# ]
		then
			printf '\t%s.o\n' "$file"
		else
			printf '\t%s.o \\\n' "$file"
		fi
	done
	printf '# End generated rules for %s\n' "$target"
}

append_cflags() {
	for flag
	do
		CFLAGS="$(printf '%s \\\n\t%s' "$CFLAGS" "$flag")"
	done
}

test_cflags() {
	[ ! -e "$outdir"/check.c ] && cat <<-EOF > "$outdir"/check.c
	int main(void) { return 0; }
	EOF
	werror=""
	case "$CFLAGS" in
		*-Werror*)
			werror="-Werror"
			;;
	esac
	if $CC $werror "$@" -o /dev/null "$outdir"/check.c >/dev/null 2>&1
	then
		append_cflags "$@"
	else
		return 1
	fi
}

find_library() {
	name="$1"
	pc="$2"
	printf "Checking for %s... " "$name"
	if ! pkg-config "$pc" 2>/dev/null
	then
		printf "NOT FOUND\n"
		printf "Tried pkg-config %s\n" "$pc"
		return 1
	fi
	printf "OK\n"
	CFLAGS="$CFLAGS $(pkg-config --cflags "$pc")"
	LIBS="$LIBS $(pkg-config --libs "$pc")"
}

docs() { true; }

run_configure() {
	mkdir -p $outdir

	for flag in -g -std=c11 -D_XOPEN_SOURCE=700 \
		-Wall -Wextra -Werror -pedantic \
		-Wno-unused-parameter
	do
		printf "Checking for %s... " "$flag"
		if test_cflags "$flag"
		then
			echo yes
		else
			echo no
		fi
	done

	append_cflags "-DVERSION='\"$1\"'"

	printf "Creating $outdir/config.mk... "
	cat <<-EOF > "$outdir"/config.mk
	CC=$CC
	AS=$AS
	LD=$LD
	QBE=$QBE
	SCDOC=$SCDOC
	LIBS=$LIBS
	PREFIX=${PREFIX:-/usr/local}
	OUTDIR=${outdir}
	SRCDIR=${srcdir}
	BINDIR?=${BINDIR:-\$(PREFIX)/bin}
	SHAREDIR?=${SHAREDIR:-\$(PREFIX)/share}
	SYSCONFDIR?=${SYSCONFDIR:-\$(PREFIX)/etc}
	LIBDIR?=${LIBDIR:-\$(PREFIX)/lib}
	MANDIR?=${MANDIR:-\$(PREFIX)/share/man}
	VARLIBDIR?=${MANDIR:-\$(PREFIX)/var/lib}
	CACHE=\$(OUTDIR)/cache
	CFLAGS=${CFLAGS}
	CFLAGS+=-Iinclude -I\$(OUTDIR)
	CFLAGS+=-DPREFIX='"\$(PREFIX)"'
	CFLAGS+=-DLIBDIR='"\$(LIBDIR)"'
	CFLAGS+=-DVARLIBDIR='"\$(VARLIBDIR)"'
	CFLAGS+=-DSYSCONFDIR='"\$(SYSCONFDIR)"'
	LDFLAGS=${LDFLAGS}

	all: ${all}
	EOF

	for target in $all
	do
		$target >>"$outdir"/config.mk
	done
	echo done

	if [ "$srcdir" = "." ]
	then
		return
	fi

	populate() (
		path="$1"
		mkdir -p "${path#$srcdir/}"
		fullpath() ( cd "$1" && pwd )
		for orig in "$path"/*
		do
			link="${orig#$srcdir/}"
			if [ -d "$orig" ]
			then
				mkdir -p $link
				populate "$orig"
			elif [ -f "$orig" ]
			then
				ln -sf "$(fullpath "$path")"/"$(basename "$orig")" "$link"
			fi
		done
	)

	printf "Populating build dir... "
	populate "$srcdir/doc"
	populate "$srcdir/include"
	populate "$srcdir/src"
	ln -sf "$srcdir"/Makefile ./
	echo done
}
