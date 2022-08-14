#include "backend_helper.h"

#define _CUPS_NO_DEPRECATED 1

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
        char *def = cpdbGetStringCopy(dest->name);
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
            b->default_printer = cpdbGetStringCopy(ippGetString(attr, 0, NULL));
            ippDelete(response);
            return b->default_printer;
        }
    }
    ippDelete(response);
    b->default_printer = cpdbGetStringCopy("NA");
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
    g_hash_table_insert(b->dialogs, cpdbGetStringCopy(dialog_name), d);
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
    char *printer_name = cpdbGetStringCopy(dest->name);
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
    char *printer_name = cpdbGetStringCopy(dest->name);
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
    builder = g_variant_builder_new(G_VARIANT_TYPE(CPDB_JOB_ARRAY_ARGS));

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
    variant = g_variant_new(CPDB_JOB_ARRAY_ARGS, builder);
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
            return cpdbGetStringCopy("automatic-rotation");
        default:
            return cpdbGetStringCopy(ippEnumString(CUPS_ORIENTATION, atoi(def_value)));
        }
    }
    ensure_printer_connection(p);
    ipp_attribute_t *attr = NULL;

    attr = cupsFindDestDefault(p->http, p->dest, p->dinfo, CUPS_ORIENTATION);
    if (!attr)
        return cpdbGetStringCopy("NA");

    const char *str = ippEnumString(CUPS_ORIENTATION, ippGetInteger(attr, 0));
    if (strcmp("0", str) == 0)
        str = "automatic-rotation";
    return cpdbGetStringCopy(str);
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
            return cpdbGetStringCopy(ippEnumString(option_name, atoi(def_value)));

        return cpdbGetStringCopy(def_value);
    }
    if (def_attr)
    {
        return extract_ipp_attribute(def_attr, 0, option_name);
    }
    return cpdbGetStringCopy("NA");
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
        opt[i].option_name = cpdbGetStringCopy(name);
        opt[i].default_value = cpdbGetStringCopy(default_val);
        opt[i].num_supported = num_sup;
        opt[i].supported_values = cpdbNewCStringArray(num_sup);
        for (j = 0; j < num_sup; j++)
        {
            g_variant_iter_loop(array_iter, "(s)", &str);
            opt[i].supported_values[j] = cpdbGetStringCopy(str); //mem
        }
        print_option(&opt[i]);
    }

    *options = opt;
}
GVariant *pack_option(const Option *opt)
{
    GVariant **t = g_new(GVariant *, 4);
    t[0] = g_variant_new_string(opt->option_name);
    t[1] = g_variant_new_string(opt->default_value); //("s", cpdbGetStringCopy(opt->default_value));
    t[2] = g_variant_new_int32(opt->num_supported);
    t[3] = cpdbPackStringArray(opt->num_supported, opt->supported_values);
    GVariant *tuple_variant = g_variant_new_tuple(t, 4);
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
        option_names[num_options+i] = cpdbGetStringCopy(additional_options[i]);
    num_options += sz;

    int i, j, optsIndex = 0;                                         /**Looping variables **/

    Option *opts = (Option *)(malloc(sizeof(Option) * (num_options+20))); /**Option array, which will be filled **/
    ipp_attribute_t *vals, *default_val;                                               /** Variable to store the values of the options **/
    
    ipp_t *media_col, *media_size_col;                                  /** Variable to store collections **/
    ipp_attribute_t *x_dim, *y_dim, *media_margin;                      /** Variable to store media-dimensions and media-margins **/
    pwg_media_t *pwg_media;                                             /** Variable to store pwg_media **/
    
    typedef struct MediaSize 
    {
        int x;
        int y;
    } MediaSize;

    typedef struct MediaRange
    {
        int x_low;
        int x_high;
        int y_low;
        int y_high;
    } MediaRange;
    
    int media_size_num;    

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
                opts[optsIndex].supported_values[j] = cpdbGetStringCopy("NA");
            }
        }

        /** Retrieve the default value for that option **/
        opts[optsIndex].default_value = get_default(p, option_names[i]);
        if (opts[optsIndex].default_value == NULL)
        {
            opts[optsIndex].default_value = cpdbGetStringCopy("NA");
        }

        optsIndex++;
    }
    

    /* 
     * Store unique media-size & media-range in interal arrays
     */
    vals = cupsFindDestSupported(p->http, p->dest, p->dinfo, "media-size");
    if (vals)
        media_size_num = ippGetCount(vals);
    else
        media_size_num = 0;
        
    MediaSize *media_size = (MediaSize *) malloc(media_size_num * sizeof(MediaSize));	/* Internal array of unique media-sizes */
    MediaRange *media_range = (MediaRange *) malloc(media_size_num * sizeof(MediaRange)); /* Internal array of media-ranges */

    int k1 = 0;  /* number of elements in media_size */
    int k2 = 0;  /* number of elements in media_range */
    for (i = 0; i < media_size_num; i++)
    {
        media_size_col = ippGetCollection(vals, i);

        int value_tag = ippGetValueTag(ippFirstAttribute(media_size_col));
        if (value_tag == IPP_TAG_INTEGER)
        {
            x_dim = ippFindAttribute(media_size_col, "x-dimension", IPP_TAG_INTEGER);
            y_dim = ippFindAttribute(media_size_col, "y-dimension", IPP_TAG_INTEGER);
            int x = ippGetInteger(x_dim, 0);
            int y = ippGetInteger(y_dim, 0);

            int exists = 0; 
            for (j = 0; j < k1; j++)          /* Check if duplicate exists */
            {
                if ((x == media_size[j].x && y == media_size[j].y) ||      /* duplicate exists (eg. borderless) */
                    (x == media_size[j].y && y == media_size[j].x))         /* transverse duplicate exists */
                {
                    exists = 1;
                    break;
                }
            }
            if (!exists)
            {
                media_size[k1].x = x;
                media_size[k1].y = y;
                k1++;
            }
        }
        else if (value_tag == IPP_TAG_RANGE)
        {
            x_dim = ippFindAttribute(media_size_col, "x-dimension", IPP_TAG_RANGE);
            y_dim = ippFindAttribute(media_size_col, "y-dimension", IPP_TAG_RANGE);
            int xu, xl = ippGetRange(x_dim, 0, &xu);                   /* xu & xl are upper and lower x-dimension respectively */
            int yu, yl = ippGetRange(y_dim, 0, &yu);                   /* yu & yl are upper and lower y-dimension respectively */

            media_range[k2].x_low = xl;
            media_range[k2].x_high = xu;
            media_range[k2].y_low = yl;
            media_range[k2].y_high = yu;
            k2++;
        }
    }
    
    /* free extra space */
    media_size = (MediaSize *) realloc(media_size, k1 * sizeof(MediaSize));
    media_range = (MediaRange *) realloc(media_range, k2 * sizeof(MediaRange));
    media_size_num = k1+(k2*2);
    

    /* 
     * Add the media option 
     */
    opts[optsIndex].option_name = cpdbGetStringCopy("media");
    opts[optsIndex].num_supported = media_size_num;

    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    for (i = 0; i < k1; i++)
    {
        pwg_media = pwgMediaForSize(media_size[i].x, media_size[i].y);
        if (pwg_media != NULL)
            opts[optsIndex].supported_values[i] = cpdbGetStringCopy(pwg_media->pwg);

        if (opts[optsIndex].supported_values[i] == NULL)
            opts[optsIndex].supported_values[i] = cpdbGetStringCopy("NA");
    }
    for (i = 0; i < k2; i++)
    {
        double xl, xu, yl, yu;
        xl = media_range[i].x_low / 100.0;
        xu = media_range[i].x_high / 100.0;
        yl = media_range[i].y_low / 100.0;
        yu = media_range[i].y_high / 100.0;

        char *s1 = (char *) malloc(48);
        char *s2 = (char *) malloc(48);
        sprintf(s1, "custom_min_%.2fx%.2fmm", xl, yl);
        sprintf(s2, "custom_max_%.2fx%.2fmm", xu, yu);

        opts[optsIndex].supported_values[k1+2*i] = cpdbGetStringCopy(s1);
        opts[optsIndex].supported_values[k1+2*i+1] = cpdbGetStringCopy(s2);

        free(s1); free(s2);
    }

    vals = cupsFindDestDefault(p->http, p->dest, p->dinfo, "media-col");
    media_col = ippGetCollection(vals, 0);
    media_size_col = ippGetCollection(ippFindAttribute(media_col, "media-size", IPP_TAG_BEGIN_COLLECTION), 0);
    x_dim = ippFindAttribute(media_size_col, "x-dimension", IPP_TAG_INTEGER);
    y_dim = ippFindAttribute(media_size_col, "y-dimension", IPP_TAG_INTEGER);
    pwg_media = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));

    opts[optsIndex].default_value = cpdbGetStringCopy(pwg_media->pwg);
    if (opts[optsIndex].default_value == NULL)
        opts[optsIndex].default_value = cpdbGetStringCopy("NA");
    
    optsIndex++;


    /*
     *Add the media-{top,left,right,bottom}-margin option 
     */
    char def[16];
    char *attrs[] = {"media-left-margin", "media-bottom-margin", "media-top-margin", "media-right-margin"};

    default_val = cupsFindDestDefault(p->http, p->dest, p->dinfo, "media-col");
    
    for (i = 0; i < 4; i++) // for each attr in attrs
    {
        vals = cupsFindDestSupported(p->http, p->dest, p->dinfo, attrs[i]);
        opts[optsIndex].option_name = cpdbGetStringCopy(attrs[i]);
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
                opts[optsIndex].supported_values = cpdbGetStringCopy("NA");
            }
        }

        media_col = ippGetCollection(default_val, 0);
        media_margin = ippFindAttribute(media_col, attrs[i], IPP_TAG_INTEGER);
        snprintf(def, 16, "%d", ippGetInteger(media_margin, 0));

        opts[optsIndex].default_value = cpdbGetStringCopy(def);
        if (opts[optsIndex].default_value == NULL)
        {
            opts[optsIndex].default_value = cpdbGetStringCopy("NA");
        }

        optsIndex++;
    }


    /* Add the booklet option */
    opts[optsIndex].option_name = cpdbGetStringCopy("booklet");
    opts[optsIndex].num_supported = 3;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("off");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("on");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("shuffle-only");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the ipp-attribute-fidelity option */
    opts[optsIndex].option_name = cpdbGetStringCopy("ipp-attribute-fidelity");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("off");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("on");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the job-sheets option */
    opts[optsIndex].option_name = cpdbGetStringCopy("job-sheets");
    opts[optsIndex].num_supported = 8;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("none");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("classified");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("confidential");
    opts[optsIndex].supported_values[3] = cpdbGetStringCopy("form");
    opts[optsIndex].supported_values[4] = cpdbGetStringCopy("secret");
    opts[optsIndex].supported_values[5] = cpdbGetStringCopy("standard");
    opts[optsIndex].supported_values[6] = cpdbGetStringCopy("topsecret");
    opts[optsIndex].supported_values[7] = cpdbGetStringCopy("unclassified");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy("none,none");
    }
    optsIndex++;

    /* Add the mirror option */
    opts[optsIndex].option_name = cpdbGetStringCopy("mirror");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("off");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("on");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the multiple-document-handling option */
    opts[optsIndex].option_name = cpdbGetStringCopy("multiple-document-handling");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("separate-documents-uncollated-copies");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("separate-documents-collated-copies");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the number-up option */
    opts[optsIndex].option_name = cpdbGetStringCopy("number-up");
    opts[optsIndex].num_supported = 6;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("1");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("2");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("4");
    opts[optsIndex].supported_values[3] = cpdbGetStringCopy("6");
    opts[optsIndex].supported_values[4] = cpdbGetStringCopy("9");
    opts[optsIndex].supported_values[5] = cpdbGetStringCopy("16");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the number-up-layout option */
    opts[optsIndex].option_name = cpdbGetStringCopy("number-up-layout");
    opts[optsIndex].num_supported = 8;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("lrtb");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("lrbt");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("rltb");
    opts[optsIndex].supported_values[3] = cpdbGetStringCopy("rlbt");
    opts[optsIndex].supported_values[4] = cpdbGetStringCopy("tblr");
    opts[optsIndex].supported_values[5] = cpdbGetStringCopy("tbrl");
    opts[optsIndex].supported_values[6] = cpdbGetStringCopy("btlr");
    opts[optsIndex].supported_values[7] = cpdbGetStringCopy("btrl");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the orientation-requested option */
    opts[optsIndex].option_name = cpdbGetStringCopy("orientation-requested");
    opts[optsIndex].num_supported = 4;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("3");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("4");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("5");
    opts[optsIndex].supported_values[3] = cpdbGetStringCopy("6");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    else
    {
        if (strcmp(opts[optsIndex].default_value, "potrait") == 0)
            opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
        else if (strcmp(opts[optsIndex].default_value, "landscape") == 0)
            opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[1]);
        else if (strcmp(opts[optsIndex].default_value, "reverse-landscape") == 0)
            opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[2]);
        else if (strcmp(opts[optsIndex].default_value, "reverse-potrait") == 0)
            opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[3]);
        else
            opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the page-border option */
    opts[optsIndex].option_name = cpdbGetStringCopy("page-border");
    opts[optsIndex].num_supported = 5;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("none");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("single");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("single-thick");
    opts[optsIndex].supported_values[3] = cpdbGetStringCopy("double");
    opts[optsIndex].supported_values[4] = cpdbGetStringCopy("double-thick");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the page-delivery option */
    opts[optsIndex].option_name = cpdbGetStringCopy("page-delivery");
    opts[optsIndex].num_supported = 2;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("same-order");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("reverse-order");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the page-set option */
    opts[optsIndex].option_name = cpdbGetStringCopy("page-set");
    opts[optsIndex].num_supported = 3;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("all");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("even");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("odd");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the position option */
    opts[optsIndex].option_name = cpdbGetStringCopy("position");
    opts[optsIndex].num_supported = 9;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("center");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("top");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("bottom");
    opts[optsIndex].supported_values[3] = cpdbGetStringCopy("left");
    opts[optsIndex].supported_values[4] = cpdbGetStringCopy("right");
    opts[optsIndex].supported_values[5] = cpdbGetStringCopy("top-left");
    opts[optsIndex].supported_values[6] = cpdbGetStringCopy("top-right");
    opts[optsIndex].supported_values[7] = cpdbGetStringCopy("bottom-left");
    opts[optsIndex].supported_values[8] = cpdbGetStringCopy("bottom-right");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
    }
    optsIndex++;

    /* Add the print-scaling option */
    opts[optsIndex].option_name = cpdbGetStringCopy("print-scaling");
    opts[optsIndex].num_supported = 5;
    opts[optsIndex].supported_values = cpdbNewCStringArray(opts[optsIndex].num_supported);
    opts[optsIndex].supported_values[0] = cpdbGetStringCopy("auto");
    opts[optsIndex].supported_values[1] = cpdbGetStringCopy("auto-fit");
    opts[optsIndex].supported_values[2] = cpdbGetStringCopy("fill");
    opts[optsIndex].supported_values[3] = cpdbGetStringCopy("fit");
    opts[optsIndex].supported_values[4] = cpdbGetStringCopy("none");
    opts[optsIndex].default_value = get_default(p, opts[optsIndex].option_name);
    if (strcmp(opts[optsIndex].default_value, "NA") == 0)
    {
        opts[optsIndex].default_value = cpdbGetStringCopy(opts[optsIndex].supported_values[0]);
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
                    opts[i].supported_values[j] = cpdbGetStringCopy("3");
                    continue;
                }
                if (strcasecmp(opts[i].supported_values[j], "normal") == 0)
                {
                    free(opts[i].supported_values[j]);
                    opts[i].supported_values[j] = cpdbGetStringCopy("4");
                    continue;
                }
                if (strcasecmp(opts[i].supported_values[j], "high") == 0)
                {
                    free(opts[i].supported_values[j]);
                    opts[i].supported_values[j] = cpdbGetStringCopy("5");
                    continue;
                }
            }

            if (strcasecmp(opts[i].default_value, "draft") == 0)
            {
                free(opts[i].default_value);
                opts[i].default_value = cpdbGetStringCopy("3");
                continue;
            }
            if (strcasecmp(opts[i].default_value, "normal") == 0)
            {
                free(opts[i].default_value);
                opts[i].default_value = cpdbGetStringCopy("4");
                continue;
            }
            if (strcasecmp(opts[i].default_value, "high") == 0)
            {
                free(opts[i].default_value);
                opts[i].default_value = cpdbGetStringCopy("5");
                continue;
            }

            break;
        }
    }

    free(media_size);

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
    char *file_name = cpdbExtractFileName(file_path);
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
    ipp_res_t *units;
    *xres = ippGetResolution(attr, 0, yres, units);
}

int add_printer_to_ht(void *user_data, unsigned flags, cups_dest_t *dest)
{
    GHashTable *h = (GHashTable *)user_data;
    char *printername = cpdbGetStringCopy(dest->name);
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
    char *printername = cpdbGetStringCopy(dest->name);
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
    ans = cpdbGetStringCopy(cupsGetOption(option_name, dest->num_options, dest->options));

    if (ans)
        return ans;

    return cpdbGetStringCopy("NA");
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

    return cpdbGetStringCopy(buf);
}

char *extract_string_from_ipp(ipp_attribute_t *attr, int index)
{
    return cpdbGetStringCopy(ippGetString(attr, index, NULL));
}

char *extract_orientation_from_ipp(ipp_attribute_t *attr, int index)
{
    char *str = cpdbGetStringCopy(ippEnumString(CUPS_ORIENTATION, ippGetInteger(attr, index)));
    if (strcmp("0", str) == 0)
        str = cpdbGetStringCopy("automatic-rotation");
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

void get_media_size (const char *media, int *width, int *length)
{
    pwg_media_t *pwg;

    pwg = pwgMediaForPWG(media);
    *width = pwg->width;
    *length = pwg->length;
}

char *get_human_readable_option_name(const char *option_name)
{
    if (strcmp("page-set", option_name) == 0)
        return cpdbGetStringCopy("Page set");
    if (strcmp("position", option_name) == 0)
        return cpdbGetStringCopy("Position");
    if (strcmp("number-up", option_name) == 0)
        return cpdbGetStringCopy("Pages per side");
    if (strcmp("number-up-layout", option_name) == 0)
        return cpdbGetStringCopy("Multiple Page layout");
    if (strcmp("print-resolution", option_name) == 0)
        return cpdbGetStringCopy("Resolution");
    if (strcmp("print-quality", option_name) == 0)
        return cpdbGetStringCopy("Quality");
    if (strcmp("printer-resolution", option_name) == 0)
        return cpdbGetStringCopy("Printer resolution");
    if (strcmp("sides", option_name) == 0)
        return cpdbGetStringCopy("Two-sided");
    if (strcmp("multiple-document-handling", option_name) == 0)
        return cpdbGetStringCopy("Collate");
    if (strcmp("media", option_name) == 0)
        return cpdbGetStringCopy("Paper size");
    if (strcmp("page-delivery", option_name) == 0)
        return cpdbGetStringCopy("Reverse");
    if (strcmp("page-border", option_name) == 0)
        return cpdbGetStringCopy("Page border");
    if (strcmp("job-hold-until", option_name) == 0)
        return cpdbGetStringCopy("Print job at");
    if (strcmp("job-priority", option_name) == 0)
        return cpdbGetStringCopy("Priority");
    if (strcmp("job-name", option_name) == 0)
        return cpdbGetStringCopy("Job name");
    if (strcmp("output-bin", option_name) == 0)
        return cpdbGetStringCopy("Output tray");
    if (strcmp("page-ranges", option_name) == 0)
        return cpdbGetStringCopy("Range");
    if (strcmp("print-color-mode", option_name) == 0)
        return cpdbGetStringCopy("Color mode");
    if (strcmp("copies", option_name) == 0)
        return cpdbGetStringCopy("Copies");
    if (strcmp("orientation-requested", option_name) == 0)
        return cpdbGetStringCopy("Orientation");
    if (strcmp("job-sheets", option_name) == 0)
        return cpdbGetStringCopy("Paper type");
    if (strcmp("finishings", option_name) == 0)
        return cpdbGetStringCopy("Finishings");
    if (strcmp("print-scaling", option_name) == 0)
        return cpdbGetStringCopy("Page scaling");
    if (strcmp("booklet", option_name) == 0)
        return cpdbGetStringCopy("Booklet");
    if (strcmp("mirror", option_name) == 0)
        return cpdbGetStringCopy("Mirror");
    return cpdbGetStringCopy(option_name);
}

char *get_human_readable_choice_name(const char *option_name, const char *choice_name)
{
    if (strcmp("page-set", option_name) == 0)
    {
        if (strcmp("all", choice_name) == 0)
            return cpdbGetStringCopy("All pages");
        if (strcmp("odd", choice_name) == 0)
            return cpdbGetStringCopy("Odd pages");
        if (strcmp("even", choice_name) == 0)
            return cpdbGetStringCopy("Even pages");
    }
    if (strcmp("position", option_name) == 0)
    {
        if (strcmp("center", choice_name) == 0)
            return cpdbGetStringCopy("Center");
        if (strcmp("top", choice_name) == 0)
            return cpdbGetStringCopy("Top");
        if (strcmp("bottom", choice_name) == 0)
            return cpdbGetStringCopy("Bottom");
        if (strcmp("left", choice_name) == 0)
            return cpdbGetStringCopy("Left");
        if (strcmp("right", choice_name) == 0)
            return cpdbGetStringCopy("Right");
        if (strcmp("top-left", choice_name) == 0)
            return cpdbGetStringCopy("Top Left");
        if (strcmp("top-right", choice_name) == 0)
            return cpdbGetStringCopy("Top Right");
        if (strcmp("bottom-left", choice_name) == 0)
            return cpdbGetStringCopy("Bottom Left");
        if (strcmp("bottom-right", choice_name) == 0)
            return cpdbGetStringCopy("Bottom Right");
    }
    if (strcmp("number-up-layout", option_name) == 0)
    {
        if (strcmp("lrtb", choice_name) == 0)
            return cpdbGetStringCopy("Left to Right, Top to Bottom");
        if (strcmp("lrbt", choice_name) == 0)

            return cpdbGetStringCopy("Left to Right, Bottom to Top");
        if (strcmp("rltb", choice_name) == 0)
            return cpdbGetStringCopy("Right to Left, Top to Bottom");  
        if (strcmp("rlbt", choice_name) == 0)
            return cpdbGetStringCopy("Right to Left, Bottom to Top"); 
        if (strcmp("tblr", choice_name) == 0)
            return cpdbGetStringCopy("Top to Bottom, Left to Right");
        if (strcmp("tbrl", choice_name) == 0)
            return cpdbGetStringCopy("Top to Bottom, Right to Left");
        if (strcmp("btlr", choice_name) == 0)
            return cpdbGetStringCopy("Bottom to Top, Left to Right");  
        if (strcmp("btrl", choice_name) == 0)
            return cpdbGetStringCopy("Bottom to Top, Right to Left"); 
    }
    if (strcmp("print-quality", option_name) == 0)
    {
        if (strcmp("3", choice_name) == 0 || strcasecmp("draft", choice_name) == 0)
            return cpdbGetStringCopy("Draft");
        if (strcmp("4", choice_name) == 0 || strcasecmp("normal", choice_name) == 0)
            return cpdbGetStringCopy("Normal");
        if (strcmp("5", choice_name) == 0 || strcasecmp("high", choice_name) == 0)
            return cpdbGetStringCopy("High");
    }
    if (strcmp("sides", option_name) == 0)
    {
        if (strcmp("one-sided", choice_name) == 0)
            return cpdbGetStringCopy("Off");
        if (strcmp("two-sided-short-edge", choice_name) == 0)
            return cpdbGetStringCopy("On, Landscape");
        if (strcmp("two-sided-long-edge", choice_name) == 0)
            return cpdbGetStringCopy("On, Potrait");
    }
    if (strcmp("multiple-document-handling", option_name) == 0)
    {
        
        if (strcmp("separate-documents-uncollated-copies", choice_name) == 0)
            return cpdbGetStringCopy("Off");
        if (strcmp("separate-documents-collated-copies", choice_name) == 0)
            return cpdbGetStringCopy("On");
    }
    if (strcmp("media", option_name) == 0)
    {
        pwg_media_t *pwg_media;
        
        pwg_media = pwgMediaForPWG(choice_name);
        if (pwg_media != NULL)
            return cpdbGetStringCopy(pwg_media->ppd);
    }
    if (strcmp("page-delivery", option_name) == 0)
    {
        if (strcmp("same-order", choice_name) == 0)
            return cpdbGetStringCopy("Off");
        if (strcmp("reverse-order", choice_name) == 0)
            return cpdbGetStringCopy("On");
    }
    if (strcmp("page-border", option_name) == 0)
    {
        if (strcmp("none", choice_name) == 0)
            return cpdbGetStringCopy("None");
        if (strcmp("single", choice_name) == 0)
            return cpdbGetStringCopy("Single");
        if (strcmp("single-thick", choice_name) == 0)
            return cpdbGetStringCopy("Single Thick");
        if (strcmp("double", choice_name) == 0)
            return cpdbGetStringCopy("Double");
        if (strcmp("double", choice_name) == 0)
            return cpdbGetStringCopy("Double Thick");
    }
    if (strcmp("job-hold-until", option_name) == 0)
    {
        if (strcmp("no-hold", choice_name) == 0)
            return cpdbGetStringCopy("No hold");
        if (strcmp("indefinite", choice_name) == 0)
            return cpdbGetStringCopy("Indefinite");
        if (strcmp("day-time", choice_name) == 0)
            return cpdbGetStringCopy("Day time");
        if (strcmp("evening", choice_name) == 0)
            return cpdbGetStringCopy("Evening");
        if (strcmp("night", choice_name) == 0)
            return cpdbGetStringCopy("Night");
        if (strcmp("second-shift", choice_name) == 0)
            return cpdbGetStringCopy("Second shift");
        if (strcmp("third-shift", choice_name) == 0)
            return cpdbGetStringCopy("Third shift");
        if (strcmp("weekend", choice_name) == 0)
            return cpdbGetStringCopy("Weekend");
    }
    if (strcmp("job-priority", option_name) == 0)
    {
        int val = atoi(choice_name);

        if (val <= 30)
            return cpdbGetStringCopy("Low");
        if (val <= 50)
            return cpdbGetStringCopy("Medium");
        if (val <= 80)
            return cpdbGetStringCopy("High");
        if (val <= 100)
            return cpdbGetStringCopy("Urgent");
    }
    if (strcmp("output-bin", option_name) == 0)
    {
		if (strcmp("top", choice_name) == 0)
			return cpdbGetStringCopy("Top bin");
		if (strcmp("middle", choice_name) == 0)
			return cpdbGetStringCopy("Middle bin");
		if (strcmp("bottom", choice_name) == 0)
			return cpdbGetStringCopy("Bottom bin");
		if (strcmp("side", choice_name) == 0)
			return cpdbGetStringCopy("Side bin");
		if (strcmp("left", choice_name) == 0)
			return cpdbGetStringCopy("Left bin");
		if (strcmp("right", choice_name) == 0)
			return cpdbGetStringCopy("Right bin");
		if (strcmp("center", choice_name) == 0)
			return cpdbGetStringCopy("Center bin");
		if (strcmp("rear", choice_name) == 0)
			return cpdbGetStringCopy("Rear bin");
        if (strcmp("face-down", choice_name) == 0)
            return cpdbGetStringCopy("Face Down Bin");
        if (strcmp("face-up", choice_name) == 0)
            return cpdbGetStringCopy("Face up Bin");
        if (strcmp("large-capacity", choice_name) == 0)
			return cpdbGetStringCopy("Large Capacity bin");
    }
    if (strcmp("print-color-mode", option_name) == 0)
    {
        if (strcmp("monochrome", choice_name) == 0)
            return cpdbGetStringCopy("Monochrome");
        if (strcmp("color", choice_name) == 0)
            return cpdbGetStringCopy("Color");
    }
    if (strcmp("orientation-requested", option_name) == 0)
    {
        if (strcmp("3", choice_name) == 0)
            return cpdbGetStringCopy("Potrait");
        if (strcmp("4", choice_name) == 0)
            return cpdbGetStringCopy("Landscape");
        if (strcmp("5", choice_name) == 0)
            return cpdbGetStringCopy("Reverse Landscape");
        if (strcmp("6", choice_name) == 0)
            return cpdbGetStringCopy("Reverse Potrait");
    }
    if (strcmp("job-sheets", option_name) == 0)
    {
        if (strcmp("none", choice_name) == 0)
            return cpdbGetStringCopy("None");
        if (strcmp("classified", choice_name) == 0)
            return cpdbGetStringCopy("Classified");
        if (strcmp("confidential", choice_name) == 0)
            return cpdbGetStringCopy("Confidential");
        if (strcmp("form", choice_name) == 0)
            return cpdbGetStringCopy("Form");
        if (strcmp("secret", choice_name) == 0)
            return cpdbGetStringCopy("Secret");
        if (strcmp("standard", choice_name) == 0)
            return cpdbGetStringCopy("Standard");
        if (strcmp("topsecret", choice_name) == 0)
            return cpdbGetStringCopy("Topsecret");
        if (strcmp("unclassified", choice_name) == 0)
            return cpdbGetStringCopy("Unclassified");
    }
    if (strcmp("finishings", option_name) == 0)
    {
        if (strcmp("none", choice_name) == 0)
            return cpdbGetStringCopy("None");
    }
    if (strcmp("print-scaling", option_name) == 0)
    {
        if (strcmp("auto", choice_name) == 0)
            return cpdbGetStringCopy("Auto");
        if (strcmp("auto-fit", choice_name) == 0)
            return cpdbGetStringCopy("Auto-Fit");
        if (strcmp("fill", choice_name) == 0)
            return cpdbGetStringCopy("Fill");
        if (strcmp("fit", choice_name) == 0)
            return cpdbGetStringCopy("Fit");
        if (strcmp("none", choice_name) == 0)
            return cpdbGetStringCopy("None");
    }
    if (strcmp("booklet", option_name) == 0)
    {
        if (strcmp("off", choice_name) == 0)
            return cpdbGetStringCopy("Off");
        if (strcmp("on", choice_name) == 0)
            return cpdbGetStringCopy("On");
        if (strcmp("shuffle-only", choice_name) == 0)
            return cpdbGetStringCopy("Shuffle");
    }
    if (strcmp("mirror", option_name) == 0)
    {
        if (strcmp("off", choice_name) == 0)
            return cpdbGetStringCopy("Off");
        if (strcmp("on", choice_name) == 0)
            return cpdbGetStringCopy("On");   
    }

    return cpdbGetStringCopy(choice_name);
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
