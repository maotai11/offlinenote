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

本專案通過完整的安全審查（v4.0.2）。所有安全機制均有對應測試驗證：

### 安全防護清單

| 機制 | 防護目標 | 測試狀態 |
|------|---------|---------|
| **XML 安全解析** | XXE 攻擊（XML External Entity） | ✅ `test_security_xxe` |
| **安全解壓縮** | Zip Bomb / 壓縮炸彈 | ✅ `test_security_zipbomb` |
| **路徑驗證器** | 路徑遍歷 (`../`)、Null byte 注入 | ✅ `test_path_validator` |
| **PDF 尺寸限制** | 惡意 PDF 記憶體耗盡 | ✅ 內建尺寸驗證 |
| **原子寫入** | 斷電/崩潰導致檔案損毀 | ✅ `test_atomic_write` |
| **檔案鎖定** | 多行程同時寫入衝突 | ✅ `test_file_lock` |

### 安全聲明對應測試

- ✅ `test_security_zipbomb.cpp` — 測試空檔案、無效 gzip、有效小檔案、PDF 尺寸限制、顏色解析
- ✅ `test_path_validator.cpp` — 測試 Null byte、過長路徑、絕對路徑、相對路徑、目錄限制
- ✅ `test_security_xxe.cpp` — 測試 XXE 防禦
- ⚠️ `test_serialization.cpp` — 測試序列化格式
- ⚠️ `test_document.cpp` / `test_stroke.cpp` — 測試文件模型

### 已知限制

- 測試框架為 **Catch2 stub**（`test/catch_amalgamated.hpp`），目前僅驗證編譯與連結，尚未執行實際測試邏輯。計劃在 v4.3 替換為真實 Catch2 v3。
- **libxml2 已於 2025 年底官方停止維護**。我們使用 `XML_PARSE_NONET` + `loadsubset=0` + `replaceEntities=0` 多層防護，不依賴全域 entity loader。
- PDF 匯入採用 **PNG 渲染模式**（非原生 PDF annotation layer），原生 annotation 支援計劃於 v4.3 實作。

### 依賴版本

| 相依套件 | 最低版本 | 實際測試版本 |
|---------|---------|------------|
| GTK+ 3 | 3.22 | 3.24.52 |
| Cairo | 1.14 | 1.18.4 |
| Poppler GLib | 0.82 | **26.02.0** |
| libxml2 | 2.9 | **2.15.2** |
| zlib | 1.2.11 | 1.3.2 |
| GDK-Pixbuf | 2.36 | 2.44.5 |

