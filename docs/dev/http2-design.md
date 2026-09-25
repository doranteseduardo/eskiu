# HTTP/2 architecture

`<http2>` brings HTTP/2 (RFC 7540) to Eskiu over the async `<eventloop>`. HTTP/2 is
a binary, multiplexed protocol: one TCP (TLS) connection carries many concurrent
request/response *streams*, each a sequence of *frames*, with header compression
and flow control. The implementation is split across a stack of stdlib modules,
each layer self-contained and testable on its own.

**Audience:** compiler/stdlib maintainers. Assumes the async stack (`<future>`,
`<eventloop>`, `<net_async>`, `<http_async>`) and `<http>`'s message types.

## Layers

### Frame layer (`stdlib/http2.esk`)

The 9-byte frame-header codec (`h2_write_header` / `h2_read_header`, big-endian
length/type/flags/stream), the frame-type and flag constants (`H2_*` /
`H2_FLAG_*`), and the connection preface. Self-contained; everything below builds
on it. Unit-tested by `tests/http2_frame`.

### Connection lifecycle (`stdlib/http2.esk`)

The SETTINGS codec (`h2_write_settings`/`h2_apply_settings` over the 6 standard
params, with an `H2Settings` model carrying the RFC defaults), the SETTINGS ACK,
PING/PONG (`h2_write_ping`/`h2_send_pong`), and GOAWAY
(`h2_write_goaway`/`h2_read_goaway`/`h2_send_goaway`) with the §7 error codes. An
`H2Conn` holds per-connection state. I/O is async over the event loop:
`h2_read_full_async` (partial-read loop), `h2_read_frame_async` (header +
payload), and `h2_server_handshake_async` (validate preface → read + apply client
SETTINGS → send ours + ACK). A server advertises `SETTINGS_ENABLE_PUSH = 0` (no
server push, §6.5.2). Tested by `http2_conn` (pure codecs) and `http2_handshake`
(the async handshake over a socketpair).

### HPACK header compression (`stdlib/hpack.esk`, `stdlib/hpack_huffman.esk`)

HPACK (RFC 7541). Prefix-integer coding (§5.1), string literals (§5.2), the
61-entry static table (Appendix A), and a dynamic table with size-based eviction
(§4) feed the §6 decoder/encoder (indexed; literal with / without / never
indexing; dynamic-table size update). The decoder maintains the dynamic table; the
encoder is stateless (indexed static matches, else literal-without-indexing).

Huffman coding (§5.2 + Appendix B) is a decode trie plus bit-packed encode over
the 257-symbol table. The table is **generated** from the RFC text by
`tools/gen_hpack_huffman.py` (→ `stdlib/hpack_huffman.esk`) rather than
hand-transcribed; the generator asserts the worked example (sym 47 = 0x18/6), the
EOS length, and completeness. The decoder validates padding (trailing bits must be
the EOS prefix) and rejects a literal EOS; the encoder picks the smaller of
raw/Huffman per string. Validated against the RFC vectors, §C.1.1 (integer),
§C.3.1 (raw request), §C.4.1 (Huffman request), plus encode→decode round-trips
(`tests/hpack`).

### Streams and flow control (`stdlib/http2.esk`)

The per-stream state machine (`H2Stream`: idle → open → half-closed-local/remote →
closed, via `h2_stream_on_recv`/`h2_stream_on_send`); credit-based flow control
(`H2Conn_can_send`/`h2_account_sent`/`h2_account_recv`/`h2_grant_window`) over
per-stream and connection windows; and the stream-frame codecs: HEADERS, DATA,
WINDOW_UPDATE, RST_STREAM (`h2_write_*`). HEADERS+CONTINUATION reassembly to
END_HEADERS is the async `h2_read_header_block_async`. RESERVED states are omitted
(no server push). Tested by `http2_stream` (state machine, flow control, codecs).

### Multiplexed server API (`stdlib/http2_server.esk`)

`http2_serve_async(lp, fd, handler, max_conns)` (and per-connection
`http2_serve_conn_async`) mirrors the concurrent `<http_async>` server and reuses
`<http>`'s `HttpRequest`/`HttpResponse`. A request (HEADERS + optional DATA) is
HPACK-decoded into an `HttpRequest`, the handler fills an `HttpResponse`, and the
response is encoded back as a HEADERS frame (`:status` + `content-length` + the
handler's headers, lowercased) followed by the body split into
`SETTINGS_MAX_FRAME_SIZE`-bounded (16384) DATA frames, the last carrying
END_STREAM.

The protocol lives in `H2Server`, a transport-agnostic connection engine: the
transport reads one frame, hands it to `H2Server_step`, and writes out whatever
the engine queued (`s.out[0..s.outlen)`). The h2c server and both TLS servers are
thin loops around it, so they share one implementation of:

- receive flow control: DATA (padding included) spends the stream and connection
  receive windows, and the engine sends WINDOW_UPDATE once half a window has been
  taken in, so uploads of any size keep flowing;
- PADDED DATA/HEADERS, PRIORITY, and header blocks split over CONTINUATION
  frames (any other frame in the middle of a block is a PROTOCOL_ERROR);
- send flow control: responses are queued per stream and sent as the stream and
  connection windows allow. Frames keep being read while a response waits, so a
  new request is decoded in order (HPACK stays in sync) and answered, and a
  WINDOW_UPDATE credits only the stream it names;
- connection errors with GOAWAY and stream errors with RST_STREAM as RFC 9113
  prescribes: window overflow past 2^31 - 1 (FLOW_CONTROL_ERROR), invalid SETTINGS
  values (`h2_settings_error`), frames on idle streams, bad frame sizes;
- requests with more than `H2_MAX_HEADERS` (64) fields get 431 and requests
  without `:method`/`:path` get 400, without calling the handler.

Streams are multiplexed (up to `H2_MAX_STREAMS`, 128, advertised as
SETTINGS_MAX_CONCURRENT_STREAMS); ready requests are answered in arrival order and
the connection has a single writer. A peer GOAWAY stops new streams; the loop ends
once the open ones are answered. Tested over a socketpair end-to-end
(`http2_server`, `http2_chunking`, `http2_multiplex`, `http2_frame_rules`), with a
window-honoring client thread (`http2_flow_control`), and for the TLS transports
with an OpenSSL stand-in (`http2_tls_engine`).

### TLS / ALPN (`stdlib/tls.esk`)

OpenSSL (libssl) by FFI. A server `SSL_CTX` loads a cert/key and installs an ALPN
callback selecting `h2` (`tls_server_ctx`); the ALPN selector is handed to OpenSSL
as a raw C function pointer via the `(*void)fn` cast (language spec §13.4). Two
server flavours run the `<http2>` frame protocol (reusing the codecs, HPACK, and
the `<http2_server>` request/response glue) over the encrypted stream:

- **Blocking**, thread-per-connection: `http2_tls_serve_conn` over
  `tls_accept`/`tls_read_full`/`tls_write_all`/`tls_close`.
- Both drive the same `H2Server` engine as the h2c server.
- **Async**, many TLS connections on one event-loop thread:
  `http2_tls_serve_async`. A non-blocking SSL pump
  (`tls_accept_async`/`tls_read_async`/`tls_write_all_async`) retries `SSL_*` on
  `WANT_READ`/`WANT_WRITE` and parks on the matching readiness via the reactor's
  `EventLoop_add_read`/`EventLoop_add_write`.

Verified end-to-end against `curl --http2`: ALPN negotiates h2 and the request is
served as `HTTP/2 200`, including multiple concurrent connections on the async
server's single thread. See `examples/http2_tls_server.esk`. (The TLS path is
outside the dependency-free automated suite: it needs OpenSSL link flags, a cert,
and a TLS client, but the pure ALPN selector logic is exercised by that
end-to-end run.)

## Frame header (RFC 7540 §4.1)

```
[ Length (24) | Type (8) | Flags (8) | R(1) | Stream Identifier (31) ]   (9 bytes, big-endian)
```

`h2_read_header` masks off the reserved top bit of the stream id; `h2_write_header`
keeps it zero. Stream id 0 is the connection-control stream. Frame types and flags
are the `H2_*` / `H2_FLAG_*` constants.

## Notes

- HTTP/2 multiplexing wants non-blocking I/O, so the connection loop is async
  throughout (one `H2Conn` parked on its fd, resumed per readable batch of frames).
- Flow control and HPACK's dynamic table are the two stateful, easy-to-get-wrong
  parts; each has its own focused test (`http2_stream`, `hpack`).
