# Migrating to Eskiu 0.11

Eskiu 0.11 removes the stdlib names that 0.9.2 and 0.10.1 renamed. Until 0.10.1 they still
compiled as thin wrappers over the new names; now a program that uses one gets an
"undefined function" error. Each table below maps an old name to the one that replaces it,
with the same arguments unless noted. A function whose first parameter is the module's
struct can also be called with dot syntax: `rng.next()` is `Rng_next(&rng)`.

From 0.11 on, a rename follows `docs/lang/stability.md`: the old name stays for one minor
release, marked `deprecated("use NEW")`, so each use prints a warning that names the
replacement.

## Names renamed in 0.9.2

### `<channel>`

| Old name | New name |
|---|---|
| `chan_send` | `Chan_send` |
| `chan_recv` | `Chan_recv` |
| `chan_free` | `Chan_free` |

### `<eventloop>`

| Old name | New name |
|---|---|
| `el_add_timer` | `EventLoop_add_timer` |
| `el_del_timer` | `EventLoop_del_timer` |
| `el_add_read` | `EventLoop_add_read` |
| `el_add_write` | `EventLoop_add_write` |
| `el_del` | `EventLoop_del` |
| `el_del_free` | `EventLoop_del_free` |
| `el_stop` | `EventLoop_stop` |
| `el_run` | `EventLoop_run` |
| `el_free` | `EventLoop_free` |

### `<executor>`

| Old name | New name |
|---|---|
| `executor_schedule` | `Executor_schedule` |
| `executor_run` | `Executor_run` |
| `executor_stop` | `Executor_stop` |
| `executor_free` | `Executor_free` |

### `<hpack>`

| Old name | New name |
|---|---|
| `hpack_huff_build` | `HpackHuff_build` |
| `hpack_huff_free` | `HpackHuff_free` |
| `hpack_decoder_init` | `HpackDecoder_init` |
| `hpack_decoder_free` | `HpackDecoder_free` |

### `<http2>`

| Old name | New name |
|---|---|
| `h2_conn_init` | `H2Conn_init` |
| `h2_stream_init` | `H2Stream_init` |
| `h2_can_send` | `H2Conn_can_send` |

### `<random>`

| Old name | New name |
|---|---|
| `rng_seed` | `Rng_seed` |
| `rng_next` | `Rng_next` |
| `rng_below` | `Rng_below` |
| `rng_range` | `Rng_range` |
| `rng_bool` | `Rng_bool` |
| `rng_double` | `Rng_double` |
| `rng_fill` | `Rng_fill` |

### `<regex>`

| Old name | New name |
|---|---|
| `regex_search` | `Regex_search` |
| `regex_free` | `Regex_free` |
| `match_group` | `Match_group` |
| `match_free` | `Match_free` |

### `<string>`

| Old name | New name |
|---|---|
| `String_is_space` | `is_space` from `<ctype>` |

### `<sysheap>`

| Old name | New name |
|---|---|
| `heap_init` | `Heap_init` |
| `heap_alloc` | `Heap_alloc` |
| `heap_free` | `Heap_free` |
| `heap_destroy` | `Heap_destroy` |

### `<time>`

| Old name | New name |
|---|---|
| `time_from_utc` | `DateTime_to_epoch` |
| `time_format_iso` | `DateTime_format_iso` |

## Names renamed in 0.10.1

### `<args>`

| Old name | New name |
|---|---|
| `args_flag` | `ArgParser_flag` |
| `args_option` | `ArgParser_option` |
| `args_option_int` | `ArgParser_option_int` |
| `args_argument` | `ArgParser_argument` |
| `args_get_flag` | `ArgParser_get_flag` |
| `args_count` | `ArgParser_count` |
| `args_get` | `ArgParser_get` |
| `args_get_at` | `ArgParser_get_at` |
| `args_get_int` | `ArgParser_get_int` |
| `args_get_all` | `ArgParser_get_all` |
| `args_positional` | `ArgParser_positional` |
| `args_positional_count` | `ArgParser_positional_count` |
| `args_rest` | `ArgParser_rest` |

### `<either>`

| Old name | New name |
|---|---|
| `opt_is_some` | `Option_is_some` |
| `opt_is_none` | `Option_is_none` |
| `opt_unwrap_or` | `Option_unwrap_or` |
| `either_is_left` | `Either_is_left` |

### `<future>`

| Old name | New name |
|---|---|
| `future_poll` | `Future_poll` |
| `future_complete` | `Future_complete` |
| `free_future` | `Future_free` |
| `free_future_polled` | `Future_free_polled` |
| `future_drop` | `Future_drop` |

### `<hash>`

| Old name | New name |
|---|---|
| `hex_encode` | `hash_hex_encode` |
| `hex_decode` | `hash_hex_decode` |
| `sha256` | `hash_sha256` |
| `sha256_hex` | `hash_sha256_hex` |
| `sha256_str` | `hash_sha256_str` |
| `sha1` | `hash_sha1` |
| `sha1_hex` | `hash_sha1_hex` |
| `sha1_str` | `hash_sha1_str` |
| `md5` | `hash_md5` |
| `md5_hex` | `hash_md5_hex` |
| `md5_str` | `hash_md5_str` |
| `hmac_sha256` | `hash_hmac_sha256` |
| `hmac_sha256_str` | `hash_hmac_sha256_str` |

### `<hpack>`

| Old name | New name |
|---|---|
| `hpack_huff_decode` | `HpackHuff_decode` |
| `hpack_decode` | `HpackDecoder_decode` |

### `<http>`

| Old name | New name |
|---|---|
| `http_parse_head` | `HttpReq_parse_head` |

### `<http2>`

| Old name | New name |
|---|---|
| `h2_settings_defaults` | `H2Settings_defaults` |
| `h2_apply_settings` | `H2Settings_apply` |
| `h2_stream_on_recv` | `H2Stream_on_recv` |
| `h2_stream_on_send` | `H2Stream_on_send` |
| `h2_account_sent` | `H2Conn_account_sent` |
| `h2_account_recv` | `H2Conn_account_recv` |
| `h2_grant_window` | `H2Conn_grant_window` |
| `h2_send_goaway` | `H2Conn_send_goaway` |

### `<string>`

| Old name | New name |
|---|---|
| `str_join` | `List_join` |
| `str_format` | `String_format` |

### `<term>`

| Old name | New name |
|---|---|
| `term_rgb` | `term_fg_rgb` |

### `<time>`

| Old name | New name |
|---|---|
| `sleep_ms` | `time_sleep_ms` |
| `duration_format` | `time_format_duration` |

### `<url>`

| Old name | New name |
|---|---|
| `url_resolve` | `Url_resolve` |

## Without a wrapper (0.10.1)

| Old name | New name | Note |
|---|---|---|
| `bsearch<T>` | `sort_search<T>` | `bsearch` clashed with the C library function. |
| `select2_hdr`, `join2_hdr`, `spawn_hdr` | `select2`, `join2`, `spawn` | The type-erased cores are internal (`_select2_hdr`, ...). |
| Undocumented helpers (`re_*`, `JParser_*`, `mp_*`, `http_lc`, `h2_*` internals, ...) | none | Internal now, with a leading `_`. Use the documented API of the module. |

## Other changes that need an edit

- **0.10.1:** `<mem>` declares `strcmp`, `strncmp`, `strchr` and `strstr` (`strchr` and
  `strstr` return `*char`). A program that imports `<mem>`, directly or through another
  module, and declares one of them with different types gets a "conflicting declaration"
  error: remove its own `extern`.
- **0.9.2:** the language follows C in places where it did not before. A global's initializer
  must be a constant (a call or a read of a non-`const` global is an error); integer
  promotions and the usual arithmetic conversions apply (a comparison of a signed and an
  unsigned value converts as in C); octal
  literals start with `0` (`010` is 8, and a digit 8 or 9 there is an error); a struct
  literal checks each field and zero-fills the rest; a duplicate declaration in one scope
  is an error. The 0.9.2 section of `CHANGELOG.md` lists each rule.
- **0.9.2:** `-lm`, `-lc++` and `-lpthread` are no longer needed on the command line: a
  module names its libraries with `#pragma link`, and the driver adds the C++ and thread
  runtimes when the program uses them.
