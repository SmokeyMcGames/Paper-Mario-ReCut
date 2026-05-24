#include "paper_rt64_context.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>

#if defined(_WIN32)
#include <Unknwn.h>
#include <ObjIdl.h>
#include <OleAuto.h>
#endif

#ifndef HLSL_CPU
#define HLSL_CPU
#endif
#include "hle/rt64_application.h"

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

    void apply_user_config(RT64::Application* app, const ultramodern::renderer::GraphicsConfig& config) {
        app->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
        app->userConfig.downsampleMultiplier = 1;
        app->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        app->userConfig.aspectRatio = to_rt64(config.ar_option);
        app->userConfig.antialiasing = to_rt64(config.msaa_option);
        app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        app->userConfig.refreshRateTarget = 60;
        app->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
        app->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;

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
            app = std::make_unique<RT64::Application>(core, app_config);

            auto config = ultramodern::renderer::get_graphics_config();
            apply_user_config(app.get(), config);
            app->userConfig.developerMode = developer_mode;
            app->enhancementConfig.f3dex.forceBranch = true;
            app->enhancementConfig.textureLOD.scale = true;

            uint32_t thread_id = 0;
#ifdef _WIN32
            thread_id = window_handle.thread_id;
#endif
            setup_result = map_setup_result(app->setup(thread_id));
            chosen_api = map_graphics_api(app->chosenGraphicsAPI);
            if (setup_result != ultramodern::renderer::SetupResult::Success) {
                app.reset();
                return;
            }
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
                (new_config.ar_option != old_config.ar_option) ||
                (new_config.msaa_option != old_config.msaa_option) ||
                (new_config.hpfb_option != old_config.hpfb_option) ||
                (new_config.ds_option != old_config.ds_option);
            app->updateUserConfig(discard_fbs);
            if (new_config.msaa_option != old_config.msaa_option) {
                app->updateMultisampling();
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
            app->updateScreen();
        }

        void shutdown() override {
            if (app) {
                app->end();
            }
        }

        uint32_t get_display_framerate() const override {
            return app ? app->presentQueue->ext.sharedResources->swapChainRate : 60;
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

    private:
        std::unique_ptr<RT64::Application> app;
    };
}

std::unique_ptr<ultramodern::renderer::RendererContext> paper_mario::renderer::create_render_context(
    uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode) {
    return std::make_unique<RT64Context>(rdram, window_handle, developer_mode);
}
