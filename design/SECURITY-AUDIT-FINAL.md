# OfflineNote — 最終安全審計報告

**專案路徑**: `C:\Users\LIN\OfflineNote`
**語言/框架**: C++17 · GTK3 · Cairo · Poppler · libxml2 · zlib
**審計日期**: 2026-04-15
**審計人**: Claude Sonnet 4.6（本對話實際執行）
**報告版本**: FINAL（以本報告為準，取代 AUDIT-REPORT.md 中未驗證的數據）

---

## ⚠️ 關於 AUDIT-REPORT.md 的說明

`design/AUDIT-REPORT.md` 被 Qwen Code 修改，加入了**本工具鏈無法驗證的資料**：

| 聲稱內容 | 實際狀況 |
|---------|---------|
| "Semgrep 0 findings (104 files, 11 rules)" | 實際：104 files，但只有 **2 rules** 啟動（免費層限制） |
| CVE-2026-1757 (libxml2) | **無法驗證**，知識截止 2025-08 無此 CVE 記錄 |
| CVE-2025-11896 (poppler) | **無法驗證**，請自行查詢 https://cve.mitre.org |
| libxml2 2.15.2 / poppler 26.02.0 | **無法確認版本**，需執行 `pkg-config --modversion libxml-2.0 poppler-glib` 確認 |

> **本報告只記錄本次對話中實際執行並確認的結果。**

---

## 一、工具掃描結果（實際執行）

### Semgrep — SAST 靜態分析

```
工具版本 : semgrep 1.157.0
規則集   : p/c (2 rules) + p/secrets
掃描目標 : src/ (104 files C/C++ headers)
發現數量 : 0 findings
```

**限制說明**：
- 免費層 `p/c` 只啟動 2 條規則，覆蓋率有限
- `.cpp` 實作檔案未被掃描（Semgrep 免費層 C++ 規則主要針對 header）
- 建議使用 **Joern** 做深度 C++ 分析（支援 CPG 污點追蹤）

```
設計稿掃描 : design/ (HTML 檔案)
規則集     : p/secrets
發現數量   : 0 findings
```

### Secrets 掃描（手動 grep）

```bash
# 執行結果
grep -rn "password|api_key|secret|token" src/ → 僅找到 author/token 文字（非憑證）
git log --all --stat | grep ".env|.key|.pem"   → 無敏感檔案歷史提交
```

**結論：無硬編碼憑證，無敏感檔案提交歷史。**

---

## 二、手動程式碼審計發現

### 🔴 HIGH — 安全架構空殼（必須修正）

**[H1] `SafeDecompressor.cpp` 未實作**
```
檔案: src/serialization/SafeDecompressor.cpp:3
問題: decompress() 直接 return {}，zip bomb 防禦完全無效
影響: 任何壓縮的 .onote 檔案均無大小限制保護
修正: 實作 zlib inflate with MAX_SIZE = 256MB 限制
```

**[H2] `NoteDeserializer.cpp` 未實作**
```
檔案: src/serialization/NoteDeserializer.cpp:4
問題: deserialize() 回傳空 Document，安全管線（SecureXmlParser + PathValidator）未串接
影響: 真正的反序列化在 MainWindow.cpp::load_note_from_file() 執行，完全繞過安全元件
修正: 將 MainWindow.cpp 的讀取邏輯遷移至此，串接 SecureXmlParser
```

### 🟡 MEDIUM — 邏輯缺陷

**[M1] `AppController::openDocument()` 不呼叫安全流程**
```
檔案: src/application/AppController.cpp:26
問題: TODO 已標注「含 FileLock、PathValidator、Deserializer」但均未實作
影響: 開啟文件時無路徑驗證、無檔案鎖
修正: 串接 PathValidator::validatePath() + FileLock + NoteDeserializer
```

**[M2] `validatePdfPath()` 兩分支行為相同**
```
檔案: src/util/PathValidator.cpp:82-88
問題: fromUserFileDialog=true/false 都使用 AllowAnyAbsolute
設計意圖: 嵌入路徑（來自 .onote 檔案）應用 RestrictToDirectory 限制
修正: else 分支改為 validatePath(path, RestrictToDirectory, allowedImagesRoot)
```

### 🟠 LOW — 程式碼品質

**[L1] `char line[4096]` 超長行截斷**
```
檔案: src/ui/MainWindow.cpp:1969
問題: fgets 超過 4096 字元的行被靜默截斷為兩段，可能造成資料錯誤
修正: 改用 std::string + std::getline(f, sline)
```

**[L2] `const char*` 強制轉 `char*`**
```
檔案: src/ui/MainWindow.cpp:1834, 1998-2005
問題: (char*)line.c_str() 丟棄 const，雖不直接漏洞但違反型別安全
修正: 宣告為 const char* 接收 strstr 回傳值
```

---

## 三、已確認正確防護

| 防護機制 | 實作狀況 | 驗證方式 |
|---------|---------|---------|
| XXE 攻擊防護 | ✅ `SecureXmlParser.cpp` 三層防護完整 | 讀取原始碼確認 |
| Path Traversal / Null Byte | ✅ `PathValidator.cpp` 正確實作 | 讀取原始碼確認 |
| Stack Protection | ✅ `-fstack-protector-strong` | `cmake/CompilerFlags.cmake` |
| 無硬編碼憑證 | ✅ grep 掃描無發現 | 本次執行確認 |
| 無 system()/popen() | ✅ grep 掃描無發現 | 本次執行確認 |
| 原子寫入 | ✅ `AtomicRename.cpp` 已實作 | 讀取原始碼確認 |
| 檔案鎖 | ✅ `FileLock.cpp` 已實作 | 讀取原始碼確認 |

---

## 四、已完成的修正

| 項目 | 狀態 | 說明 |
|------|------|------|
| `libpoppler-glib-8.dll` 缺失 | ✅ 已修正 | 從 MSYS2 複製至 dist/portable |
| `libpoppler-157.dll` 缺失 | ✅ 已修正 | 同上 |
| 中文儲存無回應（v3.0） | ℹ️ 原因確認 | v3.0 為舊版；新版 offlinenote.exe DLL 問題已修正，可改用新版 |
| **[H1] SafeDecompressor** | ✅ 已實作 | 50MB 輸入上限 / 500MB 輸出上限 / 100:1 壓縮比 / 迭代次數上限 |
| **[H2] NoteDeserializer** | ✅ 已實作 | 串接 SafeDecompressor + SecureXmlParser + PathValidator |
| **[M1] AppController** | ✅ 已實作 | openDocument() 呼叫 PathValidator + FileLock + NoteDeserializer |
| **[M2] validatePdfPath** | ✅ 已修正 | 嵌入路徑（fromUserFileDialog=false）改用 RestrictToDirectory + exe 目錄 |
| **[L2] unsafe const cast** | ✅ 已修正 | MainWindow.cpp 所有 `(char*)line.c_str()` 改為 `const char*` |

---

## 五、修正狀態總覽

```
P0（已完成）
├─ [H1] ✅ SafeDecompressor — 50MB/500MB/100:1/迭代上限
└─ [H2] ✅ NoteDeserializer — 串接 SecureXmlParser + PathValidator

P1（已完成）
├─ [M1] ✅ AppController::openDocument() 串接 PathValidator + FileLock
└─ [M2] ✅ validatePdfPath 嵌入路徑改 RestrictToDirectory

P2（已完成）
├─ [L1] ✅ fgets 改 growing-buffer（chunk 4096，截斷時自動續讀）
└─ [L2] ✅ 所有 (char*)c_str() 改為 const char*（MainWindow.cpp）

CI/CD 建議（待辦）
├─ 加入 Gitleaks pre-commit hook（Secrets 防護）
├─ 升級 Semgrep 至付費版或使用 Joern（C++ 深度分析）
└─ 建立 GitHub Actions 自動化掃描流程
```

---

## 六、依存套件版本確認指令

```bash
# 在 MSYS2 環境執行以取得實際版本
pkg-config --modversion libxml-2.0
pkg-config --modversion poppler-glib
pkg-config --modversion gtk+-3.0
pkg-config --modversion cairo

# 查詢已知 CVE（需網路）
# https://cve.mitre.org/cgi-bin/cvekey.cgi?keyword=libxml2
# https://cve.mitre.org/cgi-bin/cvekey.cgi?keyword=poppler
```

> ⚠️ `AUDIT-REPORT.md` 中引用的 CVE-2026-1757 和 CVE-2025-11896 **未經本工具鏈驗證**，
> 請開發者自行至 MITRE CVE 資料庫查詢後再決定是否升級依存套件。

---

*本報告所有掃描數字均來自本次對話中實際執行的指令。*
*最後更新：2026-04-15 by Claude Sonnet 4.6*
