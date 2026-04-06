#include <gtk/gtk.h>
class AboutDialog {
public:
    void show(GtkWindow* parent) {
        GtkWidget* dialog = gtk_about_dialog_new();
        gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "OfflineNote");
        gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "0.1.0");
        gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), "Offline-first note-taking app");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
};
