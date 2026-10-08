// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// A WASI command, a program with a main that wasmtime runs, which parses a JSON text with
// Boost.JSON, reads what it holds and prints it. It reads no input: the text is a literal, so the
// program prints the same on every target.

// tag::includes[]
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <iostream>
#include <string_view>

namespace json = boost::json;
// end::includes[]

// tag::text[]
// What a component could be configured with: its name, its routes and its limits.
constexpr std::string_view config = R"({
    "name": "greeter",
    "routes": ["/v1/greeting", "/v1/health"],
    "limits": {"body": 65536, "debug": false}
})";
// end::text[]

// tag::main[]
int main() {
    boost::system::error_code error;
    json::value parsed = json::parse(config, error);
    json::object* object = parsed.if_object();
    if (error || object == nullptr) {
        std::cout << "not a configuration: " << error.message() << '\n';
        return 1;
    }
    if (const json::value* name = object->if_contains("name")) {
        std::cout << "name: " << json::serialize(*name) << '\n';
    }
    if (const json::value* routes = object->if_contains("routes");
        routes != nullptr && routes->is_array()) {
        for (const json::value& route : routes->get_array()) {
            std::cout << "route: " << json::serialize(route) << '\n';
        }
    }
    if (json::value* limits = object->if_contains("limits");
        limits != nullptr && limits->is_object()) {
        limits->get_object()["debug"] = true;
        std::cout << "limits: " << json::serialize(*limits) << '\n';
    }

    const json::value broken = json::parse(R"({"name": })", error);
    std::cout << "a broken text: " << error.message() << ", read as " << json::serialize(broken)
              << '\n';
}

// end::main[]
