@echo off
setlocal

set "APP_DIR=%~dp0dist\portable\"

if not exist "%APP_DIR%offlinenote.exe" (
    echo [ERROR] Portable build not found: "%APP_DIR%offlinenote.exe"
    echo [HINT] Re-run windows-setup\bundle-portable.ps1 after building.
    exit /b 1
)

call "%APP_DIR%OfflineNote-Portable.bat" %*
exit /b %ERRORLEVEL%
