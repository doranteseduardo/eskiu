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

# Shown in the shared top bar (kept in step with the homepage). Bump per release.
VERSION = "v0.6.2"
GH = "https://github.com/doranteseduardo/eskiu"

CSS = """
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
:root {
  --bg: #ffffff; --surface: #f6f8fa; --border: #d0d7de; --border-mut: #e4e8ed;
  --ink: #1f2328; --ink2: #424a53; --muted: #6e7781;
  --accent: #4c31bb; --accent-lt: #6448d4;
  --inline: rgba(120, 110, 180, 0.12);
  --sans: -apple-system, BlinkMacSystemFont, "Segoe UI", "Noto Sans", Helvetica, Arial, sans-serif;
  --mono: ui-monospace, "SF Mono", "SFMono-Regular", Menlo, Consolas, "Liberation Mono", monospace;
}
html { scroll-behavior: smooth; -webkit-text-size-adjust: 100%; text-size-adjust: 100%; }
body { background: var(--bg); color: var(--ink); font-family: var(--sans); font-size: 16px;
       line-height: 1.7; -webkit-font-smoothing: antialiased; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }

/* Top bar (shared with the homepage) */
header.top { border-bottom: 1px solid var(--border); background: var(--bg); position: sticky; top: 0; z-index: 30; }
.top-in { max-width: 860px; margin: 0 auto; padding: 0 1.25rem; height: 54px; display: flex; align-items: center; gap: 0.55rem; }
.brand { display: flex; align-items: center; gap: 0.5rem; font-family: var(--mono); font-weight: 700; font-size: 1.02rem; color: var(--ink); }
.brand:hover { text-decoration: none; }
.brand img { width: 24px; height: 24px; }
.brand .v { font-family: var(--mono); font-size: 0.68rem; color: var(--muted); font-weight: 500; }
.top-links { margin-left: auto; display: flex; align-items: center; gap: 1.25rem; font-family: var(--mono); font-size: 0.82rem; }
.top-links a { color: var(--ink2); }
.top-links a:hover { color: var(--accent); text-decoration: none; }

.wrap { max-width: 860px; margin: 0 auto; padding: 2.5rem 1.25rem 4rem; }
.docnav { display: flex; flex-wrap: wrap; gap: 0.35rem 0.4rem; margin-bottom: 2.5rem;
          padding-bottom: 1.25rem; border-bottom: 1px solid var(--border); }
.docnav a { font-family: var(--mono); font-size: 0.78rem; color: var(--ink2);
            padding: 0.25rem 0.6rem; border-radius: 6px; border: 1px solid transparent; }
.docnav a:hover { color: var(--accent); background: var(--surface); text-decoration: none; }
.docnav a.active { color: var(--accent); border-color: var(--border); background: var(--surface); font-weight: 600; }
.content h1 { font-family: var(--mono); font-size: clamp(1.6rem, 4.5vw, 2.2rem); font-weight: 700;
              letter-spacing: -0.03em; line-height: 1.15; margin: 0 0 1.25rem; }
.content h2 { font-family: var(--mono); font-size: 1.4rem; font-weight: 700; letter-spacing: -0.01em;
              margin: 2.75rem 0 1rem; padding-bottom: 0.4rem; border-bottom: 1px solid var(--border); }
.content h3 { font-family: var(--mono); font-size: 1.08rem; font-weight: 600; margin: 1.9rem 0 0.6rem; }
.content h4 { font-family: var(--mono); font-size: 0.95rem; margin: 1.4rem 0 0.5rem; color: var(--ink2); }
.content p { margin: 0 0 1rem; }
.content ul, .content ol { margin: 0 0 1rem 1.3rem; }
.content li { margin-bottom: 0.4rem; color: var(--ink2); }
.content strong { font-weight: 600; color: var(--ink); }
.content hr { border: none; border-top: 1px solid var(--border); margin: 2.25rem 0; }
.content blockquote { border-left: 3px solid var(--accent); background: var(--surface);
                      padding: 0.8rem 1.1rem; border-radius: 0 8px 8px 0; margin: 0 0 1.25rem;
                      color: var(--ink2); }
.content blockquote p:last-child { margin-bottom: 0; }
.content code { font-family: var(--mono); font-size: 0.86em; background: var(--inline);
                padding: 0.15em 0.4em; border-radius: 4px; }
.content pre { background: var(--surface); border: 1px solid var(--border); border-radius: 8px;
               padding: 0.9rem 1.1rem; margin: 0 0 1.5rem; overflow-x: auto;
               font-family: var(--mono); font-size: 0.8rem; line-height: 1.7; color: var(--ink); }
.content pre code { background: none; padding: 0; font-size: inherit; color: inherit; }
.tablewrap { overflow-x: auto; margin: 0 0 1.5rem; border: 1px solid var(--border); border-radius: 8px; }
.content table { width: 100%; border-collapse: collapse; font-size: 0.88rem; }
.content th, .content td { text-align: left; padding: 0.55rem 0.85rem;
                          border-bottom: 1px solid var(--border-mut); vertical-align: top; }
.content th { font-family: var(--mono); font-size: 0.72rem; text-transform: uppercase;
              letter-spacing: 0.05em; color: var(--muted); background: var(--surface); white-space: nowrap; }
.content tbody tr:last-child td { border-bottom: none; }
.content td code { white-space: nowrap; }

/* Footer (shared with the homepage) */
footer { border-top: 1px solid var(--border); margin-top: 3.5rem; }
.footer-in { max-width: 860px; margin: 0 auto; padding: 1.5rem 1.25rem; display: flex; align-items: center;
             gap: 1.25rem; flex-wrap: wrap; font-family: var(--mono); font-size: 0.78rem; }
.footer-in .fl { display: flex; gap: 1.1rem; flex-wrap: wrap; }
.footer-in a { color: var(--ink2); }
.footer-in a:hover { color: var(--accent); text-decoration: none; }
.footer-in .cp { margin-left: auto; color: var(--muted); }
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
  <style>{css}</style>
</head>
<body>
  <header class="top">
    <div class="top-in">
      <a class="brand" href="../index.html"><img src="../../assets/logo.png" alt="Eskiu" />eskiu <span class="v">{version}</span></a>
      <nav class="top-links">
        <a href="index.html">docs</a>
        <a href="../the-book-of-eskiu.html">book</a>
        <a href="{gh}">github</a>
      </nav>
    </div>
  </header>
  <div class="wrap">
    <nav class="docnav">{nav}</nav>
    <div class="content">
{body}
    </div>
  </div>
  <footer>
    <div class="footer-in">
      <div class="fl">
        <a href="../the-book-of-eskiu.html">book</a>
        <a href="index.html">docs</a>
        <a href="internals.html">internals</a>
        <a href="../changelog.html">changelog</a>
        <a href="{gh}">github</a>
      </div>
      <span class="cp">MIT · Eduardo Dorantes</span>
    </div>
  </footer>
</body>
</html>
"""


# Top-level standalone pages (siblings of index.html, not under docs/). The repo
# is private, so the site can't link the CHANGELOG on GitHub; this mirrors it.
PAGE_TOP = """<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>{title} · Eskiu</title>
  <meta name="description" content="{desc}" />
  <link rel="canonical" href="https://eskiu-lang.org/{out}" />
  <link rel="icon" type="image/png" href="assets/logo.png" />
  <style>{css}</style>
</head>
<body>
  <header class="top">
    <div class="top-in">
      <a class="brand" href="index.html"><img src="assets/logo.png" alt="Eskiu" />eskiu <span class="v">{version}</span></a>
      <nav class="top-links">
        <a href="docs/index.html">docs</a>
        <a href="the-book-of-eskiu.html">book</a>
        <a href="{gh}">github</a>
      </nav>
    </div>
  </header>
  <div class="wrap">
    <nav class="docnav">{nav}</nav>
    <div class="content">
{body}
    </div>
  </div>
  <footer>
    <div class="footer-in">
      <div class="fl">
        <a href="the-book-of-eskiu.html">book</a>
        <a href="docs/index.html">docs</a>
        <a href="docs/internals.html">internals</a>
        <a href="changelog.html">changelog</a>
        <a href="{gh}">github</a>
      </div>
      <span class="cp">MIT · Eduardo Dorantes</span>
    </div>
  </footer>
</body>
</html>
"""

TOP_NAV = [
    ("index.html", "Home"),
    ("docs/index.html", "Docs"),
    ("the-book-of-eskiu.html", "Book"),
    ("quickstart.html", "Quickstart"),
    ("changelog.html", "Changelog"),
]


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


def build_top_nav(active):
    out = []
    for href, label in TOP_NAV:
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
            version=VERSION,
            gh=GH,
            nav=build_nav(section, out),
            body=body,
        )
        with open(os.path.join(OUT, out), "w", encoding="utf-8") as f:
            f.write(page)
        print(f"  {src}  ->  site/docs/{out}")

    # Top-level: mirror the CHANGELOG so the site never links the (private) GitHub.
    with open(os.path.join(ROOT, "CHANGELOG.md"), encoding="utf-8") as f:
        cl_text = f.read()
    md.reset()
    cl_body = md.convert(cl_text)
    cl_body = cl_body.replace("<table>", '<div class="tablewrap"><table>').replace(
        "</table>", "</table></div>"
    )
    cl_page = PAGE_TOP.format(
        title="Changelog",
        desc=html.escape(first_paragraph(cl_text)),
        out="changelog.html",
        css=CSS,
        version=VERSION,
        gh=GH,
        nav=build_top_nav("changelog.html"),
        body=cl_body,
    )
    with open(os.path.join(ROOT, "site", "changelog.html"), "w", encoding="utf-8") as f:
        f.write(cl_page)
    print("  CHANGELOG.md  ->  site/changelog.html")

    print(f"Done. {len(PAGES)} docs pages + changelog.html")


if __name__ == "__main__":
    main()
