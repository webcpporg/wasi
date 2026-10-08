// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests that a body longer than one write arrives whole: 3 x 4096 + 17 bytes,
// three times the most that one blocking-write-and-flush of wasip2 takes, and
// a rest. The body is 192 numbered lines of 64 bytes, so a chunk of 4096 ends
// at a line's end and a missing one shows as lines that are not there, and
// then a last line of 17 bytes. On wasip3, wasmtime 47 takes the whole body in
// one write, so the writer's loop never takes a second turn there: the test
// holds that the body arrives whole, not that the loop goes on.

#include <webcpp/wasi/http/entrypoint.hpp>

#include <string>

namespace {

/** Returns the body of 3 x 4096 + 17 bytes. */
std::string long_body() {
    std::string body;
    for (int line = 1; line <= 192; ++line) {
        std::string text = "line " + std::to_string(line) + " ";
        text.resize(63, '.');
        body += text;
        body += '\n';
    }
    body += "end of the body.\n";
    return body;
}

}  // namespace

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    if (method != "GET" || target != "/long") {
        return {.status = 404};
    }
    return {.status = 200, .content_type = "text/plain", .body = long_body()};
WEBCPP_WASI_HTTP_MAIN_END()
