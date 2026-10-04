#pragma once
#include "NativeRenderer.h"

namespace j37::render {
std::unique_ptr<NativeRenderer> createD3D11Renderer();
}
