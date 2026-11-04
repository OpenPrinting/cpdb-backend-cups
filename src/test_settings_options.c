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
    got = settings_to_cups_options(settings, claimed, &options);
    testEnd(glib_criticals == 0 && got == want && options != NULL
            && strcmp(options[0].name, "media") == 0
            && strcmp(options[0].value, "na_letter_8.5x11in") == 0);
    cupsFreeOptions(got, options);
    g_variant_unref(settings);
}

int
main(void)
{
    GVariant *empty;
    cups_option_t *options;
    int got;

    g_log_set_handler("GLib", G_LOG_LEVEL_CRITICAL, count_critical, NULL);

    check_media("one setting claimed once",
                one_setting("media", "na_letter_8.5x11in"), 1, 1);
    check_media("one setting claimed five times",
                one_setting("media", "na_letter_8.5x11in"), 5, 1);

    empty = g_variant_ref_sink(g_variant_new_array(G_VARIANT_TYPE("(ss)"), NULL, 0));
    options = (cups_option_t *)0x1;
    glib_criticals = 0;
    testBegin("empty settings claimed five times");
    got = settings_to_cups_options(empty, 5, &options);
    testEnd(glib_criticals == 0 && got == 0 && options == NULL);
    g_variant_unref(empty);

    check_media("two settings claimed once", two_settings(), 1, 1);

    if (testsPassed)
        return 0;
    return 1;
}
