backenddir = $(CPDB_BACKEND_DIR)
backend_PROGRAMS = cups
cups_SOURCES = print_backend_cups.c backend_helper.c backend_helper.h
cups_CPPFLAGS  = $(CPDB_CFLAGS)
cups_CPPFLAGS += $(LIBCUPSFILTERS_CFLAGS)
cups_CPPFLAGS += $(GLIB_CFLAGS)
cups_CPPFLAGS += $(GIO_CFLAGS)
cups_CPPFLAGS += $(GIOUNIX_CFLAGS)

cups_LDADD  = -lcups -lpthread -lm -lcrypt
cups_LDADD += $(CPDB_LIBS)
cups_LDADD += $(LIBCUPSFILTERS_LIBS)
cups_LDADD += $(GLIB_LIBS)
cups_LDADD += $(GIO_LIBS)
cups_LDADD += $(GIOUNIX_LIBS)
