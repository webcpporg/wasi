// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The response a program's main answers an HTTP request with, in a header of its own.

 It includes nothing of the component ABI, so the rest of a program can build,
 hold and pass responses (as messages between actors, for example) and be
 tested natively.

 @see "The response", in the guide.
*/
#ifndef WEBCPP_WASI_HTTP_RESPONSE_HPP
#define WEBCPP_WASI_HTTP_RESPONSE_HPP

#include <string>
#include <string_view>

namespace webcpp::wasi::http {

/**
 What the program's main answers one HTTP request with: a status, a content type and a body.

 The handler sends the status, a `content-type` header when the content type is
 not empty, and the body; it sends no other header.

 @note An aggregate whose every member has a default, so that a designated
 initializer names only what differs: `{.status = 404}`.
 @see "The response", in the guide.
*/
struct response {
    /**
     The status code, 200 unless set.

     It is sent as it is: keep it within 100 to 599, the codes wasi:http
     accepts.
    */
    unsigned status = 200;
    /**
     The value of the `content-type` header, which is sent only when it is not empty.

     @note It is read after the main has returned, so it views what outlives
     the main: a string literal, such as `"application/json"`, or the method
     or the target the main was given, which the handler frees only once the
     response is written; never a string of the main's own. A view costs no
     allocation, where a std::string of 16 characters exceeds the small-string
     buffer of wasm32's libc++.
    */
    std::string_view content_type{};
    /**
     The body, sent whole, however long.
    */
    std::string body{};
};

}  // namespace webcpp::wasi::http

#endif  // WEBCPP_WASI_HTTP_RESPONSE_HPP
