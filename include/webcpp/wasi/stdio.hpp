// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_WASI_STDIO_HPP
#define WEBCPP_WASI_STDIO_HPP

#include <webcpp/wasi/config.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#ifdef __wasip3__
#include <wasi/wasip3.h>  // lint-world: wasi-sdk's standard CLI stream bindings
#elif defined(__wasip2__)
#include <wasi/wasip2.h>  // lint-world: wasi-sdk's standard CLI stream bindings
#elif !defined(__MRDOCS__) && !defined(WEBCPP_WASI_HTTP_P2) && !defined(WEBCPP_WASI_HTTP_P3)
#error "webcpp::wasi::stdio_channel requires WASIp2 or WASIp3"
#endif

namespace webcpp::wasi {

/** The observable result of a partial host stream operation. */
enum class stream_status {
    /** Bytes transferred successfully. */ ready,
    /** No progress before the bounded wait ended. */ blocked,
    /** Input ended. */ eof,
    /** The host reported an I/O failure. */ failed,
};

/** Bytes copied by a partial stream operation, independent of any wire protocol. */
struct stream_result {
    /** Bytes copied during this operation. */
    std::size_t count = 0;
    /** Transfer status, distinct from the byte count. */
    stream_status status = stream_status::ready;
};

#if !defined(__wasip2__) && !defined(__wasip3__)
/** Owns standard WASI CLI streams.

    Readiness polling waits are bounded to ten milliseconds. */
class stdio_channel {
public:
    /** Acquires the standard input and output streams. */
    stdio_channel();
    /** Releases all owned CLI stream resources. */
    ~stdio_channel();
    /** Reads a partial byte span.
        @param buffer Destination bytes.
    @return The partial byte count and readiness or failure status. */
    stream_result read(std::span<char> buffer);
    /** Writes a partial byte span.
        @param buffer Source bytes.
    @return The partial byte count and readiness or failure status. */
    [[nodiscard]] stream_result write(std::span<const char> buffer) const;
};
#else
/** Owns standard CLI stream resources directly, independently of libc's blocking descriptors.

    Readiness polling waits are bounded to ten milliseconds. Synchronous host imports
    still follow the host blocking behavior. The caller checks cancellation/deadline
    between operations. No callback or data pointer remains
    borrowed by the host after an operation returns. Diagnostics must use stderr. */
class stdio_channel {
#ifdef __wasip2__
    streams_own_input_stream_t input_ = stdin_get_stdin();
    streams_own_output_stream_t output_ = stdout_get_stdout();

    static void wait(poll_own_pollable_t source) {
        auto const timer = monotonic_clock_subscribe_duration(10000000);
        std::array<poll_borrow_pollable_t, 2> items{
            poll_borrow_pollable(source),
            poll_borrow_pollable(timer),
        };
        poll_list_borrow_pollable_t list{.ptr = items.data(), .len = items.size()};
        wasip2_list_u32_t ready{};
        poll_poll(&list, &ready);
        wasip2_list_u32_free(&ready);
        poll_pollable_drop_own(timer);
        poll_pollable_drop_own(source);
    }
#else
    stdin_tuple2_stream_u8_future_result_void_error_code_t input_{};
    stdin_stream_u8_writer_t output_{};
    stdout_future_result_void_error_code_t output_result_{};
    bool eof_ = false;
    bool completed_ = false;
    bool failed_ = false;

    static std::uint32_t await(std::uint32_t handle) {
        const auto set = wasip3_waitable_set_new();
        wasip3_waitable_join(handle, set);
        wasip3_event_t event{};
        wasip3_waitable_set_wait(set, &event);
        wasip3_waitable_join(handle, 0);
        wasip3_waitable_set_drop(set);
        return event.code;
    }

    static std::uint32_t cancel(std::uint32_t handle, bool writing, bool future) {
        if (future) {
            return stdin_future_result_void_error_code_cancel_read(handle);
        }
        return writing ? stdin_stream_u8_cancel_write(handle) : stdin_stream_u8_cancel_read(handle);
    }

    static std::uint32_t wait(std::uint32_t handle, bool writing, bool future = false) {
        auto const timer_status = monotonic_clock_wait_until(monotonic_clock_now() + 10000000);
        auto const timer = timer_status >> 4U;
        if (timer == 0) {
            const auto cancelled = cancel(handle, writing, future);
            return cancelled == WASIP3_WAITABLE_STATUS_BLOCKED ? await(handle) : cancelled;
        }
        const auto set = wasip3_waitable_set_new();
        wasip3_waitable_join(handle, set);
        wasip3_waitable_join(timer, set);
        wasip3_event_t event{};
        wasip3_waitable_set_wait(set, &event);
        wasip3_waitable_join(handle, 0);
        wasip3_waitable_join(timer, 0);
        wasip3_waitable_set_drop(set);
        if (event.waitable != timer) {
            auto const cancelled = wasip3_subtask_cancel(timer);
            if ((cancelled & 15U) != WASIP3_SUBTASK_RETURNED &&
                (cancelled & 15U) != WASIP3_SUBTASK_RETURNED_CANCELLED) {
                await(timer);
            }
        }
        wasip3_subtask_drop(timer);
        if (event.waitable == handle) {
            return event.code;
        }
        const auto cancelled = cancel(handle, writing, future);
        return cancelled == WASIP3_WAITABLE_STATUS_BLOCKED ? await(handle) : cancelled;
    }

    stream_result finish_input() {
        if (!completed_) {
            stdin_result_void_error_code_t result{};
            auto status = stdin_future_result_void_error_code_read(input_.f1, &result);
            if (status == WASIP3_WAITABLE_STATUS_BLOCKED) {
                status = wait(input_.f1, false, true);
            }
            if ((status & 15U) == WASIP3_WAITABLE_CANCELLED) {
                return {.status = stream_status::blocked};
            }
            completed_ = true;
            failed_ = (status & 15U) != WASIP3_WAITABLE_COMPLETED || result.is_err;
            if ((status & 15U) == WASIP3_WAITABLE_COMPLETED) {
                stdin_result_void_error_code_free(&result);
            }
        }
        return {.status = failed_ ? stream_status::failed : stream_status::eof};
    }

#endif
public:
    /** Acquires the standard input and output host streams. */
    stdio_channel() {
#ifdef __wasip3__
        stdin_read_via_stream(&input_);
        const auto reader = stdin_stream_u8_new(&output_);
        output_result_ = stdout_write_via_stream(reader);
#endif
    }

    /** Forbids duplicated resource ownership. */
    stdio_channel(const stdio_channel&) = delete;
    /** Forbids duplicated resource ownership. */
    stdio_channel& operator=(const stdio_channel&) = delete;

    /** Releases all stream resources. */
    ~stdio_channel() {
#ifdef __wasip2__
        streams_input_stream_drop_own(input_);
        streams_output_stream_drop_own(output_);
#else
        stdin_stream_u8_drop_readable(input_.f0);
        stdin_future_result_void_error_code_drop_readable(input_.f1);
        stdin_stream_u8_drop_writable(output_);
        stdout_future_result_void_error_code_drop_readable(output_result_);
#endif
    }

    /** Reads one bounded chunk with explicit EOF.
        @param buffer Destination bytes.
    @return The partial byte count and readiness or failure status. */
    stream_result read(std::span<char> buffer) {
        if (buffer.empty()) {
            return {};
        }
#ifdef __wasip2__
        wasip2_list_u8_t bytes{};
        streams_stream_error_t error{};
        if (!streams_method_input_stream_read(streams_borrow_input_stream(input_), buffer.size(),
                                              &bytes, &error)) {
            const auto status = error.tag == STREAMS_STREAM_ERROR_CLOSED ? stream_status::eof
                                                                         : stream_status::failed;
            streams_stream_error_free(&error);
            return {.count = 0, .status = status};
        }
        const auto count = bytes.len;
        std::copy_n(reinterpret_cast<const char*>(bytes.ptr), count, buffer.data());
        wasip2_list_u8_free(&bytes);
        if (count == 0) {
            wait(streams_method_input_stream_subscribe(streams_borrow_input_stream(input_)));
            return {.count = 0, .status = stream_status::blocked};
        }
        return {.count = count};
#else
        if (eof_) {
            return finish_input();
        }
        auto status = stdin_stream_u8_read(
            input_.f0, reinterpret_cast<std::uint8_t*>(buffer.data()), buffer.size());
        if (status == WASIP3_WAITABLE_STATUS_BLOCKED) {
            status = wait(input_.f0, false);
        }
        const auto count = status >> 4U;
        if ((status & 15U) == WASIP3_WAITABLE_DROPPED) {
            eof_ = true;
        }
        if (count != 0) {
            return {.count = count};
        }
        return eof_ ? finish_input() : stream_result{.status = stream_status::blocked};
#endif
    }

    /** Writes one bounded chunk, reporting back-pressure explicitly.
        @param buffer Source bytes.
    @return The partial byte count and readiness or failure status. */
    [[nodiscard]] stream_result write(std::span<const char> buffer) const {
        if (buffer.empty()) {
            return {};
        }
#ifdef __wasip2__
        std::uint64_t available = 0;
        streams_stream_error_t error{};
        if (!streams_method_output_stream_check_write(streams_borrow_output_stream(output_),
                                                      &available, &error)) {
            streams_stream_error_free(&error);
            return {
                .count = 0,
                .status = stream_status::failed,
            };
        }
        if (available == 0) {
            wait(streams_method_output_stream_subscribe(streams_borrow_output_stream(output_)));
            return {.count = 0, .status = stream_status::blocked};
        }
        const auto count =
            std::min<std::size_t>({buffer.size(), static_cast<std::size_t>(available), 4096});
        wasip2_list_u8_t bytes{
            .ptr = reinterpret_cast<std::uint8_t*>(const_cast<char*>(buffer.data())),
            .len = count,
        };
        if (!streams_method_output_stream_write(streams_borrow_output_stream(output_), &bytes,
                                                &error)) {
            streams_stream_error_free(&error);
            return {
                .count = 0,
                .status = stream_status::failed,
            };
        }
        return {.count = count};
#else
        auto status = stdin_stream_u8_write(
            output_, reinterpret_cast<const std::uint8_t*>(buffer.data()), buffer.size());
        if (status == WASIP3_WAITABLE_STATUS_BLOCKED) {
            status = wait(output_, true);
        }
        const auto count = status >> 4U;
        if ((status & 15U) == WASIP3_WAITABLE_DROPPED) {
            return {
                .count = count,
                .status = stream_status::failed,
            };
        }
        return {
            .count = count,
            .status = count == 0 ? stream_status::blocked : stream_status::ready,
        };
#endif
    }
};
#endif  // Documentation declarations or WASI implementation.
}  // namespace webcpp::wasi

#endif
