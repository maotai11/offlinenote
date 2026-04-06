# OfflineNote

**開源、跨平台、完全離線的手寫筆記與 PDF 批註軟體**

[![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-blue.svg)](LICENSE)

---

## 核心特點

- 🖊️ **手寫筆記** — 支援壓力感應觸控筆與數位繪圖板
- 📄 **PDF 批註** — 在 PDF 上直接手寫、螢光標示
- 📴 **完全離線** — 不需要網路、不需要帳號、不上傳任何資料
- 🖥️ **跨平台** — Windows、macOS、Linux 原生支援
- 📦 **開箱即用** — 安裝後直接使用，不需要安裝額外依賴

## 快速開始

### 從原始碼建置

**Linux (Ubuntu/Debian):**
```bash
# 安裝依賴
sudo apt install cmake ninja-build libgtk-3-dev libcairo2-dev \
    libpoppler-glib-dev libgdk-pixbuf2.0-dev libxml2-dev zlib1g-dev

# 建置
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix ~/.local
```

**Windows (MSYS2):**
```bash
# 在 MSYS2 MinGW64 環境中
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 \
    mingw-w64-x86_64-poppler mingw-w64-x86_64-libxml2

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

**macOS:**
```bash
brew install gtk+3 poppler libxml2 cairo
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## 專案結構

```
OfflineNote/
├── CMakeLists.txt              # 根 CMake
├── cmake/                      # CMake 模組
│   ├── CompilerFlags.cmake
│   ├── DependencyVersions.cmake
│   ├── Dependencies.cmake
│   ├── InstallDirs.cmake
│   ├── Packaging.cmake
│   └── Sanitizers.cmake
├── src/
│   ├── main.cpp                # 進入點
│   ├── application/            # GTK Application 生命週期
│   ├── document/               # 文件模型
│   ├── serialization/          # 文件格式讀寫（含安全防護）
│   ├── rendering/              # 渲染引擎
│   ├── input/                  # 輸入處理
│   ├── tools/                  # 工具定義
│   ├── export/                 # 匯出引擎
│   ├── import/                 # 匯入引擎
│   ├── ui/                     # GTK UI 層
│   ├── platform/               # 平台抽象層
│   └── util/                   # 通用工具
├── test/                       # 測試
├── resources/                  # 執行期資源
├── scripts/                    # 開發腳本
└── README.md
```

## 授權

本軟體以 [GNU General Public License v2.0 or later](LICENSE) 授權釋出。

## 安全規格

本專案通過完整的安全審查，包含：
- XXE 攻擊防禦（三層防護）
- Zip Bomb 防禦
- 原子寫入保護
- 檔案鎖定機制
- 路徑遍歷防護

詳細安全規格請參閱 `development/` 目錄下的文件。
