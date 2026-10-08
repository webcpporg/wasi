// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// A component that answers JSON, which Boost.JSON writes: the list of a catalog's items, and one
// item by its id, served by wasmtime.

// tag::includes[]
#include <webcpp/wasi/http/entrypoint.hpp>

#include <boost/json.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <utility>
// end::includes[]

// tag::catalog[]
namespace {

namespace catalog {

struct item {
    std::int64_t id;
    std::string_view name;
};

constexpr std::array items{item{.id = 1, .name = "apple"}, item{.id = 2, .name = "pear"}};

boost::json::object json_of(const item& entry) {
    return {{"id", entry.id}, {"name", entry.name}};
}

// The item a target /v1/items/<id> names, or nullptr.
const item* find(std::string_view target) {
    constexpr std::string_view prefix = "/v1/items/";
    if (!target.starts_with(prefix)) {
        return nullptr;
    }
    const std::string_view digits = target.substr(prefix.size());
    std::int64_t id = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return nullptr;
    }
    for (const item& entry : items) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

webcpp::wasi::http::response answer(unsigned status, const boost::json::value& body) {
    return {
        .status = status,
        .content_type = "application/json",
        .body = boost::json::serialize(body) + "\n",
    };
}

}  // namespace catalog

}  // namespace

// end::catalog[]

// tag::main[]
WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    if (method != "GET") {
        return catalog::answer(405, boost::json::object{{"error", "only GET"}});
    }
    if (target == "/v1/items") {
        boost::json::array all;
        for (const catalog::item& entry : catalog::items) {
            all.push_back(catalog::json_of(entry));
        }
        return catalog::answer(200, boost::json::object{{"items", std::move(all)}});
    }
    if (const catalog::item* entry = catalog::find(target)) {
        return catalog::answer(200, catalog::json_of(*entry));
    }
    return catalog::answer(404, boost::json::object{{"error", "no such item"}, {"target", target}});
WEBCPP_WASI_HTTP_MAIN_END()
// end::main[]
