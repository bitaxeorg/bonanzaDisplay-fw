#!/bin/sh
# Build and run the desktop UI simulator (no Pico SDK or LVGL needed).
#
#   tools/sim.sh                       interactive window (needs SDL2)
#   tools/sim.sh host:boot run:9000 shot:/tmp/mining.png
#                                      scripted run; see sim/sim_main.c for steps
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/build-sim"
mkdir -p "$build_dir"
sources="$project_dir/sim/sim_main.c $project_dir/src/control/bap_client.c $project_dir/ui/app/*.c"
flags="-std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
       -I$project_dir/ui -I$project_dir/src/control"

if [ "$#" -eq 0 ]; then
    command -v sdl2-config >/dev/null || { echo "Interactive mode needs SDL2 (brew install sdl2)." >&2; exit 1; }
    # No sanitizers here: under AddressSanitizer, Homebrew's SDL2 (sdl2-compat)
    # cannot find its SDL3 backend and blocks on an error dialog at start-up.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -O2 -I"$project_dir/ui" \
        -I"$project_dir/src/control" -DSIM_SDL $(sdl2-config --cflags) $sources \
        $(sdl2-config --libs) -lz -o "$build_dir/sim-sdl"
    exec "$build_dir/sim-sdl"
fi
# shellcheck disable=SC2086
"${CC:-cc}" $flags $sources -lz -o "$build_dir/sim"
exec "$build_dir/sim" "$@"
