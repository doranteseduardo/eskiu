# HTTP/2 — design & roadmap

`<http2>` brings HTTP/2 (RFC 7540) to Eskiu over the async `<eventloop>`. HTTP/2 is
a binary, multiplexed protocol: one TCP (TLS) connection carries many concurrent
request/response *streams*, each a sequence of *frames*, with header compression
and flow control. It is a large protocol, built here in stages so each lands
tested and useful on its own.

**Audience:** compiler/stdlib maintainers. Assumes the async stack (`<future>`,
`<eventloop>`, `<net_async>`, `<http_async>`) and `<http>`'s message types.

## Stages

- [x] **Stage 1 — Frame layer** (`stdlib/http2.esk`). The 9-byte frame header
  codec (`h2_write_header` / `h2_read_header`, big-endian length/type/flags/stream),
  frame-type and flag constants, and the connection preface. Self-contained and
  unit-tested (`tests/http2_frame`). Everything below is built on it.
- [x] **Stage 2 — Connection lifecycle** (`stdlib/http2.esk`). SETTINGS codec
  (`h2_write_settings`/`h2_apply_settings` over the 6 standard params + an
  `H2Settings` model with RFC defaults), the SETTINGS ACK, PING/PONG
  (`h2_write_ping`/`h2_send_pong`), and GOAWAY (`h2_write_goaway`/`h2_read_goaway`/
  `h2_send_goaway`) with the §7 error codes. An `H2Conn` holds per-connection
  state. Async I/O over the event loop: `h2_read_full_async` (partial-read loop),
  `h2_read_frame_async` (header + payload), and `h2_server_handshake_async`
  (validate preface → read + apply client SETTINGS → send ours + ACK). Tested:
  `http2_conn` (pure codecs) and `http2_handshake` (the async handshake over a
  socketpair).
- [~] **Stage 3 — HPACK** (RFC 7541), `stdlib/hpack.esk`. Header compression.
  - [x] *3a* — prefix-integer coding (§5.1), raw string literals (§5.2, H=0), the
    61-entry static table (Appendix A), a dynamic table with size-based eviction
    (§4), and the §6 decoder/encoder (indexed; literal with / without / never
    indexing; dynamic-table size update). Checked against the RFC's own vectors
    (§C.1.1 integer, §C.3.1 request block) plus an encode→decode round-trip
    (`tests/hpack`). The decoder maintains the dynamic table; the encoder is
    stateless (indexed static matches, else literal-without-indexing).
  - [ ] *3b* — Huffman coding (§5.2 + Appendix B): the 257-symbol table, decode
    (required to read real clients' headers) and encode. A Huffman-coded string
    (H bit set) currently decodes to an error; this is the only HPACK gap.
- [ ] **Stage 4 — Streams & flow control.** Stream state machine (idle → open →
  half-closed → closed), HEADERS+CONTINUATION assembly, DATA framing, per-stream
  and connection WINDOW_UPDATE flow control, RST_STREAM.
- [ ] **Stage 5 — TLS / ALPN.** HTTP/2 in browsers requires TLS with ALPN
  negotiating `h2`. Planned via OpenSSL by FFI (already proven — the crypto
  pipeline links OpenSSL via `extern`). Cleartext `h2c` is the interim test path.
- [ ] **Stage 6 — Server API.** `http2_serve_async(lp, fd, handler, …)` mirroring
  the concurrent `<http_async>` server, reusing `<http>`'s `HttpRequest`/`Response`.

## Frame header (RFC 7540 §4.1)

```
[ Length (24) | Type (8) | Flags (8) | R(1) | Stream Identifier (31) ]   — 9 bytes, big-endian
```

`h2_read_header` masks off the reserved top bit of the stream id; `h2_write_header`
keeps it zero. Stream id 0 is the connection-control stream. Frame types and flags
are the `H2_*` / `H2_FLAG_*` constants.

## Notes

- HTTP/2 multiplexing wants non-blocking I/O, so the connection loop is async from
  the start (one `H2Conn` parked on its fd, resumed per readable batch of frames).
- Flow control and HPACK's dynamic table are the two stateful, easy-to-get-wrong
  parts; both get their own focused tests before the server API is wired up.
