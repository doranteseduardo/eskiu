#!/usr/bin/env python3
"""
Generate the site's docs pages from the Markdown sources in docs/.

The repo is private, so the public can't read the docs on GitHub. This renders
the language docs (docs/lang/*, docs/API.md, docs/GLOSSARY.md) and the
contributor docs (docs/dev/*, minus phases.md) into self-contained, styled HTML
under site/docs/, matching the look of the hand-written pages (case-study,
quickstart).

Usage:  python3 site/build-docs.py        (run from anywhere)

Requires: markdown  (pip install --user markdown)
Re-run whenever the Markdown sources change, then commit site/docs/.
"""

import html
import os
import re

import markdown

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "site", "docs")

# source (relative to repo root) -> (output basename, page title, section)
PAGES = [
    ("docs/lang/index.md",           "index.html",           "Documentation",        "lang"),
    ("docs/lang/getting-started.md", "getting-started.html", "Getting started",      "lang"),
    ("docs/lang/spec.md",            "spec.html",            "Language spec",        "lang"),
    ("docs/lang/grammar.md",         "grammar.html",         "Grammar",              "lang"),
    ("docs/lang/build.md",           "build.html",           "Building & tooling",   "lang"),
    ("docs/API.md",                  "api.html",             "Compiler C++ API",     "dev"),
    ("docs/GLOSSARY.md",             "glossary.html",        "Glossary",             "lang"),
    ("docs/dev/index.md",            "internals.html",       "Compiler internals",   "dev"),
    ("docs/dev/architecture.md",     "architecture.html",    "Architecture",         "dev"),
    ("docs/dev/design.md",           "design.html",          "Design",               "dev"),
    ("docs/dev/abi.md",              "abi.html",             "ABI",                  "dev"),
    ("docs/dev/async-design.md",     "async-design.html",    "Async design",         "dev"),
    ("docs/dev/self-hosting.md",     "self-hosting.html",    "Self-hosting",         "dev"),
    ("docs/dev/http2-design.md",     "http2-design.html",    "HTTP/2 design",        "dev"),
    ("docs/dev/debugging.md",        "debugging.html",       "Debugging",            "dev"),
    ("docs/dev/contributing.md",     "contributing.html",    "Contributing",         "dev"),
]

# repo-relative source path (normalized) -> output html, for link rewriting
PATHMAP = {os.path.normpath(src): out for src, out, _, _ in PAGES}

# top nav, per section
NAV_LANG = [
    ("index.html", "Overview"),
    ("getting-started.html", "Getting started"),
    ("spec.html", "Spec"),
    ("grammar.html", "Grammar"),
    ("build.html", "Tooling"),
    ("glossary.html", "Glossary"),
    ("internals.html", "Internals →"),
]
NAV_DEV = [
    ("index.html", "← Language docs"),
    ("internals.html", "Overview"),
    ("architecture.html", "Architecture"),
    ("design.html", "Design"),
    ("abi.html", "ABI"),
    ("async-design.html", "Async"),
    ("self-hosting.html", "Self-hosting"),
    ("http2-design.html", "HTTP/2"),
    ("api.html", "C++ API"),
    ("debugging.html", "Debugging"),
    ("contributing.html", "Contributing"),
]
NAVS = {"lang": NAV_LANG, "dev": NAV_DEV}

# un-ported docs (e.g. dev/phases.md) fall back to the GitHub source
GH_BLOB = "https://github.com/doranteseduardo/eskiu/blob/main/"

CSS = """
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
:root {
  --bg: #f9f7f3; --surface: #f2efe8; --border: #ddd8ce;
  --ink: #1b1915; --ink2: #6b6560; --purple: #4c31bb; --purple-lt: #6448d4;
  --code: #1b1728; --code-bd: #2c2444;
  --sans: "Space Grotesk", system-ui, -apple-system, sans-serif;
  --mono: "JetBrains Mono", Menlo, monospace;
}
html { -webkit-text-size-adjust: 100%; text-size-adjust: 100%; }
body { background: var(--bg); color: var(--ink); font-family: var(--sans);
       line-height: 1.65; -webkit-font-smoothing: antialiased; }
.wrap { max-width: 800px; margin: 0 auto; padding: 2.25rem 1.25rem 5rem; }
.top { display: flex; align-items: center; gap: 0.6rem; margin-bottom: 1.5rem; }
.top img { width: 30px; height: 30px; }
.top a { color: var(--ink2); text-decoration: none; font-size: 0.85rem; }
.top a:hover { color: var(--purple); }
.docnav { display: flex; flex-wrap: wrap; gap: 0.35rem 0.5rem; margin-bottom: 2.5rem;
          padding-bottom: 1.25rem; border-bottom: 1px solid var(--border); }
.docnav a { font-size: 0.8rem; color: var(--ink2); text-decoration: none;
            padding: 0.25rem 0.6rem; border-radius: 6px; border: 1px solid transparent; }
.docnav a:hover { color: var(--purple); background: var(--surface); }
.docnav a.active { color: var(--purple); border-color: var(--border); background: #fff; font-weight: 600; }
.content h1 { font-size: clamp(1.7rem, 4.5vw, 2.4rem); letter-spacing: -0.03em;
              line-height: 1.12; margin: 0 0 1.25rem; }
.content h2 { font-size: 1.35rem; letter-spacing: -0.02em; margin: 2.5rem 0 0.85rem;
              padding-top: 0.5rem; }
.content h3 { font-size: 1.08rem; margin: 1.75rem 0 0.6rem; }
.content h4 { font-size: 0.95rem; margin: 1.4rem 0 0.5rem; color: var(--ink2); }
.content p { margin: 0 0 1rem; }
.content a { color: var(--purple); }
.content ul, .content ol { margin: 0 0 1rem 1.3rem; }
.content li { margin-bottom: 0.4rem; }
.content strong { font-weight: 600; }
.content hr { border: none; border-top: 1px solid var(--border); margin: 2rem 0; }
.content blockquote { border-left: 3px solid var(--purple); background: var(--surface);
                      padding: 0.8rem 1.1rem; border-radius: 0 8px 8px 0; margin: 0 0 1.25rem;
                      color: var(--ink2); }
.content blockquote p:last-child { margin-bottom: 0; }
.content code { font-family: var(--mono); font-size: 0.86em; background: var(--surface);
                padding: 0.1em 0.35em; border-radius: 4px; }
.content pre { background: var(--code); border: 1px solid var(--code-bd); border-radius: 8px;
               padding: 0.95rem 1.1rem; margin: 0 0 1.5rem; overflow-x: auto;
               font-family: var(--mono); font-size: 0.78rem; line-height: 1.75; color: #b4bccc; }
.content pre code { background: none; padding: 0; font-size: inherit; color: inherit; }
.tablewrap { overflow-x: auto; margin: 0 0 1.5rem; }
.content table { width: 100%; border-collapse: collapse; font-size: 0.88rem; }
.content th, .content td { text-align: left; padding: 0.5rem 0.75rem;
                          border-bottom: 1px solid var(--border); vertical-align: top; }
.content th { color: var(--ink2); font-weight: 600; white-space: nowrap; }
.content td code { white-space: nowrap; }
.foot { margin-top: 3rem; padding-top: 1.5rem; border-top: 1px solid var(--border);
        font-size: 0.9rem; color: var(--ink2); }
@media (max-width: 600px) {
  .content table { font-size: 0.8rem; }
  .content th, .content td { padding: 0.4rem 0.55rem; white-space: normal; }
}
""".strip()

PAGE = """<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>{title} · Eskiu docs</title>
  <meta name="description" content="{desc}" />
  <link rel="canonical" href="https://eskiu-lang.org/docs/{out}" />
  <link rel="icon" type="image/png" href="../../assets/logo.png" />
  <link rel="preconnect" href="https://fonts.googleapis.com" />
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
  <link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet" />
  <style>{css}</style>
</head>
<body>
  <div class="wrap">
    <div class="top">
      <img src="../../assets/logo.png" alt="Eskiu" />
      <a href="../index.html">← eskiu</a>
    </div>
    <nav class="docnav">{nav}</nav>
    <div class="content">
{body}
    </div>
    <div class="foot"><a href="../index.html">← Back to eskiu</a></div>
  </div>
</body>
</html>
"""


def make_rewriter(srcdir):
    """Return a regex callback that rewrites .md hrefs relative to srcdir."""

    def rewrite(m):
        target = m.group(1)
        anchor = ""
        if "#" in target:
            target, anchor = target.split("#", 1)
            anchor = "#" + anchor
        if not target:  # pure in-page anchor
            return f'href="{anchor}"'
        if "://" in target:  # already absolute
            return m.group(0)
        # resolve relative to the source file's directory, against repo root
        resolved = os.path.normpath(os.path.join(srcdir, target))
        if resolved in PATHMAP:
            return f'href="{PATHMAP[resolved]}{anchor}"'
        if target.endswith(".md"):  # un-ported doc -> GitHub source
            return f'href="{GH_BLOB}{resolved}{anchor}"'
        return m.group(0)

    return rewrite


def build_nav(section, active):
    out = []
    for href, label in NAVS[section]:
        cls = ' class="active"' if href == active else ""
        out.append(f'<a href="{href}"{cls}>{html.escape(label)}</a>')
    return "".join(out)


def first_paragraph(md_text):
    for line in md_text.splitlines():
        s = line.strip()
        if s and not s.startswith("#") and not s.startswith("```"):
            return re.sub(r"[`*\[\]]", "", s)[:155]
    return "Eskiu language documentation."


def main():
    os.makedirs(OUT, exist_ok=True)
    md = markdown.Markdown(
        extensions=["extra", "sane_lists", "toc"],
        output_format="html5",
    )
    for src, out, title, section in PAGES:
        srcdir = os.path.dirname(src)
        with open(os.path.join(ROOT, src), encoding="utf-8") as f:
            text = f.read()
        md.reset()
        body = md.convert(text)
        body = re.sub(r'href="([^"]+)"', make_rewriter(srcdir), body)
        body = body.replace("<table>", '<div class="tablewrap"><table>').replace(
            "</table>", "</table></div>"
        )
        page = PAGE.format(
            title=html.escape(title),
            desc=html.escape(first_paragraph(text)),
            out=out,
            css=CSS,
            nav=build_nav(section, out),
            body=body,
        )
        with open(os.path.join(OUT, out), "w", encoding="utf-8") as f:
            f.write(page)
        print(f"  {src}  ->  site/docs/{out}")
    print(f"Done. {len(PAGES)} pages in site/docs/")


if __name__ == "__main__":
    main()
