@echo off
rem Enrollment App launcher - sets the bundled-runtime paths and starts the app.
setlocal
cd /d "%~dp0"
set "PATH=%~dp0;%PATH%"
set "GDK_PIXBUF_MODULE_FILE=%~dp0lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"
set "GSETTINGS_SCHEMA_DIR=%~dp0share\glib-2.0\schemas"
if exist "%~dp0etc\fonts" set "FONTCONFIG_PATH=%~dp0etc\fonts"
if exist "%~dp0etc\fonts" set "FONTCONFIG_FILE=fonts.conf"
set "CURL_CA_BUNDLE=%~dp0ca-bundle.crt"
set "SSL_CERT_FILE=%~dp0ca-bundle.crt"
start "" "%~dp0enrollment-app.exe"
endlocal
