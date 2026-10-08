#!/bin/sh
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
#
# Builds the complete example of wasi's page, example/http_hello.cpp, by hand:
# with wasi-sdk, wit-bindgen and the WIT of wasi:http alone, without b2. The
# commands are the regions p2 and p3, which the page and the README show as
# they are written here. webcpp.serve-script runs this script on each WASI
# lane, as
#
#     build.sh <p2|p3> <component.wasm>
#
# with WASI_SDK, wasi-sdk's directory, WIT_BINDGEN, the wit-bindgen program,
# and WASI_WIT, the wit/deps directory of the wasi:http WIT of the version, in
# its environment, and serves what it builds. It works in <component.wasm>.work,
# a directory laid out as the page's: main.cpp, the world in wit/world.wit and
# the WIT linked at wit/deps.
set -e

version=$1
case $2 in
    /*) component=$2 ;;
    *) component=$(pwd)/$2 ;;
esac
here=$(cd "$(dirname "$0")" && pwd)
WASI=$(cd "$here/../.." && pwd)

work=$component.work
rm -rf "$work"
mkdir -p "$work"
cp -R "$here/$version/wit" "$work/wit"
ln -s "$WASI_WIT" "$work/wit/deps"
cp "$here/../http_hello.cpp" "$work/main.cpp"
cd "$work"

case $version in
p2)
# tag::p2[]
"$WIT_BINDGEN" c --world service --rename-world webcpp_wasi_http --out-dir gen wit

"$WASI_SDK/bin/clang" --target=wasm32-wasip2 \
    -c gen/webcpp_wasi_http.c -o gen/webcpp_wasi_http.o -Igen
"$WASI_SDK/bin/clang++" --target=wasm32-wasip2 -std=c++20 -fno-exceptions \
    -mexec-model=reactor -DWEBCPP_WASI_HTTP_P2 -I"$WASI/include" -Igen \
    main.cpp gen/webcpp_wasi_http.o gen/webcpp_wasi_http_component_type.o \
    -o hello.wasm
# end::p2[]
    ;;
p3)
# tag::p3[]
"$WIT_BINDGEN" c --world service --rename-world webcpp_wasi_http \
    --async 'wasi:http/handler@0.3.0#handle' --out-dir gen wit

"$WASI_SDK/bin/clang" --target=wasm32-wasip3 -pthread \
    -c gen/webcpp_wasi_http.c -o gen/webcpp_wasi_http.o -Igen
"$WASI_SDK/bin/clang++" --target=wasm32-wasip3 -std=c++20 \
    -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false \
    -mexec-model=reactor -DWEBCPP_WASI_HTTP_P3 -I"$WASI/include" -Igen \
    main.cpp gen/webcpp_wasi_http.o gen/webcpp_wasi_http_component_type.o \
    -fwasm-exceptions -lunwind -o hello.wasm
# end::p3[]
    ;;
*)
    echo "build.sh: $version is neither p2 nor p3" >&2
    exit 2
    ;;
esac

mv hello.wasm "$component"
