# Eskiu Language — VS Code Extension

Syntax highlighting for `.esk` files in Visual Studio Code.

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

## Future work

- Semantic highlighting via LSP (go-to-definition, hover types)
- Error squiggles from `eskiuc --test-typechecker`
- Snippets for common patterns (`struct`, `Result<T,E>`, `extern`)
