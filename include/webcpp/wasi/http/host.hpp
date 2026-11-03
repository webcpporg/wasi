// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_WASI_HTTP_HOST_HPP
#define WEBCPP_WASI_HTTP_HOST_HPP

#include <webcpp/wasi/config.hpp>
#include <webcpp/wasi/http/message.hpp>

namespace webcpp::wasi::http {

#ifdef WEBCPP_WASI_HTTP_P2
/** Owned request resource of the selected WASI ABI. */
using request_handle = wasi_http_types_own_incoming_request_t;
/** P2 response commitment resource. */
using response_out = wasi_http_types_own_response_outparam_t;
#else
/** Owned request resource of the selected WASI ABI. */
using request_handle = wasi_http_types_own_request_t;

/** P3 returns a response from the active task rather than a P2 outparam resource. */
struct response_out {};
#endif

/** An incoming host request.

    Its child body must be destroyed before this owner on P2. */
class incoming_request {
#ifdef WEBCPP_WASI_HTTP_P2
    detail::resource<request_handle, wasi_http_types_incoming_request_drop_own> request_;
#else
    detail::resource<request_handle, wasi_http_types_request_drop_own> request_;
#endif
    wasi_http_types_method_t method_{};
    webcpp_wasi_http_string_t target_{};
    webcpp_wasi_http_string_t authority_{};
    bool has_target_ = false;
    bool has_authority_ = false;

public:
    /** Takes ownership of the incoming request.
        @param request Request supplied by the exported handler. */
    explicit incoming_request(request_handle request) : request_(request) {
#ifdef WEBCPP_WASI_HTTP_P2
        const auto borrowed = wasi_http_types_borrow_incoming_request(request);
        wasi_http_types_method_incoming_request_method(borrowed, &method_);
        has_target_ = wasi_http_types_method_incoming_request_path_with_query(borrowed, &target_);
        has_authority_ = wasi_http_types_method_incoming_request_authority(borrowed, &authority_);
#else
        const auto borrowed = wasi_http_types_borrow_request(request);
        wasi_http_types_method_request_get_method(borrowed, &method_);
        has_target_ = wasi_http_types_method_request_get_path_with_query(borrowed, &target_);
        has_authority_ = wasi_http_types_method_request_get_authority(borrowed, &authority_);
#endif
    }

    /** Forbids copying resource ownership. */
    incoming_request(const incoming_request&) = delete;
    /** Forbids copying resource ownership. */
    incoming_request& operator=(const incoming_request&) = delete;

    /** Releases metadata and any unconsumed request. */
    ~incoming_request() {
        wasi_http_types_method_free(&method_);
        if (has_target_) {
            webcpp_wasi_http_string_free(&target_);
        }
        if (has_authority_) {
            webcpp_wasi_http_string_free(&authority_);
        }
    }

    /** Borrows the method name.
    @return A view valid while this request owner remains alive. */
    [[nodiscard]] std::string_view method() const { return detail::method_name(method_); }

    /** Borrows the original path and query.
    @return A view valid while this request owner remains alive. */
    [[nodiscard]] std::string_view target() const {
        return has_target_ ? detail::view_of(target_) : "/";
    }

    /** Borrows the request authority.
    @return A view valid while this request owner remains alive. */
    [[nodiscard]] std::string_view authority() const {
        return has_authority_ ? detail::view_of(authority_) : "";
    }

    /** Acquires the exposed header fields.
    @return Owning fields exposing the host headers. */
    [[nodiscard]] message_fields headers() const {
#ifdef WEBCPP_WASI_HTTP_P2
        return message_fields(wasi_http_types_method_incoming_request_headers(
            wasi_http_types_borrow_incoming_request(request_.get())));
#else
        return message_fields(wasi_http_types_method_request_get_headers(
            wasi_http_types_borrow_request(request_.get())));
#endif
    }

    /** Consumes the content once, after reading metadata.
        @param error Receives an invalid-state error on repeated consumption.
    @return The body reader, or an empty reader on failure. */
    /** Consumes the response or request body once.
        @param error Receives a resource-state failure. */
    body_reader consume(message_error& error) {
        if (!request_) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
#ifdef WEBCPP_WASI_HTTP_P2
        wasi_http_types_own_incoming_body_t body{};
        if (!wasi_http_types_method_incoming_request_consume(
                wasi_http_types_borrow_incoming_request(request_.get()), &body)) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        return body_reader(body);
#else
        wasi_http_types_future_result_void_error_code_writer_t writer{};
        const auto completion = wasi_http_types_future_result_void_error_code_new(&writer);
        wasi_http_types_tuple2_stream_u8_future_result_option_own_trailers_error_code_t body{};
        wasi_http_types_static_request_consume_body(request_.release(), completion, &body);
        return {body, writer};
#endif
    }
};

/** Reports a pre-commit error.

    Never call after committing.
    @param out Active response commitment resource. */
inline void refuse_response(response_out out) {
#ifdef WEBCPP_WASI_HTTP_P2
    detail::refuse(out);
#else
    static_cast<void>(out);
    detail::refuse();
#endif
}

/** Commits response headers once and returns its body writer.
    @param out Active response commitment resource.
    @param status HTTP status code.
    @param headers Header fields transferred to the host.
    @param error Receives any pre-commit failure.
    @return The committed body writer, or an empty writer on failure. */
inline body_writer commit_response(response_out out, std::uint16_t status, message_fields headers,
                                   message_error& error) {
    if (!detail::sendable(status)) {
        error = {.code = message_errc::invalid_message};
        refuse_response(out);
        return {};
    }
#ifdef WEBCPP_WASI_HTTP_P2
    auto const response = wasi_http_types_constructor_outgoing_response(headers.release());
    static_cast<void>(wasi_http_types_method_outgoing_response_set_status_code(
        wasi_http_types_borrow_outgoing_response(response), status));
    wasi_http_types_own_outgoing_body_t body{};
    if (!wasi_http_types_method_outgoing_response_body(
            wasi_http_types_borrow_outgoing_response(response), &body)) {
        wasi_http_types_outgoing_response_drop_own(response);
        error = {.code = message_errc::invalid_state};
        refuse_response(out);
        return {};
    }
    wasi_http_types_result_own_outgoing_response_error_code_t result{};
    result.val.ok = response;
    wasi_http_types_static_response_outparam_set(out, &result);
    return body_writer(body);
#else
    static_cast<void>(out);
    wasi_http_types_stream_u8_writer_t writer{};
    auto contents = wasi_http_types_stream_u8_new(&writer);
    wasi_http_types_future_result_option_own_trailers_error_code_writer_t trailers_writer{};
    const auto trailers =
        wasi_http_types_future_result_option_own_trailers_error_code_new(&trailers_writer);
    wasi_http_types_tuple2_own_response_future_result_void_error_code_t made{};
    wasi_http_types_static_response_new(headers.release(), &contents, trailers, &made);
    wasi_http_types_future_result_void_error_code_drop_readable(made.f1);
    static_cast<void>(wasi_http_types_method_response_set_status_code(
        wasi_http_types_borrow_response(made.f0), status));
    exports_wasi_http_handler_result_own_response_error_code_t result{};
    result.val.ok = made.f0;
    exports_wasi_http_handler_handle_return(result);
    return {writer, trailers_writer};
#endif
}

/** A response received from the host client, including its body resource lifetime. */
class incoming_response {
#ifdef WEBCPP_WASI_HTTP_P2
    detail::resource<wasi_http_types_own_incoming_response_t,
                     wasi_http_types_incoming_response_drop_own>
        response_;
#else
    detail::resource<wasi_http_types_own_response_t, wasi_http_types_response_drop_own> response_;
#endif
public:
    /** Constructs an empty response owner. */
    incoming_response() = default;
    /** Transfers response ownership. */
    incoming_response(incoming_response&&) = default;
    /** Replaces response ownership.
    @return A reference to this object. */
    incoming_response& operator=(incoming_response&&) = default;
#ifdef WEBCPP_WASI_HTTP_P2
    /** Takes ownership of a P2 response.
        @param value Incoming host response. */
    explicit incoming_response(wasi_http_types_own_incoming_response_t value) : response_(value) {}

    /** Returns the host response status code.
    @return The HTTP status code. */
    [[nodiscard]] std::uint16_t status() const {
        return wasi_http_types_method_incoming_response_status(
            wasi_http_types_borrow_incoming_response(response_.get()));
    }

    /** Acquires the exposed header fields.
    @return Owning fields exposing the host headers. */
    [[nodiscard]] message_fields headers() const {
        return message_fields(wasi_http_types_method_incoming_response_headers(
            wasi_http_types_borrow_incoming_response(response_.get())));
    }

    /** Consumes the response or request body once.
        @param error Receives a resource-state failure.
    @return The body reader, or an empty reader on failure. */
    body_reader consume(message_error& error) {
        wasi_http_types_own_incoming_body_t body{};
        if (!response_ || !wasi_http_types_method_incoming_response_consume(
                              wasi_http_types_borrow_incoming_response(response_.get()), &body)) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        return body_reader(body);
    }
#else
    /** Takes ownership of a P3 response.
        @param value Incoming host response. */
    explicit incoming_response(wasi_http_types_own_response_t value) : response_(value) {}

    /** Returns the host response status code.
    @return The HTTP status code. */
    [[nodiscard]] std::uint16_t status() const {
        return wasi_http_types_method_response_get_status_code(
            wasi_http_types_borrow_response(response_.get()));
    }

    /** Acquires the exposed header fields.
    @return Owning fields exposing the host headers. */
    [[nodiscard]] message_fields headers() const {
        return message_fields(wasi_http_types_method_response_get_headers(
            wasi_http_types_borrow_response(response_.get())));
    }

    /** Consumes the response or request body once.
        @param error Receives a resource-state failure.
    @return The body reader, or an empty reader on failure. */
    body_reader consume(message_error& error) {
        if (!response_) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        wasi_http_types_future_result_void_error_code_writer_t writer{};
        auto const completion = wasi_http_types_future_result_void_error_code_new(&writer);
        wasi_http_types_tuple2_stream_u8_future_result_option_own_trailers_error_code_t body{};
        wasi_http_types_static_response_consume_body(response_.release(), completion, &body);
        return {body, writer};
    }
#endif
};

/** An outgoing exchange.

    Destroying it cancels a pending response; send the body before receive. */
class outgoing_exchange {
    friend class response_task;
#ifdef WEBCPP_WASI_HTTP_P2
    detail::resource<wasi_http_types_own_future_incoming_response_t,
                     wasi_http_types_future_incoming_response_drop_own>
        future_;
#else
    std::uint32_t subtask_ = 0;
    wasi_http_client_result_own_response_error_code_t result_{};
    bool started_ = false;
    bool returned_ = false;
#endif
public:
    /** Constructs an idle exchange. */
    outgoing_exchange() = default;
    /** Forbids duplicate exchange ownership. */
    outgoing_exchange(const outgoing_exchange&) = delete;
    /** Forbids duplicate exchange ownership. */
    outgoing_exchange& operator=(const outgoing_exchange&) = delete;
#ifdef WEBCPP_WASI_HTTP_P3
    /** Cancels any pending response and releases its resources. */
    ~outgoing_exchange() {
        if (subtask_ != 0) {
            auto status = webcpp_wasi_http_subtask_cancel(subtask_);
            if ((status & 15U) != WEBCPP_WASI_HTTP_SUBTASK_RETURNED &&
                (status & 15U) != WEBCPP_WASI_HTTP_SUBTASK_RETURNED_CANCELLED) {
                status = detail::wait_for(subtask_);
            }
            returned_ = returned_ || (status & 15U) == WEBCPP_WASI_HTTP_SUBTASK_RETURNED;
            webcpp_wasi_http_subtask_drop(subtask_);
        }
        if (returned_) {
            wasi_http_client_result_own_response_error_code_free(&result_);
        }
    }
#endif
    /** Starts an outgoing host request without guest DNS, TCP or TLS.
        @param scheme HTTP or HTTPS.
        @param authority Host and optional port.
        @param method HTTP method name.
        @param target Path and query.
        @param headers Fields transferred to the host.
        @param error Receives any request construction or host failure.
    @return The committed body writer, or an empty writer on failure. */
    body_writer start(std::string_view scheme, std::string_view authority, std::string_view method,
                      std::string_view target, message_fields headers, message_error& error) {
        if (scheme != "http" && scheme != "https") {
            error = {.code = message_errc::invalid_message};
            return {};
        }
        auto path = detail::abi_string(target);
        auto host = detail::abi_string(authority);
        wasi_http_types_method_t verb{};
        verb.tag = WASI_HTTP_TYPES_METHOD_OTHER;
        verb.val.other = detail::abi_string(method);
        wasi_http_types_scheme_t protocol{};
        protocol.tag =
            scheme == "https" ? WASI_HTTP_TYPES_SCHEME_HTTPS : WASI_HTTP_TYPES_SCHEME_HTTP;
#ifdef WEBCPP_WASI_HTTP_P2
        if (future_) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        detail::resource<wasi_http_types_own_outgoing_request_t,
                         wasi_http_types_outgoing_request_drop_own>
            request(wasi_http_types_constructor_outgoing_request(headers.release()));
        const auto borrowed = wasi_http_types_borrow_outgoing_request(request.get());
        if (!wasi_http_types_method_outgoing_request_set_method(borrowed, &verb) ||
            !wasi_http_types_method_outgoing_request_set_path_with_query(borrowed, &path) ||
            !wasi_http_types_method_outgoing_request_set_scheme(borrowed, &protocol) ||
            !wasi_http_types_method_outgoing_request_set_authority(borrowed, &host)) {
            error = {.code = message_errc::invalid_message};
            return {};
        }
        wasi_http_types_own_outgoing_body_t body{};
        if (!wasi_http_types_method_outgoing_request_body(borrowed, &body)) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        body_writer writer(body);
        wasi_http_types_own_future_incoming_response_t future{};
        wasi_http_types_error_code_t failure{};
        if (!wasi_http_outgoing_handler_handle(request.release(), nullptr, &future, &failure)) {
            error = detail::host_failure(failure);
            return {};
        }
        future_.reset(future);
        return writer;
#else
        if (started_) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        wasi_http_types_stream_u8_writer_t writer_handle{};
        auto contents = wasi_http_types_stream_u8_new(&writer_handle);
        wasi_http_types_future_result_option_own_trailers_error_code_writer_t trailers_writer{};
        auto const trailers =
            wasi_http_types_future_result_option_own_trailers_error_code_new(&trailers_writer);
        body_writer writer(writer_handle, trailers_writer);
        wasi_http_types_tuple2_own_request_future_result_void_error_code_t made{};
        wasi_http_types_static_request_new(headers.release(), &contents, trailers, nullptr, &made);
        wasi_http_types_future_result_void_error_code_drop_readable(made.f1);
        detail::resource<wasi_http_types_own_request_t, wasi_http_types_request_drop_own> request(
            made.f0);
        auto const borrowed = wasi_http_types_borrow_request(request.get());
        if (!wasi_http_types_method_request_set_method(borrowed, &verb) ||
            !wasi_http_types_method_request_set_path_with_query(borrowed, &path) ||
            !wasi_http_types_method_request_set_scheme(borrowed, &protocol) ||
            !wasi_http_types_method_request_set_authority(borrowed, &host)) {
            error = {.code = message_errc::invalid_message};
            return {};
        }
        auto const status = wasi_http_client_send(request.release(), &result_);
        started_ = true;
        returned_ = (status & 15U) == WEBCPP_WASI_HTTP_SUBTASK_RETURNED;
        subtask_ = status >> 4U;
        return writer;
#endif
    }

    /** Waits for response headers independently of its body.
        @param options Cancellation and deadline.
        @param error Receives any host or cancellation failure.
    @return The response owner, or an empty owner on failure. */
    incoming_response receive(const transfer_options& options, message_error& error) {
#ifdef WEBCPP_WASI_HTTP_P2
        if (!future_) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        detail::pollable const ready(wasi_http_types_method_future_incoming_response_subscribe(
            wasi_http_types_borrow_future_incoming_response(future_.get())));
        error = detail::wait_ready(ready.get(), options);
        if (error) {
            return {};
        }
        wasi_http_types_result_result_own_incoming_response_error_code_void_t result{};
        if (!wasi_http_types_method_future_incoming_response_get(
                wasi_http_types_borrow_future_incoming_response(future_.get()), &result) ||
            result.is_err) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        if (result.val.ok.is_err) {
            error = detail::host_failure(result.val.ok.val.err);
            return {};
        }
        return incoming_response(result.val.ok.val.ok);
#else
        if (!started_) {
            error = {.code = message_errc::invalid_state};
            return {};
        }
        while (!returned_) {
            const auto status = detail::wait_operation(subtask_, options, error);
            if (error) {
                return {};
            }
            returned_ = status == WEBCPP_WASI_HTTP_SUBTASK_RETURNED;
            if (status == WEBCPP_WASI_HTTP_SUBTASK_RETURNED_CANCELLED) {
                error = {.code = message_errc::cancelled};
                return {};
            }
        }
        if (subtask_ != 0) {
            webcpp_wasi_http_subtask_drop(subtask_);
            subtask_ = 0;
        }
        returned_ = false;
        started_ = false;
        if (result_.is_err) {
            error = detail::host_failure(result_.val.err);
            return {};
        }
        return incoming_response(result_.val.ok);
#endif
    }
};

}  // namespace webcpp::wasi::http

#endif
