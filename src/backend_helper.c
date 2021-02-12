#include "backend_helper.h"
Mappings *map;
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
    /** If it was  previously querie, don't query again */
    if (b->default_printer)
    {
        return b->default_printer;
    }

    /**first query to see if the user default printer is set**/
    int num_dests;
    cups_dest_t *dests;
    num_dests = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);          /** Get the list of all destinations */
    cups_dest_t *dest = cupsGetDest(NULL, NULL, num_dests, dests); /** Get the default  one */
    if (dest)
    {
        /** Return the user default printer */
        char *def = get_string_copy(dest->name);
        cupsFreeDests(num_dests, dests);
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
            b->default_printer = get_string_copy(ippGetString(attr, 0, NULL));
            ippDelete(response);
            return b->default_printer;
        }
    }
    ippDelete(response);
    b->default_printer = get_string_copy("NA");
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
        MSG_LOG("Error connecting CUPS Backend to D-Bus.\n", ERR);
    }
}

void add_frontend(BackendObj *b, const char *dialog_name)
{
    Dialog *d = get_new_Dialog();
    g_hash_table_insert(b->dialogs, get_string_copy(dialog_name), d);
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
    return &d->cancel;
}
void set_dialog_cancel(BackendObj *b, const char *dialog_name)
{
    int *x = get_dialog_cancel(b, dialog_name);
    *x = 1;
}
void reset_dialog_cancel(BackendObj *b, const char *dialog_name)
{
    int *x = get_dialog_cancel(b, dialog_name);
    *x = 0;
}
void set_hide_remote_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    d->hide_remote = TRUE;
}
void unset_hide_remote_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    d->hide_remote = FALSE;
}
void set_hide_temp_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    d->hide_temp = TRUE;
}
void unset_hide_temp_printers(BackendObj *b, const char *dialog_name)
{
    Dialog *d = (Dialog *)(g_hash_table_lookup(b->dialogs, dialog_name));
    d->hide_temp = FALSE;
}

gboolean dialog_contains_printer(BackendObj *b, const char *dialog_name, const char *printer_name)
{
    Dialog *d = g_hash_table_lookup(b->dialogs, dialog_name);

    if (d == NULL || d->printers == NULL)
    {
	char *msg = malloc(sizeof(char) * (strlen(dialog_name) + 50));
        sprintf(msg, "Can't retrieve printers for dialog %s.\n", dialog_name);
        MSG_LOG(msg, ERR);
	free(msg);
        return FALSE;
    }
    if (g_hash_table_contains(d->printers, printer_name))
        return TRUE;
    return FALSE;
}

PrinterCUPS *add_printer_to_dialog(BackendObj *b, const char *dialog_name, const cups_dest_t *dest)
{
    char *printer_name = get_string_copy(dest->name);
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    if (d == NULL)
    {
	char *msg = malloc(sizeof(char) * (strlen(dialog_name) + 50));
        sprintf(msg, "Invalid dialog name %s.\n", dialog_name);
        MSG_LOG(msg, ERR);
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
        MSG_LOG(msg, WARN);
	free(msg);
        return;
    }
    g_hash_table_remove(d->printers, printer_name);
}

void send_printer_added_signal(BackendObj *b, const char *dialog_name, cups_dest_t *dest)
{

    if (dest == NULL)
    {
        MSG_LOG("Failed to send printer added signal.\n", ERR);
        exit(EXIT_FAILURE);
    }
    char *printer_name = get_string_copy(dest->name);
    GVariant *gv = g_variant_new(PRINTER_ADDED_ARGS,
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
                                  PRINTER_ADDED_SIGNAL,
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
                                  PRINTER_REMOVED_SIGNAL,
                                  g_variant_new("(ss)", printer_name, "CUPS"),
                                  &error);
    g_assert_no_error(error);
}

void notify_removed_printers(BackendObj *b, const char *dialog_name, GHashTable *new_table)
{
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);

    GHashTable *prev = d->printers;
    GList *prevlist = g_hash_table_get_keys(prev);
    printf("Notifying removed printers.\n");
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
    GHashTable *prev = d->printers;
    printf("Notifying added printers.\n");
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

gboolean get_hide_remote(BackendObj *b, char *dialog_name)
{
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    return d->hide_remote;
}
gboolean get_hide_temp(BackendObj *b, char *dialog_name)
{
    Dialog *d = (Dialog *)g_hash_table_lookup(b->dialogs, dialog_name);
    return d->hide_temp;
}
void refresh_printer_list(BackendObj *b, char *dialog_name)
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
        MSG_LOG("Invalid dialog name.\n", ERR);
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
        printf("Printer '%s' does not exist for the dialog %s.\n", printer_name, dialog_name);
        exit(EXIT_FAILURE);
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
        printf("Printer '%s' does not exist for the dialog %s.\n", printer_name, dialog_name);
    }
    return p->dest;
}
GVariant *get_all_jobs(BackendObj *b, const char *dialog_name, int *num_jobs, gboolean active_only)
{
    int CUPS_JOB_FLAG;
    if (active_only)
        CUPS_JOB_FLAG = CUPS_WHICHJOBS_ACTIVE;
    else
        CUPS_JOB_FLAG = CUPS_WHICHJOBS_ALL;

    GHashTable *printers = get_dialog_printers(b, dialog_name);

    GVariantBuilder *builder;
    GVariant *variant;
    builder = g_variant_builder_new(G_VARIANT_TYPE(JOB_ARRAY_ARGS));

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, printers);

    int ncurr = 0;
    int n = 0;

    int num_printers = g_hash_table_size(printers);
    cups_job_t **jobs = g_new(cups_job_t *, num_printers);

    int i_printer = 0;
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        /** iterate over all the printers of this dialog **/
        PrinterCUPS *p = (PrinterCUPS *)value;
        ensure_printer_connection(p);
        printf(" .. %s ..", p->name);
        /**This is NOT reporting jobs for ipp printers : Probably a bug in cupsGetJobs2:(( **/
        ncurr = cupsGetJobs2(p->http, &(jobs[i_printer]), p->name, 0, CUPS_JOB_FLAG);
        printf("%d\n", ncurr);
        n += ncurr;

        for (int i = 0; i < ncurr; i++)
        {
            printf("i = %d\n", i);
            printf("%d %s\n", jobs[i_printer][i].id, jobs[i_printer][i].title);

            g_variant_builder_add_value(builder, pack_cups_job(jobs[i_printer][i]));
        }
        cupsFreeJobs(ncurr, jobs[i_printer]);
        i_printer++;
    }
    free(jobs);

    *num_jobs = n;
    variant = g_variant_new(JOB_ARRAY_ARGS, builder);
    return variant;
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
        MSG_LOG("Error creating PrinterCUPS", WARN);
        return NULL;
    }
    p->dest = dest_copy;
    p->name = dest_copy->name;
    p->http = NULL;
    p->dinfo = NULL;

    return p;
}

void free_PrinterCUPS(PrinterCUPS *p)
{
    printf("Freeing printerCUPS \n");
    cupsFreeDests(1, p->dest);
    if (p->dinfo)
    {
        cupsFreeDestInfo(p->dinfo);
    }
}

gboolean ensure_printer_connection(PrinterCUPS *p)
{
    if (p->http)
        return TRUE;
    p->http = cupsConnectDest(p->dest, CUPS_DEST_FLAGS_NONE, 300, NULL, NULL, 0, NULL, NULL);
    if (p->http == NULL)
        return FALSE;
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
            return get_string_copy("automatic-rotation");
        default:
            return get_string_copy(ippEnumString(CUPS_ORIENTATION, atoi(def_value)));
        }
    }
    ensure_printer_connection(p);
    ipp_attribute_t *attr = NULL;

    attr = cupsFindDestDefault(p->http, p->dest, p->dinfo, CUPS_ORIENTATION);
    if (!attr)
        return get_string_copy("NA");

    const char *str = ippEnumString(CUPS_ORIENTATION, ippGetInteger(attr, 0));
    if (strcmp("0", str) == 0)
        str = "automatic-rotation";
    return get_string_copy(str);
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
            return get_string_copy(ippEnumString(option_name, atoi(def_value)));

        return get_string_copy(def_value);
    }
    if (def_attr)
    {
        return extract_ipp_attribute(def_attr, 0, option_name);
    }
    return get_string_copy("NA");
}
/**************Option************************************/
Option *get_NA_option()
{
    Option *o = (Option *)malloc(sizeof(Option));
    o->option_name = "NA";
    o->default_value = "NA";
    o->num_supported = 0;
    o->supported_values = new_cstring_array(1);
    o->supported_values[0] = "bub";

    return o;
}
void print_option(const Option *opt)
{
    g_message("%s", opt->option_name);
    int i;
    for (i = 0; i < opt->num_supported; i++)
    {
        printf(" %s\n", opt->supported_values[i]);
    }
    printf("****DEFAULT: %s\n", opt->default_value);
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
        //printf("i = %d\n", i);

        g_variant_iter_loop(iter, "(ssia(s))", &name, &default_val,
                            &num_sup, &array_iter);
        opt[i].option_name = get_string_copy(name);
        opt[i].default_value = get_string_copy(default_val);
        opt[i].num_supported = num_sup;
        opt[i].supported_values = new_cstring_array(num_sup);
        for (j = 0; j < num_sup; j++)
        {
            g_variant_iter_loop(array_iter, "(s)", &str);
            opt[i].supported_values[j] = get_string_copy(str); //mem
        }
        print_option(&opt[i]);
    }

    *options = opt;
}
GVariant *pack_option(const Option *opt)
{
    GVariant **t = g_new(GVariant *, 4);
    t[0] = g_variant_new_string(opt->option_name);
    t[1] = g_variant_new_string(opt->default_value); //("s", get_string_copy(opt->default_value));
    t[2] = g_variant_new_int32(opt->num_supported);
    t[3] = pack_string_array(opt->num_supported, opt->supported_values);
    GVariant *tuple_variant = g_variant_new_tuple(t, 4);
    g_free(t);
    return tuple_variant;
}

int get_all_options(PrinterCUPS *p, Option **options)
{
    ensure_printer_connection(p);

    char **option_names;
    int num_options = get_job_creation_attributes(p, &option_names); /** number of options to be returned**/
    int i, j, optsIndex = 0;                                         /**Looping variables **/

    Option *opts = (Option *)(malloc(sizeof(Option) * (num_options+1))); /**Option array, which will be filled, +1 for page-set option **/
    ipp_attribute_t *vals;                                               /** Variable to store the values of the options **/

    for (i = 0; i < num_options; i++)
    {
        if(strcmp(option_names[i], "media-col") == 0)
            continue;

        opts[optsIndex].option_name = option_names[i];
        vals = cupsFindDestSupported(p->http, p->dest, p->dinfo, option_names[i]);
        if (vals)
            opts[optsIndex].num_supported = ippGetCount(vals);
        else
            opts[optsIndex].num_supported = 0;

        /** Retreive all the supported values for that option **/
        opts[optsIndex].supported_values = new_cstring_array(opts[optsIndex].num_supported);
        for (j = 0; j < opts[optsIndex].num_supported; j++)
        {
            opts[optsIndex].supported_values[j] = extract_ipp_attribute(vals, j, option_names[i]);
            if (opts[optsIndex].supported_values[j] == NULL)
            {
                opts[optsIndex].supported_values[j] = get_string_copy("NA");
            }
        }

        /** Retrieve the default value for that option **/
        opts[optsIndex].default_value = get_default(p, option_names[i]);
        if (opts[optsIndex].default_value == NULL)
        {
            opts[optsIndex].default_value = get_string_copy("NA");
        }

        optsIndex++;
    }

    /* Add the page-set option */
    opts[optsIndex].option_name = get_string_copy("page-set");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = new_cstring_array(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = get_string_copy("even");
    opts[optsIndex].supported_values[1] = get_string_copy("odd");
    opts[optsIndex].default_value = get_string_copy("all");
    optsIndex++;

    *options = (Option *) realloc(opts, sizeof(Option) * optsIndex);
    return optsIndex;
}

const char *get_printer_state(PrinterCUPS *p)
{
    const char *str;
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
        printf("Request failed: %s\n", cupsLastErrorString());
        return "NA";
    }

    ipp_attribute_t *attr;
    if ((attr = ippFindAttribute(response, "printer-state",
                                 IPP_TAG_ENUM)) != NULL)
    {

        printf("printer-state=%d\n", ippGetInteger(attr, 0));
        str = map->state[ippGetInteger(attr, 0)];
    }
    return str;
}
int print_file(PrinterCUPS *p, const char *file_path, int num_settings, GVariant *settings)
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
        printf(" %s : %s\n", option_name, option_value);

        /**
         * to do:
         * instead of directly adding the option,convert it from the frontend's lingo 
         * to the specific lingo of the backend
         * 
         * use PWG names instead
         */
        num_options = cupsAddOption(option_name, option_value, num_options, &options);
    }
    char *file_name = extract_file_name(file_path);
    int job_id = 0;
    cupsCreateDestJob(p->http, p->dest, p->dinfo,
                      &job_id, file_name, num_options, options);
    if (job_id)
    {
        /** job creation was successful , 
         * Now let's submit a document 
         * and start writing data onto it **/
        printf("Created job %d\n", job_id);
        http_status_t http_status; /**document creation status **/
        http_status = cupsStartDestDocument(p->http, p->dest, p->dinfo, job_id,
                                            file_name, CUPS_FORMAT_AUTO,
                                            num_options, options, 1);
        if (http_status == HTTP_STATUS_CONTINUE)
        {
            /**Document submitted successfully;
             * Now write the data onto it**/
            FILE *fp = fopen(file_path, "rb");
            size_t bytes;
            char buffer[65536];
            /** Read and write the data chunk by chunk **/
            while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
            {
                http_status = cupsWriteRequestData(p->http, buffer, bytes);
                if (http_status != HTTP_STATUS_CONTINUE)
                {
                    printf("Error writing print data to server.\n");
                    break;
                }
            }

            if (cupsFinishDestDocument(p->http, p->dest, p->dinfo) == IPP_STATUS_OK)
                printf("Document send succeeded.\n");
            else
                printf("Document send failed: %s\n",
                       cupsLastErrorString());

            fclose(fp);

            return job_id; /**some correction needed here **/
        }
    }
    else
    {
        printf("Unable to create job %s\n", cupsLastErrorString());
        return 0;
    }
}
int get_active_jobs_count(PrinterCUPS *p)
{
    ensure_printer_connection(p);
    cups_job_t *jobs;
    int num_jobs = cupsGetJobs2(p->http, &jobs, p->name, 0, CUPS_WHICHJOBS_ACTIVE);
    cupsFreeJobs(num_jobs, jobs);
    return num_jobs;
}
gboolean cancel_job(PrinterCUPS *p, int jobid)
{
    ensure_printer_connection(p);
    ipp_status_t status = cupsCancelDestJob(p->http, p->dest, jobid);
    if (status == IPP_STATUS_OK)
        return TRUE;
    return FALSE;
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
static void list_group(ppd_file_t *ppd,    /* I - PPD file */
                       ppd_group_t *group) /* I - Group to show */
{
    printf("List group %s\n", group->name);
    /** Now iterate through the options in the particular group*/
    printf("It has %d options.\n", group->num_options);
    printf("Listing all of them ..\n");
    int i;
    for (i = 0; i < group->num_options; i++)
    {
        printf("    Option %d : %s\n", i, group->options[i].keyword);
    }
}
void tryPPD(PrinterCUPS *p)
{
    const char *filename; /* PPD filename */
    ppd_file_t *ppd;      /* PPD data */
    ppd_group_t *group;   /* Current group */
    if ((filename = cupsGetPPD(p->dest->name)) == NULL)
    {
        printf("Error getting ppd file.\n");
        return;
    }
    g_message("Got ppd file %s\n", filename);
    if ((ppd = ppdOpenFile(filename)) == NULL)
    {
        printf("Error opening ppd file.\n");
        return;
    }
    printf("Opened ppd file.\n");
    ppdMarkDefaults(ppd);

    cupsMarkOptions(ppd, p->dest->num_options, p->dest->options);

    group = ppd->groups;
    for (int i = ppd->num_groups; i > 0; i--)
    {
        /**iterate through all the groups in the ppd file */
        list_group(ppd, group);
        group++;
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
    printf("freeing dialog..\n");
    g_hash_table_destroy(d->printers);
    free(d);
}

/*********Mappings********/
Mappings *get_new_Mappings()
{
    Mappings *m = (Mappings *)(malloc(sizeof(Mappings)));
    m->state[3] = STATE_IDLE;
    m->state[4] = STATE_PRINTING;
    m->state[5] = STATE_STOPPED;

    m->orientation[atoi(CUPS_ORIENTATION_LANDSCAPE)] = ORIENTATION_LANDSCAPE;
    m->orientation[atoi(CUPS_ORIENTATION_PORTRAIT)] = ORIENTATION_PORTRAIT;
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

    return get_boolean(val);
}

void cups_get_Resolution(cups_dest_t *dest, int *xres, int *yres)
{
    http_t *http = cupsConnectDest(dest, CUPS_DEST_FLAGS_NONE, 500, NULL, NULL, 0, NULL, NULL);
    g_assert_nonnull(http);
    cups_dinfo_t *dinfo = cupsCopyDestInfo(http, dest);
    g_assert_nonnull(dinfo);
    ipp_attribute_t *attr = cupsFindDestDefault(http, dest, dinfo, "printer-resolution");
    ipp_res_t *units;
    *xres = ippGetResolution(attr, 0, yres, units);
}

int add_printer_to_ht(void *user_data, unsigned flags, cups_dest_t *dest)
{
    GHashTable *h = (GHashTable *)user_data;
    char *printername = get_string_copy(dest->name);
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
    char *printername = get_string_copy(dest->name);
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
        type = CUPS_PRINTER_LOCAL;
        mask = CUPS_PRINTER_REMOTE;
    }
    if (notemp)
    {
        cb = add_printer_to_ht_no_temp;
    }

    GHashTable *printers_ht = g_hash_table_new(g_str_hash, g_str_equal);
    cupsEnumDests(CUPS_DEST_FLAGS_NONE,
                  1000,         //timeout
                  NULL,         //cancel
                  type,         //TYPE
                  mask,         //MASK
                  cb,           //function
                  printers_ht); //user_data

    return printers_ht;
}
GHashTable *cups_get_all_printers()
{
    printf("all printers\n");
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
    printf("local printers\n");
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
    ans = get_string_copy(cupsGetOption(option_name, dest->num_options, dest->options));

    if (ans)
        return ans;

    return get_string_copy("NA");
}

gboolean cups_is_temporary(cups_dest_t *dest)
{
    g_assert_nonnull(dest);
    if (cupsGetOption("printer-uri-supported", dest->num_options, dest->options))
        return FALSE;
    return TRUE;
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
        str = (char *)(malloc(sizeof(char) * 50));
        snprintf(str, sizeof(str), "%d", ippGetInteger(attr, index));
        break;

    case IPP_TAG_ENUM:
        attrstr = ippEnumString(option_name, ippGetInteger(attr, index));
	str = strdup(attrstr);
        break;

    case IPP_TAG_RANGE:
        str = (char *)(malloc(sizeof(char) * 100));
        int upper, lower = ippGetRange(attr, index, &upper);
        snprintf(str, sizeof(str), "%d-%d", lower, upper);
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

    return get_string_copy(buf);
}

char *extract_string_from_ipp(ipp_attribute_t *attr, int index)
{
    return get_string_copy(ippGetString(attr, index, NULL));
}

char *extract_orientation_from_ipp(ipp_attribute_t *attr, int index)
{
    char *str = get_string_copy(ippEnumString(CUPS_ORIENTATION, ippGetInteger(attr, index)));
    if (strcmp("0", str) == 0)
        str = get_string_copy("automatic-rotation");
    return str;
}

void print_job(cups_job_t *j)
{
    printf("title : %s\n", j->title);
    printf("dest : %s\n", j->dest);
    printf("job-id : %d\n", j->id);
    printf("user : %s\n", j->user);

    char *state = translate_job_state(j->state);
    printf("state : %s\n", state);
}

char *get_human_readable_option_name(const char *option_name)
{
    if (strcmp("page-set", option_name) == 0)
        return get_string_copy("Page set");
    if (strcmp("number-up", option_name) == 0)
        return get_string_copy("Pages per side");
    if (strcmp("print-resolution", option_name) == 0)
        return get_string_copy("Resolution");
    if (strcmp("print-quality", option_name) == 0)
        return get_string_copy("Quality");
    if (strcmp("printer-resolution", option_name) == 0)
        return get_string_copy("Printer resolution");
    if (strcmp("sides", option_name) == 0)
        return get_string_copy("Two-sided");
    if (strcmp("multiple-document-handling", option_name) == 0)
        return get_string_copy("Collate");
    if (strcmp("media", option_name) == 0)
        return get_string_copy("Paper size");
    if (strcmp("page-delivery", option_name) == 0)
        return get_string_copy("Reverse");
    if (strcmp("job-hold-until", option_name) == 0)
        return get_string_copy("Print job at");
    if (strcmp("job-priority", option_name) == 0)
        return get_string_copy("Priority");
    if (strcmp("job-name", option_name) == 0)
        return get_string_copy("Job name");
    if (strcmp("output-bin", option_name) == 0)
        return get_string_copy("Output tray");
    if (strcmp("page-ranges", option_name) == 0)
        return get_string_copy("Range");
    if (strcmp("print-color-mode", option_name) == 0)
        return get_string_copy("Color mode");
    if (strcmp("copies", option_name) == 0)
        return get_string_copy("Copies");
    if (strcmp("orientation-requested", option_name) == 0)
        return get_string_copy("Orientation");
    if (strcmp("job-sheets", option_name) == 0)
        return get_string_copy("Paper type");
    if (strcmp("finishings", option_name) == 0)
        return get_string_copy("Finishings");
    if (strcmp("print-scaling", option_name) == 0)
        return get_string_copy("Page scaling");
    return get_string_copy(option_name);
}

char *translate_job_state(ipp_jstate_t state)
{
    switch (state)
    {
    case IPP_JSTATE_ABORTED:
        return JOB_STATE_ABORTED;
    case IPP_JSTATE_CANCELED:
        return JOB_STATE_CANCELLED;
    case IPP_JSTATE_HELD:
        return JOB_STATE_HELD;
    case IPP_JSTATE_PENDING:
        return JOB_STATE_PENDING;
    case IPP_JSTATE_PROCESSING:
        return JOB_STATE_PRINTING;
    case IPP_JSTATE_STOPPED:
        return JOB_STATE_STOPPED;
    case IPP_JSTATE_COMPLETED:
        return JOB_STATE_COMPLETED;
    default:
        return "NA";
    }
}

GVariant *pack_cups_job(cups_job_t job)
{
    printf("%s\n", job.dest);
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

void MSG_LOG(const char *msg, int msg_level)
{
    if (MSG_LOG_LEVEL >= msg_level)
    {
        printf("%s\n", msg);
        fflush(stdout);
    }
}

void free_string(char *str)
{
    if (str)
    {
        free(str);
    }
}
