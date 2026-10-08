// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests that the entry point refuses a translation unit that defines
// both WEBCPP_WASI_HTTP_P2 and WEBCPP_WASI_HTTP_P3: the compile
// stops with the guard's own error, which clang's -verify finds, and not with
// another. The expectation names the guard's whole message, longer than a
// line, which clang-format would otherwise break.

// clang-format off
// expected-error@webcpp/wasi/http/entrypoint.hpp:* {{define exactly one of WEBCPP_WASI_HTTP_P2 and WEBCPP_WASI_HTTP_P3}}
// clang-format on
#include <webcpp/wasi/http/entrypoint.hpp>
