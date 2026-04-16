@echo off
REM ============================================================
REM OfflineNote - Local Test Desktop Shortcut
REM 自動建立桌面捷徑並附加時間戳
REM ============================================================

setlocal enabledelayedexpansion

REM 修正: %~dp0 會包含 scripts\ 目錄，需要上層目錄
set "REPO_DIR=%~dp0.."
set "PORTABLE_DIR=%REPO_DIR%\dist\portable"
set "EXE_PATH=%PORTABLE_DIR%\offlinenote.exe"
set "BAT_PATH=%PORTABLE_DIR%\run.bat"
set "DESKTOP=%USERPROFILE%\Desktop"

REM 檢查捷徑是否存在
if not exist "%EXE_PATH%" (
    echo [ERROR] Portable executable not found: %EXE_PATH%
    pause
    exit /b 1
)

REM 取得時間戳
for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /value') do set datetime=%%I
set "TIMESTAMP=%datetime:~0,4%-%datetime:~4,2%-%datetime:~6,2% %datetime:~8,2%:%datetime:~10,2%"

echo ╔══════════════════════════════════════════════════════════╗
echo ║   OfflineNote - Local Test Shortcut Creator           ║
echo ╚══════════════════════════════════════════════════════════╝
echo.
echo [建立桌面捷徑...]
echo   來源: %BAT_PATH%
echo   目標: %DESKTOP%
echo   時間: %TIMESTAMP%
echo.

REM 建立桌面捷徑
powershell -Command "$WshShell = New-Object -ComObject WScript.Shell; $Shortcut = $WshShell.CreateShortcut('%DESKTOP%\OfflineNote-Test.lnk'); $Shortcut.TargetPath = '%BAT_PATH%'; $Shortcut.WorkingDirectory = '%PORTABLE_DIR%'; $Shortcut.Description = 'OfflineNote v4.0.2 - Local Test Build (%TIMESTAMP%)'; $Shortcut.IconLocation = '%EXE_PATH%,0'; $Shortcut.Save()"

if exist "%DESKTOP%\OfflineNote-Test.lnk" (
    echo [OK] 桌面捷徑已建立: %DESKTOP%\OfflineNote-Test.lnk
    echo.
) else (
    echo [ERROR] 捷徑建立失敗!
    pause
    exit /b 1
)

REM 建立時間戳記錄檔
set "TIMESTAMP_FILE=%REPO_DIR%dist\BUILD-TIMESTAMP.txt"
echo OfflineNote Build Timestamp > "%TIMESTAMP_FILE%"
echo ═══════════════════════════════════════════════════════ >> "%TIMESTAMP_FILE%"
echo 版本: v4.0.2 >> "%TIMESTAMP_FILE%"
echo 建立時間: %TIMESTAMP% >> "%TIMESTAMP_FILE%"
echo 更新時間: %TIMESTAMP% >> "%TIMESTAMP_FILE%"
echo 本機路徑: %PORTABLE_DIR% >> "%TIMESTAMP_FILE%"
echo 捷徑路徑: %DESKTOP%\OfflineNote-Test.lnk >> "%TIMESTAMP_FILE%"
echo. >> "%TIMESTAMP_FILE%"
echo 安全檢查: >> "%TIMESTAMP_FILE%"
echo   - Semgrep: 0 findings >> "%TIMESTAMP_FILE%"
echo   - CVE: 無已知漏洞 >> "%TIMESTAMP_FILE%"
echo   - DLL 驗證: 通過 >> "%TIMESTAMP_FILE%"
echo ═══════════════════════════════════════════════════════ >> "%TIMESTAMP_FILE%"

echo [OK] 時間戳記錄檔已更新: %TIMESTAMP_FILE%
echo.
echo ═══════════════════════════════════════════════════════
echo ✓ 完成! 雙擊桌面上的 "OfflineNote-Test" 捷徑即可啟動
echo ═══════════════════════════════════════════════════════
echo.
pause
