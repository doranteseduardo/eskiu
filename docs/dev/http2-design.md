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
- [x] **Stage 3 — HPACK** (RFC 7541), `stdlib/hpack.esk`. Header compression.
  - *3a* — prefix-integer coding (§5.1), string literals (§5.2), the 61-entry
    static table (Appendix A), a dynamic table with size-based eviction (§4), and
    the §6 decoder/encoder (indexed; literal with / without / never indexing;
    dynamic-table size update). The decoder maintains the dynamic table; the
    encoder is stateless (indexed static matches, else literal-without-indexing).
  - *3b* — Huffman coding (§5.2 + Appendix B): a decode trie + bit-packed encode
    over the 257-symbol table. The table is **generated** from the RFC text by
    `tools/gen_hpack_huffman.py` (→ `stdlib/hpack_huffman.esk`), so it is never
    hand-transcribed; the generator asserts the worked example (sym 47 = 0x18/6),
    EOS length, and completeness. The decoder validates padding (trailing bits
    must be the EOS prefix) and rejects a literal EOS. The encoder picks the
    smaller of raw/Huffman per string.
  - Verified against the RFC's own vectors — §C.1.1 (integer), §C.3.1 (raw
    request), §C.4.1 (Huffman request) — plus encode→decode round-trips
    (`tests/hpack`).
- [x] **Stage 4 — Streams & flow control** (`stdlib/http2.esk`). The per-stream
  state machine (`H2Stream`, idle → open → half-closed-local/remote → closed via
  `h2_stream_on_recv`/`h2_stream_on_send`); credit-based flow control (`h2_can_send`/
  `h2_account_sent`/`h2_account_recv`/`h2_grant_window`) over per-stream and
  connection windows; and the stream-frame codecs — HEADERS, DATA, WINDOW_UPDATE,
  RST_STREAM (`h2_write_*`). HEADERS+CONTINUATION reassembly to END_HEADERS is the
  async `h2_read_header_block_async`. Tested by `http2_stream` (state machine,
  flow control, codecs). RESERVED states are omitted (no server push; we advertise
  `ENABLE_PUSH = 0`).
- [ ] **Stage 5 — TLS / ALPN.** HTTP/2 in browsers requires TLS with ALPN
  negotiating `h2`. Planned via OpenSSL by FFI (already proven — the crypto
  pipeline links OpenSSL via `extern`). Cleartext `h2c` is the interim test path.
- [x] **Stage 6 — Server API** (`stdlib/http2_server.esk`). `http2_serve_async(lp,
  fd, handler, max_conns)` (and per-connection `http2_serve_conn_async`) mirroring
  the concurrent `<http_async>` server and reusing `<http>`'s `HttpRequest`/
  `HttpResponse`. Drives the handshake, then a frame-dispatch loop: a request
  (HEADERS + optional DATA) is HPACK-decoded into an `HttpRequest`, the handler
  fills an `HttpResponse`, and the response is encoded back as a HEADERS frame
  (`:status` + `content-length` + the handler's headers, lowercased) and a DATA
  frame with END_STREAM; SETTINGS/PING are answered, GOAWAY/EOF ends the loop.
  Tested end-to-end over a socketpair (`http2_server`). v1 handles streams
  sequentially (correct request/response, not yet concurrent multiplexing) and is
  cleartext h2c (TLS is stage 5).

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
