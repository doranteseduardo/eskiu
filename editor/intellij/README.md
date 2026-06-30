# Eskiu Language — IntelliJ IDEA / JetBrains IDEs

Syntax highlighting for `.esk` files in IntelliJ IDEA (and other JetBrains IDEs:
CLion, GoLand, PyCharm, RustRover, …) by **reusing the same TextMate grammar that
powers the VS Code extension** — so the two never drift apart.

JetBrains IDEs ship with a built-in TextMate engine, so no custom lexer/parser is
needed: the grammar at `bundle/Syntaxes/eskiu.tmLanguage.json` (a copy of the
canonical `editor/vscode/syntaxes/eskiu.tmLanguage.json`) drives highlighting
directly.

---

## Option A — Import the bundle (works today, recommended)

The fastest path, no build required. In IntelliJ:

1. **Settings/Preferences → Editor → TextMate Bundles**
2. Click **+** and select this folder:
   `…/eskiu/editor/intellij/bundle`
3. **Apply**. Open any `.esk` file — keywords, strings, comments, numbers, types,
   operators, and `#pragma`/preprocessor directives are now highlighted.

To pick a color theme for the scopes: **Settings → Editor → Color Scheme → TextMate**.

This binds `.esk` to the `source.eskiu` scope automatically (the grammar's
`fileTypes` lists `esk`).

---

## Option B — Package as a distributable plugin

For a one-click install (and JetBrains Marketplace), `plugin/` is a Gradle project
that wraps the same bundle as an installable plugin. It depends on the platform
TextMate plugin (`org.jetbrains.plugins.textmate`) and contributes the bundle via a
`TextMateBundleProvider`, so installing the plugin = the grammar is registered with
no manual import.

```bash
cd editor/intellij/plugin
./gradlew buildPlugin        # → build/distributions/eskiu-intellij-*.zip
```

Install the resulting zip via **Settings → Plugins → ⚙ → Install Plugin from Disk…**.

> The Gradle project targets a recent IntelliJ platform (2023.2+, where the
> `TextMateBundleProvider` API is stable). Adjust `platformVersion` in
> `plugin/gradle.properties` to your IDE build if needed. Building requires a JDK 17+
> and the IntelliJ Gradle plugin (fetched on first build).

---

## Keeping the grammar in sync

The **canonical** grammar is `editor/vscode/syntaxes/eskiu.tmLanguage.json`. The copy
here (and the one inside `plugin/`) must be updated in lockstep whenever the language
gains or loses syntax. Refresh both with:

```bash
make -C editor sync-grammar     # or: cp editor/vscode/syntaxes/eskiu.tmLanguage.json editor/intellij/bundle/Syntaxes/
```

(No language-syntax change ⇒ nothing to sync — the same rule as the VS Code grammar.)

## Scope: highlighting only

This integration gives **syntax highlighting + `.esk` file association**. It does not
provide completion, go-to-definition, or inline error checking (the VS Code extension
gets live `eskiuc --test-typechecker` diagnostics via its language server). A full
JetBrains language plugin (Kotlin lexer/parser/PSI + an external annotator calling
`eskiuc`) would add those — a larger, separate effort.
