// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_WASI_HTTP_POLL_HPP
#define WEBCPP_WASI_HTTP_POLL_HPP
#include <webcpp/wasi/http/host.hpp>

namespace webcpp::wasi::http {
namespace detail {
#ifdef WEBCPP_WASI_HTTP_P3
/** Checks one pending operation without waiting for host readiness. */
inline std::uint32_t poll_operation(std::uint32_t handle) {
    auto const set = webcpp_wasi_http_waitable_set_new();
    webcpp_wasi_http_waitable_join(handle, set);
    webcpp_wasi_http_event_t event{};
    webcpp_wasi_http_waitable_set_poll(set, &event);
    webcpp_wasi_http_waitable_join(handle, 0);
    webcpp_wasi_http_waitable_set_drop(set);
    return event.event == WEBCPP_WASI_HTTP_EVENT_NONE ? WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED
                                                      : event.code;
}
#endif
/** Yields the current component task for one millisecond between readiness scans. */
inline void yield_polling_task() {
#ifdef WEBCPP_WASI_HTTP_P2
    pollable const timer(wasi_clocks_monotonic_clock_subscribe_duration(1000000));
    wasi_io_poll_method_pollable_block(wasi_io_poll_borrow_pollable(timer.get()));
#else
    auto status =
        wasi_clocks_monotonic_clock_wait_until(wasi_clocks_monotonic_clock_now() + 1000000);
    auto const task = status >> 4U;
    if (task != 0u) {
        while ((status & 15U) != WEBCPP_WASI_HTTP_SUBTASK_RETURNED &&
               (status & 15U) != WEBCPP_WASI_HTTP_SUBTASK_RETURNED_CANCELLED) {
            status = wait_for(task);
        }
        webcpp_wasi_http_subtask_drop(task);
    }
#endif
}
}  // namespace detail

/** Owns a nonblocking body read and its pending completion resources.

    A pending read borrows the same destination span until a later poll completes.
    Destroying this object cancels pending host access before releasing resources. */
class read_task {
    body_reader body_;
#ifdef WEBCPP_WASI_HTTP_P2
    detail::resource<wasi_http_types_own_future_trailers_t,
                     wasi_http_types_future_trailers_drop_own>
        trailers_;
    detail::pollable ready_;
#else
    bool reading_ = false;
    bool finishing_ = false;
    bool completing_ = false;
    bool result_ready_ = false;
    wasi_http_types_result_option_own_trailers_error_code_t result_{};
    wasi_http_types_result_void_error_code_t completed_{};
#endif
    bool finished_ = false;

public:
    /** Takes ownership of an incoming body.
        @param body Incoming body owner. */
    explicit read_task(body_reader body) : body_(std::move(body)) {}

    /** Forbids moving a task with host-borrowed result storage. */
    read_task(read_task&&) = delete;

    /** Cancels pending host operations before releasing their borrowed memory. */
    ~read_task() {
#ifdef WEBCPP_WASI_HTTP_P3
        if (reading_) {
            auto const status = wasi_http_types_stream_u8_cancel_read(body_.stream_.get());
            if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                detail::wait_for(body_.stream_.get());
            }
        }
        if (finishing_) {
            auto status = wasi_http_types_future_result_option_own_trailers_error_code_cancel_read(
                body_.trailers_.get());
            if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                status = detail::wait_for(body_.trailers_.get());
            }
            result_ready_ = (status & 15U) == WEBCPP_WASI_HTTP_WAITABLE_COMPLETED;
        }
        if (completing_) {
            auto status =
                wasi_http_types_future_result_void_error_code_cancel_write(body_.completion_.get());
            if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                status = detail::wait_for(body_.completion_.get());
            }
            if ((status & 15U) == WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
                body_.completion_.reset();
            }
        }
        if (result_ready_) {
            wasi_http_types_result_option_own_trailers_error_code_free(&result_);
        }
#endif
    }

    /** Reports whether body content ended; finish still resolves trailers.
        @return Whether input reached EOF. */
    [[nodiscard]] bool eof() const noexcept { return body_.eof_; }

    /** Polls one partial read without blocking for data.
        @param buffer Stable destination while a read is pending.
        @param count Receives the completed byte count.
        @param options Incoming limit, cancellation and deadline.
        @param error Receives any completed failure.
        @return False while pending, true when bytes, EOF or failure are available. */
    bool read(std::span<char> buffer, std::size_t& count, const transfer_options& options,
              message_error& error) {
        count = 0;
        error = options.check();
        if (error || eof() || buffer.empty()) {
            return true;
        }
        if (!body_.stream_) {
            error = {.code = message_errc::invalid_state};
            return true;
        }
#ifdef WEBCPP_WASI_HTTP_P2
        webcpp_wasi_http_list_u8_t bytes{};
        wasi_io_streams_stream_error_t failure{};
        if (!wasi_io_streams_method_input_stream_read(
                wasi_io_streams_borrow_input_stream(body_.stream_.get()), buffer.size(), &bytes,
                &failure)) {
            if (failure.tag == WASI_IO_STREAMS_STREAM_ERROR_CLOSED) {
                body_.eof_ = true;
            } else {
                error = {.code = message_errc::closed};
            }
            wasi_io_streams_stream_error_free(&failure);
            return true;
        }
        count = bytes.len;
        std::copy_n(reinterpret_cast<char*>(bytes.ptr), count, buffer.data());
        webcpp_wasi_http_list_u8_free(&bytes);
        if (count == 0u) {
            return false;
        }
#else
        auto const status =
            reading_ ? detail::poll_operation(body_.stream_.get())
                     : wasi_http_types_stream_u8_read(
                           body_.stream_.get(), reinterpret_cast<std::uint8_t*>(buffer.data()),
                           buffer.size());
        reading_ = status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED;
        if (reading_) {
            return false;
        }
        count = status >> 4U;
        body_.eof_ = (status & 15U) == WEBCPP_WASI_HTTP_WAITABLE_DROPPED;
#endif
        if (count > options.body_limit - std::min(body_.received_, options.body_limit)) {
            count = 0;
            error = {.code = message_errc::body_limit};
        } else {
            body_.received_ += count;
        }
        return true;
    }

    /** Polls trailers and consumption completion after EOF.
        @tparam Visitor Callable accepting trailer name and value views.
        @param visitor Callback for complete trailer fields.
        @param options Cancellation and deadline.
        @param error Receives any completed failure.
        @return False while pending, true on completion or failure. */
    template <class Visitor>
    bool finish(Visitor&& visitor, const transfer_options& options, message_error& error) {
        error = options.check();
        if (error) {
            return true;
        }
        if (!eof() || finished_) {
            error = {.code = message_errc::invalid_state};
            return true;
        }
        body_.stream_.reset();
#ifdef WEBCPP_WASI_HTTP_P2
        if (!trailers_) {
            trailers_.reset(wasi_http_types_static_incoming_body_finish(body_.body_.release()));
        }
        if (!ready_) {
            ready_.reset(wasi_http_types_method_future_trailers_subscribe(
                wasi_http_types_borrow_future_trailers(trailers_.get())));
        }
        if (!wasi_io_poll_method_pollable_ready(wasi_io_poll_borrow_pollable(ready_.get()))) {
            return false;
        }
        wasi_http_types_result_result_option_own_trailers_error_code_void_t outer{};
        if (!wasi_http_types_method_future_trailers_get(
                wasi_http_types_borrow_future_trailers(trailers_.get()), &outer)) {
            return false;
        }
        ready_.reset();
        trailers_.reset();
        if (outer.is_err) {
            error = {.code = message_errc::invalid_state};
            return true;
        }
        auto result = outer.val.ok;
#else
        if (!result_ready_) {
            auto const status =
                finishing_ ? detail::poll_operation(body_.trailers_.get())
                           : wasi_http_types_future_result_option_own_trailers_error_code_read(
                                 body_.trailers_.get(), &result_);
            finishing_ = status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED;
            if (finishing_) {
                return false;
            }
            body_.trailers_.reset();
            if ((status & 15U) != WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
                error = {.code = message_errc::closed};
                return true;
            }
            result_ready_ = true;
        }
        auto const status = completing_ ? detail::poll_operation(body_.completion_.get())
                                        : wasi_http_types_future_result_void_error_code_write(
                                              body_.completion_.get(), &completed_);
        completing_ = status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED;
        if (completing_) {
            return false;
        }
        body_.completion_.reset();
        auto result = result_;
        result_ready_ = false;
#endif
        finished_ = true;
        if (result.is_err) {
            error = detail::host_failure(result.val.err);
        } else if (result.val.ok.is_some) {
            message_fields const fields(result.val.ok.val);
            fields.for_each(std::forward<Visitor>(visitor));
        }
        return true;
    }
};

/** Owns a nonblocking body write and stable pending trailer storage. */
class write_task {
    body_writer body_;
#ifdef WEBCPP_WASI_HTTP_P3
    bool writing_ = false;
    bool finishing_ = false;
    wasi_http_types_result_option_own_trailers_error_code_t result_{};
#endif
public:
    /** Takes ownership of the only outgoing body.
        @param body Outgoing body owner. */
    explicit write_task(body_writer body) : body_(std::move(body)) {}

    /** Forbids moving a task with host-borrowed result storage. */
    write_task(write_task&&) = delete;

    /** Cancels pending writes before releasing borrowed payload storage. */
    ~write_task() {
#ifdef WEBCPP_WASI_HTTP_P3
        if (writing_) {
            auto const status = wasi_http_types_stream_u8_cancel_write(body_.stream_.get());
            if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                detail::wait_for(body_.stream_.get());
            }
        }
        if (finishing_) {
            auto status = wasi_http_types_future_result_option_own_trailers_error_code_cancel_write(
                body_.trailers_.get());
            if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                status = detail::wait_for(body_.trailers_.get());
            }
            if ((status & 15U) == WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
                body_.trailers_.reset();
            } else {
                wasi_http_types_result_option_own_trailers_error_code_free(&result_);
            }
        }
#endif
    }

    /** Polls a bounded partial write without waiting for readiness.
        @param bytes Stable source until the pending write completes.
        @param count Receives the completed byte count.
        @param options Cancellation and deadline.
        @param error Receives any completed failure.
        @return False while pending, true when bytes or failure are available. */
    bool write(std::string_view bytes, std::size_t& count, const transfer_options& options,
               message_error& error) {
        count = 0;
        error = options.check();
        if (error || bytes.empty()) {
            return true;
        }
        if (!body_.stream_) {
            error = {.code = message_errc::invalid_state};
            return true;
        }
#ifdef WEBCPP_WASI_HTTP_P2
        std::uint64_t available = 0;
        wasi_io_streams_stream_error_t failure{};
        if (!wasi_io_streams_method_output_stream_check_write(
                wasi_io_streams_borrow_output_stream(body_.stream_.get()), &available, &failure)) {
            wasi_io_streams_stream_error_free(&failure);
            error = {.code = message_errc::closed};
            return true;
        }
        if (available == 0u) {
            return false;
        }
        count = std::min<std::size_t>({bytes.size(), static_cast<std::size_t>(available), 4096});
        webcpp_wasi_http_list_u8_t buffer{
            .ptr = reinterpret_cast<std::uint8_t*>(const_cast<char*>(bytes.data())),
            .len = count,
        };
        if (!wasi_io_streams_method_output_stream_write(
                wasi_io_streams_borrow_output_stream(body_.stream_.get()), &buffer, &failure)) {
            wasi_io_streams_stream_error_free(&failure);
            count = 0;
            error = {.code = message_errc::closed};
        }
#else
        auto const status =
            writing_ ? detail::poll_operation(body_.stream_.get())
                     : wasi_http_types_stream_u8_write(
                           body_.stream_.get(), reinterpret_cast<const std::uint8_t*>(bytes.data()),
                           std::min<std::size_t>(bytes.size(), 4096));
        writing_ = status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED;
        if (writing_) {
            return false;
        }
        count = status >> 4U;
        if ((status & 15U) != WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
            error = {.code = message_errc::closed};
        }
#endif
        return true;
    }

    /** Polls the final trailer write without committing another response.
        @param trailers Optional fields consumed when finishing begins.
        @param error Receives any completed failure.
        @return False while pending, true on completion or failure. */
    bool finish(message_fields* trailers, message_error& error) {
#ifdef WEBCPP_WASI_HTTP_P2
        error = body_.finish(trailers);
        return true;
#else
        if (!body_.trailers_) {
            error = {.code = message_errc::invalid_state};
            return true;
        }
        if (!finishing_) {
            body_.stream_.reset();
            result_.val.ok.is_some = trailers != nullptr;
            if (trailers != nullptr) {
                result_.val.ok.val = trailers->release();
            }
        }
        auto const status =
            finishing_ ? detail::poll_operation(body_.trailers_.get())
                       : wasi_http_types_future_result_option_own_trailers_error_code_write(
                             body_.trailers_.get(), &result_);
        finishing_ = status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED;
        if (finishing_) {
            return false;
        }
        body_.trailers_.reset();
        if ((status & 15U) != WEBCPP_WASI_HTTP_WAITABLE_COMPLETED) {
            wasi_http_types_result_option_own_trailers_error_code_free(&result_);
            error = {.code = message_errc::closed};
        }
        return true;
#endif
    }
};

/** Polls an outgoing exchange's response without waiting for network readiness. */
class response_task {
    outgoing_exchange& exchange_;
#ifdef WEBCPP_WASI_HTTP_P2
    detail::pollable ready_;
#endif

public:
    /** Borrows a started exchange until response completion.
        @param exchange Started outgoing host exchange. */
    explicit response_task(outgoing_exchange& exchange) : exchange_(exchange) {}

    /** Polls response headers once.
        @param response Receives ownership of the completed response.
        @param options Cancellation and deadline.
        @param error Receives any completed failure.
        @return False while pending, true on response or failure. */
    bool poll(incoming_response& response, const transfer_options& options, message_error& error) {
        error = options.check();
        if (error) {
            return true;
        }
#ifdef WEBCPP_WASI_HTTP_P2
        if (!exchange_.future_) {
            error = {.code = message_errc::invalid_state};
            return true;
        }
        if (!ready_) {
            ready_.reset(wasi_http_types_method_future_incoming_response_subscribe(
                wasi_http_types_borrow_future_incoming_response(exchange_.future_.get())));
        }
        if (!wasi_io_poll_method_pollable_ready(wasi_io_poll_borrow_pollable(ready_.get()))) {
            return false;
        }
        wasi_http_types_result_result_own_incoming_response_error_code_void_t result{};
        if (!wasi_http_types_method_future_incoming_response_get(
                wasi_http_types_borrow_future_incoming_response(exchange_.future_.get()),
                &result)) {
            return false;
        }
        ready_.reset();
        exchange_.future_.reset();
        if (result.is_err) {
            error = {.code = message_errc::invalid_state};
        } else if (result.val.ok.is_err) {
            error = detail::host_failure(result.val.ok.val.err);
        } else {
            response = incoming_response(result.val.ok.val.ok);
        }
#else
        if (!exchange_.started_) {
            error = {.code = message_errc::invalid_state};
            return true;
        }
        if (!exchange_.returned_) {
            auto status = detail::poll_operation(exchange_.subtask_);
            if (status == WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED) {
                return false;
            }
            if (status == WEBCPP_WASI_HTTP_SUBTASK_RETURNED_CANCELLED) {
                error = {.code = message_errc::cancelled};
                return true;
            }
            exchange_.returned_ = status == WEBCPP_WASI_HTTP_SUBTASK_RETURNED;
            if (!exchange_.returned_) {
                return false;
            }
        }
        response = exchange_.receive(options, error);
#endif
        return true;
    }
};
}  // namespace webcpp::wasi::http
#endif
