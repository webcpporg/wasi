# wasi

wasi is a header-only helper for C++20 programs built as WebAssembly
components, one of the libraries of
[webcpp](https://github.com/webcpporg/webcpp). Its first part,
`webcpp::wasi::http`, is an HTTP handler. You write one function, the main,
that turns a request's method and target into a response; wasi exports the
handler `wasmtime serve` (or any host of `wasi:http`) calls, reads the
request, calls your main and writes the answer back. It covers WASI 0.2
(wasip2) and WASI 0.3 (wasip3).

| Header | What it holds |
| --- | --- |
| `<webcpp/wasi.hpp>` | the whole library: it includes the entry point |
| `<webcpp/wasi/http/entrypoint.hpp>` | the handler and the main's macros; it compiles only for wasip2 and wasip3 |
| `<webcpp/wasi/http/response.hpp>` | only the response, for code that needs no ABI; it also compiles natively |

Requirements: C++20, wasi-sdk 34, wit-bindgen's C generator (0.62.0
measured), the `wasi:http` WIT of your target, and a host such as wasmtime
(47.0.3 measured). The headers include only the standard library and the
bindings wit-bindgen writes. The helper needs neither exceptions nor RTTI
and imposes neither: webcpp builds it without exceptions on wasip2 and with
them on wasip3.

## Contents

1. [A complete example](#a-complete-example)
2. [Building with b2](#building-with-b2)
3. [Your main](#your-main)
4. [The response](#the-response)
5. [What your build provides](#what-your-build-provides)
6. [wasip3](#wasip3)
7. [How a request flows](#how-a-request-flows)
8. [Limits](#limits)
9. [Building and testing](#building-and-testing)
10. [Documentation](#documentation)
11. [License](#license)

## A complete example

A component that greets whoever calls it, built by hand with wasi-sdk. These
are the exact files and commands, which webcpp's test `by_hand` runs on every
build, with wasi-sdk 34, wit-bindgen 0.62.0 and wasmtime 47.0.3
([example/by_hand/build.sh](example/by_hand/build.sh)).

`wit/world.wit`, the world your component exports (name it as you like):

<!-- include::example/by_hand/p2/wit/world.wit[tag=world] -->
```wit
package example:hello;

world service {
  export wasi:http/incoming-handler@0.2.12;
}
```

with `wit/deps/` linked to the `wasi:http` 0.2.12 WIT: the `wit/deps`
directory of the Rust crate `wasip2` 1.0.4.

`main.cpp`, which is [example/http_hello.cpp](example/http_hello.cpp):

<!-- include::example/http_hello.cpp[tag=main] -->
```cpp
#include <webcpp/wasi/http/entrypoint.hpp>

WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)
    if (method != "GET") {
        return {.status = 405, .content_type = "text/plain", .body = "only GET\n"};
    }
    return {
        .status = 200,
        .content_type = "text/plain",
        .body = "hello from " + std::string(target) + "\n",
    };
WEBCPP_WASI_HTTP_MAIN_END()
```

Generate the bindings, renaming the world to `webcpp_wasi_http`, then compile
and link a reactor with wasi-sdk's `clang` and `clang++`, where `WIT_BINDGEN`
is the wit-bindgen program, `WASI_SDK` wasi-sdk's directory and `WASI` this
repository's:

<!-- include::example/by_hand/build.sh[tag=p2] -->
```sh
"$WIT_BINDGEN" c --world service --rename-world webcpp_wasi_http \
    --out-dir gen wit

"$WASI_SDK/bin/clang" --target=wasm32-wasip2 \
    -c gen/webcpp_wasi_http.c -o gen/webcpp_wasi_http.o -Igen
"$WASI_SDK/bin/clang++" --target=wasm32-wasip2 -std=c++20 -fno-exceptions \
    -mexec-model=reactor -DWEBCPP_WASI_HTTP_P2 -I"$WASI/include" -Igen \
    main.cpp gen/webcpp_wasi_http.o gen/webcpp_wasi_http_component_type.o \
    -o hello.wasm
```

Serve it:

```sh
wasmtime serve -S cli --addr 127.0.0.1:8080 hello.wasm
```

and call it. `curl -i` also prints the two headers wasmtime adds to every
response, `date` and `transfer-encoding`, which are left out here; the rest
is [example/http_hello.expected](example/http_hello.expected), the
transcript that webcpp's test of the example compares:

<!-- include::example/http_hello.expected[] -->
```
$ curl -i -X GET 'http://localhost:8080/v1/greeting?name=ana'
HTTP/1.1 200 OK
content-type: text/plain

hello from /v1/greeting?name=ana

$ curl -i -X POST http://localhost:8080/x
HTTP/1.1 405 Method Not Allowed
content-type: text/plain

only GET
```

## Building with b2

In a b2 project that uses webcpp, a program that exports the HTTP handler and
nothing else links `/webcpp/wasi//http`, and is done. That target adds wasi's
headers; generates with wit-bindgen the bindings of the standard world of the
program's target, `wit/http-p2.wit` or `wit/http-p3.wit`, renamed
`webcpp_wasi_http`, and links them; and defines `WEBCPP_WASI_HTTP_P2` on the
toolset `clang-wasip2`, or `WEBCPP_WASI_HTTP_P3` on `clang-wasip3`. A
component is then an `exe` with that library and
`<linkflags>-mexec-model=reactor`. `example/Jamfile` builds the example so,
and serves it:

```
webcpp.targets wasip2 wasip3 ;
webcpp.serve http_hello.cpp : <library>/webcpp/wasi//http ;
```

A program whose world exports more than the HTTP handler uses
`/webcpp/wasi//wasi`, the headers alone, generates its own world's bindings
with `webcpp.wit-bindings`, renamed `webcpp_wasi_http`, and defines the macro
of its target itself.

b2 looks for wit-bindgen at `-sWIT_BINDGEN=<path>`, else at
`.local/wit-bindgen/wit-bindgen` in the superproject, else on `PATH`; for the
`wasi:http` WIT of each version at `-sWASI_WIT_P2=<dir>` and
`-sWASI_WIT_P3=<dir>`, else at `.local/wasi-wit/p2` and `.local/wasi-wit/p3`;
and, to serve a component, for wasmtime at `-sWASMTIME=<path>`, else on
`PATH`. A build that needs one that is not there stops, naming it and every
place it looked. A native build needs none of them.

## Your main

Your main's body is written between the two macros, as `main.cpp` above
does. `WEBCPP_WASI_HTTP_MAIN_BEGIN(method, target)` and
`WEBCPP_WASI_HTTP_MAIN_END()` define the function
`webcpp::wasi::http::http_main`, which takes the two `std::string_view`
parameters named in the first macro and returns a
`webcpp::wasi::http::response`, and, before it, the `extern "C"` handler your
world exports. Write the pair once in the program, in one `.cpp`.

| Parameter | What it holds |
| --- | --- |
| `method` | the method, as `"GET"`, `"POST"`, ...; an extension method as it was sent |
| `target` | the request target, path and query, as `"/v1/greeting?name=ana"`; `"/"` when the request carried none |

Both are views into the request, which the handler frees once it has written
the response: the response's content type may view them, and you copy what
you keep beyond the request. To split the target, parse it with a URL library, Boost.URL's
`parse_origin_form` for example.

The main's body is inside a function of `namespace webcpp::wasi::http`: name
your own code with its full namespace, or with a namespace alias declared at
the top of the body.

clang-format reads the body as a function's when its configuration names the
two macros, as this repository's `.clang-format` does:

```
MacroBlockBegin: "^WEBCPP_WASI_HTTP_MAIN_BEGIN$"
MacroBlockEnd: "^WEBCPP_WASI_HTTP_MAIN_END$"
```

## The response

`webcpp::wasi::http::response`, in `<webcpp/wasi/http/response.hpp>`, is a
struct of three members:

| Member | Type | What it holds |
| --- | --- | --- |
| `status` | `unsigned`, 200 by default | the status, sent as it is; keep it within 100 to 599 |
| `content_type` | `std::string_view`, empty by default | the `content-type` header, sent only when not empty; read after your main returns, so it views a literal, like `"application/json"`, or the method or the target, never a string of your main's own |
| `body` | `std::string`, empty by default | the body, sent whole, however long |

No other header is sent.

`response.hpp` includes nothing of the component ABI, so the rest of your
program can build responses, hold them, pass them around (as messages between
actors, for example) and be tested natively.

## What your build provides

What `/webcpp/wasi//http` provides in b2, and what a build by hand provides
itself:

| What | Why |
| --- | --- |
| `-DWEBCPP_WASI_HTTP_P2` or `-DWEBCPP_WASI_HTTP_P3`, exactly one | chooses the target; the header refuses to compile with neither or both |
| the bindings generated with `--rename-world webcpp_wasi_http` | the header includes `webcpp_wasi_http.h` and uses the names wit-bindgen gives the world, `webcpp_wasi_http_string_t` and the like; renaming makes them the same in every project, whatever its world is called |
| `webcpp_wasi_http.c` compiled as C for the target, and `webcpp_wasi_http_component_type.o`, linked | the glue that calls your handler, and the world's type information for the component linker |
| the directory of the bindings on the include path | for `webcpp_wasi_http.h` |
| `-mexec-model=reactor` on the link | the program is a reactor: it exports a handler and has no C `main`; wasmtime 47 also serves it linked as a command, but webcpp builds and tests reactors |

The host has its part too: `wasmtime serve` runs a component of wasi with
`-S cli`, since wasi-libc imports parts of `wasi:cli`, and on wasip3 with
`-S cli,p3 -W component-model-async`.

## wasip3

The same main works on WASI 0.3; only the build differs, as webcpp builds it
and serves it on wasip3:

- the world exports `wasi:http/handler@0.3.0`, with the WIT of `wasi:http`
  0.3.0 in `wit/deps/`: the `wit/deps` directory of the Rust crate `wasip3`
  0.9.0;
- the bindings are generated with
  `--async 'wasi:http/handler@0.3.0#handle'` beside `--rename-world`;
- everything is compiled for `--target=wasm32-wasip3`, the bindings' `.c`
  with `-pthread`, and the program with `-DWEBCPP_WASI_HTTP_P3`;
- wasmtime serves it with `-S cli,p3 -W component-model-async`.

webcpp compiles a wasip3 program with exceptions on,
`-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false`, and links it with
`-fwasm-exceptions -lunwind`; wasmtime serves it with the flags above and no
other. Built with `-fno-exceptions` in their place, as the commands of the
complete example do for wasip2, the component answers the same.

The handler is lifted asynchronously, waits for every write inside the task,
and always ends with `EXIT`.

## How a request flows

```
host (wasmtime serve)
  └─ calls the exported handler            exports_wasi_http_incoming_handler_handle
       ├─ reads the method and the target
       ├─ calls your main                  http_main(method, target) -> response
       ├─ sends the status and the content-type
       ├─ writes the body, every byte      wasip2: chunks of 4096; wasip3: until the stream took all
       └─ frees the request
```

A failure the host reports before the response is sent answers an internal
error; after it is sent, a body not written whole is reported the way each
WASI version defines.

## Limits

The handler hands your main only the method and the target. It reads no
request header and no request body, and sends no response header but
`content-type`. A main that needs a request body, or to set other headers,
waits for the helper to grow.

## Building and testing

wasi is developed inside the webcpp superproject, as a Boost library is
developed inside Boost:

    git clone --recursive https://github.com/webcpporg/webcpp
    cd webcpp
    b2 toolset=clang-wasip2 testing.launcher=wasmtime libs/wasi/test libs/wasi/example

builds and runs the tests and the examples that are not served, for
wasm32-wasip2, with the toolset that `user-config.jam` registers against
wasi-sdk. The components wasmtime serves, the example's among them, run in a
lane of their own, `http`, in the tests and in the examples:

    b2 toolset=clang-wasip2 testing.launcher=wasmtime \
        libs/wasi/test//http libs/wasi/example//http

builds each, has wasmtime serve it and compares its answers with its
`.expected` file. `toolset=clang-wasip3` does the same for wasm32-wasip3.
Natively, only what compiles without the component ABI is built.

## Documentation

wasi's page, with its API reference, is published at
<https://webcpporg.github.io/webcpp/libs/wasi/>; `b2 libs/wasi/doc` builds it
into `doc/html/index.html`.

## License

Distributed under the [Boost Software License, Version 1.0](LICENSE_1_0.txt).
