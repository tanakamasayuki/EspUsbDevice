#!/usr/bin/env python3
"""Check every relative link and #anchor in the repository's Markdown.

The docs cross-reference each other constantly - the guides link into each
other's sections, the READMEs link into the guides, the change-request record
links into both - and every one of those links is a heading slug that nothing
verifies at build time. Renaming a heading silently breaks them. This walks all
Markdown outside `third_party/`, resolves each relative path, and for a link
with an `#anchor` checks that the target file actually has a heading that slugs
to it.

Exits non-zero when something is broken, so it can gate a release. Run it after
editing headings, after adding a document, and before tagging - see
`docs/RELEASE_CHECKLIST.md`.

  python3 tools/check_doc_links.py           # whole repository
  python3 tools/check_doc_links.py docs      # one subtree

Slug rules follow GitHub's: lower-case, spaces to hyphens, punctuation dropped,
inline code and emphasis stripped first, duplicate headings suffixed `-1`, `-2`.
Non-ASCII survives, which is what makes the Japanese documents' anchors work.

**`_` is not emphasis here.** Stripping `_..._` as emphasis eats the underscores
inside identifiers, so `### 2.5 \x60usb_persist_restart()\x60 ...` slugs to
`...-usbpersistrestart-...` and every correct link to it reads as broken. That
bug was written three times while this lived outside the repository, which is
why the file is here now, and why only `*` is treated as emphasis below.
"""

from __future__ import annotations

import re
import sys
import unicodedata
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {".git", "third_party", "node_modules", "__pycache__", ".venv", "build"}

# Markdown inline link, ignoring images. The label may contain one nested [].
LINK_RE = re.compile(r'(?<!\!)\[(?:[^\]\[]|\[[^\]]*\])*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
ATX_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE_RE = re.compile(r"^\s*(```|~~~)")
EXPLICIT_ANCHOR_RE = re.compile(r'(?:name|id)="([^"]+)"')


def slug(text: str) -> str:
    """GitHub's heading slug for one heading's text."""
    text = re.sub(r"`([^`]*)`", r"\1", text)            # inline code
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # links keep their label
    text = re.sub(r"<[^>]+>", "", text)                 # inline HTML
    text = re.sub(r"\*{1,3}(.+?)\*{1,3}", r"\1", text)  # emphasis - '*' only, see module docstring
    out = []
    for ch in text.strip().lower():
        if ch.isspace():
            out.append("-")
        elif ch in "-_":
            out.append(ch)
        elif unicodedata.category(ch)[0] in ("L", "N", "M"):
            out.append(ch)
        # everything else (punctuation, symbols) is dropped
    return "".join(out)


def strip_fences(lines):
    """Yield (line_number, line) for lines outside fenced code blocks."""
    fence = None
    for number, line in enumerate(lines, 1):
        match = FENCE_RE.match(line)
        if match:
            if fence is None:
                fence = match.group(1)
            elif line.strip().startswith(fence):
                fence = None
            continue
        if fence is None:
            yield number, line


def anchors(path: Path) -> set[str]:
    """Every anchor a link may target in this file."""
    text = path.read_text(encoding="utf-8")
    found: set[str] = set()
    seen: dict[str, int] = {}
    for _, line in strip_fences(text.splitlines()):
        heading = ATX_RE.match(line)
        if not heading:
            continue
        base = slug(heading.group(2))
        count = seen.get(base, 0)
        seen[base] = count + 1
        found.add(base if count == 0 else f"{base}-{count}")
        found.add(base)
    found.update(EXPLICIT_ANCHOR_RE.findall(text))
    return found


def markdown_files(base: Path):
    for path in sorted(base.rglob("*.md")):
        if not any(part in SKIP_DIRS for part in path.parts):
            yield path


def main() -> int:
    base = ROOT if len(sys.argv) < 2 else (ROOT / sys.argv[1]).resolve()
    if not base.exists():
        print(f"no such path: {base}", file=sys.stderr)
        return 2

    cache: dict[Path, set[str]] = {}
    broken: list[str] = []
    files = list(markdown_files(base))

    for md in files:
        for number, line in strip_fences(md.read_text(encoding="utf-8").splitlines()):
            for match in LINK_RE.finditer(line):
                target = match.group(1)
                if target.startswith(("http://", "https://", "mailto:", "#!")):
                    continue
                path_part, _, anchor = target.partition("#")
                path_part = urllib.parse.unquote(path_part)
                anchor = urllib.parse.unquote(anchor)

                dest = (md.parent / path_part).resolve() if path_part else md
                if path_part and not dest.exists():
                    broken.append(f"{md.relative_to(ROOT)}:{number}: missing path -> {target}")
                    continue
                if anchor and dest.suffix == ".md":
                    if dest not in cache:
                        cache[dest] = anchors(dest)
                    if anchor.lower() not in {a.lower() for a in cache[dest]}:
                        broken.append(f"{md.relative_to(ROOT)}:{number}: missing anchor -> {target}")

    for line in broken:
        print(line)
    print(f"{len(broken)} broken link(s) across {len(files)} markdown files")
    return 1 if broken else 0


if __name__ == "__main__":
    raise SystemExit(main())
