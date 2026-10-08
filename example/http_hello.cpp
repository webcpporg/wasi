// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// tag::main[]
#include <webcpp/wasi/http/entrypoint.hpp>

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    if (method != "GET") {
        return {.status = 405, .content_type = "text/plain", .body = "only GET\n"};
    }
    return {
        .status = 200,
        .content_type = "text/plain",
        .body = "hello from " + std::string(target) + "\n",
    };
WEBCPP_WASI_HTTP_MAIN_END()
// end::main[]
