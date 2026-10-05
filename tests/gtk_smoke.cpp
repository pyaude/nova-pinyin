// SPDX-License-Identifier: GPL-3.0-or-later
#include <fstream>
#include <gtk/gtk.h>
static void changed(GtkEditable *entry, gpointer path) {
    std::ofstream out(static_cast<const char *>(path));
    out << gtk_entry_get_text(GTK_ENTRY(entry));
}
static void activated(GtkEntry *, gpointer path) {
    static int count = 0;
    std::ofstream(std::string(static_cast<const char *>(path)) + ".activated") << ++count;
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    if (argc != 2)
        return 2;
    auto *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "NovaPinyin GTK Smoke");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 100);
    auto *entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(window), entry);
    g_signal_connect(entry, "changed", G_CALLBACK(changed), argv[1]);
    std::ofstream(std::string(argv[1]) + ".activated") << 0;
    g_signal_connect(entry, "activate", G_CALLBACK(activated), argv[1]);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(entry);
    gtk_main();
}
