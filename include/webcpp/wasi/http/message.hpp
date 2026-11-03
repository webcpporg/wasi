// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_WASI_HTTP_MESSAGE_HPP
#define WEBCPP_WASI_HTTP_MESSAGE_HPP

#include <cstddef>
#include <webcpp/wasi/config.hpp>
#include <webcpp/wasi/http/entrypoint.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <span>
#include <utility>

namespace webcpp::wasi::http {

/** Errors of a host message operation, independent of a C++ exception runtime. */
enum class message_errc : std::uint8_t {
    /** No failure. */
    success = 0,
    /** Invalid HTTP metadata. */
    invalid_message = 1,
    /** A field forbidden by the host. */
    forbidden_header = 2,
    /** The incoming body exceeded its configured limit. */
    body_limit = 3,
    /** The caller cancelled the transfer. */
    cancelled = 4,
    /** The transfer deadline expired. */
    timed_out = 5,
    /** A stream closed before the operation completed. */
    closed = 6,
    /** A pinned wasi:http error-code. */
    host_error = 7,
    /** The resource has already been consumed or finished. */
    invalid_state = 8,
};

/** An operation failure, retaining the pinned wasi:http error-code discriminant. */
struct message_error {
    /** Portable failure classification. */
    message_errc code = message_errc::success;
    /** Host error-code discriminant when code is host_error. */
    std::uint8_t host_code = 0;

    /** Whether this value represents failure.
    @return Whether the object holds a resource or failure, as documented. */
    explicit operator bool() const noexcept { return code != message_errc::success; }
};

/** Limits shared by a transfer.

    Cancellation is checked while waiting and between chunks. */
struct transfer_options {
    /** Maximum incoming payload size in bytes. */
    std::uint64_t body_limit = static_cast<long long>(static_cast<std::uint64_t>(1024) * 1024);
    /** Absolute host monotonic deadline, or the maximum value for none. */
    std::uint64_t deadline = std::numeric_limits<std::uint64_t>::max();
    /** Optional cancellation predicate, called on the component task. */
    bool (*cancelled)(void*) = nullptr;
    /** Opaque state supplied to the cancellation predicate. */
    void* cancellation_context = nullptr;

    /** Sets an absolute monotonic deadline.
        @param nanoseconds Duration from the current host monotonic time. */
    void expires_after(std::uint64_t nanoseconds) {
        const auto now = wasi_clocks_monotonic_clock_now();
        deadline = nanoseconds > std::numeric_limits<std::uint64_t>::max() - now
                       ? std::numeric_limits<std::uint64_t>::max()
                       : now + nanoseconds;
    }

    /** Checks the cancellation source and the absolute deadline.
    @return An empty error value on success, otherwise the failure. */
    [[nodiscard]] message_error check() const {
        if ((cancelled != nullptr) && cancelled(cancellation_context)) {
            return {.code = message_errc::cancelled};
        }
        if (deadline != std::numeric_limits<std::uint64_t>::max() &&
            wasi_clocks_monotonic_clock_now() >= deadline) {
            return {.code = message_errc::timed_out};
        }
        return {};
    }
};

namespace detail {

/** Owns one component resource, including handle zero, with move-only lifetime. */
template <class Handle, auto Drop>
class resource {
    std::optional<Handle> value_;

public:
    /** Constructs an empty owner. */
    resource() = default;

    /** Takes ownership of one handle. */
    explicit resource(Handle value) : value_(value) {}

    /** Forbids duplicated resource ownership. */
    resource(const resource&) = delete;
    /** Forbids duplicated resource ownership. */
    resource& operator=(const resource&) = delete;

    /** Transfers resource ownership. */
    resource(resource&& other) noexcept : value_(std::exchange(other.value_, {})) {}

    /** Replaces ownership with another resource.
    @return A reference to this object. */
    resource& operator=(resource&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, {});
        }
        return *this;
    }

    /** Drops a remaining handle. */
    ~resource() { reset(); }

    /** Whether this object owns a handle.
    @return Whether the object holds a resource or failure, as documented. */
    explicit operator bool() const noexcept { return value_.has_value(); }

    /** Borrows the owned handle.
    @return The borrowed resource handle. */
    [[nodiscard]] Handle get() const {
        assert(value_.has_value());
        return *value_;
    }

    /** Transfers the handle to the caller.
    @return The resource handle, whose ownership passes to the caller. */
    Handle release() {
        assert(value_.has_value());
        auto value = *value_;
        value_.reset();
        return value;
    }

    /** Releases the owned handle. */
    void reset() {
        if (value_) {
            Drop(*value_);
            value_.reset();
        }
    }

    /** Replaces the owned handle. */
    void reset(Handle value) {
        reset();
        value_ = value;
    }
};

/** Borrows an ABI string without allocating. */
inline webcpp_wasi_http_string_t abi_string(std::string_view value) {
    return {
        .ptr = reinterpret_cast<std::uint8_t*>(const_cast<char*>(value.data())),
        .len = value.size(),
    };
}

/** Captures a host error discriminant and frees its ABI payload. */
inline message_error host_failure(wasi_http_types_error_code_t& error) {
    const auto code = error.tag;
    wasi_http_types_error_code_free(&error);
    return {.code = message_errc::host_error, .host_code = code};
}

#ifdef WEBCPP_WASI_HTTP_P2
/** Scoped ownership of a P2 pollable. */
using pollable = resource<wasi_io_poll_own_pollable_t, wasi_io_poll_pollable_drop_own>;

/** Waits for P2 readiness while checking cancellation and deadline. */
inline message_error wait_ready(wasi_io_poll_own_pollable_t source,
                                const transfer_options& options) {
    while (!wasi_io_poll_method_pollable_ready(wasi_io_poll_borrow_pollable(source))) {
        if (auto error = options.check()) {
            return error;
        }
        const auto now = wasi_clocks_monotonic_clock_now();
        const auto until = std::min(options.deadline, now + 10000000);
        pollable const timer(wasi_clocks_monotonic_clock_subscribe_instant(until));
        std::array<wasi_io_poll_borrow_pollable_t, 2> values{
            wasi_io_poll_borrow_pollable(source),
            wasi_io_poll_borrow_pollable(timer.get()),
        };
        wasi_io_poll_list_borrow_pollable_t pending{.ptr = values.data(), .len = values.size()};
        webcpp_wasi_http_list_u32_t ready{};
        wasi_io_poll_poll(&pending, &ready);
        webcpp_wasi_http_list_u32_free(&ready);
    }
    return options.check();
}
#else
/** Waits without using wasi-libc's task context slots. */
inline std::uint32_t wait_operation(std::uint32_t waitable, const transfer_options& options,
                                    message_error& error) {
    const auto set = webcpp_wasi_http_waitable_set_new();
    webcpp_wasi_http_waitable_join(waitable, set);
    std::uint32_t answer = 0;
    for (;;) {
        error = options.check();
        if (error) {
            break;
        }
        const auto now = wasi_clocks_monotonic_clock_now();
        const auto timer_status =
            wasi_clocks_monotonic_clock_wait_until(std::min(options.deadline, now + 10000000));
        const auto timer = timer_status >> 4U;
        if (timer == 0) {
            continue;
        }
        webcpp_wasi_http_waitable_join(timer, set);
        webcpp_wasi_http_event_t event{};
        webcpp_wasi_http_waitable_set_wait(set, &event);
        if (timer != 0) {
            webcpp_wasi_http_waitable_join(timer, 0);
            if (event.waitable != timer) {
                const auto cancelled = webcpp_wasi_http_subtask_cancel(timer);
                if ((cancelled & 15U) != WEBCPP_WASI_HTTP_SUBTASK_RETURNED &&
                    (cancelled & 15U) != WEBCPP_WASI_HTTP_SUBTASK_RETURNED_CANCELLED) {
                    static_cast<void>(wait_for(timer));
                }
            }
            webcpp_wasi_http_subtask_drop(timer);
        }
        if (event.waitable == waitable) {
            answer = event.code;
            break;
        }
    }
    webcpp_wasi_http_waitable_join(waitable, 0);
    webcpp_wasi_http_waitable_set_drop(set);
    return answer;
}
#endif
}  // namespace detail

/** Owning HTTP fields.

    Duplicate values retain their order. */
class message_fields {
    detail::resource<wasi_http_types_own_fields_t, wasi_http_types_fields_drop_own> fields_;

public:
    /** Constructs empty mutable fields in the host. */
    message_fields() : fields_(wasi_http_types_constructor_fields()) {}

    /** Takes ownership of host fields.
        @param fields The host resource to consume. */
    explicit message_fields(wasi_http_types_own_fields_t fields) : fields_(fields) {}

    /** Transfers the field resource. */
    message_fields(message_fields&&) = default;
    /** Replaces the field resource.
    @return A reference to this object. */
    message_fields& operator=(message_fields&&) = default;

    /** Transfers the resource to the component ABI.
    @return The resource handle, whose ownership passes to the caller. */
    auto release() { return fields_.release(); }

    /** Appends a field without silently dropping forbidden values.
        @param name Field name.
        @param value Field value.
    @return An empty error value on success, otherwise the failure. */
    message_error append(std::string_view name, std::string_view value) {
        auto key = detail::abi_string(name);
        wasi_http_types_field_value_t bytes{
            .ptr = reinterpret_cast<std::uint8_t*>(const_cast<char*>(value.data())),
            .len = value.size(),
        };
        wasi_http_types_header_error_t error{};
        if (wasi_http_types_method_fields_append(wasi_http_types_borrow_fields(fields_.get()), &key,
                                                 &bytes, &error)) {
            return {};
        }
        const auto code = error.tag;
        wasi_http_types_header_error_free(&error);
        return {
            .code = code == WASI_HTTP_TYPES_HEADER_ERROR_FORBIDDEN ? message_errc::forbidden_header
                                                                   : message_errc::invalid_message,
        };
    }

    /** Visits borrowed field strings, valid only during the call.
        @tparam Visitor Callable accepting name and value string views.
        @param visitor Callback invoked for each field. */
    template <class Visitor>
    void for_each(Visitor&& visitor) const {
        wasi_http_types_list_tuple2_field_name_field_value_t list{};
#ifdef WEBCPP_WASI_HTTP_P2
        wasi_http_types_method_fields_entries(wasi_http_types_borrow_fields(fields_.get()), &list);
#else
        wasi_http_types_method_fields_copy_all(wasi_http_types_borrow_fields(fields_.get()), &list);
#endif
        struct cleanup {
            wasi_http_types_list_tuple2_field_name_field_value_t* list;

            ~cleanup() { wasi_http_types_list_tuple2_field_name_field_value_free(list); }
        } const guard{&list};

        for (std::size_t i = 0; i != list.len; ++i) {
            visitor(detail::view_of(list.ptr[i].f0),
                    std::string_view(reinterpret_cast<const char*>(list.ptr[i].f1.ptr),
                                     list.ptr[i].f1.len));
        }
    }
};

/** A host body reader, with bounded partial I/O and explicit trailers completion. */
class body_reader {
    friend class read_task;
#ifdef WEBCPP_WASI_HTTP_P2
    detail::resource<wasi_http_types_own_incoming_body_t, wasi_http_types_incoming_body_drop_own>
        body_;
    detail::resource<wasi_io_streams_own_input_stream_t, wasi_io_streams_input_stream_drop_own>
        stream_;
#else
    detail::resource<wasi_http_types_stream_u8_t, wasi_http_types_stream_u8_drop_readable> stream_;
    detail::resource<wasi_http_types_future_result_option_own_trailers_error_code_t,
                     wasi_http_types_future_result_option_own_trailers_error_code_drop_readable>
        trailers_;
    detail::resource<wasi_http_types_future_result_void_error_code_writer_t,
                     wasi_http_types_future_result_void_error_code_drop_writable>
        completion_;
#endif
    std::uint64_t received_ = 0;
    bool eof_ = false;

public:
    /** Constructs an empty reader. */
    body_reader() = default;

    /** Fails and releases any unfinished body. */
    ~body_reader() {
#ifdef WEBCPP_WASI_HTTP_P3
        // A P3 future writer must publish a value even when the body was abandoned.
        if (completion_) {
            stream_.reset();
            trailers_.reset();
            wasi_http_types_result_void_error_code_t result{};
            result.is_err = true;
            result.val.err.tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
            const auto status =
                wasi_http_types_future_result_void_error_code_write(completion_.get(), &result);
            if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                detail::wait_for(completion_.get());
            }
            completion_.reset();
        }
#endif
    }

    /** Transfers body ownership. */
    body_reader(body_reader&&) = default;
    /** Forbids replacing a potentially unfinished body. */
    body_reader& operator=(body_reader&&) = delete;
#ifdef WEBCPP_WASI_HTTP_P2
    /** Takes ownership of a P2 incoming body.
        @param body Incoming body resource. */
    explicit body_reader(wasi_http_types_own_incoming_body_t body) : body_(body) {
        wasi_io_streams_own_input_stream_t stream{};
        if (wasi_http_types_method_incoming_body_stream(wasi_http_types_borrow_incoming_body(body),
                                                        &stream)) {
            stream_.reset(stream);
        }
    }
#else
    /** Takes ownership of P3 body and completion resources.
        @param body Content reader and trailer future.
        @param completion Completion writer resolved after consuming the body. */
    body_reader(
        wasi_http_types_tuple2_stream_u8_future_result_option_own_trailers_error_code_t body,
        wasi_http_types_future_result_void_error_code_writer_t completion)
        : stream_(body.f0), trailers_(body.f1), completion_(completion) {}
#endif
    /** Whether the content stream has reached its end; finish still checks body failure.
    @return Whether the body content stream has ended. */
    [[nodiscard]] bool eof() const noexcept { return eof_; }

    /** Reads at most the supplied span.
        @param output Destination bytes.
        @param options Body limit, cancellation and deadline.
        @param error Receives any failure.
    @return The number of bytes transferred before completion or failure. */
    std::size_t read_some(std::span<char> output, const transfer_options& options,
                          message_error& error) {
        error = options.check();
        if (error || eof_ || output.empty()) {
            return 0;
        }
        if (!stream_) {
            error = {.code = message_errc::invalid_state};
            return 0;
        }
        std::size_t count = 0;
#ifdef WEBCPP_WASI_HTTP_P2
        for (;;) {
            webcpp_wasi_http_list_u8_t bytes{};
            wasi_io_streams_stream_error_t stream_error{};
            if (!wasi_io_streams_method_input_stream_read(
                    wasi_io_streams_borrow_input_stream(stream_.get()), output.size(), &bytes,
                    &stream_error)) {
                if (stream_error.tag == WASI_IO_STREAMS_STREAM_ERROR_CLOSED) {
                    eof_ = true;
                } else {
                    error = {.code = message_errc::closed};
                }
                wasi_io_streams_stream_error_free(&stream_error);
                break;
            }
            count = bytes.len;
            std::copy_n(reinterpret_cast<const char*>(bytes.ptr), count, output.data());
            webcpp_wasi_http_list_u8_free(&bytes);
            if (count != 0) {
                break;
            }
            detail::pollable const ready(wasi_io_streams_method_input_stream_subscribe(
                wasi_io_streams_borrow_input_stream(stream_.get())));
            error = detail::wait_ready(ready.get(), options);
            if (error) {
                break;
            }
        }
#else
        auto status = wasi_http_types_stream_u8_read(
            stream_.get(), reinterpret_cast<std::uint8_t*>(output.data()), output.size());
        if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
            status = detail::wait_operation(stream_.get(), options, error);
            if (error) {
                status = wasi_http_types_stream_u8_cancel_read(stream_.get());
                if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                    status = detail::wait_for(stream_.get());
                }
            }
        }
        count = status >> 4U;
        eof_ = (status & 15U) == WEBCPP_WASI_HTTP_WAITABLE_DROPPED;
#endif
        if (count > options.body_limit - std::min(received_, options.body_limit)) {
            error = {.code = message_errc::body_limit};
            return 0;
        }
        received_ += count;
        return count;
    }

    /** Resolves body completion, then visits trailers.

        Call after EOF.
        @tparam Visitor Callable accepting trailer name and value views.
        @param visitor Callback for the received trailers.
        @param options Cancellation and deadline while waiting for trailers.
    @return An empty error value on success, otherwise the failure. */
    template <class Visitor>
    message_error finish(Visitor&& visitor, const transfer_options& options) {
        if (!eof_) {
            return {.code = message_errc::invalid_state};
        }
        stream_.reset();
        wasi_http_types_result_option_own_trailers_error_code_t result{};
#ifdef WEBCPP_WASI_HTTP_P2
        if (!body_) {
            return {.code = message_errc::invalid_state};
        }
        detail::resource<wasi_http_types_own_future_trailers_t,
                         wasi_http_types_future_trailers_drop_own> const
            future(wasi_http_types_static_incoming_body_finish(body_.release()));
        detail::pollable const ready(wasi_http_types_method_future_trailers_subscribe(
            wasi_http_types_borrow_future_trailers(future.get())));
        if (auto error = detail::wait_ready(ready.get(), options)) {
            return error;
        }
        wasi_http_types_result_result_option_own_trailers_error_code_void_t outer{};
        if (!wasi_http_types_method_future_trailers_get(
                wasi_http_types_borrow_future_trailers(future.get()), &outer) ||
            outer.is_err) {
            return {.code = message_errc::invalid_state};
        }
        result = outer.val.ok;
#else
        if (!trailers_) {
            return {.code = message_errc::invalid_state};
        }
        message_error error;
        auto status = wasi_http_types_future_result_option_own_trailers_error_code_read(
            trailers_.get(), &result);
        if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
            status = detail::wait_operation(trailers_.get(), options, error);
            if (error) {
                status = wasi_http_types_future_result_option_own_trailers_error_code_cancel_read(
                    trailers_.get());
                if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                    detail::wait_for(trailers_.get());
                }
                return error;
            }
        }
        trailers_.reset();
        if ((status & 15U) != WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
            return {.code = message_errc::closed};
        }
        const wasi_http_types_result_void_error_code_t completed{};
        auto const written =
            wasi_http_types_future_result_void_error_code_write(completion_.get(), &completed);
        if (written == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
            detail::wait_for(completion_.get());
        }
        completion_.reset();
#endif
        if (result.is_err) {
            return detail::host_failure(result.val.err);
        }
        if (result.val.ok.is_some) {
            message_fields const trailers(result.val.ok.val);
            trailers.for_each(std::forward<Visitor>(visitor));
        }
        return {};
    }
};

/** A committed body writer.

    Destruction without finish fails the body, never commits again. */
class body_writer {
    friend class write_task;
#ifdef WEBCPP_WASI_HTTP_P2
    detail::resource<wasi_http_types_own_outgoing_body_t, wasi_http_types_outgoing_body_drop_own>
        body_;
    detail::resource<wasi_io_streams_own_output_stream_t, wasi_io_streams_output_stream_drop_own>
        stream_;
#else
    detail::resource<wasi_http_types_stream_u8_writer_t, wasi_http_types_stream_u8_drop_writable>
        stream_;
    detail::resource<wasi_http_types_future_result_option_own_trailers_error_code_writer_t,
                     wasi_http_types_future_result_option_own_trailers_error_code_drop_writable>
        trailers_;
#endif
public:
    /** Constructs an empty writer. */
    body_writer() = default;

    /** Fails and releases any unfinished outgoing body. */
    ~body_writer() {
#ifdef WEBCPP_WASI_HTTP_P3
        // A failed body closes its content stream and resolves trailers as an error.
        if (trailers_) {
            stream_.reset();
            wasi_http_types_result_option_own_trailers_error_code_t result{};
            result.is_err = true;
            result.val.err.tag = WASI_HTTP_TYPES_ERROR_CODE_INTERNAL_ERROR;
            auto const status = wasi_http_types_future_result_option_own_trailers_error_code_write(
                trailers_.get(), &result);
            if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                detail::wait_for(trailers_.get());
            }
            trailers_.reset();
        }
#endif
    }

    /** Transfers outgoing body ownership. */
    body_writer(body_writer&&) = default;
    /** Forbids replacing a potentially unfinished body. */
    body_writer& operator=(body_writer&&) = delete;
#ifdef WEBCPP_WASI_HTTP_P2
    /** Takes ownership of a P2 outgoing body.
        @param body The outgoing body resource. */
    explicit body_writer(wasi_http_types_own_outgoing_body_t body) : body_(body) {
        wasi_io_streams_own_output_stream_t stream{};
        if (wasi_http_types_method_outgoing_body_write(wasi_http_types_borrow_outgoing_body(body),
                                                       &stream)) {
            stream_.reset(stream);
        }
    }
#else
    /** Takes ownership of P3 body writers.
        @param stream Content writer.
        @param trailers Completion and trailers writer. */
    body_writer(wasi_http_types_stream_u8_writer_t stream,
                wasi_http_types_future_result_option_own_trailers_error_code_writer_t trailers)
        : stream_(stream), trailers_(trailers) {}
#endif
    /** Writes a partial span, waiting only for host readiness and honouring transfer cancellation.
        @param bytes Source payload bytes.
        @param options Cancellation and deadline.
        @param error Receives any transfer failure.
    @return The number of bytes transferred before completion or failure. */
    std::size_t write_some(std::string_view bytes, const transfer_options& options,
                           message_error& error) {
        error = options.check();
        if (error || bytes.empty()) {
            return 0;
        }
        if (!stream_) {
            error = {.code = message_errc::invalid_state};
            return 0;
        }
#ifdef WEBCPP_WASI_HTTP_P2
        std::uint64_t available = 0;
        wasi_io_streams_stream_error_t failed{};
        while (available == 0) {
            if (!wasi_io_streams_method_output_stream_check_write(
                    wasi_io_streams_borrow_output_stream(stream_.get()), &available, &failed)) {
                wasi_io_streams_stream_error_free(&failed);
                error = {.code = message_errc::closed};
                return 0;
            }
            if (available != 0) {
                break;
            }
            detail::pollable const ready(wasi_io_streams_method_output_stream_subscribe(
                wasi_io_streams_borrow_output_stream(stream_.get())));
            error = detail::wait_ready(ready.get(), options);
            if (error) {
                return 0;
            }
        }
        const auto count =
            std::min<std::size_t>({bytes.size(), static_cast<std::size_t>(available), 4096});
        webcpp_wasi_http_list_u8_t buffer{
            .ptr = reinterpret_cast<std::uint8_t*>(const_cast<char*>(bytes.data())),
            .len = count,
        };
        if (!wasi_io_streams_method_output_stream_write(
                wasi_io_streams_borrow_output_stream(stream_.get()), &buffer, &failed)) {
            wasi_io_streams_stream_error_free(&failed);
            error = {.code = message_errc::closed};
            return 0;
        }
        return count;
#else
        auto status = wasi_http_types_stream_u8_write(
            stream_.get(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
            status = detail::wait_operation(stream_.get(), options, error);
            if (error) {
                status = wasi_http_types_stream_u8_cancel_write(stream_.get());
                if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                    status = detail::wait_for(stream_.get());
                }
            }
        }
        if (!error && (status & 15U) != WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
            error = {.code = message_errc::closed};
        }
        return status >> 4U;
#endif
    }

    /** Writes all supplied bytes, with bounded host buffering.
        @param bytes Source payload.
        @param options Cancellation and deadline.
        @return An empty error value on success, otherwise the failure. */
    message_error write(std::string_view bytes, const transfer_options& options = {}) {
        message_error error;
        while (!bytes.empty()) {
            const auto written = write_some(bytes, options, error);
            if (error) {
                return error;
            }
            bytes.remove_prefix(written);
        }
        return {};
    }

    /** Finishes the only committed body.
        @param trailers Optional fields whose resource is transferred to the host.
    @return An empty error value on success, otherwise the failure. */
    message_error finish(message_fields* trailers = nullptr) {
        stream_.reset();
#ifdef WEBCPP_WASI_HTTP_P2
        if (!body_) {
            return {.code = message_errc::invalid_state};
        }
        wasi_http_types_own_fields_t fields{};
        if (trailers != nullptr) {
            fields = trailers->release();
        }
        wasi_http_types_error_code_t error{};
        if (!wasi_http_types_static_outgoing_body_finish(
                body_.release(), (trailers != nullptr) ? &fields : nullptr, &error)) {
            return detail::host_failure(error);
        }
#else
        if (!trailers_) {
            return {.code = message_errc::invalid_state};
        }
        wasi_http_types_result_option_own_trailers_error_code_t result{};
        result.val.ok.is_some = trailers != nullptr;
        if (trailers != nullptr) {
            result.val.ok.val = trailers->release();
        }
        auto status = wasi_http_types_future_result_option_own_trailers_error_code_write(
            trailers_.get(), &result);
        if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
            status = detail::wait_for(trailers_.get());
        }
        trailers_.reset();
        if ((status & 15U) != WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
            wasi_http_types_result_option_own_trailers_error_code_free(&result);
            return {.code = message_errc::closed};
        }
#endif
        return {};
    }
};

}  // namespace webcpp::wasi::http

#endif
