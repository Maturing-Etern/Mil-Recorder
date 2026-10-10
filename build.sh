




set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BAT_FILE="$SCRIPT_DIR/build.bat"

if [ ! -f "$BAT_FILE" ]; then
    echo "[ERROR] build.bat not found at: $BAT_FILE"
    exit 1
fi


CMD_EXE=""
for candidate in \
    "$SYSTEMROOT/System32/cmd.exe" \
    "$SYSTEMROOT/cmd.exe" \
    "/c/Windows/System32/cmd.exe" \
    "/c/Windows/cmd.exe"; do
    if [ -f "$candidate" ]; then
        CMD_EXE="$candidate"
        break
    fi
done

if [ -z "$CMD_EXE" ]; then
    echo "[ERROR] cmd.exe not found. Please run build.bat from Windows cmd directly."
    exit 1
fi

if [ "$1" = "clean" ]; then
    echo "[CLEAN] Removing old build artifacts..."
    rm -f "$SCRIPT_DIR/milthm-player.exe" \
          "$SCRIPT_DIR/milthm_player.res.o" \
          "$SCRIPT_DIR/source.cpp.patched.cpp"
    echo "[CLEAN] Done."
fi

echo "[INFO] Launching cmd.exe to run build.bat..."
echo "[INFO] cmd: $CMD_EXE"
echo "[INFO] bat: $BAT_FILE"
echo ""


WIN_SCRIPT_DIR=$(cygpath -w "$SCRIPT_DIR" 2>/dev/null || echo "$SCRIPT_DIR")


"$CMD_EXE" /c "cd /d \"$WIN_SCRIPT_DIR\" && build.bat"
