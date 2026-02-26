#include "backend_helper.h"
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cupsfilters/ipp.h>



#define MAX_ADDRESSES 10 
#define _CUPS_NO_DEPRECATED 1

static http_t *system_conn = NULL;
static unsigned int HttpLocalTimeout = 5;

static void *print_data_thread(void *data);

Mappings *map;

char *get_printer_name_for_cups_dest(const cups_dest_t *dest)
{
    if (!dest)
        return NULL;

    if (dest->instance)
        return g_strconcat(dest->name, "/", dest->instance, NULL);

    return g_strdup(dest->name);
}

/*****************BackendObj********************************/
BackendObj *get_new_BackendObj()
{
    map = get_new_Mappings();

    BackendObj *b = (BackendObj *)(malloc(sizeof(BackendObj)));
    b->dbus_connection = NULL;
    b->dialogs = g_hash_table_new_full(g_str_hash, g_str_equal,
                                       (GDestroyNotify)free_string,
                                       (GDestroyNotify)free_Dialog);
    b->num_frontends = 0;
    b->obj_path = NULL;
    b->default_printer = NULL;
    return b;
}

/** Don't free the returned value; it is owned by BackendObj */
char *get_default_printer(BackendObj *b)
{
    /**first query to see if the user default printer is set**/
    int num_dests;
    cups_dest_t *dests;
    num_dests = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);          /** Get the list of all destinations */
    cups_dest_t *dest = cupsGetDest(NULL, NULL, num_dests, dests); /** Get the default  one */
    if (dest)
    {
        /** Return the user default printer */
        char *def = get_printer_name_for_cups_dest(dest);
        cupsFreeDests(num_dests, dests);
        g_free(b->default_printer);
        b->default_printer = def;
        return def;
    }
    cupsFreeDests(num_dests, dests);

    /** Then query the system default printer **/
    ipp_t *request = ippNewRequest(IPP_OP_CUPS_GET_DEFAULT);
    ipp_t *response;
    ipp_attribute_t *attr;
    if ((response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/")) != NULL)
    {
        if ((attr = ippFindAttribute(response, "printer-name",
                                     IPP_TAG_NAME)) != NULL)
        {
            g_free(b->default_printer);
            b->default_printer = g_strdup(ippGetString(attr, 0, NULL));
            ippDelete(response);
            return b->default_printer;
        }
    }
    ippDelete(response);
    g_free(b->default_printer);
    b->default_printer = NULL;
    return b->default_printer;
}

void connect_to_dbus(BackendObj *b, char *obj_path)
{
    b->obj_path = obj_path;
    GError *error = NULL;
    g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(b->skeleton),
                                     b->dbus_connection,
                                     obj_path,
                                     &error);
    if (error)
    {
        logerror("Error connecting CUPS Backend to D-Bus.\n");
    }
}

void add_frontend(BackendObj *b, const char *dialog_name)
{
    Dialog *d = get_new_Dialog();
    g_hash_table_insert(b->dialogs, g_strdup(dialog_name), d);
    b->num_frontends++;
}

void remove_frontend(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    if (d)
    {
        g_hash_table_remove(b->dialogs, dialog_name);
        b->num_frontends--;
    }
    g_message("Removed Frontend entry for %s", dialog_name);
}
gboolean no_frontends(BackendObj *b)
{
    if ((b->num_frontends) == 0)
        return TRUE;
    return FALSE;
}
Dialog *find_dialog(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    return d;
}
int *get_dialog_cancel(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    return (d ? &d->cancel : NULL);
}
void set_dialog_cancel(BackendObj *b, const char *dialog_name)
{
    int *x = get_dialog_cancel(b, dialog_name);
    if (x) *x = 1;
}
void reset_dialog_cancel(BackendObj *b, const char *dialog_name)
{
    int *x = get_dialog_cancel(b, dialog_name);
    if (x) *x = 0;
}
void set_hide_remote_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    if (d) d->hide_remote = TRUE;
}
void unset_hide_remote_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    if (d) d->hide_remote = FALSE;
}
void set_hide_temp_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    if (d) d->hide_temp = TRUE;
}
void unset_hide_temp_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    if (d) d->hide_temp = FALSE;
}

static int
http_timeout_cb(http_t *http,
		void *user_data)
{
    logdebug("HTTP timeout! (consider increasing HttpLocalTimeout/HttpRemoteTimeout value)\n");
    return (0);
}

/* Connect to the system's CUPS daemon and also tell the libcups functions to
   use the system's CUPS */
static http_t *
http_connect_system(void)
{
    const char *server = cupsServer();
    int port = ippPort();

    if (!system_conn)
    {
        if (server[0] == '/')
            logdebug("Creating http connection to CUPS daemon via domain socket: %s\n",
                        server);
        else
            logdebug("Creating http connection to CUPS daemon: %s:%d\n",
                        server, port);
        system_conn = httpConnect2(server, port, NULL, AF_UNSPEC, cupsEncryption(), 1, 3000, NULL);
    }

    if (system_conn)
    {
        httpSetTimeout(system_conn, HttpLocalTimeout, http_timeout_cb, NULL);
    }
    else
    {
        if (server[0] == '/')
            logwarn("Failed creating http connection to CUPS daemon via domain socket: %s\n",
                server);
        else
            logwarn("Failed creating http connection to CUPS daemon: %s:%d\n",
                server, port);
    }

    return (system_conn);
}

/* Close connection to system's CUPS */
static void
http_close_system(void)
{
    logdebug("Closing connection to system's CUPS daemon.\n");
    if (system_conn)
    {
        httpClose(system_conn);
        system_conn = NULL;
    }
}

/* Create a subscription for D-Bus notifications on the system's
   CUPS. This makes the CUPS daemon fire up a D-Bus notifier
   process. */
int create_subscription ()
{
    ipp_t *req;
    ipp_t *resp;
    ipp_attribute_t *attr;
    int id = 0;
    http_t *conn = NULL;

    conn = http_connect_system();
    if (conn == NULL)
    {
        logwarn("Cannot connect to local CUPS to subscribe to notifications.\n");
        return (0);
    }

    req = ippNewRequest(IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS);
    ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_URI,
                "printer-uri", NULL, "/");
    ippAddString(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
                "notify-events", NULL, "all");
    ippAddString(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
                "notify-recipient-uri", NULL, "dbus://");
    ippAddInteger(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                "notify-lease-duration", NOTIFY_LEASE_DURATION);

    resp = cupsDoRequest(conn, req, "/");
    if (!resp || cupsLastError() != IPP_STATUS_OK)
    {
        logwarn("Error subscribing to CUPS notifications: %s\n",
                cupsLastErrorString ());
        return (0);
    }

    attr = ippFindAttribute(resp, "notify-subscription-id", IPP_TAG_INTEGER);
    if (attr)
    {
        id = ippGetInteger(attr, 0);
    }
    else
    {
        logwarn("ipp-create-printer-subscription response doesn't contain"
                "subscription id.\n");
    }

    ippDelete(resp);
    return (id);
}

/* Renew the D-Bus notification subscription, telling to CUPS that we
   are still there and it should not let the notifier time out. */
gboolean renew_subscription (int id)
{
    ipp_t *req;
    ipp_t *resp;
    http_t *http = NULL;

    http = http_connect_system();
    if (http == NULL)
    {
        logwarn("Cannot connect to system's CUPS to renew subscriptions!\n");
        return FALSE;
    }

    req = ippNewRequest(IPP_OP_RENEW_SUBSCRIPTION);
    ippAddInteger(req, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
                    "notify-subscription-id", id);
    ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_URI,
                    "printer-uri", NULL, "/");
    ippAddString(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
                    "notify-recipient-uri", NULL, "dbus://");
    ippAddInteger(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                    "notify-lease-duration", NOTIFY_LEASE_DURATION);

    resp = cupsDoRequest(http, req, "/");
    if (!resp || cupsLastError() != IPP_STATUS_OK)
    {
        logwarn("Error renewing CUPS subscription %d: %s\n",
                id, cupsLastErrorString());
        http_close_system();
        return FALSE;
    }

    ippDelete(resp);
    http_close_system();
    return TRUE;
}

/* Function which is called as a timeout event handler to let the
   renewal of the D-Bus subscription be done to the right time. */
gboolean renew_subscription_timeout (gpointer userdata)
{
    int *subscription_id = userdata;

    logdebug("renew_subscription_timeout() in THREAD %ld\n", pthread_self());

    if (*subscription_id <= 0 || !renew_subscription(*subscription_id))
        *subscription_id = create_subscription();

    return TRUE;
}

/* Cancel the D-Bus notifier subscription, so that CUPS can terminate its
   notifier when we shut down. */
void cancel_subscription (int id)
{
    ipp_t *req;
    ipp_t *resp;
    http_t *http = NULL;

    if (id <= 0)
        return;

    http = http_connect_system();
    if (http == NULL)
    {
        logwarn("Cannot connect to system's CUPS to cancel subscriptions.\n");
        return;
    }

    req = ippNewRequest(IPP_OP_CANCEL_SUBSCRIPTION);
    ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_URI,
                    "printer-uri", NULL, "/");
    ippAddInteger(req, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
                    "notify-subscription-id", id);

    resp = cupsDoRequest(http, req, "/");
    if (!resp || cupsLastError() != IPP_STATUS_OK)
    {
        logwarn("Error canceling subscription to CUPS notifications: %s\n",
                cupsLastErrorString());
        http_close_system();
        return;
    }

    ippDelete(resp);
    http_close_system();
}

gboolean dialog_contains_printer(BackendObj *b, const char *dialog_name, const char *printer_name)
{
    Dialog *d = g_hash_table_lookup(b->dialogs, dialog_name);

    if (d == NULL || d->printers == NULL)
    {
	char *msg = malloc(sizeof(char) * (strlen(dialog_name) + 50));
        sprintf(msg, "Can't retrieve printers for dialog %s.\n", dialog_name);
        logerror(msg);
	free(msg);
        return FALSE;
    }
    if (g_hash_table_contains(d->printers, printer_name))
        return TRUE;
    return FALSE;
}

PrinterCUPS *add_printer_to_dialog(BackendObj *b, const char *dialog_name, const cups_dest_t *dest)
{
    char *printer_name = get_printer_name_for_cups_dest(dest);
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    if (d == NULL)
    {
	char *msg = malloc(sizeof(char) * (strlen(dialog_name) + 50));
        sprintf(msg, "Invalid dialog name %s.\n", dialog_name);
        logerror(msg);
	free(msg);
        return NULL;
    }

    PrinterCUPS *p = get_new_PrinterCUPS(dest);
    g_hash_table_insert(d->printers, printer_name, p);
    return p;
}

void remove_printer_from_dialog(BackendObj *b, const char *dialog_name, const char *printer_name)
{
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    if (d == NULL)
    {
	char *msg = malloc(sizeof(char) * (strlen(printer_name) + 50));
        sprintf(msg, "Unable to remove printer %s.\n", printer_name);
        logwarn(msg);
	free(msg);
        return;
    }
    g_hash_table_remove(d->printers, printer_name);
}

void send_printer_added_signal(BackendObj *b, const char *dialog_name, cups_dest_t *dest)
{

    if (dest == NULL)
    {
        logerror("Failed to send printer added signal.\n");
        return;
    }
    char *printer_name = get_printer_name_for_cups_dest(dest);
    GVariant *gv = g_variant_new(CPDB_PRINTER_ADDED_ARGS,
                                 printer_name,                                   //id
                                 printer_name,                                   //name
                                 cups_retrieve_string(dest, "printer-info"),     //info
                                 cups_retrieve_string(dest, "printer-location"), //location
                                 cups_retrieve_string(dest, "printer-make-and-model"),
                                 cups_is_accepting_jobs(dest),
                                 cups_printer_state(dest),
                                 "CUPS");

    GError *error = NULL;
    g_dbus_connection_emit_signal(b->dbus_connection,
                                  dialog_name,
                                  b->obj_path,
                                  "org.openprinting.PrintBackend",
                                  CPDB_SIGNAL_PRINTER_ADDED,
                                  gv,
                                  &error);
    g_assert_no_error(error);
}

void send_printer_removed_signal(BackendObj *b, const char *dialog_name, const char *printer_name)
{
    GError *error = NULL;
    g_dbus_connection_emit_signal(b->dbus_connection,
                                  dialog_name,
                                  b->obj_path,
                                  "org.openprinting.PrintBackend",
                                  CPDB_SIGNAL_PRINTER_REMOVED,
                                  g_variant_new("(ss)", printer_name, "CUPS"),
                                  &error);
    g_assert_no_error(error);
}

void send_printer_state_changed_signal(BackendObj *b, const char *dialog_name, const char *printer_name,
                                        const char *printer_state, gboolean printer_is_accepting_jobs)
{
    GError *error = NULL;
    g_dbus_connection_emit_signal(b->dbus_connection,
                                  dialog_name,
                                  b->obj_path,
                                  "org.openprinting.PrintBackend",
                                  CPDB_SIGNAL_PRINTER_STATE_CHANGED,
                                  g_variant_new("(ssbs)", printer_name, printer_state,
                                                printer_is_accepting_jobs, "CUPS"),
                                  &error);
    g_assert_no_error(error);
}

void notify_removed_printers(BackendObj *b, const char *dialog_name, GHashTable *new_table)
{
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    if (!d) return;

    GHashTable *prev = d->printers;
    GList *prevlist = g_hash_table_get_keys(prev);
    logdebug("Notifying removed printers.\n");
    gpointer printer_name = NULL;
    while (prevlist)
    {
        printer_name = (char *)(prevlist->data);
        if (!g_hash_table_contains(new_table, (gchar *)printer_name))
        {
            g_message("Printer %s removed\n", (char *)printer_name);
            send_printer_removed_signal(b, dialog_name, (char *)printer_name);
            remove_printer_from_dialog(b, dialog_name, (char *)printer_name);
        }
        prevlist = prevlist->next;
    }
}

void notify_added_printers(BackendObj *b, const char *dialog_name, GHashTable *new_table)
{
    GHashTableIter iter;
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    if (!d) return;

    GHashTable *prev = d->printers;
    logdebug("Notifying added printers.\n");
    gpointer printer_name;
    gpointer value;
    cups_dest_t *dest = NULL;
    g_hash_table_iter_init(&iter, new_table);
    while (g_hash_table_iter_next(&iter, &printer_name, &value))
    {
        if (!g_hash_table_contains(prev, (gchar *)printer_name))
        {
            g_message("Printer %s added\n", (char *)printer_name);
            dest = (cups_dest_t *)value;
            send_printer_added_signal(b, dialog_name, dest);
            add_printer_to_dialog(b, dialog_name, dest);
        }
    }
}

gboolean get_hide_remote(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    return d->hide_remote;
}
gboolean get_hide_temp(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    return d->hide_temp;
}
void refresh_printer_list(BackendObj *b, const char *dialog_name)
{
    GHashTable *new_printers;
    new_printers = cups_get_printers(get_hide_temp(b, dialog_name), get_hide_remote(b, dialog_name));
    notify_removed_printers(b, dialog_name, new_printers);
    notify_added_printers(b, dialog_name, new_printers);
}
GHashTable *get_dialog_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    if (d == NULL)
    {
        logerror("Invalid dialog name.\n");
        return NULL;
    }
    return d->printers;
}
PrinterCUPS *get_printer_by_name(BackendObj *b, const char *dialog_name, const char *printer_name)
{
    GHashTable *printers = get_dialog_printers(b, dialog_name);
    g_assert_nonnull(printers);
    PrinterCUPS *p = (g_hash_table_lookup(printers, printer_name));
    if (p == NULL)
    {
        logerror("Printer '%s' does not exist for the dialog %s.\n", printer_name, dialog_name);
        return NULL;
    }
    return p;
}
cups_dest_t *get_dest_by_name(BackendObj *b, const char *dialog_name, const char *printer_name)
{
    GHashTable *printers = get_dialog_printers(b, dialog_name);
    g_assert_nonnull(printers);
    PrinterCUPS *p = (g_hash_table_lookup(printers, printer_name));
    if (p == NULL)
    {
        logerror("Printer '%s' does not exist for the dialog %s.\n", printer_name, dialog_name);
	return NULL;
    }
    return p->dest;
}
/***************************PrinterObj********************************/
PrinterCUPS *get_new_PrinterCUPS(const cups_dest_t *dest)
{
    PrinterCUPS *p = (PrinterCUPS *)(malloc(sizeof(PrinterCUPS)));

    /** Make a copy of dest, because there are no guarantees 
     * whether dest will always exist or if it will be freed**/
    cups_dest_t *dest_copy = NULL;
    cupsCopyDest((cups_dest_t *)dest, 0, &dest_copy);
    if (dest_copy == NULL)
    {
        logerror("Error creating PrinterCUPS");
        return NULL;
    }
    p->dest = dest_copy;
    p->name = get_printer_name_for_cups_dest(dest_copy);
    p->http = NULL;
    p->dinfo = NULL;
    p->stream_socket_path = NULL;

    return p;
}

void free_PrinterCUPS(PrinterCUPS *p)
{
    logdebug("Freeing printerCUPS \n");
    cupsFreeDests(1, p->dest);
    g_free(p->name);
    if (p->dinfo)
    {
        cupsFreeDestInfo(p->dinfo);
    }
}

gboolean ensure_printer_connection(PrinterCUPS *p)
{
    if (p->http)
        return TRUE;

    int temp = FALSE;
    if (cups_is_temporary(p->dest)) temp = TRUE;

    p->http = cupsConnectDest(p->dest, CUPS_DEST_FLAGS_NONE, 300, NULL, NULL, 0, NULL, NULL);
    if (p->http == NULL)
        return FALSE;

    // update dest after temporary CUPS queue has been created
    if (temp)
    {
        cups_dest_t *new_dest = cupsGetNamedDest(p->http, p->name, NULL);
        cupsFreeDests(1, p->dest);
        p->dest = new_dest;
    }

    p->dinfo = cupsCopyDestInfo(p->http, p->dest);
    if (p->dinfo == NULL)
        return FALSE;

    return TRUE;
}

int get_supported(PrinterCUPS *p, char ***supported_values, const char *option_name)
{
    char **values;
    ensure_printer_connection(p);
    ipp_attribute_t *attrs =
        cupsFindDestSupported(p->http, p->dest, p->dinfo, option_name);
    int i, count = ippGetCount(attrs);
    if (!count)
    {
        *supported_values = NULL;
        return 0;
    }

    values = malloc(sizeof(char *) * count);

    for (i = 0; i < count; i++)
    {
        values[i] = extract_ipp_attribute(attrs, i, option_name);
    }
    *supported_values = values;
    return count;
}

char *get_orientation_default(PrinterCUPS *p)
{
    const char *def_value = cupsGetOption(CUPS_ORIENTATION, p->dest->num_options, p->dest->options);
    if (def_value)
    {
        switch (def_value[0])
        {
        case '0':
            return g_strdup("automatic-rotation");
        default:
            return g_strdup(ippEnumString(CUPS_ORIENTATION, atoi(def_value)));
        }
    }
    ensure_printer_connection(p);
    ipp_attribute_t *attr = NULL;

    attr = cupsFindDestDefault(p->http, p->dest, p->dinfo, CUPS_ORIENTATION);
    if (!attr)
        return g_strdup("NA");

    const char *str = ippEnumString(CUPS_ORIENTATION, ippGetInteger(attr, 0));
    if (strcmp("0", str) == 0)
        str = "automatic-rotation";
    return g_strdup(str);
}

int get_job_creation_attributes(PrinterCUPS *p, char ***values)
{
    return get_supported(p, values, "job-creation-attributes");
}

char *get_default(PrinterCUPS *p, char *option_name)
{
    /** first take care of special cases**/
    if (strcmp(option_name, CUPS_ORIENTATION) == 0)
        return get_orientation_default(p);

    /** Generic cases next **/
    ensure_printer_connection(p);
    ipp_attribute_t *def_attr = cupsFindDestDefault(p->http, p->dest, p->dinfo, option_name);
    const char *def_value = cupsGetOption(option_name, p->dest->num_options, p->dest->options);

    /** First check the option is already there in p->dest->options **/
    if (def_value)
    {
        if (def_attr && (ippGetValueTag(def_attr) == IPP_TAG_ENUM))
            return g_strdup(ippEnumString(option_name, atoi(def_value)));

        return g_strdup(def_value);
    }
    if (def_attr)
    {
        return extract_ipp_attribute(def_attr, 0, option_name);
    }
    return g_strdup("NA");
}
/**************Option************************************/
Option *get_NA_option()
{
    Option *o = (Option *)malloc(sizeof(Option));
    o->option_name = "NA";
    o->default_value = "NA";
    o->num_supported = 0;
    o->supported_values = cpdbNewCStringArray(1);
    o->supported_values[0] = "bub";

    return o;
}
void print_option(const Option *opt)
{
    g_message("%s", opt->option_name);
    int i;
    for (i = 0; i < opt->num_supported; i++)
    {
        logdebug(" %s\n", opt->supported_values[i]);
    }
    logdebug("****DEFAULT: %s\n", opt->default_value);
}
void free_options(int count, Option *opts)
{
    int i, j; /**Looping variables */
    for (i = 0; i < count; i++)
    {
        free(opts[i].option_name);
        for (j = 0; j < opts[i].num_supported; j++)
        {
            free(opts[i].supported_values[j]);
        }
        free(opts[i].supported_values);
        free(opts[i].default_value);
    }
    free(opts);
}
void unpack_option_array(GVariant *var, int num_options, Option **options)
{
    Option *opt = (Option *)(malloc(sizeof(Option) * num_options));
    int i, j;
    char *str;
    GVariantIter *iter;
    GVariantIter *array_iter;
    char *name, *default_val;
    int num_sup;
    g_variant_get(var, "a(ssia(s))", &iter);
    for (i = 0; i < num_options; i++)
    {
        //logdebug("i = %d\n", i);

        g_variant_iter_loop(iter, "(ssia(s))", &name, &default_val,
                            &num_sup, &array_iter);
        opt[i].option_name = g_strdup(name);
        opt[i].default_value = g_strdup(default_val);
        opt[i].num_supported = num_sup;
        opt[i].supported_values = cpdbNewCStringArray(num_sup);
        for (j = 0; j < num_sup; j++)
        {
            g_variant_iter_loop(array_iter, "(s)", &str);
            opt[i].supported_values[j] = g_strdup(str); //mem
        }
        print_option(&opt[i]);
    }

    *options = opt;
}
GVariant *pack_option(const Option *opt)
{
    char *group_name = cpdbGetGroup(opt->option_name);
    GVariant **t = g_new(GVariant *, 5);
    t[0] = g_variant_new_string(opt->option_name);
    t[1] = g_variant_new_string(group_name);
    t[2] = g_variant_new_string(opt->default_value);
    t[3] = g_variant_new_int32(opt->num_supported);
    t[4] = cpdbPackStringArray(opt->num_supported, opt->supported_values);
    GVariant *tuple_variant = g_variant_new_tuple(t, 5);
    g_free(t);
    free(group_name);
    return tuple_variant;
}
GVariant *pack_media(const Media *media)
{
	GVariant **t = g_new(GVariant *, 5);
	t[0] = g_variant_new_string(media->name);
	t[1] = g_variant_new_int32(media->width);
	t[2] = g_variant_new_int32(media->length);
	t[3] = g_variant_new_int32(media->num_margins);
	t[4] = cpdbPackMediaArray(media->num_margins, media->margins);
	GVariant *tuple_variant = g_variant_new_tuple(t, 5);
	g_free(t);
	return tuple_variant;
}
int get_all_options(PrinterCUPS *p, Option **options)
{
    ensure_printer_connection(p);

    char **option_names;
    int num_options = get_job_creation_attributes(p, &option_names); /** number of options to be returned**/

    /** Addition options not present in "job-creation-attributes" **/
    char *additional_options[] = {"media-source", "media-type"}; 
    int sz = sizeof(additional_options) / sizeof(char *);

    /** Add additional attributes to current option_names list **/
    option_names = realloc(option_names, sizeof(char *) * (num_options+sz)); 
    for (int i=0; i<sz; i++) 
        option_names[num_options+i] = g_strdup(additional_options[i]);
    num_options += sz;

    int i, j, optsIndex = 0;                                         /**Looping variables **/

    Option *opts = (Option *)(malloc(sizeof(Option) * (num_options+20))); /**Option array, which will be filled **/
    ipp_attribute_t *vals;                                                /** Variable to store the values of the options **/
    

    for (i = 0; i < num_options; i++)
    {
        // Hardcode CUPS specific option
        if(
            (strcmp(option_names[i], "booklet") == 0) ||
            (strcmp(option_names[i], "ipp-attribute-fidelity") == 0) ||
            (strcmp(option_names[i], "job-sheets") == 0) ||
            (strcmp(option_names[i], "media") == 0) ||
            (strcmp(option_names[i], "media-col") == 0) ||
            (strcmp(option_names[i], "mirror") == 0) ||
            (strcmp(option_names[i], "multiple-document-handling") == 0) ||
            (strcmp(option_names[i], "number-up") == 0) ||
            (strcmp(option_names[i], "number-up-layout") == 0) ||
            (strcmp(option_names[i], "orientation-requested") == 0) ||
            (strcmp(option_names[i], "page-border") == 0) ||
            (strcmp(option_names[i], "page-delivery") == 0) ||
            (strcmp(option_names[i], "page-set") == 0) ||
            (strcmp(option_names[i], "position") == 0) ||
            (strcmp(option_names[i], "print-scaling") == 0)
        )
            continue;

        opts[optsIndex].option_name = option_names[i];
        vals = cupsFindDestSupported(p->http, p->dest, p->dinfo, option_names[i]);
        if (vals)
            opts[optsIndex].num_supported = ippGetCount(vals);
        else
            opts[optsIndex].num_supported = 0;

        /** Retreive all the supported values for that option **/
        opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
        for (j = 0; j < opts[optsIndex].num_supported; j++)
        {
            opts[optsIndex].supported_values[j] = extract_ipp_attribute(vals, j, option_names[i]);
            if (opts[optsIndex].supported_values[j] == NULL)
            {
                opts[optsIndex].supported_values[j] = g_strdup("NA");
            }
        }

        /** Retrieve the default value for that option **/
        opts[optsIndex].default_value = get_default(p, option_names[i]);
        if (opts[optsIndex].default_value == NULL)
        {
            opts[optsIndex].default_value = g_strdup("NA");
        }

        optsIndex++;
    }

    /* Add the booklet option */
    opts[optsIndex].option_name = g_strdup("booklet");
    opts[optsIndex].num_supported = 3;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("off");
    opts[optsIndex].supported_values[1] = g_strdup("on");
    opts[optsIndex].supported_values[2] = g_strdup("shuffle-only");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the ipp-attribute-fidelity option */
    opts[optsIndex].option_name = g_strdup("ipp-attribute-fidelity");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("off");
    opts[optsIndex].supported_values[1] = g_strdup("on");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the job-sheets option */
    opts[optsIndex].option_name = g_strdup("job-sheets");
    opts[optsIndex].num_supported = 8;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("none");
    opts[optsIndex].supported_values[1] = g_strdup("classified");
    opts[optsIndex].supported_values[2] = g_strdup("confidential");
    opts[optsIndex].supported_values[3] = g_strdup("form");
    opts[optsIndex].supported_values[4] = g_strdup("secret");
    opts[optsIndex].supported_values[5] = g_strdup("standard");
    opts[optsIndex].supported_values[6] = g_strdup("topsecret");
    opts[optsIndex].supported_values[7] = g_strdup("unclassified");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup("none,none");
    }
    optsIndex++;

    /* Add the mirror option */
    opts[optsIndex].option_name = g_strdup("mirror");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("off");
    opts[optsIndex].supported_values[1] = g_strdup("on");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the multiple-document-handling option */
    opts[optsIndex].option_name = g_strdup("multiple-document-handling");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("separate-documents-uncollated-copies");
    opts[optsIndex].supported_values[1] = g_strdup("separate-documents-collated-copies");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the number-up option */
    opts[optsIndex].option_name = g_strdup("number-up");
    opts[optsIndex].num_supported = 6;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("1");
    opts[optsIndex].supported_values[1] = g_strdup("2");
    opts[optsIndex].supported_values[2] = g_strdup("4");
    opts[optsIndex].supported_values[3] = g_strdup("6");
    opts[optsIndex].supported_values[4] = g_strdup("9");
    opts[optsIndex].supported_values[5] = g_strdup("16");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the number-up-layout option */
    opts[optsIndex].option_name = g_strdup("number-up-layout");
    opts[optsIndex].num_supported = 8;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("lrtb");
    opts[optsIndex].supported_values[1] = g_strdup("lrbt");
    opts[optsIndex].supported_values[2] = g_strdup("rltb");
    opts[optsIndex].supported_values[3] = g_strdup("rlbt");
    opts[optsIndex].supported_values[4] = g_strdup("tblr");
    opts[optsIndex].supported_values[5] = g_strdup("tbrl");
    opts[optsIndex].supported_values[6] = g_strdup("btlr");
    opts[optsIndex].supported_values[7] = g_strdup("btrl");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the orientation-requested option */
    opts[optsIndex].option_name = g_strdup("orientation-requested");
    opts[optsIndex].num_supported = 4;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("3");
    opts[optsIndex].supported_values[1] = g_strdup("4");
    opts[optsIndex].supported_values[2] = g_strdup("5");
    opts[optsIndex].supported_values[3] = g_strdup("6");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    else
    {
        if (strcmp(opts[optsIndex].default_value, "potrait") == 0)
            opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
        else if (strcmp(opts[optsIndex].default_value, "landscape") == 0)
            opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[1]);
        else if (strcmp(opts[optsIndex].default_value, "reverse-landscape") == 0)
            opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[2]);
        else if (strcmp(opts[optsIndex].default_value, "reverse-potrait") == 0)
            opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[3]);
        else
            opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the page-border option */
    opts[optsIndex].option_name = g_strdup("page-border");
    opts[optsIndex].num_supported = 5;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("none");
    opts[optsIndex].supported_values[1] = g_strdup("single");
    opts[optsIndex].supported_values[2] = g_strdup("single-thick");
    opts[optsIndex].supported_values[3] = g_strdup("double");
    opts[optsIndex].supported_values[4] = g_strdup("double-thick");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the page-delivery option */
    opts[optsIndex].option_name = g_strdup("page-delivery");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("same-order");
    opts[optsIndex].supported_values[1] = g_strdup("reverse-order");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the page-set option */
    opts[optsIndex].option_name = g_strdup("page-set");
    opts[optsIndex].num_supported = 3;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("all");
    opts[optsIndex].supported_values[1] = g_strdup("even");
    opts[optsIndex].supported_values[2] = g_strdup("odd");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the position option */
    opts[optsIndex].option_name = g_strdup("position");
    opts[optsIndex].num_supported = 9;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("center");
    opts[optsIndex].supported_values[1] = g_strdup("top");
    opts[optsIndex].supported_values[2] = g_strdup("bottom");
    opts[optsIndex].supported_values[3] = g_strdup("left");
    opts[optsIndex].supported_values[4] = g_strdup("right");
    opts[optsIndex].supported_values[5] = g_strdup("top-left");
    opts[optsIndex].supported_values[6] = g_strdup("top-right");
    opts[optsIndex].supported_values[7] = g_strdup("bottom-left");
    opts[optsIndex].supported_values[8] = g_strdup("bottom-right");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the print-scaling option */
    opts[optsIndex].option_name = g_strdup("print-scaling");
    opts[optsIndex].num_supported = 5;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = g_strdup("auto");
    opts[optsIndex].supported_values[1] = g_strdup("auto-fit");
    opts[optsIndex].supported_values[2] = g_strdup("fill");
    opts[optsIndex].supported_values[3] = g_strdup("fit");
    opts[optsIndex].supported_values[4] = g_strdup("none");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the billing-info option */
    opts[optsIndex].option_name = g_strdup("billing-info");
    opts[optsIndex].num_supported = 0;
    opts[optsIndex].supported_values = NULL;
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = g_strdup("");
    }
    optsIndex++;

    /* Correct the print-quality option */
    for (i = 0; i < optsIndex; i++)
    {
        if (strcmp(opts[i].option_name, "print-quality") == 0)
        {
            for (j = 0; j < opts[i].num_supported; j++)
            {
                if (strcasecmp(opts[i].supported_values[j], "draft") == 0)
                {
                    free(opts[i].supported_values[j]);
                    opts[i].supported_values[j] = g_strdup("3");
                    continue;
                }
                if (strcasecmp(opts[i].supported_values[j], "normal") == 0)
                {
                    free(opts[i].supported_values[j]);
                    opts[i].supported_values[j] = g_strdup("4");
                    continue;
                }
                if (strcasecmp(opts[i].supported_values[j], "high") == 0)
                {
                    free(opts[i].supported_values[j]);
                    opts[i].supported_values[j] = g_strdup("5");
                    continue;
                }
            }

            if (strcasecmp(opts[i].default_value, "draft") == 0)
            {
                free(opts[i].default_value);
                opts[i].default_value = g_strdup("3");
                continue;
            }
            if (strcasecmp(opts[i].default_value, "normal") == 0)
            {
                free(opts[i].default_value);
                opts[i].default_value = g_strdup("4");
                continue;
            }
            if (strcasecmp(opts[i].default_value, "high") == 0)
            {
                free(opts[i].default_value);
                opts[i].default_value = g_strdup("5");
                continue;
            }

            break;
        }
    }

    *options = (Option *) realloc(opts, sizeof(Option) * optsIndex);
    return optsIndex;
}
int get_all_media(PrinterCUPS *p, Media **medias)
{	
	ensure_printer_connection(p);
	ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
	const char *uri = cupsGetOption("printer-uri-supported", 
									p->dest->num_options,
									p->dest->options);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
                 "printer-uri", NULL, uri);
    const char *const requested_attributes[] = {"media-col-database"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                  "requested-attributes", 1, NULL,
                  requested_attributes);

    ipp_t *response = cupsDoRequest(p->http, request, "/");
    if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
    {
        /* request failed */
        logerror("Request failed: %s\n", cupsLastErrorString());
        return 0;
    }
    
    int media_num = 0;				/** Number of unqiue media sizes **/
    Media *meds = NULL;				/** Array of unique media sizes **/

    ipp_attribute_t *mdb; // media database
    if ((mdb = ippFindAttribute(response, "media-col-database", IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
		int i, j;
		gpointer key, value;		/** Looping variables **/
		
        const char *name;			/** PWG name for a media **/
		int width, length;			/** Width and length of a media **/
		pwg_media_t *pwg_media;
		
		ipp_t *tuple; 				/** Single media entry in media-col-database **/
		ipp_t *media_size;			/** media-size collection in a media tuple **/ 
		ipp_attribute_t *attr;		/** Temporary variable for ipp attributes in a single tuple **/
		
		typedef struct Margin {
			int left;
			int right;
			int top;
			int bottom;
		} Margin;
        
        Margin *margin;				/** Single margin struct for some media size**/
		GList *margins, *listIter;	/** List of all different margins for some media size **/
		GHashTable *table;			/** [media-size]-->[margins] **/
		GHashTableIter iter;		/** For iterating over the table **/
		
		table = g_hash_table_new(g_str_hash, g_str_equal);
		
		int count = ippGetCount(mdb);
		for (int i = 0; i < count; i++)
		{
			tuple = ippGetCollection(mdb, i);
			
			attr = ippFindAttribute(tuple, "media-size", IPP_TAG_BEGIN_COLLECTION);
			media_size = ippGetCollection(attr, 0);
			attr = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
			width = ippGetInteger(attr, 0);
			attr = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);
			length = ippGetInteger(attr, 0);

			if (width <= 0 || length <= 0)
			  continue;

			pwg_media = pwgMediaForSize(width, length);
			name = pwg_media->pwg;

			margin = g_new0(Margin, 1);

			attr = ippFindAttribute(tuple, "media-left-margin", IPP_TAG_INTEGER);
			margin->left = ippGetInteger(attr, 0);
			attr = ippFindAttribute(tuple, "media-right-margin", IPP_TAG_INTEGER);
			margin->right = ippGetInteger(attr, 0);
			attr = ippFindAttribute(tuple, "media-top-margin", IPP_TAG_INTEGER);
			margin->top = ippGetInteger(attr, 0);
			attr = ippFindAttribute(tuple, "media-bottom-margin", IPP_TAG_INTEGER);
			margin->bottom = ippGetInteger(attr, 0);
			
			margins = g_hash_table_lookup(table, name);
			margins = g_list_prepend(margins, margin);
			g_hash_table_replace(table, (gpointer) name, margins);
		}
		
		media_num = g_hash_table_size(table);
        meds = g_new0 (Media, media_num);
		
		i = 0;
		g_hash_table_iter_init(&iter, table);
		while (g_hash_table_iter_next(&iter, &key, &value))
		{
            name = (char *) key;
            pwg_media = pwgMediaForPWG(name);

			margins = (GList *) value;
            margins = g_list_reverse(margins);
            
            meds[i].name = g_strdup(name);
            meds[i].width = pwg_media->width;
            meds[i].length = pwg_media->length;
            meds[i].num_margins = g_list_length(margins);
            meds[i].margins = malloc(sizeof(int) * meds[i].num_margins * 4);
            
            j = 0;
            listIter = margins;
            while (listIter != NULL)
            {
				margin = (Margin *) listIter->data;
				
				meds[i].margins[j][0] = margin->left;
				meds[i].margins[j][1] = margin->right;
				meds[i].margins[j][2] = margin->top;
				meds[i].margins[j][3] = margin->bottom;

				free(margin);

				listIter = listIter->next;
				j++;
			}
			g_list_free(margins);
			i++;
		}

		g_hash_table_destroy(table);
	}
	
	ippDelete(response);
	
	*medias = meds;
	return media_num;
}
int add_media_to_options(PrinterCUPS *p, Media *medias, int media_count, Option **options, int count)
{
    int i, j;							/** Looping variables **/
    int num_media;						/** Variable for number of "media" supported using CUPS call **/
    char *media_name;					/** Variable for media name **/
    int optsIndex = count;				/** Index for fillings options **/
    ipp_t *media_col;				/** media_col collection in IPP request **/
    ipp_attribute_t *vals, *default_val, *attr;

    count += 5;	/** "media", "media-{top, bottom, left, right}-margins" **/
    Option *opts = *options;
    
    opts = realloc(opts, sizeof(Option) * count);
    
     /* Add the media option */
    opts[optsIndex].option_name = g_strdup("media");
	opts[optsIndex].num_supported = media_count;
	opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported + 2);	/** 2 extra for custom_min and custom_max sizes **/
	for (i = 0; i < opts[optsIndex].num_supported; i++)
    {
        opts[optsIndex].supported_values[i] = g_strdup(medias[i].name);
    }

    opts[optsIndex].default_value = get_default(p, "media");
    if (opts[optsIndex].default_value == NULL)
    {
        opts[optsIndex].default_value = g_strdup("NA");
    }
    
    /** Add custom_min and custom_max media if they exist **/
    vals = cupsFindDestSupported(p->http, p->dest, p->dinfo, "media");
    if (vals)
		num_media = ippGetCount(vals);
	else
		num_media = 0;
	
	for (j = 0; j < num_media && i < (media_count + 2); j++)
	{
		media_name = extract_ipp_attribute(vals, i, "media");
		
		if (media_name == NULL)
			continue;
		
		if (strncmp(media_name, "custom_min", 10) == 0 || strncmp(media_name, "custom_max", 10) == 0)
		{
            opts[optsIndex].supported_values[i] = g_strdup(media_name);
			i++;
		}
		
		free(media_name);
	}
	opts[optsIndex].num_supported = media_count = i;
	
	optsIndex++;
    
    /* Add the media-{top,left,right,bottom}-margin option */
    char def[16];
    char *attrs[] = {"media-left-margin", "media-bottom-margin", "media-top-margin", "media-right-margin"};

    default_val = cupsFindDestDefault(p->http, p->dest, p->dinfo, "media-col");
    
    for (i = 0; i < 4; i++) // for each attr in attrs
    {
        vals = cupsFindDestSupported(p->http, p->dest, p->dinfo, attrs[i]);
        opts[optsIndex].option_name = g_strdup(attrs[i]);
        if (vals)
            opts[optsIndex].num_supported = ippGetCount(vals);
        else
            opts[optsIndex].num_supported = 0;

        opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
        for (j = 0; j < opts[optsIndex].num_supported; j++)
        {
            opts[optsIndex].supported_values[j] = extract_ipp_attribute(vals, j, attrs[i]);
            if (opts[optsIndex].supported_values[j] == NULL)
            {
                opts[optsIndex].supported_values[j] = g_strdup("NA");
            }
        }

        media_col = ippGetCollection(default_val, 0);
        attr = ippFindAttribute(media_col, attrs[i], IPP_TAG_INTEGER);
        snprintf(def, 16, "%d", ippGetInteger(attr, 0));

        opts[optsIndex].default_value = g_strdup(def);
        if (opts[optsIndex].default_value == NULL)
        {
            opts[optsIndex].default_value = g_strdup("NA");
        }

        optsIndex++;
    }

    *options = opts;
    return count;
}
const char *get_printer_state(PrinterCUPS *p)
{
    const char *str = NULL;
    ensure_printer_connection(p);
    ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    const char *uri = cupsGetOption("printer-uri-supported",
                                    p->dest->num_options,
                                    p->dest->options);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
                 "printer-uri", NULL, uri);
    const char *const requested_attributes[] = {"printer-state"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                  "requested-attributes", 1, NULL,
                  requested_attributes);

    ipp_t *response = cupsDoRequest(p->http, request, "/");
    if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
    {
        /* request failed */
        logerror("Request failed: %s\n", cupsLastErrorString());
        return "NA";
    }

    ipp_attribute_t *attr;
    if ((attr = ippFindAttribute(response, "printer-state",
                                 IPP_TAG_ENUM)) != NULL)
    {
        logdebug("printer-state=%d\n", ippGetInteger(attr, 0));
        str = map->state[ippGetInteger(attr, 0)];
    }
    return str;
}



void print_socket(PrinterCUPS *p, int num_settings, GVariant *settings, char *job_id_str, char *socket_path, const char *title)
{
    ensure_printer_connection(p);
    int num_options = 0;
    cups_option_t *options;

    GVariantIter *iter;
    g_variant_get(settings, "a(ss)", &iter);

    int i = 0;
    char *option_name, *option_value;
    for (i = 0; i < num_settings; i++)
    {
        g_variant_iter_loop(iter, "(ss)", &option_name, &option_value);
        logdebug(" %s : %s\n", option_name, option_value);

        /**
         * to do:
         * instead of directly adding the option,convert it from the frontend's lingo 
         * to the specific lingo of the backend
         * 
         * use PWG names instead
         */
        num_options = cupsAddOption(option_name, option_value, num_options, &options);
    }
   
    
    // Prepare the socket connection
    
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(socket_fd ==-1) {
   	perror("Error creating socket");
	return;
    }
       
    char *home = getenv("HOME");
    char base_dir[512];
    char socket_dir[512];

   snprintf(base_dir, sizeof(base_dir), "%s/cpdb" , home);
    if(mkdir(base_dir, 0700) == -1 && errno != EEXIST){
    	perror("Error creating base directory");
    }

    snprintf(socket_dir, sizeof(socket_dir), "%s/sockets", home);
    if(mkdir(socket_dir, 0700) == -1 && errno != EEXIST) { 
	perror("Error creating sockets directory");	
     }
    
   // socket option
    int socket_option = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(socket_option));

    // Create the CUPS JOB

    int job_id = 0;
    if(cupsCreateDestJob(p->http, p->dest, p->dinfo, &job_id, title,
       num_options, options) != IPP_STATUS_OK) { 
	   printf("job not created: %s\n" , cupsLastErrorString());
           close(socket_fd);
	   return;
  }
     snprintf(job_id_str, 32, "%d", job_id);

     snprintf(socket_path, 1024, "%s/cups-%s.sock", socket_dir, job_id_str);
	p->stream_socket_path = g_strdup(socket_path);

       
     struct sockaddr_un server_addr;
     memset(&server_addr, 0, sizeof(server_addr));
     server_addr.sun_family = AF_UNIX;
     strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);
        unlink(socket_path);
	if(bind(socket_fd, (struct sockaddr *)&server_addr , sizeof(server_addr)) == -1){
		perror("Bind failed");
		close(socket_fd);
		return;
	} 
   	
         
	 if(listen(socket_fd, 1) == -1) {
	 perror("listen failed");
	 close(socket_fd);
	 return;
	}
        // start cups document
	if(cupsStartDestDocument(p->http, p->dest, p->dinfo, job_id, title, CUPS_FORMAT_AUTO,
	   num_options, options, 1) != HTTP_STATUS_CONTINUE) {
		printf("could not start document: %s\n", cupsLastErrorString());
		close(socket_fd);
		return;
	}
	  printf("Backend running and listening on: %s\n", socket_path);
    // Create a struct to pass data to the thread
    PrintDataThreadData *thread_data = g_malloc(sizeof(PrintDataThreadData));
    thread_data->printer = p;
    thread_data->num_options = num_options;
    thread_data->options = options;
    thread_data->socket_fd = socket_fd;

    // Create a thread for handling data transfer to CUPS
    pthread_t thread;
    if (pthread_create(&thread, NULL, print_data_thread, thread_data) != 0) {
        perror("Error creating thread");
        close(socket_fd);
    } else {
        // Detach the thread to allow it to run independently
        pthread_detach(thread);
	}
    

}

static void *print_data_thread(void *data) {
    PrintDataThreadData *thread_data = (PrintDataThreadData *)data;

    // Allocate dynamic memory for the buffer within the thread
    char *buffer = g_malloc(1024);

    // Accept incoming connections
    int client_fd = accept(thread_data->socket_fd, NULL, NULL);
    if (client_fd == -1) {
        perror("Error accepting connection");
        close(thread_data->socket_fd);
    }

    // Placeholder logic for reading data from the socket
    ssize_t bytesRead;
    while ((bytesRead = read(client_fd, buffer, 1024)) > 0) {
        // Send data to CUPS using cupsWriteRequestData
        http_status_t http_status = cupsWriteRequestData(thread_data->printer->http, buffer, bytesRead);
        if (http_status != HTTP_STATUS_CONTINUE) {
            logerror("Error writing print data to server.\n");
            break;
        }
    }

    // Cleanup and free resources
    close(thread_data->socket_fd);
    if (cupsFinishDestDocument(thread_data->printer->http, thread_data->printer->dest, thread_data->printer->dinfo) == IPP_STATUS_OK)
        logdebug("Document send succeeded.\n");
    else
        logerror("Document send failed: %s\n", cupsLastErrorString());
    cupsFreeOptions(thread_data->num_options, thread_data->options);
    g_free(thread_data);
    g_free(buffer);

    return NULL;
}

void printAllJobs(PrinterCUPS *p)
{
    ensure_printer_connection(p);
    cups_job_t *jobs;
    int num_jobs = cupsGetJobs2(p->http, &jobs, p->name, 1, CUPS_WHICHJOBS_ALL);
    for (int i = 0; i < num_jobs; i++)
    {
        print_job(&jobs[i]);
    }
}

/**********Dialog related funtions ****************/
Dialog *get_new_Dialog()
{
    Dialog *d = g_new(Dialog, 1);
    d->cancel = 0;
    d->hide_remote = FALSE;
    d->hide_temp = FALSE;
    d->keep_alive = FALSE;
    d->printers = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        (GDestroyNotify)free_string,
                                        (GDestroyNotify)free_PrinterCUPS);
    return d;
}

void free_Dialog(Dialog *d)
{
    logdebug("freeing dialog..\n");
    g_hash_table_destroy(d->printers);
    free(d);
}

/*********Mappings********/
Mappings *get_new_Mappings()
{
    Mappings *m = (Mappings *)(malloc(sizeof(Mappings)));
    m->state[3] = CPDB_STATE_IDLE;
    m->state[4] = CPDB_STATE_PRINTING;
    m->state[5] = CPDB_STATE_STOPPED;

    m->orientation[atoi(CUPS_ORIENTATION_LANDSCAPE)] = CPDB_ORIENTATION_LANDSCAPE;
    m->orientation[atoi(CUPS_ORIENTATION_PORTRAIT)] = CPDB_ORIENTATION_PORTRAIT;
    return m;
}

/*****************CUPS and IPP helpers*********************/
const char *cups_printer_state(cups_dest_t *dest)
{
    //cups_dest_t *dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, printer_name, NULL);
    g_assert_nonnull(dest);
    const char *state = cupsGetOption("printer-state", dest->num_options,
                                      dest->options);
    if (state == NULL)
        return "NA";
    return map->state[state[0] - '0'];
}

gboolean cups_is_accepting_jobs(cups_dest_t *dest)
{

    g_assert_nonnull(dest);
    const char *val = cupsGetOption("printer-is-accepting-jobs", dest->num_options,
                                    dest->options);

    return cpdbGetBoolean(val);
}

void cups_get_Resolution(cups_dest_t *dest, int *xres, int *yres)
{
    http_t *http = cupsConnectDest(dest, CUPS_DEST_FLAGS_NONE, 500, NULL, NULL, 0, NULL, NULL);
    g_assert_nonnull(http);
    cups_dinfo_t *dinfo = cupsCopyDestInfo(http, dest);
    g_assert_nonnull(dinfo);
    ipp_attribute_t *attr = cupsFindDestDefault(http, dest, dinfo, "printer-resolution");
    ipp_res_t units;
    *xres = ippGetResolution(attr, 0, yres, &units);
}

int add_printer_to_ht(void *user_data, unsigned flags, cups_dest_t *dest)
{
    GHashTable *h = (GHashTable *)user_data;
    char *printername = get_printer_name_for_cups_dest(dest);
    cups_dest_t *dest_copy = NULL;
    cupsCopyDest(dest, 0, &dest_copy);
    g_hash_table_insert(h, printername, dest_copy);

    return 1;
}
int add_printer_to_ht_no_temp(void *user_data, unsigned flags, cups_dest_t *dest)
{
    if (cups_is_temporary(dest))
        return 1;
    GHashTable *h = (GHashTable *)user_data;
    char *printername = get_printer_name_for_cups_dest(dest);
    cups_dest_t *dest_copy = NULL;
    cupsCopyDest(dest, 0, &dest_copy);
    g_hash_table_insert(h, printername, dest_copy);
    return 1;
}

int add_printer_to_ht_no_remote(void *user_data, unsigned flags, cups_dest_t *dest)
{
    if (cups_is_remote(dest))
        return 1;
    GHashTable *h = (GHashTable *)user_data;
    char *printername = get_printer_name_for_cups_dest(dest);
    cups_dest_t *dest_copy = NULL;
    cupsCopyDest(dest, 0, &dest_copy);
    g_hash_table_insert(h, printername, dest_copy);
    return 1;
}

GHashTable *cups_get_printers(gboolean notemp, gboolean noremote)
{
    cups_dest_cb_t cb = add_printer_to_ht;
    unsigned type = 0, mask = 0;
    if (noremote)
    {
        cb = add_printer_to_ht_no_remote;
    }
    if (notemp)
    {
        cb = add_printer_to_ht_no_temp;
    }

    GHashTable *printers_ht = g_hash_table_new(g_str_hash, g_str_equal);
    cupsEnumDests(CUPS_DEST_FLAGS_NONE,
                  3000,         //timeout
                  NULL,         //cancel
                  type,         //TYPE
                  mask,         //MASK
                  cb,           //function
                  printers_ht); //user_data

    return printers_ht;
}
GHashTable *cups_get_all_printers()
{
    logdebug("all printers\n");
    // to do : fix
    GHashTable *printers_ht = g_hash_table_new(g_str_hash, g_str_equal);
    cupsEnumDests(CUPS_DEST_FLAGS_NONE,
                  3000,              //timeout
                  NULL,              //cancel
                  0,                 //TYPE
                  0,                 //MASK
                  add_printer_to_ht, //function
                  printers_ht);      //user_data

    return printers_ht;
}
GHashTable *cups_get_local_printers()
{
    logdebug("local printers\n");
    //to do: fix
    GHashTable *printers_ht = g_hash_table_new(g_str_hash, g_str_equal);
    cupsEnumDests(CUPS_DEST_FLAGS_NONE,
                  1200,                //timeout
                  NULL,                //cancel
                  CUPS_PRINTER_LOCAL,  //TYPE
                  CUPS_PRINTER_REMOTE, //MASK
                  add_printer_to_ht,   //function
                  printers_ht);        //user_data

    return printers_ht;
}
char *cups_retrieve_string(cups_dest_t *dest, const char *option_name)
{
    /** this funtion is kind of a wrapper , to ensure that the return value is never NULL
    , as that can cause the backend to segFault
    **/
    g_assert_nonnull(dest);
    g_assert_nonnull(option_name);
    char *ans = NULL;
    ans = g_strdup(cupsGetOption(option_name, dest->num_options, dest->options));

    if (ans)
        return ans;

    return g_strdup("NA");
}

gboolean cups_is_temporary(cups_dest_t *dest)
{
    g_assert_nonnull(dest);
    if (cupsGetOption("printer-uri-supported", dest->num_options, dest->options))
        return FALSE;
    return TRUE;
}

char *extractHostFromURI(const char *uri) {
    const char *host_start, *host_end;
    char *host = NULL;

    // Find the start of the host part
    host_start = strstr(uri, "://");
    if (host_start != NULL) {
        host_start += 3; // Move past "://"
    } else {
        return NULL;
    }

    // Find the end of the host part
    const char *slash_pos = strchr(host_start, '/');
    const char *colon_pos = strchr(host_start, ':');

    if (slash_pos == NULL && colon_pos == NULL) {
        host_end = host_start + strlen(host_start); // If no "/" or ":", host extends to end of string
    } else if (slash_pos == NULL) {
        host_end = colon_pos;
    } else if (colon_pos == NULL) {
        host_end = slash_pos;
    } else {
        host_end = (slash_pos < colon_pos) ? slash_pos : colon_pos;
    }

    // Allocate memory for the host part
    host = (char *)malloc(host_end - host_start + 1);
    if (host != NULL) {
        // Copy the host part
        strncpy(host, host_start, host_end - host_start);
        host[host_end - host_start] = '\0'; // Null-terminate the string
    }

    fprintf(stderr, "XXX12: URI: %s Host: %s\n", uri, host);

    return host;
}

gboolean checkRemote(const char *uri) {
    //GET LOCALHOST ADDRESSES
    char hostname[1024];

    // Get the hostname of the local machine
    if (gethostname(hostname, sizeof(hostname)) == -1) {
        perror("gethostname");
        return 1;
    }

    strcat(hostname, ".local");

    // Get the network IP addresses associated with the hostname
    struct addrinfo hints, *res, *p;
    int status;
    int num_addresses = 0;
    AddressList addresses[MAX_ADDRESSES];

    // Get the IPv4 addresses associated with the hostname
    {

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; // IPv4
        hints.ai_socktype = SOCK_STREAM;

        if ((status = getaddrinfo(hostname, NULL, &hints, &res)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
            return 1;
        }

        // Iterate over the linked list of addrinfo structures
        for (p = res; p != NULL && num_addresses < MAX_ADDRESSES; p = p->ai_next) {
            inet_ntop(p->ai_family, &((struct sockaddr_in *)p->ai_addr)->sin_addr, addresses[num_addresses].ipstr, sizeof(addresses[num_addresses].ipstr));
            addresses[num_addresses].family = p->ai_family;
            num_addresses++;
        }

        freeaddrinfo(res); // Free the linked list
    }

    // Get the IPv6 addresses associated with the hostname
    {

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6; // IPv6
        hints.ai_socktype = SOCK_STREAM;

        if ((status = getaddrinfo(hostname, NULL, &hints, &res)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
            return 1;
        }

        // Iterate over the linked list of addrinfo structures
        for (p = res; p != NULL && num_addresses < MAX_ADDRESSES; p = p->ai_next) {
            inet_ntop(p->ai_family, &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr, addresses[num_addresses].ipstr, sizeof(addresses[num_addresses].ipstr));
            addresses[num_addresses].family = p->ai_family;
            num_addresses++;
        }

        freeaddrinfo(res); // Free the linked list
    }

    // COMPARE LOCAL IP ADDRESSES WITH THAT OF THE URI
    struct addrinfo *uri_res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const char* new_uri = extractHostFromURI(uri);

    if ((status = getaddrinfo(new_uri, NULL, &hints, &uri_res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return 1;
    }


    for (p = uri_res; p != NULL; p = p->ai_next) {
        void *addr;
        char ipstr[INET6_ADDRSTRLEN];

        // Get the pointer to the address itself,
        // different fields in IPv4 and IPv6:
        if (p->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
        }

        // Convert the IP to a string
        inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr));

        // Check if this IP address matches any of the addresses associated with the local hostname
        for (int i = 0; i < num_addresses; i++) {
            if (p->ai_family == addresses[i].family && strcmp(ipstr, addresses[i].ipstr) == 0) {
                return FALSE;
            }
        }
    }

    freeaddrinfo(uri_res); // Free the linked list
    return TRUE;
}

gboolean cups_is_remote(cups_dest_t *dest)
{
    g_assert_nonnull(dest);
    const char *device_uri = cupsGetOption("device-uri", dest->num_options, dest->options);

    const char *uri = cfResolveURI(device_uri);

    fprintf(stderr, "XXX10: deviceURI: %s \n", device_uri);
    fprintf(stderr, "XXX11: cfURI: %s \n", uri);

    // Check for "localhost"
    if (strstr(uri, "localhost") != NULL) {
        return FALSE;
    }
    
    // Check for "::1"
    if (strstr(uri, "::1") != NULL) {
        return FALSE;
    }
    
    // Check for IPv4 IP addresses starting with "127"
    if (strncmp(uri, "127.", 4) == 0) {
        return FALSE;
    }

    // Check if the URI starts with "usb://" or "parallel://"
    if (strncmp(uri, "usb://", 6) == 0 || strncmp(uri, "parallel://", 11) == 0) {
        return FALSE;
    }

    return checkRemote(uri);
}

char *extract_ipp_attribute(ipp_attribute_t *attr, int index, const char *option_name)
{
    /** first deal with the totally unique cases **/
    if (strcmp(option_name, CUPS_ORIENTATION) == 0)
        return extract_orientation_from_ipp(attr, index);

    /** Then deal with the generic cases **/
    char *str;
    const char *attrstr;
    switch (ippGetValueTag(attr))
    {
    case IPP_TAG_INTEGER:
        #define TAG_INTEGER_LEN 50
        str = (char *)(malloc(sizeof(char) * TAG_INTEGER_LEN));
        snprintf(str, TAG_INTEGER_LEN, "%d", ippGetInteger(attr, index));
        break;

    case IPP_TAG_ENUM:
        attrstr = ippEnumString(option_name, ippGetInteger(attr, index));
	str = strdup(attrstr);
        break;

    case IPP_TAG_RANGE:
        #define TAG_RANGE_LEN 100
        str = (char *)(malloc(sizeof(char) * TAG_RANGE_LEN));
        int upper, lower = ippGetRange(attr, index, &upper);
        snprintf(str, TAG_RANGE_LEN, "%d-%d", lower, upper);
        break;

    case IPP_TAG_RESOLUTION:
        return extract_res_from_ipp(attr, index);
    default:
        return extract_string_from_ipp(attr, index);
    }

    return str;
}

char *extract_res_from_ipp(ipp_attribute_t *attr, int index)
{
    int xres, yres;
    ipp_res_t units;
    xres = ippGetResolution(attr, index, &yres, &units);

    char *unit = units == IPP_RES_PER_INCH ? "dpi" : "dpcm";
    char buf[100];
    if (xres == yres)
        snprintf(buf, sizeof(buf), "%d%s", xres, unit);
    else
        snprintf(buf, sizeof(buf), "%dx%d%s", xres, yres, unit);

    return g_strdup(buf);
}

char *extract_string_from_ipp(ipp_attribute_t *attr, int index)
{
    return g_strdup(ippGetString(attr, index, NULL));
}

char *extract_orientation_from_ipp(ipp_attribute_t *attr, int index)
{
    char *str = g_strdup(ippEnumString(CUPS_ORIENTATION, ippGetInteger(attr, index)));
    if (strcmp("0", str) == 0)
        str = g_strdup("automatic-rotation");
    return str;
}

void print_job(cups_job_t *j)
{
    logdebug("title : %s\n", j->title);
    logdebug("dest : %s\n", j->dest);
    logdebug("job-id : %d\n", j->id);
    logdebug("user : %s\n", j->user);

    char *state = translate_job_state(j->state);
    logdebug("state : %s\n", state);
}

char *get_option_translation(PrinterCUPS *p,
                             const char *option_name,
                             const char *locale)
{
    char *copy;
    const char *uri, *translation;
    static const char *const req_attrs[] = {"printer-strings-uri"};
    ipp_attribute_t *attr;
    ipp_t *request, *response;
    cups_array_t *opts_catalog, *printer_opts_catalog;

    ensure_printer_connection(p);
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    uri = cupsGetOption("printer-uri-supported", 
                        p->dest->num_options,
                        p->dest->options);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
                    "printer-uri", NULL, uri);
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                    "requested-attributes", 1, NULL, req_attrs);
    response = cupsDoRequest(p->http, request, "/");
    if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
    {
        /* request failed */
        logerror("Request failed: %s\n", cupsLastErrorString());
        return g_strdup(option_name);
    }

    opts_catalog = cfCatalogOptionArrayNew();
    cfCatalogLoad(NULL, locale, opts_catalog);
    printer_opts_catalog = cfCatalogOptionArrayNew();
    if ((attr = ippFindAttribute(response, "printer-strings-uri",
                                    IPP_TAG_URI)) != NULL)
    {
        cfCatalogLoad(ippGetString(attr, 0, NULL), NULL, printer_opts_catalog);
    }

    translation = cfCatalogLookUpOption((char *)option_name, 
                                        opts_catalog, printer_opts_catalog);
    copy = g_strdup(translation);
    cupsArrayDelete(opts_catalog);
    cupsArrayDelete(printer_opts_catalog);
    return copy;
}

char *get_choice_translation(PrinterCUPS *p,
                             const char *option_name,
                             const char *choice_name,
                             const char *locale)
{
    char *copy;
    const char *uri, *translation;
    static const char *const req_attrs[] = {"printer-strings-uri"};
    ipp_attribute_t *attr;
    ipp_t *request, *response;
    cups_array_t *opts_catalog, *printer_opts_catalog;

    ensure_printer_connection(p);
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    uri = cupsGetOption("printer-uri-supported", 
                        p->dest->num_options,
                        p->dest->options);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
                    "printer-uri", NULL, uri);
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                    "requested-attributes", 1, NULL, req_attrs);
    response = cupsDoRequest(p->http, request, "/");
    if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
    {
        /* request failed */
        logerror("Request failed: %s\n", cupsLastErrorString());
        return g_strdup(choice_name);
    }

    opts_catalog = cfCatalogOptionArrayNew();
    cfCatalogLoad(NULL, locale, opts_catalog);
    printer_opts_catalog = cfCatalogOptionArrayNew();
    if ((attr = ippFindAttribute(response, "printer-strings-uri",
                                    IPP_TAG_URI)) != NULL)
    {
        cfCatalogLoad(ippGetString(attr, 0, NULL), NULL, printer_opts_catalog);
    }

    translation = cfCatalogLookUpChoice((char *)choice_name, (char *)option_name,
                                        opts_catalog, printer_opts_catalog);
    copy = g_strdup(translation);
    cupsArrayDelete(opts_catalog);
    cupsArrayDelete(printer_opts_catalog);
    return copy;
}

GVariant *get_printer_translations(PrinterCUPS *p, const char *locale)
{
    int num_opts;
    Option *opts;
    GVariant *translations;
    GVariantBuilder *builder;

    char *group;
    char *name_tr, *group_tr, *choice_tr;
    char *name_key, *group_key, *choice_key;

    num_opts = get_all_options(p, &opts);
    builder = g_variant_builder_new(G_VARIANT_TYPE(CPDB_TL_DICT_ARGS));
    for (int i = 0; i < num_opts; i++)
    {
        /* add translation for option name */
        name_tr = get_option_translation(p, opts[i].option_name, locale);
        name_key = cpdbConcatSep(CPDB_OPT_PREFIX, opts[i].option_name);
        if (name_tr)
        {
            logdebug("Translation '%s' : '%s'\n", name_key, name_tr);
            g_variant_builder_add(builder, CPDB_TL_ARGS, name_key, name_tr);
        }
        g_free(name_tr);

        /* add translation for option group */
        group = cpdbGetGroup(opts[i].option_name);
        group_key = cpdbConcatSep(CPDB_GRP_PREFIX, group);
        group_tr = cpdbGetGroupTranslation2(group, locale);
        if (group_tr)
        {
            logdebug("Translation '%s' : '%s'\n", group_key, group_tr);
            g_variant_builder_add(builder, CPDB_TL_ARGS, group_key, group_tr);
        }
        g_free(group);
        g_free(group_key);
        g_free(group_tr);

        /* add translation for option choices */
        for (int j = 0; j < opts[i].num_supported; j++)
        {
            choice_tr = get_choice_translation(p, opts[i].option_name, opts[i].supported_values[j], locale);
            choice_key = cpdbConcatSep(name_key, opts[i].supported_values[j]);
            if (choice_tr)
            {
                logdebug("Translation '%s' : '%s'\n", choice_key, choice_tr);
                g_variant_builder_add(builder, CPDB_TL_ARGS, choice_key, choice_tr);
            }
            g_free(choice_key);
            g_free(choice_tr);
        }

        g_free(name_key);
    }
    translations = g_variant_builder_end(builder);
    free_options(num_opts, opts);

    return translations;
}

char *translate_job_state(ipp_jstate_t state)
{
    switch (state)
    {
    case IPP_JSTATE_ABORTED:
        return CPDB_JOB_STATE_ABORTED;
    case IPP_JSTATE_CANCELED:
        return CPDB_JOB_STATE_CANCELLED;
    case IPP_JSTATE_HELD:
        return CPDB_JOB_STATE_HELD;
    case IPP_JSTATE_PENDING:
        return CPDB_JOB_STATE_PENDING;
    case IPP_JSTATE_PROCESSING:
        return CPDB_JOB_STATE_PRINTING;
    case IPP_JSTATE_STOPPED:
        return CPDB_JOB_STATE_STOPPED;
    case IPP_JSTATE_COMPLETED:
        return CPDB_JOB_STATE_COMPLETED;
    default:
        return "NA";
    }
}

GVariant *pack_cups_job(cups_job_t job)
{
    logdebug("%s\n", job.dest);
    GVariant **t = g_new0(GVariant *, 7);
    char jobid[20];
    snprintf(jobid, sizeof(jobid), "%d", job.id);
    t[0] = g_variant_new_string(jobid);
    t[1] = g_variant_new_string(job.title);
    t[2] = g_variant_new_string(job.dest);
    t[3] = g_variant_new_string(job.user);
    t[4] = g_variant_new_string(translate_job_state(job.state));
    t[5] = g_variant_new_string(httpGetDateString(job.creation_time));
    t[6] = g_variant_new_int32(job.size);
    GVariant *tuple_variant = g_variant_new_tuple(t, 7);
    g_free(t);
    return tuple_variant;
}

void free_string(char *str)
{
    if (str)
    {
        free(str);
    }
}
