// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The configuration of wasi, which each of its headers includes first: whether
 wasi is built without exceptions.

 @see "Exceptions on wasip2 and wasip3", in the guide.
*/
#ifndef WEBCPP_WASI_CONFIG_HPP
#define WEBCPP_WASI_CONFIG_HPP

// MrDocs parses wasi as a build with exceptions does, where the macro is not
// defined: this definition, undone at once, is the one the reference lists, and
// the parse goes on with every API that throws in it.
#ifdef __MRDOCS__
/**
 Defined when wasi is built without exceptions: automatically when the
 compiler has none (where Boost.Config defines `BOOST_NO_EXCEPTIONS`), when the
 developer defines `WEBCPP_NO_EXCEPTIONS` for every webcpp library, or when the
 developer defines it alone, to disable wasi's in a build that has them.

 wasi raises no exception of its own, and its handler catches none, so the
 macro changes nothing in it. It is provided so that a program can set it for
 every webcpp library alike. An exception wasi raised would go through
 `boost::throw_exception`, and an API that throws would be absent while this is
 defined.

 @see "Exceptions on wasip2 and wasip3", in the guide.
*/
#define WEBCPP_WASI_NO_EXCEPTIONS
#undef WEBCPP_WASI_NO_EXCEPTIONS
#endif

// wasi includes nothing of Boost, nor, until its redesign, <webcpp/config.hpp>,
// which includes Boost.Config: it reads the developer's WEBCPP_NO_EXCEPTIONS
// itself, and tells a build without exceptions as Boost.Config does, by the
// compiler's own macros: __cpp_exceptions, which Clang and GCC define with
// exceptions, and _CPPUNWIND, MSVC's. BOOST_NO_EXCEPTIONS counts too, defined by
// the build or by Boost.Config included before.
#if !defined(WEBCPP_WASI_NO_EXCEPTIONS) &&                            \
    (defined(WEBCPP_NO_EXCEPTIONS) || defined(BOOST_NO_EXCEPTIONS) || \
     !(defined(__cpp_exceptions) || defined(_CPPUNWIND)))
#define WEBCPP_WASI_NO_EXCEPTIONS
#endif

#endif  // WEBCPP_WASI_CONFIG_HPP
