#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([cpdb-backend-cups], [2.0b1], [https://github.com/OpenPrinting/cpdb-backend-cups/issues/], [cpdb-backend-cups], [https://github.com/OpenPrinting/cpdb-backend-cups])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([src/print_backend_cups.c])
AM_INIT_AUTOMAKE([-Wall foreign])

: ${CFLAGS=""}
# Check for a C compiler
AC_PROG_CC


# Checks for backend library 
PKG_CHECK_MODULES([CPDB],[cpdb >= 2])
PKG_CHECK_MODULES([LIBCUPSFILTERS], [libcupsfilters >= 2])
PKG_CHECK_MODULES([GIO],[gio-2.0])
PKG_CHECK_MODULES([GIOUNIX],[gio-unix-2.0])
PKG_CHECK_MODULES([GLIB],[glib-2.0])

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

# The info directory which will be read by the frontend
AC_MSG_CHECKING([CPDB backend info path])
PKG_CHECK_VAR([INFO_DIR], [cpdb], [cpdb_backend_info_dir],
  AC_MSG_RESULT($INFO_DIR),
  AC_MSG_FAILURE([cannot locate cpdb_backend_info_dir])
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
