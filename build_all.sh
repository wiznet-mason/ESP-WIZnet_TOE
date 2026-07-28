#!/usr/bin/env bash
# Build every example into its own build directory, so ONE command produces all
# the app binaries. ESP-IDF links exactly one app_main per image, so "build all"
# means one binary per example (x backend) -- not a single combined image.
#
# Each config goes to builds/<example>_toe<toe>/ (own -B dir), so no fullclean is
# needed between configs and the normal `idf.py build` in build/ is left alone.
# (The per-config dirs live under builds/, NOT under build/ -- a build dir nested
# inside another build dir breaks ESP-IDF's linker-script generation.)
#
# Run inside an ESP-IDF environment (idf.py on PATH):
#     . $HOME/esp/esp-idf/export.sh
#     ./build_all.sh                     # every example, both backends (TOE 1 and 0)
#     TOE="1" ./build_all.sh             # every example, TOE=1 only
#     EXAMPLES="loopback" ./build_all.sh # one example, both backends
#
# Output binary: builds/<example>_toe<toe>/hello_world.bin
set -u
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ensure idf.py is available; if not, try to activate ESP-IDF via IDF_PATH,
# otherwise fail fast with a clear message.
if ! command -v idf.py >/dev/null 2>&1; then
    if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
        echo "Activating ESP-IDF from $IDF_PATH ..."
        # shellcheck disable=SC1091
        . "$IDF_PATH/export.sh"
    fi
fi
if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py not found -- ESP-IDF environment is not active." >&2
    echo "  Source export.sh first, e.g.:  . \$IDF_PATH/export.sh  (then re-run ./build_all.sh)" >&2
    exit 1
fi

: "${TOE:=1 0}"
if [ -z "${EXAMPLES:-}" ]; then
    EXAMPLES=""
    for d in "$root"/examples/*/; do
        [ -f "${d}CMakeLists.txt" ] && EXAMPLES="$EXAMPLES $(basename "$d")"
    done
fi

echo "Examples:$EXAMPLES   Backends (WIZNET_TOE): $TOE"

fail=0
summary=""
for ex in $EXAMPLES; do
    for toe in $TOE; do
        bdir="$root/builds/${ex}_toe${toe}"
        echo ""
        echo "=== $ex  (WIZNET_TOE=$toe)  ->  $bdir ==="
        if idf.py -B "$bdir" -DEXAMPLE="$ex" -DWIZNET_TOE="$toe" build; then
            summary="${summary}\n  ${ex}  TOE=${toe}  OK"
        else
            summary="${summary}\n  ${ex}  TOE=${toe}  FAIL"
            fail=1
        fi
    done
done

echo ""
echo "==================== SUMMARY ===================="
printf "%b\n" "$summary"
if [ "$fail" -ne 0 ]; then
    echo "Some builds FAILED."
    exit 1
fi
echo "All builds OK."
