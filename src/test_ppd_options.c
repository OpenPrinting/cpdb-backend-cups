/*
 * Integration test for the vendor PPD options of driver-based queues.
 *
 * CUPS 2.x does not report the vendor keywords of a PPD as IPP
 * attributes, so the backend fetches the PPD of the queue and offers
 * its options to the frontend.  This test checks that the options of
 * a queue with a PPD (src/testpdf.ppd) reach the frontend, that they
 * carry the choices of the PPD and that they are shown with the
 * human-readable text of the PPD.
 *
 * The test talks to the backend of the test bed directly, so it is run
 * by run-tests.sh while the backend is running.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <gio/gio.h>

#define BACKEND_NAME "org.openprinting.Backend.CUPS"
#define BACKEND_PATH "/"
#define BACKEND_IFACE "org.openprinting.PrintBackend"

static int failures = 0;

static void
fail (const char *format,
      ...)
{
  va_list args;

  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  fputc ('\n', stderr);
  failures++;
}

/*
 * Options that are free text fields and have no values by nature.
 */
static gboolean
is_text_option (const char *name)
{
  return (strcmp (name, "billing-info") == 0);
}

/*
 * Call a method of the backend and return the reply.
 */
static GVariant *
call_backend (GDBusConnection *connection,
              const char      *method,
              GVariant        *parameters,
              GError         **error)
{
  return (g_dbus_connection_call_sync (connection, BACKEND_NAME, BACKEND_PATH,
                                       BACKEND_IFACE, method, parameters, NULL,
                                       G_DBUS_CALL_FLAGS_NONE, 30000, NULL,
                                       error));
}

int
main (int argc,
      char **argv)
{
  const char *printer_id = argc > 1 ? argv[1] : "printer-1";
  const char *locale     = argc > 2 ? argv[2] : "en";

  /*
   * The vendor PPD options of src/testpdf.ppd and their human-readable
   * text in the PPD.
   */
  static const struct
  {
    const char *keyword;
    const char *text;
  } vendor_options[] =
  {
    { "ColorModel", "Color" },
    { "Duplex", "Double-Sided Printing" },
    { "Resolution", "Resolution" }
  };

  GDBusConnection *connection;
  GError *error = NULL;
  GVariant *reply, *options, *translation;

  /*
   * Asking for the printers registers this client with the backend, so
   * that the backend knows the printers of this client.
   */
  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (!connection)
  {
    fail ("Cannot connect to the session bus: %s", error->message);
    return (1);
  }

  reply = call_backend (connection, "GetAllPrinters", NULL, &error);
  if (error)
  {
    fail ("Cannot get the printers: %s", error->message);
    return (1);
  }
  g_variant_unref (reply);

  reply = call_backend (connection, "GetAllOptions",
                        g_variant_new ("(s)", printer_id), &error);
  if (error)
  {
    fail ("Cannot get the options of '%s': %s", printer_id, error->message);
    return (1);
  }

  options = g_variant_get_child_value (reply, 1);

  {
    GVariantIter *iter = NULL;
    const char *name, *group, *def;
    gint32 num;
    GVariant *values;
    gboolean *found = g_new0 (gboolean, G_N_ELEMENTS (vendor_options));

    g_variant_get (options, "a(sssia(s))", &iter);
    while (g_variant_iter_loop (iter, "(sssia(s))", &name, &group, &def, &num,
                                &values))
    {
      /*
       * Every option has to offer at least one choice, an option
       * without values is of no use to the user.
       */
      if (!is_text_option (name) && num <= 0)
        fail ("Option '%s' has no supported value", name);

      /*
       * The vendor options of the PPD have to be there, with their
       * choices.
       */
      for (size_t i = 0; i < G_N_ELEMENTS (vendor_options); i++)
      {
        if (strcmp (name, vendor_options[i].keyword) != 0)
          continue;

        found[i] = TRUE;

        if (num <= 0)
          fail ("Vendor option '%s' has no supported value", name);
      }
    }
    g_variant_iter_free (iter);

    for (size_t i = 0; i < G_N_ELEMENTS (vendor_options); i++)
      if (!found[i])
        fail ("Vendor option '%s' is missing", vendor_options[i].keyword);

    g_free (found);
  }

  g_variant_unref (options);
  g_variant_unref (reply);

  /*
   * The vendor options have to be shown with the human-readable text of
   * the PPD, not with their keyword.
   */
  for (size_t i = 0; i < G_N_ELEMENTS (vendor_options); i++)
  {
    reply = call_backend (connection, "getOptionTranslation",
                          g_variant_new ("(sss)", printer_id,
                                         vendor_options[i].keyword, locale),
                          &error);
    if (error)
    {
      fail ("Cannot get the translation of '%s': %s",
            vendor_options[i].keyword, error->message);
      return (1);
    }

    translation = g_variant_get_child_value (reply, 0);
    if (strcmp (g_variant_get_string (translation, NULL),
                vendor_options[i].text) != 0)
      fail ("Vendor option '%s' is translated as '%s' instead of '%s'",
            vendor_options[i].keyword,
            g_variant_get_string (translation, NULL), vendor_options[i].text);

    g_variant_unref (translation);
    g_variant_unref (reply);
  }

  /*
   * The choices of the vendor options have to be shown with the
   * human-readable text of the PPD as well.
   */
  reply = call_backend (connection, "getChoiceTranslation",
                        g_variant_new ("(ssss)", printer_id, "ColorModel",
                                       "grayscale", locale), &error);
  if (error)
  {
    fail ("Cannot get the translation of the choice 'grayscale': %s",
          error->message);
    return (1);
  }

  translation = g_variant_get_child_value (reply, 0);
  if (strcmp (g_variant_get_string (translation, NULL), "Black and White") != 0)
    fail ("The choice 'grayscale' of 'ColorModel' is translated as '%s' "
          "instead of 'Black and White'",
          g_variant_get_string (translation, NULL));

  g_variant_unref (translation);
  g_variant_unref (reply);
  g_object_unref (connection);

  if (failures)
  {
    fprintf (stderr, "FAIL: %d check(s) failed\n", failures);
    return (1);
  }

  printf ("All checks passed\n");
  return (0);
}
