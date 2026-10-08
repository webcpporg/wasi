// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests that the entry point refuses a translation unit that defines both
// WEBCPP_WASI_HTTP_P2 and WEBCPP_WASI_HTTP_P3: it does not compile.

#include <webcpp/wasi/http/entrypoint.hpp>
