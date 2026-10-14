#!/usr/bin/env python3
"""The GLSL out of Source/GUI/TapeScene.cpp, into Source/GUI/TapeSceneShaders.h.

The deck's two shader programs are 335 lines of string literals - DATA, written in
the oldest GLSL dialect that runs everywhere - and they sat in the middle of the
file's scene namespace, between the geometry that feeds them and the preparation
that compiles them. They move to a header of their own, verbatim: the payload
comment above them, both sources, and the TAPE_SCENE_HIGHP macro they are pasted
through, which has to travel with them because the strings name it at runtime and
the shrink it to the precision literal is a fact about the shader text.

Nothing cuts these strings and nothing measures them, so the only cost is the two
things a header changes: the sources are `inline` in a named namespace (they were
one copy per translation unit with internal linkage), and the two call sites in
TapeScene.cpp say where they come from.

Nothing else about the file changes. The macro is still visible in TapeScene.cpp -
it includes the header - which is what keeps prepare() compiling.
"""

from __future__ import annotations

import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
SOURCE = REPO / "Source" / "GUI" / "TapeScene.cpp"

MACRO_BLOCK = '''// JUCE_HIGHP expands to nothing on desktop GL and to "highp" on GLES, so it
// cannot be written into the shader strings as an argument: pasting an empty
// macro into a function call is not valid C++ and would not compile. This picks
// the literal instead, and the shader text stays readable as shader text.
#if defined (JUCE_OPENGL_ES) && JUCE_OPENGL_ES
 #define TAPE_SCENE_HIGHP "highp"
#else
 #define TAPE_SCENE_HIGHP ""
#endif
'''

SHADER_END = '''        "    gl_FragColor = vec4 (colour, 1.0);\\n"
        "}\\n";
'''


def banner_start(text: str, title: str) -> int:
    if text.count(title) != 1:
        raise SystemExit(f"split_tapescene_shaders: `{title.strip()}` appears "
                         f"{text.count(title)} times")
    position = text.rindex("\n", 0, text.index(title)) + 1

    while position > 0:
        previous_end = position - 1
        previous_start = text.rindex("\n", 0, previous_end) + 1
        line = text[previous_start:previous_end].strip()

        if line.startswith("//") and len(line) > 12 and set(line[2:].strip()) == {"="}:
            return previous_start

        if not line.startswith("//"):
            break

        position = previous_start

    return position


def dedent(block: str, width: int = 4) -> str:
    return "\n".join(line[width:] if line.startswith(" " * width) else line
                     for line in block.splitlines()) + "\n"


def main() -> int:
    src = SOURCE.read_text()

    if src.count(MACRO_BLOCK) != 1:
        raise SystemExit("split_tapescene_shaders: the precision macro block is not "
                         "where it was")
    if src.count(SHADER_END) != 1:
        raise SystemExit("split_tapescene_shaders: the end of the fragment shader is not "
                         "unique any more")

    glsl_start = banner_start(src, "    //  GLSL\n")
    glsl_end = src.index(SHADER_END) + len(SHADER_END)
    glsl = src[glsl_start:glsl_end]

    src = src.replace(glsl, "", 1)
    src = src.replace(MACRO_BLOCK, "", 1)

    # The two sites in this file now say where the sources come from, the way
    # J37LookAndFeel.cpp does for the paint kit's names.
    if src.count("using namespace juce::gl;\n") != 1:
        raise SystemExit("split_tapescene_shaders: the juce::gl using-declaration moved")
    src = src.replace("using namespace juce::gl;\n",
                      "using namespace juce::gl;\n"
                      "\n"
                      "// The deck's shader sources, which were the middle of this file's\n"
                      "// scene namespace until they moved to a header of their own.\n"
                      "using j37scene::fragmentShaderSource;\n"
                      "using j37scene::vertexShaderSource;\n", 1)

    if src.count('#include "TapeScene.h"\n') != 1:
        raise SystemExit("split_tapescene_shaders: TapeScene.h is not included exactly once")
    src = src.replace('#include "TapeScene.h"\n',
                      '#include "TapeScene.h"\n'
                      '#include "TapeSceneShaders.h"\n', 1)

    if src == SOURCE.read_text():
        raise SystemExit("split_tapescene_shaders: nothing changed")

    shaders = SHADERS_BANNER + MACRO_BLOCK + "\nnamespace j37scene\n{\n" + dedent(glsl) + "}\n"
    (REPO / "Source" / "GUI" / "TapeSceneShaders.h").write_text(shaders)
    SOURCE.write_text(src)

    print(f"wrote Source/GUI/TapeSceneShaders.h: {len(shaders.splitlines())} lines")
    print(f"TapeScene.cpp: 2860 -> {len(src.splitlines())} lines")
    return 0


SHADERS_BANNER = """// =============================================================================
//  The deck's shader programs: the vertex source, the fragment source, and the
//  precision macro the two of them are pasted through.
//
//  Moved out of TapeScene.cpp - prose, sources and the macro, verbatim - because
//  they are DATA: 335 lines of GLSL string literals that were sitting in the
//  middle of the scene namespace, between the geometry that feeds them and the
//  preparation that compiles them. Nothing cuts them, nothing measures them, and
//  reading the file for the deck's arithmetic no longer means stepping over them.
//
//  What the move cost is the two things a header changes:
//
//    * The two sources are `inline`, in a namespace of their own. In the .cpp they
//      were file-local objects - one copy per translation unit, internal linkage -
//      and that is what a header cannot keep quietly, even though a copy of a
//      `const char* const` is harmless here: the rule Source/GUI/PanelPaint.h
//      states applies to data as much as to state, and `inline` is one word.
//    * The TAPE_SCENE_HIGHP macro travelled with them rather than staying behind.
//      The strings NAME it - prepare() in TapeScene.cpp does a runtime
//      `.replace ("TAPE_SCENE_HIGHP", TAPE_SCENE_HIGHP)` on the compiled text, so
//      it is a fact about the shader text and belongs beside it. TapeScene.cpp
//      includes this header, so the macro is still defined where it is used.
//
//  The dialect is deliberate and stated in the banner below: JUCE's addShader()
//  hands the text to the driver verbatim, so this has to be GLSL that both a
//  legacy context and JUCE's own v3 translators accept.
// =============================================================================
#pragma once

"""


if __name__ == "__main__":
    sys.exit(main())
