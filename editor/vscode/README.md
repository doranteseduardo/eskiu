# Eskiu Language: VS Code Extension

Syntax highlighting, error checking, hover types and go-to-definition for `.esk` files in
Visual Studio Code and editors built on it (Cursor, Antigravity, VSCodium).

Errors from `eskiuc --test-typechecker` appear as underlines when a file is opened or
saved. Hovering a name shows its type (`--hover-at`), and go-to-definition jumps to where
it is declared (`--definition-at`). The extension uses the `eskiuc` on your `PATH`, or
`build/eskiuc` when it runs from a checkout of this repository.

## Install

Package the extension and install the `.vsix`:

```bash
cd editor/vscode
npx @vscode/vsce package --allow-missing-repository --skip-license
code --install-extension eskiu-language-*.vsix    # or cursor / antigravity-ide / codium
```

For development, link the folder into the extensions directory instead, so edits apply
after a window reload:

```bash
ln -s "$PWD/editor/vscode" ~/.vscode/extensions/eskiu-language
```

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
| Loop labels `outer:` before a `for`/`while`/`do` (for `break outer` / `continue outer`) | entity.name.label |
| `try catch finally throw defer errdefer` | keyword.control.exception |
| `let struct packed union interface enum fn operator extern intrinsic import` | keyword.declaration |
| `const volatile static escaping must_use async` | storage.modifier |
| `sizeof asm alloc_with thread_create thread_join va_start va_arg va_end` | keyword.other |
| `import <mem>;` stdlib imports and `type Alias = …` | namespace / declaration |
| `int uint8 float double bool char string void` … | support.type |
| Function names at declaration and call sites | entity.name.function |
| A name after `.` (a field or method, keywords included: `j.int(5)`) | variable.other.member / entity.name.function |
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

## Limitations

Go-to-definition works for functions and variables, not yet for struct fields.
