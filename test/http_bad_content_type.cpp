// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests that a content type the host refuses as a field value is refused as
// an internal error, not dropped: one that holds a CR and an LF, which would
// start a header of its own, and one that holds a NUL. A tab is a field
// value's own, and is sent as it is.

#include <webcpp/wasi/http/entrypoint.hpp>

#include <string_view>

using namespace std::string_view_literals;

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    if (method != "GET") {
        return {.status = 405};
    }
    if (target == "/crlf") {
        return {.content_type = "text/plain\r\nx-injected: 1", .body = "a CR and an LF\n"};
    }
    if (target == "/nul") {
        return {.content_type = "text/plain\0x"sv, .body = "a NUL\n"};
    }
    if (target == "/tab") {
        return {.content_type = "text/plain;\tcharset=utf-8", .body = "a tab\n"};
    }
    return {.status = 404};
WEBCPP_WASI_HTTP_MAIN_END()
