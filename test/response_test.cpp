// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests the response a main returns: its defaults, and that it owns its body.
// It includes nothing of the component ABI, so it builds natively too.

#include <webcpp/wasi/http/response.hpp>

#include <boost/core/lightweight_test.hpp>

#include <string>

namespace http = webcpp::wasi::http;

namespace {

/** A response left at its defaults answers 200, with no content type and no body. */
void defaults() {
    const http::response answer;
    BOOST_TEST_EQ(answer.status, 200U);
    BOOST_TEST(answer.content_type.empty());
    BOOST_TEST(answer.body.empty());
}

/** A designated initializer that names one member leaves the others at their defaults. */
void designated() {
    const http::response answer{.content_type = "text/plain"};
    BOOST_TEST_EQ(answer.status, 200U);
    BOOST_TEST_EQ(answer.content_type, "text/plain");
    BOOST_TEST(answer.body.empty());
}

/**
 The body is a copy: changing the text it was made from leaves it as it was.

 Tip: the handler sends the body after the main has returned, when the main's
 own strings are gone, so a body that pointed into one would be read after it
 was freed.
*/
void owns_its_body() {
    std::string text = "hello";
    const http::response answer{.status = 404, .body = text};
    text.assign("other");
    BOOST_TEST_EQ(answer.status, 404U);
    BOOST_TEST_EQ(answer.body, "hello");
}

}  // namespace

int main() {
    defaults();
    designated();
    owns_its_body();
    return boost::report_errors();
}
