// src/application/StartupCheck.h
// 首次啟動資源完整性檢查
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

class StartupCheck {
public:
    void run();

private:
    void checkResourceDirectory();
    void checkFontFallback();
    void checkWritableUserDataDir();
    void checkGtkVersion();
    void createUserDirectories();
};
