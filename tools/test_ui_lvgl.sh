#!/bin/sh
# Host test of the LVGL side of the UI (ui/bonanza_ui.c): builds LVGL for this
# machine with the firmware's lv_conf.h. Slower than tools/test_ui.sh; the
# first run compiles LVGL into build-lvgl-host/.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/build-lvgl-host"
cmake -S "$project_dir/tests/lvgl_host" -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" >/dev/null
cmake --build "$build_dir" -j >/dev/null
cd "$build_dir"
./test_ui_lvgl
