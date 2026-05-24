#pragma once

#include <memory>

#include "ultramodern/renderer_context.hpp"

namespace paper_mario::renderer {
    std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
        uint8_t* rdram,
        ultramodern::renderer::WindowHandle window_handle,
        bool developer_mode);
}
