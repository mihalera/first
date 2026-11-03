690 ## Phase 2.1 - Vulkan (the biggest API with the most scaffolding done)
691
692 **State.** 732 lines already own: instance with the Win32 surface extension,
693 device, swapchain, per-frame command buffers and fences, transfer barriers for
694 staging uploads, `SetWindowPos` resize, a recovery command path. Missing: render
695 pass, depth buffer, graphics pipeline, shaders, vertex/index buffers,
696 per-draw uniforms, `drawScene`.
697
698 **What.**
699 1. **Shaders: GLSL 450 -> SPIR-V, compiled at build time** with
700    `glslangValidator` (`find_program` in CMake; the Vulkan SDK ships it, and
701    `find_package(Vulkan)` already runs for this backend). The `.spv` is
702    embedded as a generated C array header, exactly the shape
703    `TapeSceneMetalShaders.h` is in. The GLSL **source** stays a string the
704    compiler and `tests/shaders/check_shaders.py` can read; the SPIR-V is data
705    derived from it. Configure-time fallback: if no compiler is found, the
706    backend is not compiled in (`J37_NATIVE_VULKAN` off) and the build warns -
707    the same pattern the CMake platform guards already use, and better than a
708    runtime shaderc dependency the plugin does not otherwise carry.
709
710    Shaders-first deliverable has its own document now:
711    `docs/backend-implementation-plan-vulkan-shaders.md`. It carries the GLSL 450
712    scene shader source (`tests/shaders/VulkanSceneShaders/vert.glsl`,
713    `frag.glsl`), the SPIR-V embed contract header
714    (`Source/GUI/TapeSceneVulkanShaders.h`) and the next-deliverable spec for the
715    pipeline/UBO/mesh/upload/drawScene boundary. The Metal shader
716    (`Source/GUI/TapeSceneMetalShaders.h`, commit `e382d6ea`) is the reference
717    spelling; the Vulkan shaders reproduce it in GLSL 450, not a new scene.
718
719    Same shape as the later DirectX plan, different compilation story: Vulkan
720    goes compile-time (`glslangValidator`, embedded SPIR-V), D3D11 goes runtime
721    (`D3DCompile`, one source) and D3D12 reuses that HLSL. That is why the GLSL
722    450 source and the SPIR-V embed are documented in
723    `docs/backend-implementation-plan-vulkan-shaders.md` and the next Vulkan
724    deliverable spec is kept separate from this one: Vulkan currently wants a
725    toolchain-constrained step before it can draw, and D3D11/D3D12 do not.
726
727    Concrete detail: the D3D11 constant buffer is the Vulkan UBO laid out as a
728    constant buffer, field-for-field against the same `MetalUniforms` the Vulkan
729    plan names. The next deliverable spec in
730    `docs/backend-implementation-plan-vulkan-shaders.md` is therefore a shared
731    reference for the packing, not a Vulkan-only design.
732 2. **Input layout and constant buffer:** the same `SceneVertex` layout the Vulkan
733    plan names is carried into D3D11 as the input layout, and the per-draw constant
734    buffer is the Vulkan UBO laid out as a D3D11 constant buffer field-for-field
735    against `MetalUniforms`. That single representative shape means the Vulkan next
736    deliverable spec in `docs/backend-implementation-plan-vulkan-shaders.md` stays
737    a shared reference for the packing across the two backends, not a Vulkan-only
738    stanza.
739
740 3. **Pipeline and draw:** one render pass (color + depth), one graphics pipeline
741    for all materials, the same mesh upload (`ensureMeshes` by `shared_ptr`
742    identity through a staging buffer) and the same `drawScene` boundary the Vulkan
743    plan specifies, with the same failure and child-surface rules the plan gives every
744    port.
745
746 4. **Gate:** Vulkan flips `J37_NATIVE_VULKAN`; the later D3D11 and D3D12 bits flip
747    the corresponding DirectX gates after their adapters are written.
748
749 **Why first.** Vulkan is the one pending backend that runs on Windows, Linux
750 and macOS and is therefore the backend the plan treats as the first among the
751 four ports, both because of platform reach and because lavapipe makes the same
752 Phase 3 render check possible without the GPU.
753
754 **Cost.** The remaining ~600-800 lines of `NativeRenderer_Vulkan.cpp` today draw a
755 cleared surface; this phase makes the shader path the first thing that runs against
756 them, so the port can land as scene-draw-capable and come back from staged.
757
758 **Verify.** One render pass and one pipeline; shader source and SPIR-V in
759 `docs/backend-implementation-plan-vulkan-shaders.md`; the bridge from the first
760 Vulkan deliverable (GLSL source + SPIR-V embed) to this one (pipeline, UBO, meshes,
761 drawScene) described in the next-deliverable section; contract lines in
762 `check_render_backend.py`; refusal-path record in `NativeFrameSubmission.h` not yet
763 covered by any test.
764
