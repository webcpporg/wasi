// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// <webcpp/wasi/config.hpp> defines WEBCPP_WASI_NO_EXCEPTIONS exactly when the
// build has no exceptions, and keeps the developer's own definition in a build
// that has them. test/Jamfile builds this file twice: as the build asks, and
// with the macro defined as a developer defines it, on the command line, which
// WEBCPP_TEST_WASI_DEVELOPER_DEFINED tells this file.

#include <webcpp/wasi/config.hpp>

// wasi includes nothing of Boost, and tells a build without exceptions by
// itself: Boost.Config, included only now, says whether this build is one.
#ifdef BOOST_CONFIG_HPP
#error "config.hpp includes Boost.Config, which wasi does without"
#endif

#include <boost/config.hpp>

#ifdef WEBCPP_TEST_WASI_DEVELOPER_DEFINED
#ifndef WEBCPP_WASI_NO_EXCEPTIONS
#error "config.hpp drops WEBCPP_WASI_NO_EXCEPTIONS, which the developer defined"
#endif
#elif defined(BOOST_NO_EXCEPTIONS) != defined(WEBCPP_WASI_NO_EXCEPTIONS)
#error "WEBCPP_WASI_NO_EXCEPTIONS is not defined exactly when the build has no exceptions"
#endif
