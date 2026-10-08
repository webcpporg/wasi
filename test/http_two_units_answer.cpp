// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// The second translation unit of http_two_units: it includes the entry point
// too, and defines the answer its main returns.

#include <webcpp/wasi.hpp>

#include <string>
#include <string_view>

#include "http_two_units.hpp"

namespace two_units {

webcpp::wasi::http::response answer(std::string_view method, std::string_view target) {
    return {
        .status = 200,
        .content_type = "text/plain",
        .body = "from the other unit: " + std::string(method) + " " + std::string(target) + "\n",
    };
}

}  // namespace two_units
