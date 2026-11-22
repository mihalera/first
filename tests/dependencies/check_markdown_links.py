#!/usr/bin/env python3
"""
Checks that every RELATIVE markdown link in this repository's own documents
points at a file that exists.

    python3 tests/dependencies/check_markdown_links.py

Why this exists. `THIRD_PARTY_NOTICES.md` moved into `docs/` and kept five
links it had written from the repository root - `docs/plugin-analysis.md` and
friends. From `docs/` those resolve to `docs/docs/...`, which is nowhere: the
licensing audit and the integration plan stopped being reachable from the
notice that cites them. Codex caught it in review, the fix was five paths, and
nothing in the build would have noticed either the breakage or the fix.

A path in a document is the one thing a compiler never sees, so it is checked
here: every `[...](target)` in the repository's own markdown, resolved against
the file that wrote it. Links into JUCE, ThirdParty, the build trees and the
generated graph are not this repository's to police, and anchors are left alone
because GitHub's slug rules are not this check's subject - a link to a file that
does not exist is.

Fenced code blocks and inline code are stripped first: examples of links are
not links, and the harnesses here document paths in both.

Exit code 0 when every relative link resolves, 1 otherwise.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

#  Directories whose markdown is not this repository's prose: the JUCE submodule
#  and the vendored libraries (other people's documents), the build trees, and
#  the generated knowledge graph.
SKIPPED_DIRECTORIES = {
    "JUCE",
    "ThirdParty",
    "build",
    "build-rel",
    "build-tests",
    "build_cpp23",
    "build_cpp26",
    "node_modules",
    "graphify-out",
    ".git",
}

FENCE = re.compile(r"^```.*?^```", re.M | re.S)
INLINE_CODE = re.compile(r"`[^`\n]*`")
LINK = re.compile(r"\]\(([^)\s]+)\)")


def documents():
    """Every markdown file this check owns, as repository-relative paths."""
    for path in glob.glob(os.path.join(ROOT, "**", "*.md"), recursive=True):
        relative = os.path.relpath(path, ROOT).replace("\\", "/")
        top = relative.split("/")[0]
        if top in SKIPPED_DIRECTORIES:
            continue
        yield relative


problems = []
checked_documents = 0
checked_links = 0

for relative in sorted(documents()):
    checked_documents += 1
    text = open(os.path.join(ROOT, relative), encoding="utf-8").read()
    #  Examples are not links: drop fenced blocks, then inline code spans.
    text = FENCE.sub("", text)
    text = INLINE_CODE.sub("", text)

    for target in LINK.findall(text):
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue

        path = target.split("#", 1)[0]

        if not path:
            continue

        checked_links += 1
        resolved = os.path.normpath(os.path.join(os.path.dirname(relative), path))

        if not os.path.exists(os.path.join(ROOT, resolved)):
            problems.append("%s -> %s (resolves to %s, which does not exist)"
                            % (relative, target, resolved))

print("check_markdown_links: %d document(s), %d relative link(s)" % (checked_documents,
                                                                   checked_links))

if problems:
    for problem in problems:
        print("check_markdown_links: " + problem)
    sys.exit(1)

print("check_markdown_links: every relative link resolves")
