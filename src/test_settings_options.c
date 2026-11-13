#include "test-internal.h"
#include "backend_helper.h"

static int glib_criticals;

static void
count_critical(const gchar *domain, GLogLevelFlags level,
               const gchar *message, gpointer user_data)
{
    (void)domain;
    (void)level;
    (void)message;
    (void)user_data;
    glib_criticals++;
}

static GVariant *
one_setting(const char *name, const char *value)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ss)"));
    g_variant_builder_add(&builder, "(ss)", name, value);
    return g_variant_ref_sink(g_variant_builder_end(&builder));
}

static GVariant *
two_settings(void)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ss)"));
    g_variant_builder_add(&builder, "(ss)", "media", "na_letter_8.5x11in");
    g_variant_builder_add(&builder, "(ss)", "copies", "2");
    return g_variant_ref_sink(g_variant_builder_end(&builder));
}

static void
check_media(const char *title, GVariant *settings, int claimed, int want)
{
    cups_option_t *options = (cups_option_t *)0x1;
    int got;

    glib_criticals = 0;
    testBegin("%s", title);
    got = settings_to_cups_options(NULL, settings, claimed, &options);
    testEnd(glib_criticals == 0 && got == want && options != NULL
            && strcmp(options[0].name, "media") == 0
            && strcmp(options[0].value, "na_letter_8.5x11in") == 0);
    cupsFreeOptions(got, options);
    g_variant_unref(settings);
}

#if CUPS_VERSION_MAJOR < 3
/*
 * Minimal PPD with the concepts that CUPS reports both as an IPP
 * attribute and as a PPD keyword.
 */
static const char ppd_text[] =
    "*PPD-Adobe: \"4.3\"\n"
    "*FormatVersion: \"4.3\"\n"
    "*FileVersion: \"1.0\"\n"
    "*LanguageVersion: English\n"
    "*LanguageEncoding: ISOLatin1\n"
    "*PCFileName: \"test.ppd\"\n"
    "*Manufacturer: \"Test\"\n"
    "*ModelName: \"Test\"\n"
    "*ShortNickName: \"Test\"\n"
    "*NickName: \"Test\"\n"
    "*PSVersion: \"(3010.000) 0\"\n"
    "*LanguageLevel: \"3\"\n"
    "*ColorDevice: True\n"
    "*DefaultColorSpace: RGB\n"
    "*FileSystem: False\n"
    "*Throughput: \"1\"\n"
    "*LandscapeOrientation: Plus90\n"
    "*TTRasterizer: Type42\n"
    "*OpenUI *Duplex/Double-Sided Printing: PickOne\n"
    "*DefaultDuplex: None\n"
    "*Duplex None/Off: \"\"\n"
    "*Duplex DuplexNoTumble/Long-Edge binding: \"\"\n"
    "*Duplex DuplexTumble/Short-Edge binding: \"\"\n"
    "*Duplex Booklet/Booklet: \"\"\n"
    "*CloseUI: *Duplex\n"
    "*OpenUI *ColorModel/Color: PickOne\n"
    "*DefaultColorModel: grayscale\n"
    "*ColorModel color/Color: \"\"\n"
    "*ColorModel grayscale/Black and White: \"\"\n"
    "*CloseUI: *ColorModel\n"
    "*OpenUI *Resolution/Resolution: PickOne\n"
    "*DefaultResolution: 600dpi\n"
    "*Resolution 300dpi/300 dpi: \"\"\n"
    "*Resolution 600dpi/600 dpi: \"\"\n"
    "*CloseUI: *Resolution\n";

/*
 * Check that a setting of a merged option also sends the PPD version,
 * and that a PPD-only choice is sent as PPD keyword only.
 */
static void
check_ppd_counterpart(const char *title, ppd_file_t *ppd,
                      const char *name, const char *value,
                      const char *ppd_name, const char *ppd_value,
                      gboolean expect_ipp)
{
    PrinterCUPS printer;
    cups_option_t *options = NULL;
    GVariant *settings = one_setting(name, value);
    const char *got_ppd, *got_ipp;
    int num;

    memset(&printer, 0, sizeof(printer));
    printer.ppd = ppd;
    printer.ppd_tried = TRUE;

    testBegin("%s", title);
    num = settings_to_cups_options(&printer, settings, 1, &options);
    got_ppd = cupsGetOption(ppd_name, num, options);
    got_ipp = cupsGetOption(name, num, options);

    testEnd(got_ppd != NULL && strcmp(got_ppd, ppd_value) == 0
            && (expect_ipp ? (got_ipp != NULL && strcmp(got_ipp, value) == 0)
                           : got_ipp == NULL));

    cupsFreeOptions(num, options);
    g_variant_unref(settings);
}

static ppd_file_t *
open_test_ppd(void)
{
    char *path = NULL;
    GError *error = NULL;
    ppd_file_t *ppd;

    if (!g_file_open_tmp("cpdb-test-XXXXXX.ppd", &path, &error))
    {
        g_error_free(error);
        return NULL;
    }
    if (!g_file_set_contents(path, ppd_text, -1, &error))
    {
        g_error_free(error);
        g_free(path);
        return NULL;
    }

    ppd = ppdOpenFile(path);
    unlink(path);
    g_free(path);
    return ppd;
}
#endif

int
main(void)
{
    GVariant *empty;
    cups_option_t *options;
    int got;

#if CUPS_VERSION_MAJOR < 3
    ppd_file_t *ppd;
#endif

    g_log_set_handler("GLib", G_LOG_LEVEL_CRITICAL, count_critical, NULL);

    check_media("one setting claimed once",
                one_setting("media", "na_letter_8.5x11in"), 1, 1);
    check_media("one setting claimed five times",
                one_setting("media", "na_letter_8.5x11in"), 5, 1);

    empty = g_variant_ref_sink(g_variant_new_array(G_VARIANT_TYPE("(ss)"), NULL, 0));
    options = (cups_option_t *)0x1;
    glib_criticals = 0;
    testBegin("empty settings claimed five times");
    got = settings_to_cups_options(NULL, empty, 5, &options);
    testEnd(glib_criticals == 0 && got == 0 && options == NULL);
    g_variant_unref(empty);

    check_media("two settings claimed once", two_settings(), 1, 1);

#if CUPS_VERSION_MAJOR < 3
    ppd = open_test_ppd();
    if (!ppd)
    {
        testBegin("open test PPD");
        testEnd(false);
    }
    else
    {
        check_ppd_counterpart("IPP choice also sends the PPD choice",
                              ppd, "sides", "two-sided-long-edge",
                              "Duplex", "DuplexNoTumble", TRUE);
        check_ppd_counterpart("PPD-only choice sends only the PPD choice",
                              ppd, "sides", "Booklet",
                              "Duplex", "Booklet", FALSE);
        check_ppd_counterpart("color mode is mapped to the PPD choice",
                              ppd, "print-color-mode", "monochrome",
                              "ColorModel", "grayscale", TRUE);
        check_ppd_counterpart("resolution sends the PPD choice",
                              ppd, "printer-resolution", "600dpi",
                              "Resolution", "600dpi", TRUE);
        ppdClose(ppd);
    }
#endif

    if (testsPassed)
        return 0;
    return 1;
}
