// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests that two translation units of one program include the entry point and
// the program links: the header defines only inline functions and macros, and
// the exported handler comes from the main's expansion, in this one. The other,
// http_two_units_answer.cpp, includes it through <webcpp/wasi.hpp>.

#include <webcpp/wasi/http/entrypoint.hpp>

#include "http_two_units.hpp"

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    return two_units::answer(method, target);
WEBCPP_WASI_HTTP_MAIN_END()
