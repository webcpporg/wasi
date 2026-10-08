// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests that a content type viewing the request is sent as it reads: the main
// answers the target without its leading slash as its content type, so
// GET /text/plain answers content-type: text/plain, and the target in a body
// of its own.
//
// It fails, every time, on a handler that frees the target before it reads
// the content type, because the free itself overwrites the target. Each
// request meets the heap the same: wasmtime 47 gives each request of wasip2 a
// fresh instance, and on wasip3, where it reuses one, every earlier request
// gave back all it took. The bindings allocate the target as the request is
// read, and the main the body after it, so the target's chunk lies between
// two in use when the handler frees it, and wasi-libc's allocator files it in
// a free list whose links it keeps in the freed chunk: the target's first
// bytes become addresses, which hold a zero byte (measured on both versions:
// a freed "/text/plain" reads f0 0f 01 00 f0 0f 01 00 on wasip2). The content
// type read afterwards is no longer the target, and the host refuses it.

#include <webcpp/wasi/http/entrypoint.hpp>

#include <string>

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    if (method != "GET" || target.size() < 2) {
        return {.status = 404};
    }
    return {
        .status = 200,
        .content_type = target.substr(1),
        .body = "the content type of " + std::string(target) + "\n",
    };
WEBCPP_WASI_HTTP_MAIN_END()
