@echo off
setlocal

set "REPO_DIR=%~dp0"
set "APP_EXE=%REPO_DIR%build\offlinenote.exe"
set "MSYS2_ROOT=C:\msys64\mingw64"
set "MSYS2_BIN=%MSYS2_ROOT%\bin"
set "GDK_PIXBUF_ROOT=%MSYS2_ROOT%\lib\gdk-pixbuf-2.0\2.10.0"
set "GLIB_SCHEMAS=%MSYS2_ROOT%\share\glib-2.0\schemas"

if not exist "%APP_EXE%" (
    echo [ERROR] Build output not found: "%APP_EXE%"
    exit /b 1
)

if not exist "%MSYS2_BIN%\libgtk-3-0.dll" (
    echo [ERROR] MSYS2 GTK runtime not found: "%MSYS2_BIN%\libgtk-3-0.dll"
    echo [HINT] Install or restore the MinGW64 runtime under C:\msys64\mingw64
    exit /b 1
)

set "PATH=%MSYS2_BIN%;%PATH%"
set "GDK_PIXBUF_MODULEDIR=%GDK_PIXBUF_ROOT%"
set "GDK_PIXBUF_MODULEFILE=%GDK_PIXBUF_ROOT%\loaders.cache"
set "GSETTINGS_SCHEMA_DIR=%GLIB_SCHEMAS%"
set "XDG_DATA_DIRS=%MSYS2_ROOT%\share"

"%APP_EXE%" %*
exit /b %ERRORLEVEL%
