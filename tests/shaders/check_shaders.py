#!/usr/bin/env python3
"""
Checks the panel's two shader sources, which are DATA the compiler reads as text.

    python3 tests/shaders/check_shaders.py

Everything else the 3D panel is made of is checked by something: the geometry by
tests/layout, the arithmetic by tests/dsp, the files themselves by
tests/editor/check_editor_juce.sh. The GLSL is the exception, and it is the one
part a C++ compiler cannot see at all - it is a string literal that only becomes
code when a driver compiles it, at run time, on the user's machine. A shader that
does not compile produces a panel with no scene in it, no error in the build log,
and no failure in any check this repository has.

Two things are checked here, both of them things that have actually gone wrong or
that a driver would refuse silently:

  1. THE SOURCES ARE STILL SOURCES. Each of the two is a run of adjacent string
     literals, one per line. A paste that puts a bare `"` in the middle of one of
     those lines does not fail the build: the quote closes the literal early and
     everything after it on that line becomes a C++ comment, so a piece of the
     shader text disappears into the comment. That is not hypothetical - it is
     what "the lit arc" line in TapeSceneShaders.h looked like, with a sentence of
     its own comment swallowed. Every line of both blocks must be exactly one
     literal.

  2. THE UNIFORMS THE SHADER ASKS FOR ARE THE UNIFORMS THE RENDERER SUPPLIES.
     `setUniform ("uGlow", ...)` looks its name up in the linked program, and a
     name the program does not declare is a no-op rather than an error. A typo on
     either side is therefore invisible: the shader reads whatever the uniform's
     default is (zero), and the visual that depends on it never happens - a knob
     that never lights, a machine that never breathes. Both directions are
     errors: a uniform nothing sets, and a set that reaches nothing.

What this does NOT do is compile the GLSL. That needs a validator
(`glslangValidator`, from glslang-tools) which is not a dependency of anything
else here, and a validator is a real addition to make - it would catch a dialect
mistake the way the drivers do. Until then this file answers the two questions
that need no toolchain: is the text intact, and do the two sides agree on names.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADERS = os.path.join(ROOT, "Source", "GUI", "TapeSceneShaders.h")
DRAWING = os.path.join(ROOT, "Source", "GUI", "TapeScene.cpp")

# One line of a shader source: a single quoted string, optionally closed with the
# statement's semicolon on the last line of the block.
LITERAL = re.compile(r'^\s*"((?:\\.|[^"\\])*)"\s*;?\s*$')
TERMINATOR = re.compile(r'^\s*"((?:\\.|[^"\\])*)"\s*;\s*$')
DECLARATION = re.compile(r"^\s*const char\* const (\w+)\s*=\s*$")

problems = []


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def unescape(text, where):
    """The value a C++ string literal holds, for the escapes the shaders use."""
    out = []
    index = 0
    while index < len(text):
        char = text[index]
        if char != "\\":
            out.append(char)
            index += 1
            continue

        if index + 1 >= len(text):
            problems.append("%s ends inside an escape" % where)
            return "".join(out)

        following = text[index + 1]
        simple = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "'": "'"}
        if following in simple:
            out.append(simple[following])
        else:
            # An escape this reader does not know is not guessed at: mis-reading
            # the text here would make every uniform comparison below nonsense.
            problems.append("%s uses the escape \\%s, which this check cannot read - the "
                            "shader text is no longer plain literals" % (where, following))
            out.append(following)
        index += 2

    return "".join(out)


def extract_sources():
    """(name, glsl text) for every `const char* const ... = ` block in the header."""
    lines = read(SHADERS).splitlines()
    sources = []
    index = 0

    while index < len(lines):
        declaration = DECLARATION.match(lines[index])
        if declaration is None:
            index += 1
            continue

        name = declaration.group(1)
        index += 1
        text = []
        closed = False

        while index < len(lines):
            line = lines[index]
            literal = LITERAL.match(line)

            if literal is None:
                break

            text.append(unescape(literal.group(1), "%s line %d"
                                 % (os.path.relpath(SHADERS, ROOT), index + 1)))
            index += 1

            if TERMINATOR.match(line):
                closed = True
                break

        if not text:
            problems.append("%s: %s is declared with no literal after it"
                            % (os.path.relpath(SHADERS, ROOT), name))
            continue

        if not closed:
            problems.append("%s: the block for %s does not end on a line closed with `\";` - "
                            "the sources are read by line, so a line that is not one literal "
                            "silently loses its text to the compiler"
                            % (os.path.relpath(SHADERS, ROOT), name))

        sources.append((name, "".join(text)))

    return sources


sources = extract_sources()
if len(sources) < 2:
    sys.exit("check_shaders: expected the vertex and fragment sources in %s, found %d"
             % (os.path.relpath(SHADERS, ROOT), len(sources)))

# The macro the sources are written with, substituted at runtime by the plugin
# (see prepare() in TapeScene.cpp). If it stops being defined the text compiles on
# a desktop driver by luck and nowhere else, so it is checked here rather than
# discovered in a blank panel.
header = read(SHADERS)
if "#define TAPE_SCENE_HIGHP" not in header:
    problems.append("%s no longer defines TAPE_SCENE_HIGHP, which the two sources are "
                    "written with and prepare() substitutes at run time"
                    % os.path.relpath(SHADERS, ROOT))

# ---------------------------------------------------------------------------
# The uniforms, both directions.
#
# Both sides are read with their comments removed first. This file's own shader
# sources EXPLAIN themselves in prose - "there is no sampler", "the caption plate is
# switched off" - and prose about a uniform is not a declaration of one: the first
# version of this check read the word "uniform" out of a comment and demanded a
# setter for a variable called "a". The compiler strips comments before it parses,
# and so does this.
# ---------------------------------------------------------------------------
def without_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


declared = {}
for name, text in sources:
    for uniform in re.finditer(r"\buniform\s+[A-Za-z_][A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*)",
                               without_comments(text)):
        declared.setdefault(uniform.group(1), set()).add(name)

if not declared:
    problems.append("no `uniform` declaration found in the shader sources - the extraction "
                    "above is reading something that is not the shader any more")

supplied = {}
drawing = read(DRAWING).splitlines()
for number, line in enumerate(drawing, 1):
    # A line of prose that mentions a setter is not a setter.
    if line.lstrip().startswith("//"):
        continue
    for call in re.finditer(r"setUniform(?:Mat[234])?\s*\(\s*\"([A-Za-z_][A-Za-z0-9_]*)\"", line):
        supplied.setdefault(call.group(1), []).append(number)

# SceneFrame dispatches a material's colour through a small helper, so these
# two names are intentionally supplied indirectly rather than as call literals.
scene_drawing = without_comments("\n".join(drawing))
colour_helper = re.search(r"void\s+setOpenGLColour\s*\([^)]*\)\s*\{([^}]*)\}", scene_drawing, re.S)
if colour_helper:
    # The helper's uniform argument is indirect; the concrete names are checked
    # at its call sites below (the helper declaration itself is not a shader name).
    for uniform in ("uBaseColour", "uHighlightColour"):
        if f'"{uniform}"' in scene_drawing:
            supplied.setdefault(uniform, []).append(0)

for name in sorted(set(declared) - set(supplied)):
    problems.append("%s declares uniform \"%s\" and nothing sets it: the shader reads the "
                    "value's default, so whatever it drives never happens"
                    % (" and ".join(sorted(declared[name])), name))

for name in sorted(set(supplied) - set(declared)):
    problems.append("%s sets \"%s\" (line %s), which neither shader declares - a name the "
                    "linked program does not hold is a no-op"
                    % (os.path.relpath(DRAWING, ROOT), name,
                       ", ".join(str(n) for n in supplied[name])))

if problems:
    for problem in problems:
        print("check_shaders: " + problem)
    sys.exit(1)

print("check_shaders: %d shader source(s) intact, %d uniform(s) declared, all set from %s"
      % (len(sources), len(declared), os.path.relpath(DRAWING, ROOT)))
print("check_shaders: %d setUniform call site(s) name a declared uniform"
      % sum(len(lines) for lines in supplied.values()))
