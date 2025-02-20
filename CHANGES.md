# CHANGES - Common Print Dialog Backends - CUPS Backend - v2.0b7 - 2025-02-20

## CHANGES IN V2.0b7 (20th February 2025)

- Add support for CUPS printer instances
  Don't just always use the `cups_dest_t`'s `name` for the printer
  name, but also take it's `instance` member into account and if
  present, append that for the name used for the CPDB printer name,
  separated by a slash character (Pull request #34).

- Always query current CUPS default printer
  When asked for the default printer, always query and return the
  current CUPS default printer instead of whatever was the default
  last time this was done, to take into account that the CUPS default
  printer can change while the backend is running (Pull request #33).

- Pass correct parameters to `cupsStartDestDocument()`
  Now job title and job attributes (options) are correctly passed on
  (Pull request #36).

- Use NULL Instead of "NA" if there's no default printer
  NULL makes clear that there's no default printer, while "NA" could
  even be the name of an actual printer, so use the former instead of
  the latter if no default printer could be determined (Pull request
  #35).

- Use `g_strdup` instead of `cpdbGetStringCopy`
  GLib's `g_strdup` already provides the same functionality as
  `cpdbGetStringCopy` from cpdb-libs, so use the former instead of
  relying on a custom CPDB implementation (Pull request #32).


## CHANGES IN V2.0b6 (18th June 2024)

- Stream print data through a Unix domain socket
  To ease making a Snap from the CPDB backend for CUPS we now transfer
  the print job data from the dialog to the backend via a Unix domain
  socket and not by dropping the data into a file (PR #28).

- Let the frontend not be a D-Bus service, only the backends
  To control hiding temporary or remote printers in the backend's
  printer list we have added methods to the D-Bus service provided by
  the backends now. Before, the frontends were also D-Bus services
  just to send signals to the backends for controling the filtering
  (PR #29).

- Unified logging. Always use log...() functions (PR #29).

- Updated names of some CUPS constants to CUPS 2.5.x and newer (PR #28).

- Removed the backend functions for get-all-jobs(),
  get_active_jobs_count(), and cancel_job() (PR #28).

- Removed tryPPD(), a useless, PPD-related function
  This function logs the options in the PPD of the CUPS queue but does
  nothing else. PPDs will also go away in CUPS 3.x, so we want the CUPS
  backend not depend on PPDs or PPD-supporting functions/libraries (PR #29).

- send_printer_state_changed_signal(): Send correct signal
  Send CPDB_SIGNAL_PRINTER_STATE_CHANGED, not
  CPDB_SIGNAL_PRINTER_REMOVED (#29).

- Let "dialog_name" always be "const char *", to eliminate warnings (PR #29).

- Added NULL checks for the functions dealing with dialogs

- Build test: Give more time (3 instead of 1 sec) for the print job
  submission before closing the backend, to get note of the
  confirmation of successful submission (PR #28).

- Build test: Let CPDB frontend and backend log in debug mode (PR #28).

- Build test: Create the directory for the socket files (PR #28).

- Updated build test for removal of backend info file


## CHANGES IN V2.0b5 (2nd August 2023)

- Remove CPDB backend info file
  The frontend libraries now use only the D-Bus to find available
  backends. This makes sure that everything works also if the frontend
  and/or any of the backends are installed via sanboxed packaging
  (like Snap for example) where the components cannot read each
  other's file systems (PR #26).

- `get_all_media()`: Do not crash on custom page size range entries
  The media-col-database IPP attribute contains one entry for each
  valid combination of page size dimensions, margins, and in some
  cases also media source and media type. Entries for custom page
  sizes contain ranges instead of single values. `get_all_media()`
  crashed on these. Now we let the function simply skip them.

- Build system: Removed unnecessary lines in `tools/Makefile.am`
  Removed the `TESTdir` and `TEST_SCRIPTS` entries in
  `tools/Makefile.am`.  They are not needed and let `make install` try
  to install `run-tests.sh` in the source directory, where it already
  is, causing an error.


## CHANGES IN V2.0b4 (21th March 2023)

- Added test script for `make test`/`make check`

  The script `src/run-tests.sh` starts a private session D-Bus via
  `dbus-run-session` and therein an own copy of CUPS. It uses the CPDB
  CUPS backend with this CUPS and tests it using the
  `cpdb-text-frontend` of cpdb-libs, performing several test tasks on
  the backend.


## CHANGES IN V2.0b3 (20th February 2023)

- Add handler for `GetAllTranslations` method and Bug fixes (PR #22)
  * Fixed bug when backend finds zero printers
  * Add handler for `GetAllTranslations` method
  * `get_printer_translations()` fetches translations for all printer
    strings.
  * Removed `get_human_readable_option_name()` and
    `get_human_readable_choice_name()` functions.


## CHANGES IN V2.0b2 (13th February 2023)

- Return printer list upon activation and subscribe to cups for
  updates (PR #20)
  * Return printer list synchronously upon activation
  * Subscribe to CUPS notifications for printer updates
  * Automatically update frontends about new printers found, old
    printers lost, or printer state changed

- Added the support of cpdb-libs for translations
  * Using general message string catalogs from CUPS and also message
    string catalogs from individual print queues/IPP printers.
  * Message catalog management done by libcupsfilters 2.x, via the
    `cfCatalog...()` API functions (`catalog.h`).

- Option group support

- Added `billing-info` option (PR #19)

- Log messages handled by frontend

- Use pkg-config variables instead of hardcoded paths (PR #17)

- Build system: Let "make dist" also create .tar.bz2 and .tar.xz


## CHANGES IN V2.0b1 (12th December 2022)

- Added function to query for human readable names of options/choices

  With the added functionality of cpdb-libs to poll human-readable
  names for options/attributes and their choices this commit adds a
  simple function to provide human-readable strings for the
  user-settable printer IPP attributes of CUPS queues.

- Added support for common CUPS/cups-filters options

  Options like number-up, page-set, output-order, ... Available for
  all CUPS queues, not specific to particular printer.

- Added get_media_size() function

- Support for media sizes to have multiple margin variants (like
  standard and borderless)

- Do not send media-col attribute to frontend as user-settable option

- Adapted to renamed API functions and data types of cpdb-libs 2.x

- Updated signal names to match those emitted from CPDB frontend

- Made "make dist" generate a complete source tarball

- Fixed some potential crasher bugs following warnings about
  incompatible pointer types.

- Updated README.md

  + On update the old version of the backend needs to get killed
  + Common Print Dialog -> Common Print Dialog Backends
  + Requires cpdb-libs 2.0.0 or newer
  + Updated instructions for running the backend.
  + Added link to Gaurav Guleria's GSoC work, minor fixes
  + Use third person.
