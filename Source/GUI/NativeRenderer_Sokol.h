#pragma once
#include "NativeRenderer.h"

namespace j37::render {
std::unique_ptr<NativeRenderer> createSokolRenderer();
} // namespace j37::render
