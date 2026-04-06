// src/application/Application.h
// GtkApplication 封裝
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

class AppController;
class MainWindow;

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(int argc, char* argv[]);

    static void showFatalError(const std::string& message);

private:
    static void onActivate(GtkApplication* gtkApp, gpointer userData);
    static void onOpen(GtkApplication* gtkApp, GFile** files,
                       gint nFiles, const gchar* hint, gpointer userData);
    static void onShutdown(GtkApplication* gtkApp, gpointer userData);

    void handleActivate();
    void handleOpen(GFile** files, gint nFiles);
    void handleShutdown();
    void setupCSS();

    GtkApplication* gtkApp_ = nullptr;
    std::unique_ptr<AppController> controller_;
    std::unique_ptr<MainWindow> mainWindow_;
};
