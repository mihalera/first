#!/usr/bin/env python3
"""
Checks that every third-party library under ThirdParty/ is a git SUBMODULE.

    python3 tests/dependencies/check_submodules.py

The tree used to hold three kinds of third-party code: submodules, plain
vendored directories (signalsmith-stretch was one), and libraries CPM fetched at
configure time. That is three sources of truth for "which version is this", and
the vendored copies silently drifted from the pins beside them - the CPM block
says 1.4.0 and the checked-in tree is whatever someone last copied in.

The policy now is one kind: a submodule, pinned by the gitlink the superproject
records. This checks it from both ends:

  * every directory under ThirdParty/ is mode 160000 in the index, so it is a
    submodule and not a copy of one;
  * every ThirdParty/ path in .gitmodules is present, and every directory on
    disk is declared, so the two cannot drift in either direction;
  * where a dependency ALSO has a CPM fallback pinned to a full commit, the pin
    and the gitlink are compared. A tag cannot be resolved without the network,
    so those are reported rather than compared - the discipline is stated in
    cmake/J37Dependencies.cmake, and this catches the half of it that can be
    checked offline.

Exit code 0 when the tree is all submodules, 1 otherwise.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CMAKE = os.path.join(ROOT, "cmake", "J37Dependencies.cmake")
GITMODULES = os.path.join(ROOT, ".gitmodules")

problems = []


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)


def parse_gitmodules():
    """path -> url, from .gitmodules. Parsed rather than asked of git so the
    check works on a tree whose submodules were never initialised."""
    entries = {}
    path = None
    url = None
    with open(GITMODULES, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            m = re.match(r"path\s*=\s*(\S+)", line)
            if m:
                path = m.group(1)
                continue
            m = re.match(r"url\s*=\s*(\S+)", line)
            if m:
                url = m.group(1)
                if path is not None:
                    entries[path] = url
                path = None
                url = None
    return entries


entries = parse_gitmodules()

# ---------------------------------------------------------------------------
# 1. Every ThirdParty/ directory is a submodule.
# ---------------------------------------------------------------------------
thirdparty = os.path.join(ROOT, "ThirdParty")
on_disk = sorted(d for d in os.listdir(thirdparty)
                 if os.path.isdir(os.path.join(thirdparty, d)) and not d.startswith("."))

for name in on_disk:
    path = "ThirdParty/" + name
    staged = git("ls-files", "-s", path)
    first = staged.stdout.splitlines()[0].split() if staged.stdout.splitlines() else []
    mode = first[0] if first else ""

    if mode != "160000":
        problems.append("%s is not a submodule (index mode %s) - vendored code must be "
                        "a checkout pinned by a gitlink" % (path, mode or "absent"))

    if path not in entries:
        problems.append("%s is on disk but not declared in .gitmodules" % path)

# The other direction: a declared submodule whose directory is missing would make
# this check vacuous the moment someone deletes ThirdParty/.
for path in sorted(entries):
    if not path.startswith("ThirdParty/"):
        continue
    if not os.path.isdir(os.path.join(ROOT, path)):
        problems.append("%s is declared in .gitmodules but missing on disk" % path)

# ---------------------------------------------------------------------------
# 2. Pin discipline: where a CPM fallback pins a full commit, it must be the
#    commit the submodule records.
# ---------------------------------------------------------------------------
with open(CMAKE, encoding="utf-8") as handle:
    cmake_text = handle.read()

# `CPMAddPackage(` blocks, as NAME / GITHUB_REPOSITORY / GIT_TAG triples.
blocks = re.findall(
    r"CPMAddPackage\s*\((.*?)\)", cmake_text, re.S)
pins = {}
for block in blocks:
    name = re.search(r"NAME\s+([\w-]+)", block)
    repo = re.search(r"GITHUB_REPOSITORY\s+([\w.-]+/[\w.-]+)", block)
    tag = re.search(r"GIT_TAG\s+([0-9a-fA-F]{40})", block)
    if name and repo and tag:
        pins[repo.group(1).lower()] = (name.group(1), tag.group(1).lower())

def normalise(url):
    """`https://github.com/floooh/sokol.git` and CPM's `floooh/sokol` are the
    same repository: drop the scheme, the host and the .git suffix on both
    sides so the two can be compared."""
    url = url.strip().lower()
    url = re.sub(r"\.git$", "", url)
    url = re.sub(r"^\w+://", "", url)
    url = re.sub(r"^[^/]+/", "", url)
    return url

compared = 0
for path, url in sorted(entries.items()):
    if not path.startswith("ThirdParty/"):
        continue
    entry = pins.get(normalise(url))
    if entry is None:
        continue
    name, pin = entry
    tree = git("ls-tree", "HEAD", path)
    fields = tree.stdout.split()
    if len(fields) < 3 or fields[0] != "160000":
        continue
    recorded = fields[2].lower()
    compared += 1
    if recorded != pin:
        problems.append("%s: cmake pins %s at %s but the submodule records %s"
                        % (path, name, pin[:12], recorded[:12]))

# ---------------------------------------------------------------------------
# 3. Report.
# ---------------------------------------------------------------------------
print("check_submodules: %d third-party checkout(s) under ThirdParty/, %d declared, "
      "%d commit pin(s) compared with the CMake fallbacks"
      % (len(on_disk), sum(1 for p in entries if p.startswith("ThirdParty/")), compared))

for name in on_disk:
    print("  %-22s %s" % (name, entries.get("ThirdParty/" + name, "<undeclared>")))

if problems:
    print("")
    for problem in problems:
        print("check_submodules: " + problem)
    sys.exit(1)

print("check_submodules: every third-party tree is a submodule and every commit pin agrees")
