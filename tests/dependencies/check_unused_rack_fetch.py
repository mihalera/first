#!/usr/bin/env python3
"""Prevent reintroducing the unused Rack checkout and its nested GitLab fetch."""

from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
dependencies = (root / "cmake/J37Dependencies.cmake").read_text(encoding="utf-8")
dependencies = re.sub(r"(?m)^\s*#.*$", "", dependencies)

problems = []

if re.search(r"CPMAddPackage\s*\(\s*NAME\s+rack\b", dependencies, re.IGNORECASE):
    problems.append(
        "Rack has no caller under Source/; fetching its unused SDK also fetches "
        "speexdsp from GitLab and can fail every platform configure"
    )

if re.search(r"j37_declare_header_only_library\s*\(\s*rack_dsp\b",
             dependencies, re.IGNORECASE):
    problems.append("rack_dsp is declared without a source caller")

if problems:
    for problem in problems:
        print(f"check_unused_rack_fetch: {problem}", file=sys.stderr)
    raise SystemExit(1)

print("check_unused_rack_fetch: unused Rack package is not fetched")
