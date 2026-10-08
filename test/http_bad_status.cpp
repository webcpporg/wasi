// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests that a status outside 100-599 is refused as an internal error, and
// one at the edge of the range is sent as it is: the main answers the status
// its target names, GET /<status>. 0 and 99 lie below the range, 600 above
// it, and 65736 above it by 65536 more, so that a status cut to 16 bits would
// read 200; 599 is the last status sent.

#include <webcpp/wasi/http/entrypoint.hpp>

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    if (method != "GET" || target.size() < 2) {
        return {.status = 404};
    }
    const std::string_view digits = target.substr(1);
    unsigned status = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), status);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return {.status = 404};
    }
    return {
        .status = status,
        .content_type = "text/plain",
        .body = "the status " + std::string(digits) + "\n",
    };
WEBCPP_WASI_HTTP_MAIN_END()
