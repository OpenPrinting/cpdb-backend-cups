#!/bin/sh
# ci/ci-setup.sh
#
# CI helper for building the CPDB CUPS backend against several CUPS releases on
# both native and QEMU-emulated runners.  The same source compiles against
# CUPS 2.4.x, 2.5.x and 3.x; this script provides each of those CUPS builds
# (plus a matching libcupsfilters and cpdb-libs) and then builds and tests the
# backend against it.
#
# cpdb-backend-cups depends on libcups, libcupsfilters AND cpdb-libs.  For the
# source-CUPS legs the distro libcupsfilters-dev (built against CUPS 2.4) is
# the wrong ABI, so this script builds libcupsfilters (and pdfio) from source
# against the active CUPS.  cpdb-libs is CUPS-independent but not packaged, so
# it is always built from @master.
#
# Subcommands:
#   deps                  install build dependencies
#   cups <kind>           provide libcups; <kind> is one of:
#                           system-2x    distro libcups2-dev  (CUPS 2.4.x)
#                           source-2.5.x OpenPrinting/cups@master    (CUPS 2.5.x)
#                           source-3.x   OpenPrinting/libcups@master (libcups3)
#   pdfio                 build/install pdfio (required by libcupsfilters)
#   libcupsfilters <kind> provide libcupsfilters matching the active CUPS:
#                           system-2x    distro libcupsfilters-dev
#                           source-*     OpenPrinting/libcupsfilters@master
#   cpdb-libs             build/install cpdb-libs from @master
#   build-backend         autogen + configure + make + make check
#
# Environment knobs honoured by build-backend:
#   CUPS_KIND   the <kind> above (controls test XFAILs for source CUPS)
#   EMULATED    "1" when running under QEMU emulation (controls test XFAILs)
#
# Override knobs (optional):
#   LIBCUPSFILTERS_URL / LIBCUPSFILTERS_REF   source libcupsfilters build
#   CPDB_LIBS_URL      / CPDB_LIBS_REF        source cpdb-libs build
#
# The script runs as root inside emulation containers and via sudo on native
# runners; it detects which automatically.
set -eu

# Classify a failure so CI (and humans) can tell an upstream-dependency
# breakage apart from a real cpdb-backend-cups failure.  Each invocation
# handles exactly one subcommand, so $1 identifies which side failed:
# providing the CUPS / libcupsfilters / cpdb-libs stack (built from @master on
# the source-* legs, an unpinned moving target) vs building/testing the
# backend itself.  The source-* matrix legs are non-blocking precisely because
# of the former class of transient upstream breakage.
_subcmd="${1:-}"
_classify_failure() {
	rc=$?
	[ "$rc" -eq 0 ] && exit 0
	case "$_subcmd" in
		cups|pdfio|libcupsfilters|cpdb-libs)
			echo "::error::UPSTREAM-DEP-FAILED: '$_subcmd' failed while building the CUPS/libcupsfilters/cpdb-libs stack (from @master on the source-* legs). This is an upstream-dependency breakage, not a cpdb-backend-cups bug." ;;
		build-backend)
			echo "::error::BACKEND-FAILED: cpdb-backend-cups build/test failed." ;;
	esac
	exit "$rc"
}
trap _classify_failure EXIT

PDFIO_VER=1.6.4
LIBCUPSFILTERS_URL="${LIBCUPSFILTERS_URL:-https://github.com/OpenPrinting/libcupsfilters.git}"
LIBCUPSFILTERS_REF="${LIBCUPSFILTERS_REF:-master}"
CPDB_LIBS_URL="${CPDB_LIBS_URL:-https://github.com/OpenPrinting/cpdb-libs.git}"
CPDB_LIBS_REF="${CPDB_LIBS_REF:-master}"

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"

# Make apt completely non-interactive.  Native GitHub runners ship
# needrestart, whose service-restart prompt otherwise hangs the job forever;
# the emulated containers do not have it, which is why only the native legs
# stalled.
export DEBIAN_FRONTEND=noninteractive
export NEEDRESTART_MODE=a
export NEEDRESTART_SUSPEND=1

# Source-built CUPS / libcupsfilters / cpdb-libs install their .pc files under
# $prefix/lib[/<multiarch>]/pkgconfig; make sure pkg-config (and therefore the
# backend's configure) can find them.
ma=$(gcc -dumpmachine 2>/dev/null || echo "")
PKG_CONFIG_PATH="/usr/lib/pkgconfig${ma:+:/usr/lib/$ma/pkgconfig}:/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PKG_CONFIG_PATH

apt_install() {
	$SUDO apt-get update --fix-missing -y
	$SUDO apt-get install -y "$@"
}

cmd_deps() {
	# Union of the backend's own build deps and the deps needed to build
	# libcupsfilters, pdfio and cpdb-libs from source on the source-CUPS legs.
	apt_install \
		build-essential autoconf automake libtool libtool-bin pkg-config \
		gettext autopoint autotools-dev cmake git wget tar make gcc g++ \
		file dbus \
		libglib2.0-dev libdbus-1-dev \
		libavahi-client-dev libssl-dev libpam-dev libusb-1.0-0-dev \
		zlib1g-dev libqpdf-dev libexif-dev liblcms2-dev libfontconfig1-dev \
		libfreetype6-dev libcairo2-dev libjpeg-dev libpng-dev libtiff-dev \
		libjxl-dev libpoppler-dev libpoppler-cpp-dev \
		libopenjp2-7-dev mupdf-tools poppler-utils ghostscript
	# Never let pre-shipped libcupsfilters / cpdb-libs shadow the builds under
	# test.
	$SUDO apt-get remove -y libcupsfilters-dev libcpdb-dev || true
}

# build_autoconf <url> <ref> <submodule-flag> [configure-args...]
build_autoconf() {
	url="$1"; ref="$2"; sub="$3"; shift 3
	echo "ci-setup: building $url @ $ref"
	src="$(mktemp -d)"
	git clone --depth 1 --branch "$ref" $sub "$url" "$src"
	( cd "$src"
	  [ -x ./configure ] || ./autogen.sh
	  ./configure --prefix=/usr "$@" || ./configure --prefix=/usr
	  make -j"$(nproc)"
	  $SUDO make install )
	$SUDO ldconfig || true
}

cmd_cups() {
	kind="$1"
	case "$kind" in
		system-2x)
			# libcups2-dev for headers/link; cups-daemon for the private
			# cupsd the print-through test spins up.
			apt_install cups cups-daemon libcups2-dev
			;;
		source-2.5.x)
			# CUPS 2.5 (OpenPrinting/cups master) ships cups.pc and installs
			# its own cupsd; force the multiarch libdir so libcups lands on
			# the default linker search path (configure otherwise picks
			# /usr/lib64 on 64-bit hosts).
			build_autoconf https://github.com/OpenPrinting/cups.git master "" \
				--disable-systemd ${ma:+--libdir=/usr/lib/$ma}
			;;
		source-3.x)
			# libcups3 is the client library only (no cupsd); the backend must
			# still compile and link against it.  The print-through test skips
			# gracefully when no cupsd is present.
			build_autoconf https://github.com/OpenPrinting/libcups.git master \
				"--recurse-submodules"
			;;
		*)
			echo "ci-setup: unknown cups kind: $kind" >&2; exit 2 ;;
	esac
}

cmd_pdfio() {
	echo "ci-setup: building pdfio $PDFIO_VER"
	src="$(mktemp -d)"
	( cd "$src"
	  wget -q "https://github.com/michaelrsweet/pdfio/releases/download/v$PDFIO_VER/pdfio-$PDFIO_VER.tar.gz"
	  tar -xzf "pdfio-$PDFIO_VER.tar.gz"
	  cd "pdfio-$PDFIO_VER"
	  ./configure --prefix=/usr --enable-shared
	  make all
	  $SUDO make install )
	$SUDO ldconfig || true
}

cmd_libcupsfilters() {
	kind="$1"
	case "$kind" in
		system-2x)
			apt_install libcupsfilters-dev
			;;
		source-*)
			# Build libcupsfilters against the CUPS installed above.  Its
			# configure auto-detects CUPS via pkg-config (cups3 / cups /
			# cups-config), so the same source matches every CUPS release.
			build_autoconf "$LIBCUPSFILTERS_URL" "$LIBCUPSFILTERS_REF" ""
			;;
		*)
			echo "ci-setup: unknown libcupsfilters kind: $kind" >&2; exit 2 ;;
	esac
}

cmd_cpdb_libs() {
	# cpdb-libs is CUPS-independent and not packaged; always build @master.
	build_autoconf "$CPDB_LIBS_URL" "$CPDB_LIBS_REF" "" --enable-shared
}

cmd_build() {
	./autogen.sh
	./configure
	make -j"$(nproc)" V=1

	# Report which CUPS the configure step actually selected.
	echo "ci-setup: configured against:"
	grep -E "CUPS library|libcups:|cups-config" config.log 2>/dev/null || true

	# The print-through test (run-tests.sh) needs a private cupsd and the
	# CUPS 2.x cups-config/serverbin layout; it exits 77 (SKIP) when those are
	# absent (e.g. the libcups3 leg), so make check stays green there.
	make check V=1 VERBOSE=1 \
		|| { test -f test-suite.log && cat test-suite.log; \
		     cat src/*.log 2>/dev/null; exit 1; }
}

case "${1:-}" in
	deps)            cmd_deps ;;
	cups)            shift; cmd_cups "$@" ;;
	pdfio)           cmd_pdfio ;;
	libcupsfilters)  shift; cmd_libcupsfilters "$@" ;;
	cpdb-libs)       cmd_cpdb_libs ;;
	build-backend)   cmd_build ;;
	*)
		echo "usage: ci-setup.sh {deps | cups <kind> | pdfio | libcupsfilters <kind> | cpdb-libs | build-backend}" >&2
		exit 2 ;;
esac
