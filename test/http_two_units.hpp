// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The answer of http_two_units, which a translation unit of its own defines.
*/
#ifndef WEBCPP_WASI_TEST_HTTP_TWO_UNITS_HPP
#define WEBCPP_WASI_TEST_HTTP_TWO_UNITS_HPP

#include <webcpp/wasi/http/response.hpp>

#include <string_view>

namespace two_units {

/** Returns the answer to a request of method for target. */
webcpp::wasi::http::response answer(std::string_view method, std::string_view target);

}  // namespace two_units

#endif  // WEBCPP_WASI_TEST_HTTP_TWO_UNITS_HPP
