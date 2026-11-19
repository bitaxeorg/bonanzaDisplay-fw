#!/bin/sh
# Host tests for the retro UI state machine: no Pico SDK, LVGL or board needed.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/bonanza-ui-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$project_dir/ui" \
    "$project_dir/tests/test_ui_app.c" "$project_dir"/ui/app/*.c \
    -o "$test_dir/test_ui_app"
"$test_dir/test_ui_app"
