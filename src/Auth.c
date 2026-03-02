#define GOA_API_IS_SUBJECT_TO_CHANGE

#include "auth.h"
#include <goa/goa.h>
#include <gtk/gtk.h>
#include <microhttpd.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <sys/stat.h>
#include <unistd.h>

#define PORT 8080
#define AUTHORIZATION_URL "https://github.com/login/oauth/authorize"
#define TOKEN_URL "https://github.com/login/oauth/access_token"
#define TOKEN_FILE "access_token.txt"

/* Global variables */

static gchar *auth_code = NULL;
static GoaClient *client;
GtkWidget *token_label;

/* Function to load configuration from file */

void load_config(char **client_id, char **client_secret) {
    FILE *file = fopen("config.cfg", "r");
    if (file == NULL) {
        g_error("Could not open config.cfg file");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");

        if (key != NULL && value != NULL) {
            if (strcmp(key, "CLIENT_ID") == 0) {
                *client_id = g_strdup(value);
                g_print("Loaded CLIENT_ID: %s\n", *client_id);
            } else if (strcmp(key, "CLIENT_SECRET") == 0) {
                *client_secret = g_strdup(value);
                g_print("Loaded CLIENT_SECRET: %s\n", *client_secret);
            }
        } else {
            g_warning("Malformed line in config.cfg: %s", line);
        }
    }

    fclose(file);

    if (*client_id == NULL || *client_secret == NULL) {
        g_error("Client ID or Client Secret not found in config.cfg");
    }
}

/* Function to handle writing data received from curl */

size_t write_callback(void *ptr, size_t size, size_t nmemb, char **data) {
    size_t realsize = size * nmemb;
    *data = strndup(ptr, realsize); // Duplicate the data received
    return realsize;
}

/* Function to handle HTTP requests */

static enum MHD_Result handle_request(void *cls, struct MHD_Connection *connection,
                                      const char *url, const char *method,
                                      const char *version, const char *upload_data,
                                      size_t *upload_data_size, void **con_cls) {
    g_print("Received request for URL: %s with method: %s\n", url, method);

    if (strcmp(url, "/callback") == 0) {
        if (strcmp(method, "GET") == 0) {
            const char *code = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "code");

            if (code) {
                auth_code = g_strdup(code);
                g_print("Authorization code received: %s\n", auth_code);

                char *client_id = NULL;
                char *client_secret = NULL;
                load_config(&client_id, &client_secret);

                if (client_id == NULL || client_secret == NULL) {
                    g_error("Client ID or Client Secret is NULL. Check config.cfg file.");
                }

                /* Exchange authorization code for access token using libcurl */
                
                CURL *curl;
                CURLcode res;

                curl_global_init(CURL_GLOBAL_DEFAULT);
                curl = curl_easy_init();
                if (curl) {
                    struct curl_slist *headers = NULL;
                    headers = curl_slist_append(headers, "Accept: application/json");

                    curl_easy_setopt(curl, CURLOPT_URL, TOKEN_URL);
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                    curl_easy_setopt(curl, CURLOPT_POST, 1L);

                    gchar *post_data = g_strdup_printf("client_id=%s&client_secret=%s&code=%s",
                                                       client_id, client_secret, auth_code);
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);

                    char *response = NULL;
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

                    res = curl_easy_perform(curl);
                    if (res != CURLE_OK) {
                        g_warning("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                    } 
                    else {
                        g_print("Response: %s\n", response);

                        /* Parse JSON response to extract the access token */
                        
                        json_object *json_response = json_tokener_parse(response);
                        json_object *json_access_token;
                        
                        if (json_object_object_get_ex(json_response, "access_token", &json_access_token)) {
                            const char *access_token = json_object_get_string(json_access_token);
                            g_print("Access Token: %s\n", access_token);
                            gtk_label_set_text(GTK_LABEL(token_label), access_token);

                            /* Save the access token to a file with restricted permissions */
                            
                            FILE *token_file = fopen(TOKEN_FILE, "w");
                            if (token_file) {
                                fprintf(token_file, "%s\n", access_token);
                                fclose(token_file);
                                if (chmod(TOKEN_FILE, S_IRUSR | S_IWUSR) < 0) {
                                    g_warning("Failed to set file permissions for %s", TOKEN_FILE);
                                }
                            } 
                            else {
                                g_warning("Failed to open %s for writing.", TOKEN_FILE);
                            }
                        } 
                        else {
                            g_warning("Failed to extract access token from response.");
                            gtk_label_set_text(GTK_LABEL(token_label), "Failed to extract access token.");
                        }
                        json_object_put(json_response); // free json object
                    }

                    g_free(post_data);
                    curl_easy_cleanup(curl);
                    if (response) {
                        free(response);
                    }
                }
                curl_global_cleanup();

                g_free(client_id);
                g_free(client_secret);
            }

            const char *response_html = "<html><body><h1>Authentication successful! You can close this window. <br /> Happy Printing!!</h1></body></html>";
            struct MHD_Response *response_obj = MHD_create_response_from_buffer(strlen(response_html), (void *)response_html, MHD_RESPMEM_PERSISTENT);
            int ret = MHD_queue_response(connection, MHD_HTTP_OK, response_obj);
            MHD_destroy_response(response_obj);
            return ret;
        }
    }

    return MHD_NO;
}

/* Function to start the local server */

void start_local_server() {
    struct MHD_Daemon *daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL, &handle_request, NULL, MHD_OPTION_END);
    if (daemon == NULL) {
        g_error("Failed to start local server.");
    }
    g_print("Local server started on port %d\n", PORT);
}

/* Function to start the OAuth2 flow */

void start_oauth_flow() {
    char *client_id = NULL;
    char *client_secret = NULL;
    load_config(&client_id, &client_secret);

    if (client_id == NULL) {
        g_error("Client ID is NULL. Check config.cfg file.");
    }

    gchar *auth_url = g_strdup_printf(
        "%s?client_id=%s&redirect_uri=http://localhost:%d/callback&scope=user",
        AUTHORIZATION_URL, client_id, PORT);

    g_print("Visit the following URL to authorize the application:\n%s\n", auth_url);

    /* Open the URL in the default browser */
    
    gchar *command = g_strdup_printf("xdg-open '%s'", auth_url);
    system(command);
    g_free(command);

    g_free(auth_url);
    g_free(client_id);
    g_free(client_secret);
}

/* Callback when the GoaClient is ready */

void on_goa_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    client = goa_client_new_finish(res, &error);

    if (error) {
        g_warning("Failed to initialize GoaClient: %s", error->message);
        g_error_free(error);
        return;
    }

    g_print("GoaClient initialized successfully.\n");
}

/* Initialize the GoaClient */

void initialize_goa() {
    goa_client_new(NULL, on_goa_ready, NULL);
}

/* Main function */

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    /* Create a simple GTK window */
    
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "OAuth2 Authentication");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);

    /* Create a label to display the access token */
    
    token_label = gtk_label_new("No token received yet.");

    /* Create an "Authenticate" button */
    GtkWidget *auth_button = gtk_button_new_with_label("Authenticate");
    g_signal_connect(auth_button, "clicked", G_CALLBACK(start_oauth_flow), NULL);

    /* Create a vertical box to hold the label and button */
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), token_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), auth_button, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(window), vbox);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Initialize the GoaClient and start the local server */
    
    initialize_goa();
    start_local_server();

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}

