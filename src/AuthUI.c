#include <gtk/gtk.h>
#include "auth.h"

// Declare token_label as a global variable
GtkWidget *token_label;

static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window;
    GtkWidget *button;
    GtkWidget *grid;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "OAuth2 Authentication");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);

    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);

    button = gtk_button_new_with_label("Authenticate");
    g_signal_connect(button, "clicked", G_CALLBACK(authenticate_user), NULL);
    gtk_grid_attach(GTK_GRID(grid), button, 0, 0, 1, 1);

    token_label = gtk_label_new("Access token will be displayed here.");
    gtk_grid_attach(GTK_GRID(grid), token_label, 0, 1, 1, 1);

    gtk_widget_show_all(window);

    initialize_goa();
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.example.OAuth", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

