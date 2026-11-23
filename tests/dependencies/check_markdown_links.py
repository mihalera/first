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
here: every path in the repository's own markdown - the target of an inline
`[...](path)` and the destination of a reference definition `[label]: path`,
which is where the path of a `[text][label]` link actually lives - resolved
against the file that wrote it. Links into JUCE, ThirdParty, the build trees and the
generated graph are not this repository's to police, and anchors are left alone
because GitHub's slug rules are not this check's subject - a link to a file that
does not exist is.

Both spellings matter: a reference definition is exactly the form a broken path
hides in, because the link that uses it carries no path at all.

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

#  An inline link: `](target)` or `](target "title")`, with the target optionally
#  wrapped in angle brackets.
LINK = re.compile(r"\]\(\s*(<[^>\n]+>|[^)\s]+)")

#  A reference definition: `[label]: target`, at most three spaces of indent, the
#  target optionally in angle brackets and optionally followed by a title. The
#  link that uses it (`[text][label]`, `[label][]` or `[label]`) carries no path of
#  its own - the definition does - so both forms have to be resolved.
REFERENCE = re.compile(r"^ {0,3}\[([^\]\n]+)\]:[ \t]*(<[^>\n]*>|[^\s]+)", re.M)


def documents():
    """Every markdown file this check owns, as repository-relative paths."""
    for path in glob.glob(os.path.join(ROOT, "**", "*.md"), recursive=True):
        relative = os.path.relpath(path, ROOT).replace("\\", "/")
        top = relative.split("/")[0]
        if top in SKIPPED_DIRECTORIES:
            continue
        yield relative


def unwrapped(raw):
    """The target as Markdown means it: `[x]: <https://example.com>` is a URL."""
    target = raw.strip()

    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1].strip()

    return target


#  Beyond a local path: a destination this repository does not own.
EXTERNAL = ("http://", "https://", "mailto:", "tel:")


problems = []
checked_documents = 0
checked_links = 0

for relative in sorted(documents()):
    checked_documents += 1
    text = open(os.path.join(ROOT, relative), encoding="utf-8").read()
    #  Examples are not links: drop fenced blocks, then inline code spans.
    text = FENCE.sub("", text)
    text = INLINE_CODE.sub("", text)

    #  Inline links, and the destinations of reference definitions, in one pass:
    #  a document may spell a path either way, and both are paths.
    targets = [(target, target) for target in LINK.findall(text)]
    targets += [("[%s]: %s" % (label, target), target)
                for label, target in REFERENCE.findall(text)]

    for shown, raw in targets:
        #  Angle brackets come off before anything is decided: `[x]: <https://example.com>`
        #  is an external destination, not a local path called `<https://example.com>`,
        #  and an empty `<...>` is nothing to check either.
        target = unwrapped(raw)

        if target.startswith(EXTERNAL) or target.startswith("#"):
            continue

        path = target.split("#", 1)[0]

        if not path:
            continue

        checked_links += 1
        resolved = os.path.normpath(os.path.join(os.path.dirname(relative), path))

        if not os.path.exists(os.path.join(ROOT, resolved)):
            problems.append("%s -> %s (resolves to %s, which does not exist)"
                            % (relative, shown, resolved))

print("check_markdown_links: %d document(s), %d relative link(s)" % (checked_documents,
                                                                   checked_links))

if problems:
    for problem in problems:
        print("check_markdown_links: " + problem)
    sys.exit(1)

print("check_markdown_links: every relative link resolves")
