#include <gtk/gtk.h>
class PenSizeSlider { public: GtkWidget* create() { return gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 100.0, 0.1); } };
