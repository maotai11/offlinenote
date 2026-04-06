@echo off
setlocal

set "APP_DIR=%~dp0"
set "PATH=%APP_DIR%;%PATH%"

set "GDK_PIXBUF_MODULEDIR=%APP_DIR%lib\gdk-pixbuf-2.0\2.10.0"
set "GDK_PIXBUF_MODULEFILE=%APP_DIR%lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"
set "XDG_DATA_DIRS=%APP_DIR%share"
set "FONTCONFIG_FILE=%APP_DIR%resources\fonts\fonts.conf"

start "" "%APP_DIR%offlinenote.exe"
