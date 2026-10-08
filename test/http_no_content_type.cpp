// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests the headers of an answer: none of the component's when the content
// type is empty, with a body or without one, and exactly one content-type
// when it is set.

#include <webcpp/wasi/http/entrypoint.hpp>

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    if (method != "GET") {
        return {.status = 405};
    }
    if (target == "/plain") {
        return {.content_type = "text/plain", .body = "a content type\n"};
    }
    if (target == "/empty") {
        return {};
    }
    return {.body = "no content type\n"};
WEBCPP_WASI_HTTP_MAIN_END()
