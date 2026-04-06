#include <gtk/gtk.h>
class PageCanvas { public: GtkWidget* create() { return gtk_drawing_area_new(); } };
