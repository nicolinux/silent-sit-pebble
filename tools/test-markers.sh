#!/usr/bin/env bash
#
# test-markers.sh — compile and run the host-side unit tests for the pure
# marker scheduler (src/c/markers.c). No Pebble SDK needed: test/pebble.h is a
# stub providing only the types markers.h uses.
set -euo pipefail

cd "$(dirname "$0")/.."

out="$(mktemp -d "${TMPDIR:-/tmp}/markers-test.XXXXXX")"
trap 'rm -rf "$out"' EXIT

cc -std=c99 -Wall -Wextra -Werror -Itest -Isrc/c \
  test/markers_test.c src/c/markers.c -o "$out/markers_test"

"$out/markers_test"
