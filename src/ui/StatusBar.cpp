#include <gtk/gtk.h>
class StatusBar { public: GtkWidget* create() { return gtk_statusbar_new(); } };
