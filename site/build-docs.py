#!/usr/bin/env python3
"""
Generate the site's docs pages from the Markdown sources in docs/.

The repo is private, so the public can't read the docs on GitHub. This renders
docs/lang/*, docs/API.md and docs/GLOSSARY.md into self-contained, styled HTML
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

# source (relative to repo root) -> output basename, page title
PAGES = [
    ("docs/lang/index.md",            "index.html",           "Documentation"),
    ("docs/lang/getting-started.md",  "getting-started.html", "Getting started"),
    ("docs/lang/spec.md",             "spec.html",            "Language spec"),
    ("docs/lang/grammar.md",          "grammar.html",         "Grammar"),
    ("docs/lang/build.md",            "build.html",           "Building & tooling"),
    ("docs/API.md",                   "api.html",             "Compiler C++ API"),
    ("docs/GLOSSARY.md",              "glossary.html",        "Glossary"),
]

# nav shown at the top of every docs page
NAV = [
    ("index.html", "Overview"),
    ("getting-started.html", "Getting started"),
    ("spec.html", "Spec"),
    ("grammar.html", "Grammar"),
    ("build.html", "Tooling"),
    ("api.html", "Compiler API"),
    ("glossary.html", "Glossary"),
]

# any *.md link is rewritten to one of these by basename; unknown -> GitHub
LINK_MAP = {
    "index.md": "index.html",
    "getting-started.md": "getting-started.html",
    "spec.md": "spec.html",
    "grammar.md": "grammar.html",
    "build.md": "build.html",
    "API.md": "api.html",
    "GLOSSARY.md": "glossary.html",
}
GH_BLOB = "https://github.com/doranteseduardo/eskiu/blob/main/docs/"

CSS = """
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
:root {
  --bg: #f9f7f3; --surface: #f2efe8; --border: #ddd8ce;
  --ink: #1b1915; --ink2: #6b6560; --purple: #4c31bb; --purple-lt: #6448d4;
  --code: #1b1728; --code-bd: #2c2444;
  --sans: "Space Grotesk", system-ui, -apple-system, sans-serif;
  --mono: "JetBrains Mono", Menlo, monospace;
}
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


def rewrite_link(m):
    target = m.group(1)
    # split off any #anchor
    anchor = ""
    if "#" in target:
        target, anchor = target.split("#", 1)
        anchor = "#" + anchor
    if not target:  # pure in-page anchor
        return f'href="{anchor}"'
    base = os.path.basename(target)
    if base in LINK_MAP:
        return f'href="{LINK_MAP[base]}{anchor}"'
    # an un-ported doc (e.g. dev/*.md) — fall back to the GitHub source
    if base.endswith(".md"):
        rel = target.replace("../", "")
        return f'href="{GH_BLOB}{rel}{anchor}"'
    return m.group(0)


def build_nav(active):
    out = []
    for href, label in NAV:
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
    for src, out, title in PAGES:
        with open(os.path.join(ROOT, src), encoding="utf-8") as f:
            text = f.read()
        md.reset()
        body = md.convert(text)
        # rewrite .md cross-links
        body = re.sub(r'href="([^"]+)"', rewrite_link, body)
        # wrap tables so they scroll on narrow screens
        body = body.replace("<table>", '<div class="tablewrap"><table>').replace(
            "</table>", "</table></div>"
        )
        page = PAGE.format(
            title=html.escape(title),
            desc=html.escape(first_paragraph(text)),
            out=out,
            css=CSS,
            nav=build_nav(out),
            body=body,
        )
        with open(os.path.join(OUT, out), "w", encoding="utf-8") as f:
            f.write(page)
        print(f"  {src}  ->  site/docs/{out}")
    print(f"Done. {len(PAGES)} pages in site/docs/")


if __name__ == "__main__":
    main()
