// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests that each method reaches the main by its name, the standard ones and
// an extension, and that the target, its path and its query, reaches it as it
// was sent. The answer names the method in its content type, which an answer
// to HEAD carries without a body, and echoes the method and the target in its
// body. CONNECT is left out on purpose: Python's http.client, which sends the
// requests, cannot send it to an origin server.

#include <webcpp/wasi/http/entrypoint.hpp>

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace {

/**
 The content type that names each method the requests send.

 Tip: literals, since the handler sends the content type after the main has
 returned, when a string the main built would be gone.
*/
constexpr auto content_types = std::to_array<std::pair<std::string_view, std::string_view>>({
    {"GET", "text/plain; method=GET"},
    {"HEAD", "text/plain; method=HEAD"},
    {"POST", "text/plain; method=POST"},
    {"PUT", "text/plain; method=PUT"},
    {"DELETE", "text/plain; method=DELETE"},
    {"OPTIONS", "text/plain; method=OPTIONS"},
    {"TRACE", "text/plain; method=TRACE"},
    {"PATCH", "text/plain; method=PATCH"},
    {"PURGE", "text/plain; method=PURGE"},
});

/** Returns the content type that names method, or one that says it is unknown. */
std::string_view content_type_of(std::string_view method) {
    for (const auto& [name, content_type] : content_types) {
        if (name == method) {
            return content_type;
        }
    }
    return "text/plain; method=unknown";
}

}  // namespace

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    return {
        .status = 200,
        .content_type = content_type_of(method),
        .body = std::string(method) + " " + std::string(target) + "\n",
    };
WEBCPP_WASI_HTTP_MAIN_END()
