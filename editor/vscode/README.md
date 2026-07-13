# Eskiu Language: VS Code Extension

Syntax highlighting **and real-time error checking** for `.esk` files in Visual Studio Code.

Errors from `eskiuc --test-typechecker` appear as red underlines as you save.

## Install

**Option A: symlink (development, updates automatically):**

```bash
ln -s /path/to/eskiu/editor/vscode \
      ~/.vscode/extensions/eskiu-language
```

**Option B: copy:**

```bash
cp -r /path/to/eskiu/editor/vscode \
      ~/.vscode/extensions/eskiu-language
```

Restart VS Code after installing.

## What it highlights

| Token | Color category |
|---|---|
| `//` and `/* */` comments | comment |
| `"..."` string literals (with escape sequences) | string |
| `'a'` char literals | string |
| `0xFF`, `3.14`, `42` numbers | constant.numeric |
| `true`, `false`, `null`, `__FILE__`, `__LINE__` | constant.language |
| `#define #undef #ifdef #ifndef #else #endif #pragma #error` directives | keyword.control.directive |
| `if else for while do switch case break continue return in await match` | keyword.control |
| `try catch finally throw defer errdefer` | keyword.control.exception |
| `let struct packed union interface enum fn extern intrinsic import` | keyword.declaration |
| `const volatile static escaping must_use async` | storage.modifier |
| `sizeof asm alloc_with thread_create thread_join va_start va_arg va_end` | keyword.other |
| `import <mem>;` stdlib imports and `type Alias = …` | namespace / declaration |
| `int uint8 float double bool char string void` … | support.type |
| Function names at declaration and call sites | entity.name.function |
| Type names (uppercase-starting) | entity.name.type |
| Template parameters `<T, E>`, incl. bounded `<T: Iface>` / `<T: A + B>` | entity.name.type.parameter |
| `+= == && \| ^ << ..` operators (incl. the `..` range) | keyword.operator |

## File association

Files ending in `.esk` are automatically associated with the Eskiu language.
To associate manually, add to VS Code `settings.json`:

```json
"files.associations": {
  "*.esk": "eskiu"
}
```

## How error checking works

On every file open and save, `extension.js` runs:
```
eskiuc <file.esk> --test-typechecker
```
and parses `file:line:col: message` output into VS Code diagnostics.
No npm packages required; pure VS Code extension API.

`server.js` is an alternative standalone JSON-RPC LSP server for editors
that support LSP natively (Neovim, Helix, etc.).

## Future work

- Snippets: `struct`, `Result<T,E>`, `extern`, `interface`
- Go-to-definition for struct fields (currently works for functions and variables)
