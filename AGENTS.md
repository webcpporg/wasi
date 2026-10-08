# Working on wasi

webcpp's rules apply here: read the superproject's
[AGENTS.md](../../AGENTS.md)
(<https://github.com/webcpporg/webcpp/blob/main/AGENTS.md>) first. This file
holds only what is specific to wasi.

- **It stands alone.** wasi is webcpp's own, ported from no original. Its
  headers include only the standard library and the bindings wit-bindgen
  writes, and no header, comment or example of it names another webcpp
  library. `build.jam` declares `/webcpp/wasi//wasi`, the headers (with
  Boost's, as every library's target), and `/webcpp/wasi//http`, which adds
  the bindings of the standard world of the program's target and the macro
  of that target.
- **The entry point compiles only for WASI.**
  `<webcpp/wasi/http/entrypoint.hpp>`, and `<webcpp/wasi.hpp>`, which
  includes it, need the bindings of one version and exactly one of
  `WEBCPP_WASI_HTTP_P2` and `WEBCPP_WASI_HTTP_P3`, and stop with `#error`
  under neither or both. `<webcpp/wasi/http/response.hpp>` includes nothing
  of the component ABI and compiles everywhere, natively too.
  `test/Jamfile` compiles each header alone accordingly: the first two on
  wasip2 and wasip3 with `/webcpp/wasi//http`, the response natively as
  well.
- **The standard world.** `wit/http-p2.wit` and `wit/http-p3.wit` export the
  HTTP handler and nothing else. `build.jam` generates their C bindings with
  `webcpp.wit-bindings`, renamed `webcpp_wasi_http`, so that the header's
  names (`webcpp_wasi_http.h`, `webcpp_wasi_http_string_t`,
  `WEBCPP_WASI_HTTP_WAITABLE_STATUS_BLOCKED`, ...) are the same whatever a
  program's world is called. The bindings are generated into the build
  directory, never committed and never edited.
- **One main per program.** `WEBCPP_WASI_HTTP_MAIN_BEGIN` and
  `WEBCPP_WASI_HTTP_MAIN_END` define `webcpp::wasi::http::http_main` and the
  `extern "C"` handler the world exports, so a program writes the pair once,
  and each served test or example is a program of its own. What is not
  public, the helpers the handler calls, is in `webcpp::wasi::http::detail`.
- **wasip3's handler never resumes in its callback.** It is lifted
  asynchronously, waits for each write inside the task and always ends with
  `EXIT`: context slot 0 belongs to wasi-libc on wasip3, and using it trapped.
- **Exceptions.** The headers throw, try and catch nothing. Every program is
  built without exceptions on wasip2 and with them on wasip3, as the
  superproject's Jamroot builds any; wasmtime serves both with no flag
  beyond those of `webcpp.serve`.
- **The tools,** each looked for when a target needs it, and the build stops
  naming it and every place it looked when it is not there: wit-bindgen
  0.62.0 (`-sWIT_BINDGEN=<path>`, else `.local/wit-bindgen/wit-bindgen`,
  else `PATH`); the `wasi:http` WIT of WASI 0.2.12 and 0.3.0, the `wit/deps`
  directories of the Rust crates `wasip2` 1.0.4 and `wasip3` 0.9.0
  (`-sWASI_WIT_P2=<dir>` and `-sWASI_WIT_P3=<dir>`, else
  `.local/wasi-wit/p2` and `.local/wasi-wit/p3`); wasmtime 47.0.3
  (`-sWASMTIME=<path>`, else `PATH`), looked for only when a served test
  runs. A native build needs none of them.
- **Served tests.** A program that answers HTTP is tested with
  `webcpp.serve <source> : <library>/webcpp/wasi//http ;`: built as a
  component on wasip2 and wasip3, served by `wasmtime serve` (`-S cli`, and
  `-S cli,p3 -W component-model-async` on wasip3), sent each line
  `<METHOD> <target>` of `<stem>.requests`, and passed when the transcript of
  its answers equals `<stem>.expected`. The transcript writes each request as
  the curl command that sends it, which a shell runs as written:
  `$ curl -i -X <METHOD> http://localhost:8080<target>`, quoted where a shell
  would read it otherwise, `curl -I` for HEAD, and `--request-target '*'`
  before the bare origin for `OPTIONS *`; it leaves out the headers wasmtime
  adds to every response, `date` and `transfer-encoding`.
  `tools/component/serve.py` in the superproject holds the format.
- **The README's example is a test.** Its `main.cpp` and its answers are
  copies of `example/http_hello.cpp` (the region `tag::main`) and
  `example/http_hello.expected`, which the README opens with
  `<!-- include::... -->` and doc-check compares; a change to either changes
  the README in the same commit. The hand-written commands of its complete
  example and of its section on wasip3 were run as written, and are run again
  when wasi-sdk, wit-bindgen or wasmtime moves.
- **clang-format.** `.clang-format` inherits the superproject's and names
  the two macros of the main as a block's begin and end, so that the main's
  body is indented as a function's.
