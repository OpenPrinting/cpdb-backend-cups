#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <cups/cups.h>
#include <cpdb-libs-backend.h>
#include "backend_helper.h"

#define _CUPS_NO_DEPRECATED 1
#define BUS_NAME "org.openprinting.Backend.CUPS"
#define OBJECT_PATH "/"

static void
on_name_acquired(GDBusConnection *connection,
                 const gchar *name,
                 gpointer not_used);
static void acquire_session_bus_name();
gpointer list_printers(gpointer _dialog_name);
int send_printer_added(void *_dialog_name, unsigned flags, cups_dest_t *dest);
void connect_to_signals();

BackendObj *b;

int main()
{
    /* Initialize internal default settings of the CUPS library */
    int p = ippPort();

    b = get_new_BackendObj();
    acquire_session_bus_name(BUS_NAME);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
}

static void acquire_session_bus_name(char *bus_name)
{
    g_bus_own_name(G_BUS_TYPE_SESSION,
                   bus_name,
                   0,                //flags
                   NULL,             //bus_acquired_handler
                   on_name_acquired, //name acquired handler
                   NULL,             //name_lost handler
                   NULL,             //user_data
                   NULL);            //user_data free function
}

static void
on_name_acquired(GDBusConnection *connection,
                 const gchar *name,
                 gpointer not_used)
{
    b->dbus_connection = connection;
    b->skeleton = print_backend_skeleton_new();
    connect_to_signals();
    connect_to_dbus(b, OBJECT_PATH);
}

static gboolean on_handle_activate_backend(PrintBackend *interface,
                                           GDBusMethodInvocation *invocation,
                                           gpointer not_used)
{
    /**
    This function starts the backend and starts sending the printers
    **/
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    add_frontend(b, dialog_name);
    g_thread_new(NULL, list_printers, (gpointer)dialog_name);
    return TRUE;
}

gpointer list_printers(gpointer _dialog_name)
{
    char *dialog_name = (char *)_dialog_name;
    g_message("New thread for dialog at %s\n", dialog_name);
    int *cancel = get_dialog_cancel(b, dialog_name);

    cupsEnumDests(CUPS_DEST_FLAGS_NONE,
                  -1, //NO timeout
                  cancel,
                  0, //TYPE
                  0, //MASK
                  send_printer_added,
                  _dialog_name);

    g_message("Exiting thread for dialog at %s\n", dialog_name);
    return NULL;
}

int send_printer_added(void *_dialog_name, unsigned flags, cups_dest_t *dest)
{

    char *dialog_name = (char *)_dialog_name;
    char *printer_name = dest->name;

    if (dialog_contains_printer(b, dialog_name, printer_name))
    {
        g_message("%s already sent.\n", printer_name);
        return 1;
    }

    add_printer_to_dialog(b, dialog_name, dest);
    send_printer_added_signal(b, dialog_name, dest);
    g_message("     Sent notification for printer %s\n", printer_name);

    /** dest will be automatically freed later. 
     * Don't explicitly free it
     */
    return 1; //continue enumeration
}

static void on_stop_backend(GDBusConnection *connection,
                            const gchar *sender_name,
                            const gchar *object_path,
                            const gchar *interface_name,
                            const gchar *signal_name,
                            GVariant *parameters,
                            gpointer not_used)
{
    g_message("Stop backend signal from %s\n", sender_name);
    /**
     * Ignore this signal if the dialog's keep_alive variable is set
     */
    Dialog *d = find_dialog(b, sender_name);
    if (d->keep_alive)
        return;

    set_dialog_cancel(b, sender_name);
    remove_frontend(b, sender_name);
    if (no_frontends(b))
    {
        g_message("No frontends connected .. exiting backend.\n");
        exit(EXIT_SUCCESS);
    }
}

static void on_refresh_backend(GDBusConnection *connection,
                               const gchar *sender_name,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *signal_name,
                               GVariant *parameters,
                               gpointer not_used)
{
    char *dialog_name = get_string_copy(sender_name);
    g_message("Refresh backend signal from %s\n", dialog_name);
    set_dialog_cancel(b, dialog_name); /// this stops the enumeration of printers
    refresh_printer_list(b, dialog_name);
}

static void on_hide_remote_printers(GDBusConnection *connection,
                                    const gchar *sender_name,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *signal_name,
                                    GVariant *parameters,
                                    gpointer not_used)
{
    char *dialog_name = get_string_copy(sender_name);
    g_message("%s signal from %s\n", HIDE_REMOTE_CUPS_SIGNAL, dialog_name);
    if (!get_hide_remote(b, dialog_name))
    {
        set_dialog_cancel(b, dialog_name);
        set_hide_remote_printers(b, dialog_name);
        refresh_printer_list(b, dialog_name);
    }
}

static void on_unhide_remote_printers(GDBusConnection *connection,
                                      const gchar *sender_name,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer not_used)
{
    char *dialog_name = get_string_copy(sender_name);
    g_message("%s signal from %s\n", UNHIDE_REMOTE_CUPS_SIGNAL, dialog_name);
    if (get_hide_remote(b, dialog_name))
    {
        set_dialog_cancel(b, dialog_name);
        unset_hide_remote_printers(b, dialog_name);
        refresh_printer_list(b, dialog_name);
    }
}

static void on_hide_temp_printers(GDBusConnection *connection,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name,
                                  GVariant *parameters,
                                  gpointer not_used)
{
    char *dialog_name = get_string_copy(sender_name);
    g_message("%s signal from %s\n", HIDE_TEMP_CUPS_SIGNAL, dialog_name);
    if (!get_hide_temp(b, dialog_name))
    {
        set_dialog_cancel(b, dialog_name);
        set_hide_temp_printers(b, dialog_name);
        refresh_printer_list(b, dialog_name);
    }
}

static void on_unhide_temp_printers(GDBusConnection *connection,
                                    const gchar *sender_name,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *signal_name,
                                    GVariant *parameters,
                                    gpointer not_used)
{
    char *dialog_name = get_string_copy(sender_name);
    g_message("%s signal from %s\n", UNHIDE_TEMP_CUPS_SIGNAL, dialog_name);
    if (get_hide_temp(b, dialog_name))
    {
        set_dialog_cancel(b, dialog_name);
        unset_hide_temp_printers(b, dialog_name);
        refresh_printer_list(b, dialog_name);
    }
}

static gboolean on_handle_is_accepting_jobs(PrintBackend *interface,
                                            GDBusMethodInvocation *invocation,
                                            const gchar *printer_name,
                                            gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    cups_dest_t *dest = get_dest_by_name(b, dialog_name, printer_name);
    g_assert_nonnull(dest);
    print_backend_complete_is_accepting_jobs(interface, invocation, cups_is_accepting_jobs(dest));
    return TRUE;
}

static gboolean on_handle_get_printer_state(PrintBackend *interface,
                                            GDBusMethodInvocation *invocation,
                                            const gchar *printer_name,
                                            gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    const char *state = get_printer_state(p);
    printf("%s is %s\n", printer_name, state);
    print_backend_complete_get_printer_state(interface, invocation, state);
    return TRUE;
}

static gboolean on_handle_get_human_readable_option_name(PrintBackend *interface,
                                                         GDBusMethodInvocation *invocation,
                                                         const gchar *option_name,
                                                         gpointer user_data)
{
    char *human_readable_name = get_human_readable_option_name(option_name);
    printf("Human readable name of option %s is %s\n", option_name, human_readable_name);
    print_backend_complete_get_human_readable_option_name(interface, invocation, human_readable_name);
    return TRUE;
}

static gboolean on_handle_get_human_readable_choice_name(PrintBackend *interface,
                                                         GDBusMethodInvocation *invocation,
                                                         const gchar *option_name,
                                                         const gchar *choice_name,
                                                         gpointer user_data)
{
    char *human_readable_name = get_human_readable_choice_name(option_name, choice_name);
    printf("Human readable name of choice %s for option %s is %s\n", choice_name, option_name, human_readable_name);
    print_backend_complete_get_human_readable_choice_name(interface, invocation, human_readable_name);
    return TRUE;
}

static gboolean on_handle_get_media_size(PrintBackend *interface,
                                         GDBusMethodInvocation *invocation,
                                         const gchar *media,
                                         gpointer user_data)
{
    int width, length;
    GVariant *variant;

    get_media_size(media, &width, &length);
    variant = g_variant_new ("(ii)", width, length);
    print_backend_complete_get_media_size(interface, invocation, variant);
    return TRUE;
}

static gboolean on_handle_ping(PrintBackend *interface,
                               GDBusMethodInvocation *invocation,
                               const gchar *printer_name,
                               gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    print_backend_complete_ping(interface, invocation);
    tryPPD(p);
    return TRUE;
}

static gboolean on_handle_print_file(PrintBackend *interface,
                                     GDBusMethodInvocation *invocation,
                                     const gchar *printer_name,
                                     const gchar *file_path,
                                     int num_settings,
                                     GVariant *settings,
                                     const gchar *final_file_path,
                                     gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);

    int job_id = print_file(p, file_path, num_settings, settings);

    char jobid_string[64];
    snprintf(jobid_string, sizeof(jobid_string), "%d", job_id);
    print_backend_complete_print_file(interface, invocation, jobid_string);

    /**
     * (Currently Disabled) Printing will always be the last operation, so remove that frontend
     */
    //set_dialog_cancel(b, dialog_name);
    //remove_frontend(b, dialog_name);

    if (no_frontends(b))
    {
        g_message("No frontends connected .. exiting backend.\n");
        exit(EXIT_SUCCESS);
    }
    return TRUE;
}

static gboolean on_handle_get_all_options(PrintBackend *interface,
                                          GDBusMethodInvocation *invocation,
                                          const gchar *printer_name,
                                          gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    Option *options;
    int count = get_all_options(p, &options);
    GVariantBuilder *builder;
    GVariant *variant;
    builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssia(s))"));

    for (int i = 0; i < count; i++)
    {
        GVariant *tuple = pack_option(&options[i]);
        g_variant_builder_add_value(builder, tuple);
        //g_variant_unref(tuple);
    }
    variant = g_variant_builder_end(builder);
    print_backend_complete_get_all_options(interface, invocation, count, variant);
    free_options(count, options);
    return TRUE;
}

static gboolean on_handle_get_active_jobs_count(PrintBackend *interface,
                                                GDBusMethodInvocation *invocation,
                                                const gchar *printer_name,
                                                gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    print_backend_complete_get_active_jobs_count(interface, invocation, get_active_jobs_count(p));
    return TRUE;
}
static gboolean on_handle_get_all_jobs(PrintBackend *interface,
                                       GDBusMethodInvocation *invocation,
                                       gboolean active_only,
                                       gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    int n;
    GVariant *variant = get_all_jobs(b, dialog_name, &n, active_only);
    print_backend_complete_get_all_jobs(interface, invocation, n, variant);
    return TRUE;
}
static gboolean on_handle_cancel_job(PrintBackend *interface,
                                     GDBusMethodInvocation *invocation,
                                     const gchar *job_id,
                                     const gchar *printer_name,
                                     gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    int jobid = atoi(job_id); /**to do. check if given job id is integer */
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    gboolean status = cancel_job(p, jobid);
    print_backend_complete_cancel_job(interface, invocation, status);
    return TRUE;
}

static gboolean on_handle_get_default_printer(PrintBackend *interface,
                                              GDBusMethodInvocation *invocation,
                                              gpointer user_data)
{
    char *def = get_default_printer(b);
    printf("%s\n", def);
    print_backend_complete_get_default_printer(interface, invocation, def);
    return TRUE;
}

static gboolean on_handle_keep_alive(PrintBackend *interface,
                                     GDBusMethodInvocation *invocation,
                                     gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    Dialog *d = find_dialog(b, dialog_name);
    d->keep_alive = TRUE;
    print_backend_complete_keep_alive(interface, invocation);
    return TRUE;
}
static gboolean on_handle_replace(PrintBackend *interface,
                                  GDBusMethodInvocation *invocation,
                                  const gchar *previous_name,
                                  gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    Dialog *d = find_dialog(b, previous_name);
    if (d != NULL)
    {
        g_hash_table_steal(b->dialogs, previous_name);
        g_hash_table_insert(b->dialogs, get_string_copy(dialog_name), d);
        g_message("Replaced %s --> %s\n", previous_name, dialog_name);
    }
    print_backend_complete_replace(interface, invocation);
    return TRUE;
}
void connect_to_signals()
{
    PrintBackend *skeleton = b->skeleton;
    g_signal_connect(skeleton,                               //instance
                     "handle-activate-backend",              //signal name
                     G_CALLBACK(on_handle_activate_backend), //callback
                     NULL);
    g_signal_connect(skeleton,                              //instance
                     "handle-get-all-options",              //signal name
                     G_CALLBACK(on_handle_get_all_options), //callback
                     NULL);
    g_signal_connect(skeleton,                   //instance
                     "handle-ping",              //signal name
                     G_CALLBACK(on_handle_ping), //callback
                     NULL);
    g_signal_connect(skeleton,                                  //instance
                     "handle-get-default-printer",              //signal name
                     G_CALLBACK(on_handle_get_default_printer), //callback
                     NULL);
    g_signal_connect(skeleton,                         //instance
                     "handle-print-file",              //signal name
                     G_CALLBACK(on_handle_print_file), //callback
                     NULL);
    g_signal_connect(skeleton,                                //instance
                     "handle-get-printer-state",              //signal name
                     G_CALLBACK(on_handle_get_printer_state), //callback
                     NULL);
    g_signal_connect(skeleton,                                //instance
                     "handle-is-accepting-jobs",              //signal name
                     G_CALLBACK(on_handle_is_accepting_jobs), //callback
                     NULL);
    g_signal_connect(skeleton,                                    //instance
                     "handle-get-active-jobs-count",              //signal name
                     G_CALLBACK(on_handle_get_active_jobs_count), //callback
                     NULL);
    g_signal_connect(skeleton,                           //instance
                     "handle-get-all-jobs",              //signal name
                     G_CALLBACK(on_handle_get_all_jobs), //callback
                     NULL);
    g_signal_connect(skeleton,                         //instance
                     "handle-cancel-job",              //signal name
                     G_CALLBACK(on_handle_cancel_job), //callback
                     NULL);
    g_signal_connect(skeleton,                         //instance
                     "handle-keep-alive",              //signal name
                     G_CALLBACK(on_handle_keep_alive), //callback
                     NULL);
    g_signal_connect(skeleton,                      //instance
                     "handle-replace",              //signal name
                     G_CALLBACK(on_handle_replace), //callback
                     NULL);
    g_signal_connect(skeleton,                                             //instance
                     "handle-get-human-readable-option-name",              //signal name
                     G_CALLBACK(on_handle_get_human_readable_option_name), //callback
                     NULL);
    g_signal_connect(skeleton,
                     "handle-get-human-readable-choice-name",              //instance
                     G_CALLBACK(on_handle_get_human_readable_choice_name), // signal name
                     NULL);                                                // callback
    g_signal_connect(skeleton,
                     "handle-get-media-size",              //instance
                     G_CALLBACK(on_handle_get_media_size), // signal name
                     NULL);                                // callback
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       STOP_BACKEND_SIGNAL,              //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_stop_backend,                  //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       REFRESH_BACKEND_SIGNAL,           //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_refresh_backend,               //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       HIDE_REMOTE_CUPS_SIGNAL,          //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_hide_remote_printers,          //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       UNHIDE_REMOTE_CUPS_SIGNAL,        //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_unhide_remote_printers,        //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       HIDE_TEMP_CUPS_SIGNAL,            //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_hide_temp_printers,            //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       UNHIDE_TEMP_CUPS_SIGNAL,          //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_unhide_temp_printers,          //callback
                                       NULL,                             //user_data
                                       NULL);
}
