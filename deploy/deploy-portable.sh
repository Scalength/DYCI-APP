#!/usr/bin/env bash
#
# deploy-portable.sh — production packaging for the enrollment app.
#
# RUN THIS ON YOUR WINDOWS PC, inside the same MSYS2 shell you build in
# (UCRT64 or MINGW64). It:
#   1. rebuilds the app and runs the unit tests,
#   2. assembles dist/EnrollmentApp with the exe, EVERY DLL it needs and
#      the small GTK runtime files (pixbuf loaders, GLib schemas,
#      fontconfig) — so it runs on any Windows 10/11 PC with nothing
#      installed,
#   3. writes run.bat (sets the runtime paths, starts the app) and a
#      START-HERE.txt for the staff PCs,
#   4. zips everything into EnrollmentApp-Portable.zip.
#
# Send that one zip to whoever needs the app: extract, double-click
# run.bat. Done.
#
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== Enrollment App portable packaging =="

if ! command -v make >/dev/null || ! command -v pkg-config >/dev/null; then
    echo "ERROR: run this from the MSYS2 UCRT64/MINGW64 shell (make/pkg-config not found)." >&2
    exit 1
fi
if ! pkg-config --exists gtk+-3.0; then
    echo "ERROR: GTK3 dev package not found (pacman -S mingw-w64-ucrt-x86_64-gtk3)." >&2
    exit 1
fi

PREFIX="$(pkg-config --variable=prefix gtk+-3.0)"   # e.g. /ucrt64 or /mingw64
echo "GTK prefix: $PREFIX"

echo "-- building and testing --"
make clean >/dev/null
make test_gateway test_unit test_subjects >/dev/null
./test_unit
./test_subjects
make enrollment-app

BIN="enrollment-app.exe"
[ -f "$BIN" ] || BIN="enrollment-app"   # non-Windows fallback
echo "binary: $BIN"

DIST="dist/EnrollmentApp"
rm -rf "$DIST"
mkdir -p "$DIST"

echo "-- collecting runtime DLLs --"
cp "$BIN" "$DIST/"

# Every dependency that lives in an MSYS2/MinGW prefix (skips Windows
# system DLLs, which are always present on the target PC).
ldd "$BIN" | awk '$3 ~ /^\/(mingw|ucrt|clang)64\// { print $3 }' \
    | sort -u > dist/dlls.txt
if [ ! -s dist/dlls.txt ]; then
    echo "ERROR: ldd found no MinGW DLLs — was the exe built with MSYS2 gcc?" >&2
    exit 1
fi
while IFS= read -r dll; do cp "$dll" "$DIST/"; done < dist/dlls.txt
rm dist/dlls.txt
echo "DLLs bundled: $(ls "$DIST"/*.dll | wc -l)"

echo "-- GTK runtime files --"
# gdk-pixbuf loaders (the app itself draws no images; kept for safety).
if ls "$PREFIX"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll >/dev/null 2>&1; then
    mkdir -p "$DIST/lib/gdk-pixbuf-2.0/2.10.0/loaders"
    cp "$PREFIX"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll \
       "$DIST/lib/gdk-pixbuf-2.0/2.10.0/loaders/" || true
    if command -v gdk-pixbuf-query-loaders >/dev/null; then
        gdk-pixbuf-query-loaders \
            "$DIST"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll 2>/dev/null \
            | sed -e "s#$DIST#.#g" \
                  -e "s#$(cygpath -m "$PREFIX")#.#g" \
            > "$DIST/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache" || true
    fi
fi

# Compiled GLib schemas (avoids GSettings warnings on clean machines).
mkdir -p "$DIST/share/glib-2.0/schemas"
cp "$PREFIX"/share/glib-2.0/schemas/*.xml \
   "$DIST/share/glib-2.0/schemas/" 2>/dev/null || true
if [ -n "$(ls -A "$DIST/share/glib-2.0/schemas" 2>/dev/null)" ] \
   && command -v glib-compile-schemas >/dev/null; then
    glib-compile-schemas "$DIST/share/glib-2.0/schemas" || true
fi

# Fontconfig configuration (so text renders on PCs without MSYS2).
if [ -d "$PREFIX/etc/fonts" ]; then
    mkdir -p "$DIST/etc"
    cp -r "$PREFIX/etc/fonts" "$DIST/etc/fonts"
fi

echo "-- TLS trust store (prevents curl error 77 on clean PCs) --"
CABUNDLE="$PREFIX/etc/ssl/certs/ca-bundle.crt"
if [ -f "$CABUNDLE" ]; then
    cp "$CABUNDLE" "$DIST/ca-bundle.crt"
else
    echo "WARNING: $CABUNDLE not found." >&2
    echo "         Install it:  pacman -S mingw-w64-ucrt-x86_64-ca-certificates" >&2
    echo "         Without it the portable app cannot reach Google over HTTPS." >&2
    exit 1
fi

echo "-- launcher and docs --"
# Ship the real endpoint when present (no config needed on staff PCs);
# otherwise fall back to the example.
if [ -f endpoint.txt ]; then
    cp endpoint.txt "$DIST/"
else
    cp endpoint.txt.example "$DIST/" 2>/dev/null || true
fi

# run.bat — CRLF line endings (write with LF, then convert).
cat > "$DIST/run.bat" <<'EOF'
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
EOF

cat > "$DIST/START-HERE.txt" <<'EOF'
DYCI CME APP - HOW TO USE
=========================

1. Double-click  run.bat
   (You can also make a shortcut: right-click run.bat > Send to >
   Desktop (create shortcut). Use the shortcut from then on.)

2. If Windows shows "Windows protected your PC":
   click "More info"  >  "Run anyway".  This appears once per PC
   because the app is not digitally signed - it comes from your own
   office, so it is safe.

3. The Google endpoint is already built into the app. The optional
   file endpoint.txt (one line: the /exec URL) can override it without
   rebuilding. If submissions fail, check that file first.

4. Files next to the exe - do not delete them while enrolling:
     ca-bundle.crt             needed to reach Google securely
     subjects.ini              your edited subject lists
     app.log                   what happened, when
     failed-submissions.jsonl  enrollments that failed to send
                               (Wi-Fi down etc.) - the data is not
                               lost; send them again or paste the
                               rows into the sheet manually.
EOF

# CRLF so Notepad and cmd are happy.
for f in "$DIST/run.bat" "$DIST/START-HERE.txt"; do
    unix2dos "$f" 2>/dev/null || sed -i 's/$/\r/' "$f"
done

echo "-- zip --"
rm -f EnrollmentApp-Portable.zip
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "Compress-Archive -Path 'dist/EnrollmentApp' -DestinationPath 'EnrollmentApp-Portable.zip' -Force"

echo
echo "== Done =="
echo "  dist/EnrollmentApp/        portable folder (test it: run.bat)"
echo "  EnrollmentApp-Portable.zip $(du -h EnrollmentApp-Portable.zip | cut -f1) — send this to staff PCs"
echo
echo "Staff PC setup: extract the zip, double-click run.bat."
echo "Optional single-file installer: build it with Inno Setup using"
echo "installer/enrollment.iss (see README.md, 'Production packaging')."
