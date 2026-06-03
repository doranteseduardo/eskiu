# Eskiu Language — VS Code Extension

Syntax highlighting **and real-time error checking** for `.esk` files in Visual Studio Code.

Errors from `eskiuc --test-typechecker` appear as red underlines as you save.

## Install

**Option A — symlink (development, updates automatically):**

```bash
ln -s /path/to/eskiu/editor/vscode \
      ~/.vscode/extensions/eskiu-language
```

**Option B — copy:**

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
| `true`, `false`, `null` | constant.language |
| `if else for while switch case break continue return` | keyword.control |
| `let struct interface fn extern import alloc free` | keyword.declaration |
| `int uint8 float double bool char string void` … | support.type |
| Function names at declaration and call sites | entity.name.function |
| Type names (uppercase-starting) | entity.name.type |
| Template parameters `<T, E>` | entity.name.type.parameter |
| `+= == && \| ^ <<` operators | keyword.operator |

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
No npm packages required — pure VS Code extension API.

`server.js` is an alternative standalone JSON-RPC LSP server for editors
that support LSP natively (Neovim, Helix, etc.).

## Future work

- Hover: show inferred type on mouse-over
- Go-to-definition for struct fields and functions
- Snippets: `struct`, `Result<T,E>`, `extern`, `interface`
