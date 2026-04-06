// src/ui/MainWindow.h
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <gtk/gtk.h>

class AppController;

class MainWindow {
public:
    MainWindow(GtkApplication* app, AppController& controller);
    ~MainWindow();
    void show();

private:
    GtkApplication* app_ = nullptr;
    AppController& controller_;
    GtkWidget* window_ = nullptr;
    void* state_ = nullptr;
};
