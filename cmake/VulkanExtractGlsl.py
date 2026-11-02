#!/usr/bin/env python3
"""Extract one GLSL stage from TapeSceneShaders.h for VulkanShaderGen.

Usage:
    python3 cmake/VulkanExtractGlsl.py \
        --input Source/GUI/TapeSceneShaders.h \
        --output build/generated/vulkan/VulkanSceneShaders.vert.glsl \
        --stage vertex

TapeSceneShaders.h currently carries two GLSL sources (vertex + fragment) as
run-on string literals. This helper reads the same header a human or
tests/shaders/check_shaders.py would read, and writes the stage text back out as
a plain GLSL file glslangValidator can compile.

This is a build tool, not part of the shipped source.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


LITERAL = re.compile(r"^\s*\"((?:\\.|[^\\\"])*)\"\s*;?\s*$")
TERMINATOR = re.compile(r"^\s*\"((?:\\.|[^\\\"])*)\"\s*;\s*$")
DECLARATION = re.compile(r"^\s*const char\* const (\w+)\s*=\s*$")


def unescape(text: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch != '\\':
            out.append(ch)
            i += 1
            continue
        if i + 1 >= len(text):
            print("VulkanExtractGlsl: %s ends inside an escape" %
                  text, file=sys.stderr)
            return ''.join(out)
        nxt = text[i + 1]
        table = {'n': '\n', 't': '\t', 'r': '\r', '"': '"',
                 '\\': '\\', "'": "'"}
        out.append(table.get(nxt, nxt))
        i += 2
    return ''.join(out)


def extract_sources(path: Path) -> dict[str, str]:
    lines = path.read_text(encoding='utf-8').splitlines()
    sources: dict[str, list[str]] = {}
    index = 0
    current: str | None = None
    buffer: list[str] = []

    while index < len(lines):
        line = lines[index]
        m = DECLARATION.match(line)
        if m:
            if current is not None:
                sources[current] = buffer
            current = m.group(1)
            buffer = []
            index += 1
            continue

        lm = LITERAL.match(line)
        if lm and current is not None:
            buffer.append(unescape(lm.group(1)))
            index += 1
            continue

        tm = TERMINATOR.match(line)
        if tm and current is not None:
            buffer.append(unescape(tm.group(1)))
            sources[current] = buffer
            current = None
            buffer = []
            index += 1
            continue

        index += 1

    if current is not None:
        sources[current] = buffer

    return {name: ''.join(text) for name, text in sources.items()}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Extract one GLSL stage from TapeSceneShaders.h")
    parser.add_argument("--input", required=True, type=Path,
                        help="TapeSceneShaders.h (or any header with the same shape)")
    parser.add_argument("--output", required=True, type=Path,
                        help="Output .vert/.frag GLSL file")
    parser.add_argument("--stage", required=True,
                        choices=["vertex", "fragment"],
                        help="Which stage to extract.")
    args = parser.parse_args(argv)

    sources = extract_sources(args.input)

    if not sources:
        print("VulkanExtractGlsl: no GLSL sources found in %s" %
              args.input, file=sys.stderr)
        return 1

    # Pick the stage by convention: the first source is the vertex shader, the
    # second is the fragment shader, matching the OpenGL/Sokol pairing in the
    # same header.
    order = list(sources.keys())
    if args.stage == "vertex":
        chosen = order[0] if order else None
    else:
        chosen = order[1] if len(order) > 1 else order[0]

    if chosen is None:
        print("VulkanExtractGlsl: could not find %s source in %s" %
              (args.stage, args.input), file=sys.stderr)
        return 1

    text = sources[chosen]
    if not text:
        print("VulkanExtractGlsl: %s source is empty in %s" %
              (args.stage, args.input), file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding='utf-8')
    print("VulkanExtractGlsl: wrote %s (%d bytes)" %
          (args.output, len(text)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
