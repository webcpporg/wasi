// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 What a program answers to one HTTP request: a status, a content type and a
 body.

 Tip: a header of its own with nothing but the struct, so a program can hold
 one without the component ABI, for example as a message between actors.
*/
#ifndef WEBCPP_WASI_HTTP_RESPONSE_HPP
#define WEBCPP_WASI_HTTP_RESPONSE_HPP

#include <string>
#include <string_view>

namespace webcpp::wasi::http {

/**
 What a main returns: the status, the content type and the body of its answer.

 Tip: an aggregate whose every member has a default, so a designated
 initializer names only what differs: `{.status = 404}`.
*/
struct response {
    /**
     The status, 200 unless set; sent as it is, so keep it within 100 to 599.
    */
    unsigned status = 200;
    /**
     The content-type header, sent only when not empty.

     Tip: meant for a literal; a view costs no allocation, where a std::string
     of 16 characters exceeds the small-string buffer of wasm32's libc++.
    */
    std::string_view content_type{};
    /**
     The body, sent whole, however long.
    */
    std::string body{};
};

}  // namespace webcpp::wasi::http

#endif  // WEBCPP_WASI_HTTP_RESPONSE_HPP
