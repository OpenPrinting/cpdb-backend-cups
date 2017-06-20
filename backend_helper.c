#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <cups/cups.h>
#include "backend_interface.h"
#include "backend_helper.h"
#include <glib.h>

char *cups_printer_state(cups_dest_t *dest)
{
    //cups_dest_t *dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, printer_name, NULL);
    g_assert_nonnull(dest);
    char *state = cupsGetOption("printer-state", dest->num_options,
                                dest->options);
    if (state == NULL)
        return "NA";

    switch (state[0])
    {
    case '3':
        return STATE_IDLE;
    case '4':
        return STATE_PRINTING;
    case '5':
        return STATE_STOPPED;

    default:
        return "NA";
    }
}

gboolean cups_is_accepting_jobs(cups_dest_t *dest)
{
    g_assert_nonnull(dest);
    char *val = cupsGetOption("printer-is-accepting-jobs", dest->num_options,
                              dest->options);
    return get_boolean(val);
}

void getResolution(cups_dest_t *dest, int *xres, int *yres)
{
    http_t *http = cupsConnectDest(dest, CUPS_DEST_FLAGS_NONE, 500, NULL, NULL, 0, NULL, NULL);
    g_assert_nonnull(http);
    cups_dinfo_t *dinfo = cupsCopyDestInfo(http, dest);
    g_assert_nonnull(dinfo);
    ipp_attribute_t *attr = cupsFindDestDefault(http, dest, dinfo, "printer-resolution");
    ipp_res_t *units;
    *xres = ippGetResolution(attr,  0 , yres , units);
}