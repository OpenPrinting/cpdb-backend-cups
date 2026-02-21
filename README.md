# CUPS Common Print Dialog Backend

This repository hosts the code for the CUPS **C**ommon **P**rint **D**ialog **B**ackend. This backend manages and provides information about CUPS and IPP printing destinations to the printing dialog.

## Background

The [Common Print Dialog Backends](https://openprinting.github.io/achievements/#common-print-dialog-backends) project aims to move the responsability on the part of the print dialog which communicates with the print system away from the GUI toolkit/app developers to the print system's developers and also to bring all print technologies available to the user (CUPS, cloud printing services, ...) into all application's print dialogs.

## Dependencies

- [cpdb-libs](https://github.com/OpenPrinting/cpdb-libs): Version >= 2.0.0 (or GIT Master)

- [CUPS](https://github.com/OpenPrinting/cups): Version >= 2.2
  `sudo apt install cups libcups2-dev`

- GLIB 2.0:
  `sudo apt install libglib2.0-dev`

## Build and installation

```
$ ./autogen.sh
$ ./configure
$ make
$ sudo make install
```

If you are updating from an older version, please kill the old version of the backend:
```
$ sudo killall cups
```
This way the next time when a print dialog is opened the new version of the backend is started.

## Following the development and updating

The current source code you find on the [OpenPrinting GitHub](https://github.com/OpenPrinting/cpdb-backend-cups).

## Running

The backend is auto-activated when a frontend (like a CPDB-supporting print dialog or the example frontend `demo/print_frontend` of cpdb-libs) is started, so there is no need to run it explicitly.

However, if you wish to see the debug statements in the backend code, you can run `/usr/local/lib/print-backends/cups`

NOTE: The communication protocol between frontends and backends has changed (Job data streaming via domain socket, printer list filteringvia D-Bus methods). Therefore use this backend only with frontends based on cpdb-libs of at least version 2.0b6.

## More Info

- [Nilanjana Lodh's Google Summer of Code 2017 Final Report](https://nilanjanalodh.github.io/common-print-dialog-gsoc17/)

- [Gaurav Guleria's Google Summer of Code 2022 Final Report](https://github.com/TinyTrebuchet/gsoc22/)

- [Shivam Sharma's Google Summer of Code 2024 Final Report](https://github.com/shivamsharma2509/GSOC24)
