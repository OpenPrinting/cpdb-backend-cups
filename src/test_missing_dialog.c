#include "test-internal.h"
#include "backend_helper.h"

static void
free_dialog(gpointer value)
{
    Dialog *d = value;

    if (d->printers)
        g_hash_table_destroy(d->printers);
    g_free(d);
}

static BackendObj *
backend_with_dialog(const char *name)
{
    BackendObj *b = g_new0(BackendObj, 1);

    b->dialogs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_dialog);
    if (name)
    {
        Dialog *d = g_new0(Dialog, 1);

        d->printers = g_hash_table_new(g_str_hash, g_str_equal);
        g_hash_table_insert(b->dialogs, g_strdup(name), d);
    }
    return b;
}

static void
free_backend(BackendObj *b)
{
    g_hash_table_destroy(b->dialogs);
    g_free(b);
}

int
main(void)
{
    BackendObj *b;
    PrinterCUPS *printer;
    cups_dest_t *dest;

    b = backend_with_dialog("dlg");
    testBegin("known dialog, unknown printer");
    printer = get_printer_by_name(b, "dlg", "no-such-printer");
    testEnd(printer == NULL);
    free_backend(b);

    b = backend_with_dialog(NULL);
    testBegin("missing dialog");
    printer = get_printer_by_name(b, "missing-dialog", "no-such-printer");
    testEnd(printer == NULL);

    testBegin("missing dialog dest");
    dest = get_dest_by_name(b, "missing-dialog", "no-such-printer");
    testEnd(dest == NULL);
    free_backend(b);

    if (testsPassed)
        return 0;
    return 1;
}
