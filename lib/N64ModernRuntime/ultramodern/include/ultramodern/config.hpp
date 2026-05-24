#ifndef __CONFIG_HPP__
#define __CONFIG_HPP__

#include <string>
#include <optional>

#include "json/json.hpp"

namespace ultramodern {
    namespace renderer {
        enum class Resolution {
            Original,
            Original2x,
            Auto,
            Manual,
            OptionCount
        };
        enum class WindowMode {
            Windowed,
            Fullscreen,
            OptionCount
        };
        enum class HUDRatioMode {
            Original,
            Clamp16x9,
            Full,
            OptionCount
        };
        enum class GraphicsApi {
            Auto,
            D3D12,
            Vulkan,
            Metal,
            OptionCount
        };
        enum class AspectRatio {
            Original,
            Expand,
            Manual,
            OptionCount
        };
        enum class Antialiasing {
            None,
            MSAA2X,
            MSAA4X,
            MSAA8X,
            OptionCount
        };
        enum class RefreshRate {
            Original,
            Display,
            Manual,
            OptionCount
        };
        enum class HighPrecisionFramebuffer {
            Auto,
            On,
            Off,
            OptionCount
        };
        enum class TextureFiltering {
            Nearest,
            Linear,
            PixelScaling,
            OptionCount
        };
        enum class DisplayBuffering {
            Double,
            Triple,
            OptionCount
        };
        enum class Upscale2D {
            Original,
            ScaledOnly,
            All,
            OptionCount
        };
        enum class HardwareResolve {
            Auto,
            On,
            Off,
            OptionCount
        };

        class GraphicsConfig {
        public:
            bool developer_mode = false;
            Resolution res_option = Resolution::Auto;
            WindowMode wm_option = WindowMode::Windowed;
            HUDRatioMode hr_option = HUDRatioMode::Original;
            GraphicsApi api_option = GraphicsApi::Auto;
            AspectRatio ar_option = AspectRatio::Original;
            Antialiasing msaa_option = Antialiasing::None;
            RefreshRate rr_option = RefreshRate::Original;
            HighPrecisionFramebuffer hpfb_option = HighPrecisionFramebuffer::Auto;
            TextureFiltering filtering_option = TextureFiltering::PixelScaling;
            DisplayBuffering display_buffering = DisplayBuffering::Triple;
            Upscale2D upscale_2d = Upscale2D::ScaledOnly;
            HardwareResolve hardware_resolve = HardwareResolve::Auto;
            bool three_point_filtering = true;
            int rr_manual_value = 60;
            int ds_option = 1;
            int anisotropic_filtering = 16;
            double resolution_multiplier = 2.0;

            virtual ~GraphicsConfig() = default;

            auto operator<=>(const GraphicsConfig& rhs) const = default;
        };

        const GraphicsConfig& get_graphics_config();
        void set_graphics_config(const GraphicsConfig& new_config);

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::Resolution, {
            {ultramodern::renderer::Resolution::Original, "Original"},
            {ultramodern::renderer::Resolution::Original2x, "Original2x"},
            {ultramodern::renderer::Resolution::Auto, "Auto"},
            {ultramodern::renderer::Resolution::Manual, "Manual"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::WindowMode, {
            {ultramodern::renderer::WindowMode::Windowed, "Windowed"},
            {ultramodern::renderer::WindowMode::Fullscreen, "Fullscreen"}
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::HUDRatioMode, {
            {ultramodern::renderer::HUDRatioMode::Original, "Original"},
            {ultramodern::renderer::HUDRatioMode::Clamp16x9, "Clamp16x9"},
            {ultramodern::renderer::HUDRatioMode::Full, "Full"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::GraphicsApi, {
            {ultramodern::renderer::GraphicsApi::Auto, "Auto"},
            {ultramodern::renderer::GraphicsApi::D3D12, "D3D12"},
            {ultramodern::renderer::GraphicsApi::Vulkan, "Vulkan"},
            {ultramodern::renderer::GraphicsApi::Metal, "Metal"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::AspectRatio, {
            {ultramodern::renderer::AspectRatio::Original, "Original"},
            {ultramodern::renderer::AspectRatio::Expand, "Expand"},
            {ultramodern::renderer::AspectRatio::Manual, "Manual"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::Antialiasing, {
            {ultramodern::renderer::Antialiasing::None, "None"},
            {ultramodern::renderer::Antialiasing::MSAA2X, "MSAA2X"},
            {ultramodern::renderer::Antialiasing::MSAA4X, "MSAA4X"},
            {ultramodern::renderer::Antialiasing::MSAA8X, "MSAA8X"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::RefreshRate, {
            {ultramodern::renderer::RefreshRate::Original, "Original"},
            {ultramodern::renderer::RefreshRate::Display, "Display"},
            {ultramodern::renderer::RefreshRate::Manual, "Manual"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::HighPrecisionFramebuffer, {
            {ultramodern::renderer::HighPrecisionFramebuffer::Auto, "Auto"},
            {ultramodern::renderer::HighPrecisionFramebuffer::On, "On"},
            {ultramodern::renderer::HighPrecisionFramebuffer::Off, "Off"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::TextureFiltering, {
            {ultramodern::renderer::TextureFiltering::Nearest, "Nearest"},
            {ultramodern::renderer::TextureFiltering::Linear, "Linear"},
            {ultramodern::renderer::TextureFiltering::PixelScaling, "PixelScaling"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::DisplayBuffering, {
            {ultramodern::renderer::DisplayBuffering::Double, "Double"},
            {ultramodern::renderer::DisplayBuffering::Triple, "Triple"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::Upscale2D, {
            {ultramodern::renderer::Upscale2D::Original, "Original"},
            {ultramodern::renderer::Upscale2D::ScaledOnly, "ScaledOnly"},
            {ultramodern::renderer::Upscale2D::All, "All"},
        });

        NLOHMANN_JSON_SERIALIZE_ENUM(ultramodern::renderer::HardwareResolve, {
            {ultramodern::renderer::HardwareResolve::Auto, "Auto"},
            {ultramodern::renderer::HardwareResolve::On, "On"},
            {ultramodern::renderer::HardwareResolve::Off, "Off"},
        });
    }
}

#endif
