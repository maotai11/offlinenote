#include <gtk/gtk.h>
class ColorButton { public: GtkWidget* create() { return gtk_color_button_new(); } };
