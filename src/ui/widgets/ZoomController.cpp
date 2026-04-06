#include <gtk/gtk.h>
class ZoomController { public: GtkWidget* create() { return gtk_spin_button_new_with_range(0.1, 10.0, 0.1); } };
