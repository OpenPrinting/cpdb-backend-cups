#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([cpdb-backend-cups], [2.0b7], [https://github.com/OpenPrinting/cpdb-backend-cups/issues/], [cpdb-backend-cups], [https://github.com/OpenPrinting/cpdb-backend-cups])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([src/print_backend_cups.c])
AM_INIT_AUTOMAKE([-Wall dist-xz dist-bzip2 foreign])

: ${CFLAGS=""}
# Check for a C compiler
AC_PROG_CC


# Checks for backend library
PKG_CHECK_MODULES([CPDB],[cpdb >= 2])
PKG_CHECK_MODULES([LIBCUPSFILTERS], [libcupsfilters >= 2])
PKG_CHECK_MODULES([GIO],[gio-2.0])
PKG_CHECK_MODULES([GIOUNIX],[gio-unix-2.0])
PKG_CHECK_MODULES([GLIB],[glib-2.0 >= 2.74])

# ====
# CUPS
# ====
# Discover libcups portably across CUPS 2.4.x, 2.5.x and 3.x.  The library and
# its pkg-config name differ between releases: CUPS 3.x ships cups3.pc and
# links -lcups3; CUPS 2.5 ships cups.pc and dropped cups-config; CUPS 2.4 still
# provides cups-config.  Set CUPS_CFLAGS / CUPS_LIBS from whichever is found so
# src/Makefile.am need not hard-code -lcups.  Also export the CUPS data /
# serverbin / serverroot / sbin dirs the print-through test consumes (they used
# to come from cups-config, which is absent on 2.5+).
AC_PATH_TOOL([PKGCONFIG], [pkg-config])
AC_MSG_CHECKING([for CUPS library v3.x])
AS_IF([test -n "$PKGCONFIG" && $PKGCONFIG --exists cups3], [
    CUPS_VERSION="$($PKGCONFIG --modversion cups3)"
    AC_MSG_RESULT([yes, v$CUPS_VERSION])
    CUPS_CFLAGS="$($PKGCONFIG --cflags cups3)"
    CUPS_LIBS="$($PKGCONFIG --libs cups3)"
    cups_prefix="$($PKGCONFIG --variable=prefix cups3)"
    CUPS_DATADIR="$($PKGCONFIG --variable=cups_datadir cups3)"
], [
    AC_MSG_RESULT([no])
    AC_MSG_CHECKING([for CUPS library v2.x via pkg-config])
    AS_IF([test -n "$PKGCONFIG" && $PKGCONFIG --exists cups], [
        CUPS_VERSION="$($PKGCONFIG --modversion cups)"
        AC_MSG_RESULT([yes, v$CUPS_VERSION])
        CUPS_CFLAGS="$($PKGCONFIG --cflags cups)"
        CUPS_LIBS="$($PKGCONFIG --libs cups)"
        cups_prefix="$($PKGCONFIG --variable=prefix cups)"
        CUPS_DATADIR="$($PKGCONFIG --variable=cups_datadir cups)"
    ], [
        AC_MSG_RESULT([no])
        AC_PATH_TOOL([CUPSCONFIG], [cups-config])
        AS_IF([test -n "$CUPSCONFIG"], [
            CUPS_VERSION="$($CUPSCONFIG --version)"
            AC_MSG_NOTICE([using CUPS v$CUPS_VERSION via cups-config])
            CUPS_CFLAGS="$($CUPSCONFIG --cflags)"
            CUPS_LIBS="$($CUPSCONFIG --libs)"
            # cups-config has no --prefix option; query only the dirs it does
            # expose and leave cups_prefix for the /usr fallback below.  (Never
            # capture an unsupported option's usage text into a substituted
            # variable - a multi-line value corrupts config.status.)
            CUPS_DATADIR="$($CUPSCONFIG --datadir)"
            CUPS_SERVERBIN="$($CUPSCONFIG --serverbin)"
            CUPS_SERVERROOT="$($CUPSCONFIG --serverroot)"
        ], [
            AC_MSG_FAILURE([cannot find CUPS: no cups3.pc, cups.pc or cups-config])
        ])
    ])
])
AS_IF([test "x$cups_prefix" = x], [cups_prefix=/usr])
AS_IF([test "x$CUPS_DATADIR" = x], [CUPS_DATADIR="$cups_prefix/share/cups"])
AS_IF([test "x$CUPS_SERVERBIN" = x], [CUPS_SERVERBIN="$cups_prefix/lib/cups"])
AS_IF([test "x$CUPS_SERVERROOT" = x], [CUPS_SERVERROOT="$cups_prefix/etc/cups"])
AS_IF([test "x$CUPS_SBINDIR" = x], [CUPS_SBINDIR="$cups_prefix/sbin"])
AC_SUBST(CUPS_CFLAGS)
AC_SUBST(CUPS_LIBS)
AC_SUBST(CUPS_DATADIR)
AC_SUBST(CUPS_SERVERBIN)
AC_SUBST(CUPS_SERVERROOT)
AC_SUBST(CUPS_SBINDIR)

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h cups/cups.h cupsfilters/catalog.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions
AC_FUNC_MALLOC

# The Dbus service directory
AC_MSG_CHECKING([dbus service file path])
PKG_CHECK_VAR([DBUS_SERVICES_DIR], [dbus-1], [session_bus_services_dir],
  AC_MSG_RESULT($DBUS_SERVICES_DIR),
  AC_MSG_FAILURE([cannot locate session_bus_services_dir])
)

# The directory for the backend executables
AC_MSG_CHECKING([CPDB backend executables path])
PKG_CHECK_VAR([CPDB_BACKEND_DIR], [cpdb], [cpdb_backend_exec_dir],
  AC_MSG_RESULT($CPDB_BACKEND_DIR),
  AC_MSG_FAILURE([cannot locate cpdb_backend_exec_dir])
)

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 data/Makefile])

AC_OUTPUT
