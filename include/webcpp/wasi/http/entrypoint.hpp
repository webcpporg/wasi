// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The HTTP handler a WASI component exports, and the two macros that define the
 program's main it calls.

 It compiles only for wasip2 and wasip3: the build defines exactly one of
 WEBCPP_WASI_HTTP_P2 and WEBCPP_WASI_HTTP_P3, and generates the world's C
 bindings with `--rename-world webcpp_wasi_http`, so that webcpp_wasi_http.h
 and its names are the same in every project.

 @see "What your build provides", in the guide.
*/
#ifndef WEBCPP_WASI_HTTP_ENTRYPOINT_HPP
#define WEBCPP_WASI_HTTP_ENTRYPOINT_HPP

#if defined(WEBCPP_WASI_HTTP_P2) == defined(WEBCPP_WASI_HTTP_P3)
#error "define exactly one of WEBCPP_WASI_HTTP_P2 and WEBCPP_WASI_HTTP_P3"
#endif

extern "C" {
#include "webcpp_wasi_http.h"
}

#include <webcpp/wasi/http/response.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace webcpp::wasi::http {

/**
 Answers one HTTP request, given its method and its target: the program's main.

 A program defines it once, by writing its body between
 @ref WEBCPP_WASI_HTTP_MAIN_BEGIN and @ref WEBCPP_WASI_HTTP_MAIN_END; the
 handler calls it once for each request and sends what it returns. The body
 is inside a function of the namespace webcpp::wasi::http, so it names the
 program's own code with its full namespace, or with a namespace alias
 declared at its top.

 @param method The request's method as it travels on the wire, such as
 `"GET"`, or an extension method as it was sent.
 @param target The request target, its path and query, such as
 `"/v1/greeting?name=ana"`, or `"/"` when the request carried none.
 @return The response the handler sends.
 @note Both parameters view the request, and are valid until the main
 returns: copy what it keeps.
 @see "Your main", in the guide.
*/
[[nodiscard]] response http_main(std::string_view method, std::string_view target);

namespace detail {

/**
 Views a string the component ABI handed over, without copying it.
*/
inline std::string_view view_of(const webcpp_wasi_http_string_t& text) {
    return {reinterpret_cast<const char*>(text.ptr), text.len};
}

/**
 Returns the name of an HTTP method as it travels on the wire.
*/
inline std::string_view method_name(const wasi_http_types_method_t& method) {
    switch (method.tag) {
        case WASI_HTTP_TYPES_METHOD_GET: return "GET";
        case WASI_HTTP_TYPES_METHOD_HEAD: return "HEAD";
        case WASI_HTTP_TYPES_METHOD_POST: return "POST";
        case WASI_HTTP_TYPES_METHOD_PUT: return "PUT";
        case WASI_HTTP_TYPES_METHOD_DELETE: return "DELETE";
        case WASI_HTTP_TYPES_METHOD_CONNECT: return "CONNECT";
        case WASI_HTTP_TYPES_METHOD_OPTIONS: return "OPTIONS";
        case WASI_HTTP_TYPES_METHOD_TRACE: return "TRACE";
        case WASI_HTTP_TYPES_METHOD_PATCH: return "PATCH";
        default: return view_of(method.val.other);
    }
}

/**
 Builds the response's headers: its content type when it has one, and
 nothing else.

 @note append's result is not read: it fails only on a malformed or forbidden
 header, and the name here is a literal.
*/
inline wasi_http_types_own_fields_t headers_of(const response& answer) {
    const wasi_http_types_own_fields_t headers = wasi_http_types_constructor_fields();
    if (answer.content_type.empty()) {
        return headers;
    }
    wasi_http_types_field_name_t name{};
    webcpp_wasi_http_string_set(&name, "content-type");
    wasi_http_types_field_value_t value{
        .ptr = reinterpret_cast<uint8_t*>(const_cast<char*>(answer.content_type.data())),
        .len = answer.content_type.size(),
    };
    wasi_http_types_header_error_t header_error{};
    static_cast<void>(wasi_http_types_method_fields_append(wasi_http_types_borrow_fields(headers),
                                                           &name, &value, &header_error));
    return headers;
}

#ifdef WEBCPP_WASI_HTTP_P2

/**
 Answers the outparam with an internal error instead of a response, for a
 failure found before anything was committed.
*/
inline void refuse(exports_wasi_http_incoming_handler_own_response_outparam_t out) {
    wasi_http_types_result_own_outgoing_response_error_code_t result{};
    result.is_err = true;
    result.val.err.tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
    result.val.err.val.internal_error.is_some = false;
    wasi_http_types_static_response_outparam_set(out, &result);
}

/**
 Writes every byte to the body's stream; false when the stream refused a write.

 @note blocking-write-and-flush accepts at most 4096 bytes per call.
*/
inline bool write_all(wasi_http_types_borrow_outgoing_body_t body, std::string_view pending) {
    wasi_http_types_own_output_stream_t stream{};
    if (!wasi_http_types_method_outgoing_body_write(body, &stream)) {
        return false;
    }
    bool complete = true;
    while (!pending.empty()) {
        const std::size_t size = pending.size() < 4096 ? pending.size() : 4096;
        webcpp_wasi_http_list_u8_t bytes{
            .ptr = reinterpret_cast<uint8_t*>(const_cast<char*>(pending.data())),
            .len = size,
        };
        wasi_io_streams_stream_error_t stream_error{};
        if (!wasi_io_streams_method_output_stream_blocking_write_and_flush(
                wasi_io_streams_borrow_output_stream(stream), &bytes, &stream_error)) {
            complete = false;
            break;
        }
        pending.remove_prefix(size);
    }
    wasi_io_streams_output_stream_drop_own(stream);
    return complete;
}

/**
 Sends the answer: status and headers first, then the body.

 @note Once the outparam is set nothing can be taken back: a body not written
 whole is dropped without finish, which wasi:http reports as a failed response.
*/
inline void respond(exports_wasi_http_incoming_handler_own_response_outparam_t out,
                    const response& answer) {
    const wasi_http_types_own_outgoing_response_t response =
        wasi_http_types_constructor_outgoing_response(headers_of(answer));
    // Sets the status; the result is not read, because set-status-code fails
    // only outside 100-599, where no status a main answers should be.
    static_cast<void>(wasi_http_types_method_outgoing_response_set_status_code(
        wasi_http_types_borrow_outgoing_response(response), static_cast<uint16_t>(answer.status)));

    wasi_http_types_own_outgoing_body_t body{};
    if (!wasi_http_types_method_outgoing_response_body(
            wasi_http_types_borrow_outgoing_response(response), &body)) {
        wasi_http_types_outgoing_response_drop_own(response);
        refuse(out);
        return;
    }

    wasi_http_types_result_own_outgoing_response_error_code_t result{};
    result.is_err = false;
    result.val.ok = response;
    wasi_http_types_static_response_outparam_set(out, &result);

    if (!write_all(wasi_http_types_borrow_outgoing_body(body), answer.body)) {
        wasi_http_types_outgoing_body_drop_own(body);
        return;
    }
    // Finishes the body; the result is not read, because finish fails only when
    // the body disagrees with a content-length, which this response does not send.
    wasi_http_types_error_code_t finish_error{};
    static_cast<void>(wasi_http_types_static_outgoing_body_finish(body, nullptr, &finish_error));
}

#else  // WEBCPP_WASI_HTTP_P3

/**
 Waits inside the task until the copy pending on this waitable completes, and
 returns the event's status.

 @note The task never resumes in the callback, because context slot 0 belongs
 to wasi-libc on wasip3 (measured: using it trapped).
*/
inline uint32_t wait_for(uint32_t waitable) {
    const webcpp_wasi_http_waitable_set_t set = webcpp_wasi_http_waitable_set_new();
    webcpp_wasi_http_waitable_join(waitable, set);
    webcpp_wasi_http_event_t event{};
    webcpp_wasi_http_waitable_set_wait(set, &event);
    webcpp_wasi_http_waitable_join(waitable, 0);
    webcpp_wasi_http_waitable_set_drop(set);
    return event.code;
}

/**
 Writes every byte to the body's stream, however many writes that takes; false
 when the reader went away first.
*/
inline bool write_all(wasi_http_types_stream_u8_writer_t writer, std::string_view bytes) {
    while (!bytes.empty()) {
        webcpp_wasi_http_waitable_status_t status = wasi_http_types_stream_u8_write(
            writer, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
            status = wait_for(writer);
        }
        // Consumes what the write took, and stops when the reader went away.
        // WEBCPP_WASI_HTTP_WAITABLE_COUNT and WEBCPP_WASI_HTTP_WAITABLE_STATE are
        // wit-bindgen's macros, whose int literals bugprone-signed-bitwise reports here,
        // on wasip3's aggregate translation unit.
        // NOLINTNEXTLINE(bugprone-signed-bitwise)
        bytes.remove_prefix(WEBCPP_WASI_HTTP_WAITABLE_COUNT(status));
        // NOLINTNEXTLINE(bugprone-signed-bitwise)
        if (WEBCPP_WASI_HTTP_WAITABLE_STATE(status) != WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
            return bytes.empty();
        }
    }
    return true;
}

/**
 Resolves the trailers future: with none when the body was written whole, and
 with an internal error when it was not.

 @note That error is how the wasi:http of WASI 0.3 tells the host the body is
 incomplete.
*/
inline void resolve_trailers(
    wasi_http_types_future_result_option_own_trailers_error_code_writer_t writer, bool complete) {
    wasi_http_types_result_option_own_trailers_error_code_t outcome{};
    if (complete) {
        outcome.is_err = false;
        outcome.val.ok.is_some = false;
    } else {
        outcome.is_err = true;
        outcome.val.err.tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
        outcome.val.err.val.internal_error.is_some = false;
    }
    if (wasi_http_types_future_result_option_own_trailers_error_code_write(writer, &outcome) ==
        WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
        static_cast<void>(wait_for(writer));
    }
    wasi_http_types_future_result_option_own_trailers_error_code_drop_writable(writer);
}

#endif

#ifdef WEBCPP_WASI_HTTP_P2

/**
 Handles one request on wasip2: reads the method and the target, asks the program's
 main for the answer, and writes it back.
*/
inline void handle(exports_wasi_http_incoming_handler_own_incoming_request_t request,
                   exports_wasi_http_incoming_handler_own_response_outparam_t out) {
    const wasi_http_types_borrow_incoming_request_t borrowed =
        wasi_http_types_borrow_incoming_request(request);
    wasi_http_types_method_t method{};
    wasi_http_types_method_incoming_request_method(borrowed, &method);
    webcpp_wasi_http_string_t path{};
    const bool has_path = wasi_http_types_method_incoming_request_path_with_query(borrowed, &path);

    const response reply =
        http_main(method_name(method), has_path ? view_of(path) : std::string_view("/"));

    wasi_http_types_method_free(&method);
    if (has_path) {
        webcpp_wasi_http_string_free(&path);
    }
    wasi_http_types_incoming_request_drop_own(request);
    respond(out, reply);
}

#else  // WEBCPP_WASI_HTTP_P3

/**
 Handles one request on wasip3: reads the method and the target, asks the program's
 main for the answer, returns the response and then streams its body.

 @note The handler is lifted asynchronously and always ends with EXIT.
*/
inline webcpp_wasi_http_callback_code_t handle(exports_wasi_http_handler_own_request_t request) {
    const wasi_http_types_borrow_request_t borrowed = wasi_http_types_borrow_request(request);
    wasi_http_types_method_t method{};
    wasi_http_types_method_request_get_method(borrowed, &method);
    webcpp_wasi_http_string_t path{};
    const bool has_path = wasi_http_types_method_request_get_path_with_query(borrowed, &path);

    const response reply =
        http_main(method_name(method), has_path ? view_of(path) : std::string_view("/"));

    wasi_http_types_method_free(&method);
    if (has_path) {
        webcpp_wasi_http_string_free(&path);
    }
    wasi_http_types_request_drop_own(request);

    wasi_http_types_stream_u8_writer_t body_writer{};
    wasi_http_types_stream_u8_t contents = wasi_http_types_stream_u8_new(&body_writer);
    wasi_http_types_future_result_option_own_trailers_error_code_writer_t trailers_writer{};
    const wasi_http_types_future_result_option_own_trailers_error_code_t trailers =
        wasi_http_types_future_result_option_own_trailers_error_code_new(&trailers_writer);
    wasi_http_types_tuple2_own_response_future_result_void_error_code_t made{};
    wasi_http_types_static_response_new(headers_of(reply), &contents, trailers, &made);
    // Drops the future of whether the host sent the response, unread: the guest
    // could change nothing about a response already sent.
    wasi_http_types_future_result_void_error_code_drop_readable(made.f1);
    // Sets the status; the result is not read, because set-status-code fails
    // only outside 100-599, where no status a main answers should be.
    static_cast<void>(wasi_http_types_method_response_set_status_code(
        wasi_http_types_borrow_response(made.f0), static_cast<uint16_t>(reply.status)));

    exports_wasi_http_handler_result_own_response_error_code_t result{};
    result.is_err = false;
    result.val.ok = made.f0;
    exports_wasi_http_handler_handle_return(result);

    const bool complete = write_all(body_writer, reply.body);
    wasi_http_types_stream_u8_drop_writable(body_writer);
    resolve_trailers(trailers_writer, complete);
    return WEBCPP_WASI_HTTP_CALLBACK_CODE_EXIT;
}

#endif

}  // namespace detail

}  // namespace webcpp::wasi::http

#ifdef WEBCPP_WASI_HTTP_P2

/**
 Defines the `extern "C"` handler the target's world exports, which hands each
 request to the library's handler.

 On wasip2 that is the handler of wasi:http/incoming-handler; on wasip3, the
 handler of wasi:http/handler and its callback. @ref WEBCPP_WASI_HTTP_MAIN_BEGIN
 expands it, so a program that writes its main between the two macros never
 names it.

 @note Its name is the one wit-bindgen generates for wasi:http/incoming-handler,
 and the component's glue, webcpp_wasi_http.c, calls it by that name.
 @see "How a request flows", in the guide.
*/
#define WEBCPP_WASI_HTTP_EXPORTS()                                         \
    extern "C" void exports_wasi_http_incoming_handler_handle(             \
        exports_wasi_http_incoming_handler_own_incoming_request_t request, \
        exports_wasi_http_incoming_handler_own_response_outparam_t out) {  \
        webcpp::wasi::http::detail::handle(request, out);                  \
    }

#else  // WEBCPP_WASI_HTTP_P3

/**
 Defines the `extern "C"` handler the target's world exports, which hands each
 request to the library's handler.

 On wasip3 that is the handler of wasi:http/handler and its callback; on
 wasip2, the handler of wasi:http/incoming-handler. @ref WEBCPP_WASI_HTTP_MAIN_BEGIN
 expands it, so a program that writes its main between the two macros never
 names it.

 @note Their names are the ones wit-bindgen generates for wasi:http/handler,
 and the component's glue, webcpp_wasi_http.c, calls them by those names; the
 callback is never reached, because the handler always ends with EXIT.
 @see "How a request flows", in the guide.
*/
#define WEBCPP_WASI_HTTP_EXPORTS()                                                         \
    extern "C" webcpp_wasi_http_callback_code_t exports_wasi_http_handler_handle(          \
        exports_wasi_http_handler_own_request_t request) {                                 \
        return webcpp::wasi::http::detail::handle(request);                                \
    }                                                                                      \
    extern "C" webcpp_wasi_http_callback_code_t exports_wasi_http_handler_handle_callback( \
        webcpp_wasi_http_event_t* /*event*/) {                                             \
        return WEBCPP_WASI_HTTP_CALLBACK_CODE_EXIT;                                        \
    }

#endif

/**
 Opens the definition of the program's main, after the handler the target's
 world exports.

 The main's body follows it, returns a @ref webcpp::wasi::http::response for
 the request, and ends with @ref WEBCPP_WASI_HTTP_MAIN_END. Together they
 define @ref webcpp::wasi::http::http_main and, before it, the `extern "C"`
 handler of @ref WEBCPP_WASI_HTTP_EXPORTS.

 @param method The name the body gives the request's method, a
 `std::string_view` such as `"GET"`.
 @param target The name the body gives the request target, its path and
 query, a `std::string_view` such as `"/v1/greeting?name=ana"`.
 @note Write the pair once in a program, in one source file: a program has one
 main and exports one handler.
 @see "Your main", in the guide.
*/
#define WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)                                     \
    WEBCPP_WASI_HTTP_EXPORTS()                                                          \
    webcpp::wasi::http::response webcpp::wasi::http::http_main(std::string_view method, \
                                                               std::string_view target) {
/**
 Closes the definition of the program's main that @ref WEBCPP_WASI_HTTP_MAIN_BEGIN
 opened.

 @see "Your main", in the guide.
*/
#define WEBCPP_WASI_HTTP_MAIN_END() }

#endif  // WEBCPP_WASI_HTTP_ENTRYPOINT_HPP
