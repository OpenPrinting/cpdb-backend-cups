/*
 * cups-compat.h - portability shim so cpdb-backend-cups builds against
 * CUPS 2.4.x, 2.5.x and 3.x from a single source tree.
 *
 * CUPS 2.4 and 2.5 share the libcups2 API; every symbol used by this backend
 * is identical between them.  libcups3 (CUPS 3.x) renamed a family of
 * accessors to the cupsGet / ippGet form, dropped the "2"-suffixed dest/job
 * helpers, gave httpConnect the old httpConnect2 signature, added a flags
 * argument to cupsCopyDestInfo, changed httpGetDateString to require a
 * caller-supplied buffer, renamed the CUPS_PRINTER_* printer-type constants to
 * CUPS_PTYPE_*, and switched the http-timeout / dest callbacks to return bool.
 * This header maps the 3.x spellings back to the names the backend already
 * uses, so the only non-macro-able change (httpGetDateString) is funnelled
 * through the cpdb_httpDateString() helper below.
 *
 * Include AFTER <cups/cups.h>.
 */
#ifndef CPDB_CUPS_COMPAT_H
#define CPDB_CUPS_COMPAT_H

#include <stdbool.h>
#include <time.h>
#include <cups/cups.h>
#include <cups/http.h>

#if CUPS_VERSION_MAJOR >= 3

/* Global accessors were renamed to the cupsGet / ippGet form in libcups3. */
#  define cupsServer()           cupsGetServer()
#  define cupsUser()             cupsGetUser()
#  define cupsEncryption()       cupsGetEncryption()
#  define ippPort()              ippGetPort()
#  define cupsLastError()        cupsGetError()
#  define cupsLastErrorString()  cupsGetErrorString()

/* Printer-type constants were renamed CUPS_PRINTER_* -> CUPS_PTYPE_*. */
#  define CUPS_PRINTER_LOCAL     CUPS_PTYPE_LOCAL
#  define CUPS_PRINTER_REMOTE    CUPS_PTYPE_REMOTE

/* httpConnect2() was removed; httpConnect() now takes its 8-argument form. */
#  define httpConnect2(host, port, addrlist, family, encryption, blocking, msec, cancel) \
          httpConnect((host), (port), (addrlist), (family), (encryption), (blocking), (msec), (cancel))

/* The "2"-suffixed dest/job helpers were folded back into the base names. */
#  define cupsGetDests2(http, dests) \
          cupsGetDests((http), (dests))
#  define cupsGetJobs2(http, jobs, name, myjobs, whichjobs) \
          cupsGetJobs((http), (jobs), (name), (myjobs), (whichjobs))

/* cupsCopyDestInfo() gained a cups_dest_flags_t argument.  Every call site in
 * this backend uses the local system connection, so default the flags; the
 * parenthesised name suppresses macro recursion. */
#  define cupsCopyDestInfo(http, dest) \
          (cupsCopyDestInfo)((http), (dest), CUPS_DEST_FLAGS_NONE)

/* httpGetDateString() now needs a caller-supplied buffer. */
static inline const char *cpdb_httpDateString(time_t t)
{
    static __thread char buf[256];
    return httpGetDateString(t, buf, sizeof(buf));
}

/* http timeout callbacks return bool in libcups3, int in libcups2. */
typedef bool cpdb_http_timeout_ret_t;

#else /* CUPS 2.x */

#  define cpdb_httpDateString(t)  httpGetDateString(t)

typedef int cpdb_http_timeout_ret_t;

#endif /* CUPS_VERSION_MAJOR >= 3 */

#endif /* CPDB_CUPS_COMPAT_H */
