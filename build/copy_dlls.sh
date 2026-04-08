#!/bin/bash
# Copy all mingw64 DLL dependencies to portable/ (same dir as exe)
# DLLs MUST be in the same directory as the exe for Windows to find them
DEST="/c/Users/LIN/OfflineNote/dist/portable"
mkdir -p "$DEST"

EXE="/c/Users/LIN/OfflineNote/build/offlinenote.exe"

# Get all DLL paths from ldd
DLLS=$(ldd "$EXE" | grep '=> /mingw64' | sed 's/.*=> //' | sed 's/ .*//' | sort -u)

COUNT=0
for DLL in $DLLS; do
    BASENAME=$(basename "$DLL")
    if cp "$DLL" "$DEST/" 2>/dev/null; then
        echo "OK: $BASENAME"
        COUNT=$((COUNT + 1))
    else
        echo "FAIL: $DLL"
    fi
done

echo "---"
echo "Total: $COUNT DLLs copied to $DEST"
