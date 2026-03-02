# cups-notifier

cups_notifier_sources = \
	cups-notifier.c \
	cups-notifier.h

$(cups_notifier_sources): ../data/org.cups.cupsd.Notifier.xml
	gdbus-codegen \
	    --interface-prefix org.cups.cupsd \
	    --c-namespace Cups \
	    --generate-c-code cups-notifier \
	    ../data/org.cups.cupsd.Notifier.xml

BUILT_SOURCES = $(cups_notifier_sources)
CLEANFILES = $(BUILT_SOURCES)

backenddir = $(CPDB_BACKEND_DIR)
backend_PROGRAMS = cups
cups_SOURCES = \
	print_backend_cups.c \
	backend_helper.c backend_helper.h \
	cups-notifier.c cups-notifier.h
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

# ================================
# Tests ("make test"/"make check")
# ================================

TESTS = \
        run-tests.sh \
        test_extractHostFromURI

# Unit test program
check_PROGRAMS = test_extractHostFromURI
test_extractHostFromURI_SOURCES = \
	test_extractHostFromURI.c \
	backend_helper.c backend_helper.h
test_extractHostFromURI_CPPFLAGS = $(CPDB_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(GIOUNIX_CFLAGS)
test_extractHostFromURI_LDADD = $(CPDB_LIBS) $(GLIB_LIBS) $(GIO_LIBS) $(GIOUNIX_LIBS) -lcups

EXTRA_DIST = \
        run-tests.sh \
	test.convs \
	testpdf.ppd \
	test-internal.h
