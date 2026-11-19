#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/bonanza-bap-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$project_dir/src/control" \
    "$project_dir/tests/test_bap_client.c" "$project_dir/src/control/bap_client.c" \
    -o "$test_dir/test_bap_client"
"$test_dir/test_bap_client"
