// src/application/Application.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Application.h"
#include "AppController.h"
#include "PathManager.h"
#include "StartupCheck.h"
#include "../ui/MainWindow.h"
#include "../util/Logger.h"

#include <filesystem>
#include <stdexcept>

Application::Application()
{
    gtkApp_ = gtk_application_new(
        "com.example.offlinenote",
        G_APPLICATION_FLAGS_NONE
    );

    if (!gtkApp_) {
        throw std::runtime_error("Failed to create GtkApplication");
    }

    g_signal_connect(gtkApp_, "activate", G_CALLBACK(onActivate), this);
    g_signal_connect(gtkApp_, "shutdown", G_CALLBACK(onShutdown), this);
}

Application::~Application()
{
    if (gtkApp_) {
        g_object_unref(gtkApp_);
    }
}

int Application::run(int argc, char* argv[])
{
    return g_application_run(G_APPLICATION(gtkApp_), argc, argv);
}

void Application::onActivate(GtkApplication*, gpointer userData)
{
    static_cast<Application*>(userData)->handleActivate();
}

void Application::onShutdown(GtkApplication*, gpointer userData)
{
    static_cast<Application*>(userData)->handleShutdown();
}

void Application::handleActivate()
{
    Logger::info("Application activating");

    PathManager::instance().initialize();
    Logger::info("PathManager initialized, dataDir={}",
                 PathManager::instance().getUserDataDir().string());

    Logger::fullInit(PathManager::instance().getLogDir());

    StartupCheck check;
    check.run();

    setupCSS();

    controller_ = std::make_unique<AppController>();
    mainWindow_ = std::make_unique<MainWindow>(gtkApp_, *controller_);
    mainWindow_->show();

    Logger::info("Application startup complete");
}

void Application::handleShutdown()
{
    Logger::info("Application shutting down");
    controller_.reset();
    Logger::info("Shutdown complete");
}

void Application::setupCSS()
{
    auto cssPath = PathManager::instance().getResourceDir() / "themes"
                   / "offlinenote-fallback" / "style.css";

    if (!std::filesystem::exists(cssPath)) {
        Logger::warning("CSS theme file not found: {}, using GTK default",
                        cssPath.string());
        return;
    }

    GtkCssProvider* provider = gtk_css_provider_new();
    GError* error = nullptr;
    gtk_css_provider_load_from_path(provider, cssPath.string().c_str(), &error);

    if (error) {
        Logger::warning("Failed to load CSS: {}", error->message);
        g_error_free(error);
    }
    else {
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
    g_object_unref(provider);
}

/*static*/ void Application::showFatalError(const std::string& message)
{
    if (gtk_init_check(nullptr, nullptr)) {
        GtkWidget* dialog = gtk_message_dialog_new(
            nullptr,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "OfflineNote - Fatal Error"
        );
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog), "%s", message.c_str()
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    else {
        fprintf(stderr, "[FATAL] %s\n", message.c_str());
    }
}
