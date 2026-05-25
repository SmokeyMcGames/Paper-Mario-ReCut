#include "paper_rt64_context.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Unknwn.h>
#include <ObjIdl.h>
#include <OleAuto.h>
#endif

#ifndef HLSL_CPU
#define HLSL_CPU
#endif
#include "hle/rt64_application.h"
#include "hle/rt64_state.h"

#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

namespace {
    uint8_t dmem[0x1000];
    uint8_t imem[0x1000];

    unsigned int MI_INTR_REG = 0;
    unsigned int DPC_START_REG = 0;
    unsigned int DPC_END_REG = 0;
    unsigned int DPC_CURRENT_REG = 0;
    unsigned int DPC_STATUS_REG = 0;
    unsigned int DPC_CLOCK_REG = 0;
    unsigned int DPC_BUFBUSY_REG = 0;
    unsigned int DPC_PIPEBUSY_REG = 0;
    unsigned int DPC_TMEM_REG = 0;

    void check_interrupts() {
    }

    RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
        switch (option) {
        case ultramodern::renderer::AspectRatio::Original:
            return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:
            return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:
            return RT64::UserConfiguration::AspectRatio::Manual;
        default:
            return RT64::UserConfiguration::AspectRatio::Original;
        }
    }

    RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
        switch (option) {
        case ultramodern::renderer::Antialiasing::MSAA2X:
            return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:
            return RT64::UserConfiguration::Antialiasing::MSAA8X;
        default:
            return RT64::UserConfiguration::Antialiasing::None;
        }
    }

    RT64::UserConfiguration::Resolution to_rt64(ultramodern::renderer::Resolution option) {
        switch (option) {
        case ultramodern::renderer::Resolution::Original:
            return RT64::UserConfiguration::Resolution::Original;
        case ultramodern::renderer::Resolution::Original2x:
        case ultramodern::renderer::Resolution::Manual:
            return RT64::UserConfiguration::Resolution::Manual;
        case ultramodern::renderer::Resolution::Auto:
        default:
            return RT64::UserConfiguration::Resolution::WindowIntegerScale;
        }
    }

    RT64::UserConfiguration::Filtering to_rt64(ultramodern::renderer::TextureFiltering option) {
        switch (option) {
        case ultramodern::renderer::TextureFiltering::Nearest:
            return RT64::UserConfiguration::Filtering::Nearest;
        case ultramodern::renderer::TextureFiltering::Linear:
            return RT64::UserConfiguration::Filtering::Linear;
        case ultramodern::renderer::TextureFiltering::PixelScaling:
        default:
            return RT64::UserConfiguration::Filtering::AntiAliasedPixelScaling;
        }
    }

    RT64::UserConfiguration::Upscale2D to_rt64(ultramodern::renderer::Upscale2D option) {
        switch (option) {
        case ultramodern::renderer::Upscale2D::Original:
            return RT64::UserConfiguration::Upscale2D::Original;
        case ultramodern::renderer::Upscale2D::All:
            return RT64::UserConfiguration::Upscale2D::All;
        case ultramodern::renderer::Upscale2D::ScaledOnly:
        default:
            return RT64::UserConfiguration::Upscale2D::ScaledOnly;
        }
    }

    RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
        switch (option) {
        case ultramodern::renderer::RefreshRate::Display:
            return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:
            return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::Original:
        default:
            return RT64::UserConfiguration::RefreshRate::Original;
        }
    }

    RT64::UserConfiguration::HardwareResolve to_rt64(ultramodern::renderer::HardwareResolve option) {
        switch (option) {
        case ultramodern::renderer::HardwareResolve::On:
            return RT64::UserConfiguration::HardwareResolve::Enabled;
        case ultramodern::renderer::HardwareResolve::Off:
            return RT64::UserConfiguration::HardwareResolve::Disabled;
        case ultramodern::renderer::HardwareResolve::Auto:
        default:
            return RT64::UserConfiguration::HardwareResolve::Automatic;
        }
    }

    RT64::UserConfiguration::DisplayBuffering to_rt64(ultramodern::renderer::DisplayBuffering option) {
        return option == ultramodern::renderer::DisplayBuffering::Double
            ? RT64::UserConfiguration::DisplayBuffering::Double
            : RT64::UserConfiguration::DisplayBuffering::Triple;
    }

    RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
        switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::On:
            return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:
            return RT64::UserConfiguration::InternalColorFormat::Automatic;
        default:
            return RT64::UserConfiguration::InternalColorFormat::Standard;
        }
    }

    ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult result) {
        switch (result) {
        case RT64::Application::SetupResult::Success:
            return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound:
            return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:
            return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:
            return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:
            return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
        }

        assert(false);
        return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
    }

    ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
        switch (api) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:
            return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:
            return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:
            return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic:
            return ultramodern::renderer::GraphicsApi::Auto;
        }

        assert(false);
        return ultramodern::renderer::GraphicsApi::Auto;
    }

    bool is_texture_replacement_watch_file(const std::filesystem::path& path) {
        if (path.filename() == "rt64.json") {
            return true;
        }

        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return (extension == ".png") || (extension == ".dds");
    }

    std::filesystem::file_time_type latest_texture_replacement_write_time(const std::filesystem::path& directory) {
        std::filesystem::file_time_type latest{};
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error) || error) {
            return latest;
        }

        std::filesystem::recursive_directory_iterator iterator(
            directory,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && (iterator != end)) {
            if (iterator->is_regular_file(error) && !error && is_texture_replacement_watch_file(iterator->path())) {
                const std::filesystem::file_time_type writeTime = iterator->last_write_time(error);
                if (!error && (writeTime > latest)) {
                    latest = writeTime;
                }
            }

            iterator.increment(error);
        }

        return latest;
    }

    void apply_user_config(RT64::Application* app, const ultramodern::renderer::GraphicsConfig& config) {
        app->userConfig.resolution = to_rt64(config.res_option);
        app->userConfig.resolutionMultiplier = config.res_option == ultramodern::renderer::Resolution::Original2x
            ? 2.0
            : std::clamp(config.resolution_multiplier, 1.0, 32.0);
        app->userConfig.downsampleMultiplier = std::clamp(config.ds_option, 1, 32);
        app->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        app->userConfig.aspectRatio = to_rt64(config.ar_option);
        app->userConfig.antialiasing = to_rt64(config.msaa_option);
        app->userConfig.filtering = to_rt64(config.filtering_option);
        app->userConfig.upscale2D = to_rt64(config.upscale_2d);
        app->userConfig.threePointFiltering = config.three_point_filtering;
        // ReCut must not pace Paper Mario from the desktop monitor mode. The game
        // owns its 60 VI cadence; display refresh is only a presentation detail.
        app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        app->userConfig.refreshRateTarget = 60;
        app->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
        app->userConfig.displayBuffering = to_rt64(config.display_buffering);
        app->userConfig.hardwareResolve = to_rt64(config.hardware_resolve);
        app->userConfig.idleWorkActive = false;

        switch (config.api_option) {
        case ultramodern::renderer::GraphicsApi::D3D12:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
            break;
        case ultramodern::renderer::GraphicsApi::Vulkan:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
            break;
        case ultramodern::renderer::GraphicsApi::Metal:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal;
            break;
        case ultramodern::renderer::GraphicsApi::Auto:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic;
            break;
        }
    }

    class RT64Context final : public ultramodern::renderer::RendererContext {
    public:
        RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
            static unsigned char dummy_rom_header[0x40]{};

            RT64::Application::Core core{};
#if defined(_WIN32)
            core.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
            core.window = window_handle;
#elif defined(__APPLE__)
            core.window.window = window_handle.window;
            core.window.view = window_handle.view;
#endif
            core.checkInterrupts = check_interrupts;
            core.HEADER = dummy_rom_header;
            core.RDRAM = rdram;
            core.DMEM = dmem;
            core.IMEM = imem;
            core.MI_INTR_REG = &MI_INTR_REG;
            core.DPC_START_REG = &DPC_START_REG;
            core.DPC_END_REG = &DPC_END_REG;
            core.DPC_CURRENT_REG = &DPC_CURRENT_REG;
            core.DPC_STATUS_REG = &DPC_STATUS_REG;
            core.DPC_CLOCK_REG = &DPC_CLOCK_REG;
            core.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
            core.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
            core.DPC_TMEM_REG = &DPC_TMEM_REG;

            ultramodern::renderer::ViRegs* vi = ultramodern::renderer::get_vi_regs();
            core.VI_STATUS_REG = &vi->VI_STATUS_REG;
            core.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG;
            core.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
            core.VI_INTR_REG = &vi->VI_INTR_REG;
            core.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG;
            core.VI_TIMING_REG = &vi->VI_TIMING_REG;
            core.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG;
            core.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG;
            core.VI_LEAP_REG = &vi->VI_LEAP_REG;
            core.VI_H_START_REG = &vi->VI_H_START_REG;
            core.VI_V_START_REG = &vi->VI_V_START_REG;
            core.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
            core.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG;
            core.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;

            RT64::ApplicationConfiguration app_config;
            app_config.useConfigurationFile = false;
            auto config = ultramodern::renderer::get_graphics_config();

            uint32_t thread_id = 0;
#ifdef _WIN32
            thread_id = window_handle.thread_id;
#endif

            auto setup_app = [&](const ultramodern::renderer::GraphicsConfig& setup_config) {
                app = std::make_unique<RT64::Application>(core, app_config);
                apply_user_config(app.get(), setup_config);
                app->userConfig.developerMode = developer_mode;
                app->enhancementConfig.f3dex.forceBranch = true;
                app->enhancementConfig.textureLOD.scale = true;

                setup_result = map_setup_result(app->setup(thread_id));
                chosen_api = map_graphics_api(app->chosenGraphicsAPI);
                return setup_result == ultramodern::renderer::SetupResult::Success;
            };

            if (config.api_option == ultramodern::renderer::GraphicsApi::Auto) {
                // Vulkan avoids the D3D12 foreground swapchain path that can starve
                // desktop video playback on some multi-monitor Windows systems.
                auto vulkan_config = config;
                vulkan_config.api_option = ultramodern::renderer::GraphicsApi::Vulkan;
                if (!setup_app(vulkan_config)) {
                    app.reset();
                    auto d3d12_config = config;
                    d3d12_config.api_option = ultramodern::renderer::GraphicsApi::D3D12;
                    setup_app(d3d12_config);
                }
            }
            else {
                setup_app(config);
            }

            if (setup_result != ultramodern::renderer::SetupResult::Success) {
                app.reset();
                return;
            }
            app->updateSamplerAnisotropy(std::clamp(config.anisotropic_filtering, 1, 16));
            app->setFullScreen(false);

            default_texture_replacement_directory = ultramodern::get_startup_texture_replacement_directory();
        }

        bool valid() override {
            return app != nullptr;
        }

        bool update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) override {
            if (!app || old_config == new_config) {
                return false;
            }

            apply_user_config(app.get(), new_config);
            const bool discard_fbs =
                (new_config.res_option != old_config.res_option) ||
                (new_config.resolution_multiplier != old_config.resolution_multiplier) ||
                (new_config.ar_option != old_config.ar_option) ||
                (new_config.msaa_option != old_config.msaa_option) ||
                (new_config.hpfb_option != old_config.hpfb_option) ||
                (new_config.ds_option != old_config.ds_option) ||
                (new_config.filtering_option != old_config.filtering_option) ||
                (new_config.upscale_2d != old_config.upscale_2d) ||
                (new_config.three_point_filtering != old_config.three_point_filtering) ||
                (new_config.hardware_resolve != old_config.hardware_resolve);
            app->updateUserConfig(discard_fbs);
            if (new_config.msaa_option != old_config.msaa_option) {
                app->updateMultisampling();
            }
            if (new_config.anisotropic_filtering != old_config.anisotropic_filtering) {
                app->updateSamplerAnisotropy(std::clamp(new_config.anisotropic_filtering, 1, 16));
            }
            if (new_config.wm_option != old_config.wm_option) {
                app->setFullScreen(false);
            }
            return true;
        }

        void enable_instant_present() override {
            // Paper Mario often builds a frame with multiple graphics tasks. Presenting
            // after the first task exposes a half-built framebuffer, which makes 3D
            // models blink while backgrounds remain visible.
        }

        void send_dl(const OSTask* task) override {
            app->state->rsp->reset();
            app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
            app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
        }

        void update_screen() override {
            poll_texture_replacement_changes();
            app->updateScreen();
        }

        void shutdown() override {
            if (app) {
                app->end();
            }
        }

        uint32_t get_display_framerate() const override {
            return 60;
        }

        uint64_t get_presented_frame_count() const override {
            return app ? app->presentQueue->ext.sharedResources->presentedFrameCount.load(std::memory_order_relaxed) : 0;
        }

        float get_resolution_scale() const override {
            if (!app || app->sharedQueueResources->swapChainHeight == 0) {
                return 1.0f;
            }
            constexpr int reference_height = 240;
            return std::max(float((app->sharedQueueResources->swapChainHeight + reference_height - 1) / reference_height), 1.0f);
        }

        bool load_texture_replacements(const std::filesystem::path& directory) override {
            if (!app || !app->textureCache || !std::filesystem::is_directory(directory)) {
                return false;
            }

            const bool loading_default_only = same_texture_directory(directory, default_texture_replacement_directory);
            std::vector<RT64::ReplacementDirectory> directories;
            add_replacement_directory_if_valid(directories, default_texture_replacement_directory);
            if (!loading_default_only) {
                add_replacement_directory_if_valid(directories, directory);
            }

            if (directories.empty()) {
                return false;
            }

            const bool loaded = app->textureCache->loadReplacementDirectories(directories);
            if (loaded) {
                if (loading_default_only) {
                    texture_replacement_directory.clear();
                    texture_replacement_write_time = {};
                    next_texture_replacement_scan = {};
                }
                else {
                    texture_replacement_directory = directory;
                    texture_replacement_write_time = latest_texture_replacement_write_time(directory);
                    next_texture_replacement_scan = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
                }
            }

            return loaded;
        }

        void clear_texture_replacements() override {
            if (app && app->textureCache) {
                if (!default_texture_replacement_directory.empty() && std::filesystem::is_directory(default_texture_replacement_directory)) {
                    app->textureCache->loadReplacementDirectory(RT64::ReplacementDirectory(default_texture_replacement_directory));
                }
                else {
                    app->textureCache->clearReplacementDirectories();
                }
            }

            texture_replacement_directory.clear();
            texture_replacement_write_time = {};
            next_texture_replacement_scan = {};
        }

        bool start_texture_dumping(const std::filesystem::path& directory) override {
            if (!app || !app->state) {
                return false;
            }

            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error || !std::filesystem::is_directory(directory)) {
                return false;
            }

            app->state->textureManager.dumpedSet.clear();
            app->state->textureManager.dumpSequence = 0;
            app->state->dumpingTexturesDirectory = directory;
            return true;
        }

        void stop_texture_dumping() override {
            if (app && app->state) {
                app->state->dumpingTexturesDirectory.clear();
            }
        }

    private:
        static bool same_texture_directory(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
            if (lhs.empty() || rhs.empty()) {
                return false;
            }

            std::error_code error;
            const bool same = std::filesystem::equivalent(lhs, rhs, error);
            return !error && same;
        }

        static void add_replacement_directory_if_valid(std::vector<RT64::ReplacementDirectory>& directories, const std::filesystem::path& directory) {
            std::error_code error;
            if (!directory.empty() && std::filesystem::is_directory(directory, error) && !error) {
                directories.emplace_back(RT64::ReplacementDirectory(directory));
            }
        }

        void poll_texture_replacement_changes() {
            if (!app || !app->textureCache || texture_replacement_directory.empty()) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now < next_texture_replacement_scan) {
                return;
            }

            next_texture_replacement_scan = now + std::chrono::milliseconds(750);
            const std::filesystem::file_time_type latestWriteTime = latest_texture_replacement_write_time(texture_replacement_directory);
            if (latestWriteTime == texture_replacement_write_time) {
                return;
            }

            texture_replacement_write_time = latestWriteTime;
            app->textureCache->loadReplacementDirectory(RT64::ReplacementDirectory(texture_replacement_directory));
        }

        std::unique_ptr<RT64::Application> app;
        std::filesystem::path texture_replacement_directory;
        std::filesystem::path default_texture_replacement_directory;
        std::filesystem::file_time_type texture_replacement_write_time{};
        std::chrono::steady_clock::time_point next_texture_replacement_scan{};
    };
}

std::unique_ptr<ultramodern::renderer::RendererContext> paper_mario::renderer::create_render_context(
    uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode) {
    return std::make_unique<RT64Context>(rdram, window_handle, developer_mode);
}
