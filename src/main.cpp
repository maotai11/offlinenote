// src/main.cpp
// OfflineNote — 程式進入點
// SPDX-License-Identifier: GPL-2.0-or-later

#include "application/Application.h"
#include "util/Logger.h"

#include <cstdlib>
#include <exception>
#include <cstdio>

int main(int argc, char* argv[])
{
    // 盡早初始化日誌
    Logger::earlyInit();

    try {
        Application app;
        return app.run(argc, argv);
    }
    catch (const std::exception& ex) {
        Logger::fatal("Unhandled exception: {}", ex.what());
        fprintf(stderr, "[FATAL] OfflineNote encountered a critical error:\n%s\n", ex.what());
        return EXIT_FAILURE;
    }
    catch (...) {
        Logger::fatal("Unknown unhandled exception");
        fprintf(stderr, "[FATAL] OfflineNote encountered an unknown critical error.\n");
        return EXIT_FAILURE;
    }
}
