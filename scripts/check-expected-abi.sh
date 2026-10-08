#!/usr/bin/env bash
# Build the actual compiled Redis extractor TU and qb ABI object as C++20/23
# archives, then link a separately compiled consumer in all four combinations.
set -euo pipefail

if [[ $# -ne 1 || ! -f "$1/cmake/qbConfig.cmake" ]]; then
    echo "usage: $0 <qb source directory>" >&2
    exit 2
fi

qb_dir="$(cd "$1" && pwd)"
module_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cxx="${CXX:-c++}"
archiver="${AR:-ar}"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

version_line="$(sed -n 's/^set(QB_FRAMEWORK_VERSION "\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)")$/\1 \2 \3/p' "$qb_dir/cmake/qbConfig.cmake")"
if [[ -z "$version_line" ]]; then
    echo "could not read QB_FRAMEWORK_VERSION from $qb_dir/cmake/qbConfig.cmake" >&2
    exit 1
fi
read -r major minor patch <<< "$version_line"
defines=("-DQB_VERSION_MAJOR=$major" "-DQB_VERSION_MINOR=$minor" "-DQB_VERSION_PATCH=$patch")
includes=("-I$qb_dir/src" "-I$module_dir/src")
extra=()
case "${QB_EXPECTED_ABI_INSTRUMENT:-}" in
    "") ;;
    asan) extra=(-fsanitize=address,undefined -fno-omit-frame-pointer) ;;
    tsan) extra=(-fsanitize=thread -fno-omit-frame-pointer) ;;
    coverage) extra=(--coverage) ;;
    *) echo "QB_EXPECTED_ABI_INSTRUMENT must be asan, tsan or coverage" >&2; exit 2 ;;
esac

for archive_std in 20 23; do
    "$cxx" -std="c++$archive_std" -O1 -g "${extra[@]}" "${defines[@]}" "${includes[@]}" \
        -c "$module_dir/src/qbm/redis/server_reply.cpp" -o "$build_dir/redis-$archive_std.o"
    "$cxx" -std="c++$archive_std" -O1 -g "${extra[@]}" "${defines[@]}" "${includes[@]}" \
        -c "$qb_dir/src/qb/io/abi.cpp" -o "$build_dir/qb-$archive_std.o"
    "$archiver" rcs "$build_dir/libredis-$archive_std.a" "$build_dir/redis-$archive_std.o"
    "$archiver" rcs "$build_dir/libqb-$archive_std.a" "$build_dir/qb-$archive_std.o"
done

# The parser lives in public headers. Two translation units with different
# language modes must coexist in one binary, regardless of weak-symbol order.
for std in 20 23; do
    for role in main helper; do
        role_define=()
        if [[ "$role" == main ]]; then role_define=(-DQB_EXPECTED_ABI_PARSER_MAIN=1); fi
        "$cxx" -std="c++$std" -O0 -g "${extra[@]}" "${defines[@]}" "${includes[@]}" \
            "${role_define[@]}" -c "$module_dir/tests/abi/parser-cross-standard.cpp" \
            -o "$build_dir/parser-$role-$std.o"
    done
done

for main_std in 20 23; do
    for helper_std in 20 23; do
        for order in main-first helper-first; do
            binary="$build_dir/parser-$main_std-$helper_std-$order"
            if [[ "$order" == main-first ]]; then
                objects=("$build_dir/parser-main-$main_std.o" "$build_dir/parser-helper-$helper_std.o")
            else
                objects=("$build_dir/parser-helper-$helper_std.o" "$build_dir/parser-main-$main_std.o")
            fi
            "$cxx" -std="c++$main_std" "${extra[@]}" "${objects[@]}" "$build_dir/libqb-20.a" -o "$binary"
            echo "parser main C++$main_std + helper C++$helper_std ($order)"
            "$binary"
        done
    done
done

for archive_std in 20 23; do
    for consumer_std in 20 23; do
        binary="$build_dir/consumer-$consumer_std-archive-$archive_std"
        "$cxx" -std="c++$consumer_std" -O1 -g "${extra[@]}" "${defines[@]}" "${includes[@]}" \
            "$module_dir/tests/abi/expected-cross-standard.cpp" \
            "$build_dir/libredis-$archive_std.a" "$build_dir/libqb-$archive_std.a" -o "$binary"
        echo "archive C++$archive_std -> consumer C++$consumer_std"
        output="$("$binary")"
        echo "$output"
        expected_selector=0
        if [[ "$consumer_std" == 23 ]]; then expected_selector=1; fi
        if [[ "${QB_EXPECTED_ABI_REQUIRE_SPLIT:-0}" == 1 && "$output" != *"selector=$expected_selector"* ]]; then
            echo "expected C++$consumer_std to select expected implementation $expected_selector" >&2
            exit 1
        fi
    done
done
