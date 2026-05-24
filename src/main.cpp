#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_syswm.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#include <commdlg.h>
#include <timeapi.h>
#endif

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "paper_rt64_context.h"
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

namespace paper_mario {
    void register_overlays();
}

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();

extern RspUcodeFunc n_aspMain;

namespace {
    constexpr uint64_t paper_mario_us_xxh3 = 0x1A478F060D5194CFULL;

    constexpr uint16_t A_BUTTON = 0x8000;
    constexpr uint16_t B_BUTTON = 0x4000;
    constexpr uint16_t Z_BUTTON = 0x2000;
    constexpr uint16_t START_BUTTON = 0x1000;
    constexpr uint16_t U_JPAD = 0x0800;
    constexpr uint16_t D_JPAD = 0x0400;
    constexpr uint16_t L_JPAD = 0x0200;
    constexpr uint16_t R_JPAD = 0x0100;
    constexpr uint16_t L_TRIG = 0x0020;
    constexpr uint16_t R_TRIG = 0x0010;
    constexpr uint16_t U_CBUTTONS = 0x0008;
    constexpr uint16_t D_CBUTTONS = 0x0004;
    constexpr uint16_t L_CBUTTONS = 0x0002;
    constexpr uint16_t R_CBUTTONS = 0x0001;

    SDL_Window* window = nullptr;
    SDL_GameController* controller = nullptr;
    SDL_AudioDeviceID audio_device = 0;
    SDL_AudioCVT audio_convert{};
    uint32_t sample_rate = 48000;
    uint32_t output_sample_rate = 48000;
    constexpr uint32_t input_channels = 2;
    uint32_t output_channels = 2;
    constexpr uint32_t duplicated_input_frames = 4;
    uint32_t discarded_output_frames = 0;
    constexpr uint32_t bytes_per_input_frame = input_channels * sizeof(float);

#ifdef _WIN32
    HWND main_window = nullptr;
    HWND fps_overlay_window = nullptr;
    bool fps_overlay_enabled = false;
    std::atomic<uint32_t> fps_vi_ticks{0};
    uint64_t fps_last_presented_frames = 0;
    std::chrono::steady_clock::time_point fps_last_sample = std::chrono::steady_clock::now();
    char fps_overlay_text[64] = "VI --.-\nFPS --.-";

    constexpr int fps_overlay_width = 136;
    constexpr int fps_overlay_height = 46;

    LRESULT CALLBACK fps_overlay_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect{};
            GetClientRect(hwnd, &rect);

            HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(dc, &rect, background);
            DeleteObject(background);

            SetBkMode(dc, TRANSPARENT);
            HFONT font = CreateFontA(
                16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            HFONT old_font = reinterpret_cast<HFONT>(SelectObject(dc, font));

            RECT shadow_rect = rect;
            OffsetRect(&shadow_rect, 1, 1);
            SetTextColor(dc, RGB(0, 0, 0));
            DrawTextA(dc, fps_overlay_text, -1, &shadow_rect, DT_CENTER | DT_VCENTER | DT_NOPREFIX);

            SetTextColor(dc, RGB(255, 255, 255));
            DrawTextA(dc, fps_overlay_text, -1, &rect, DT_CENTER | DT_VCENTER | DT_NOPREFIX);

            SelectObject(dc, old_font);
            DeleteObject(font);
            EndPaint(hwnd, &paint);
            return 0;
        }
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void ensure_fps_overlay_window() {
        if (fps_overlay_window || !main_window) {
            return;
        }

        const wchar_t* class_name = L"PaperMarioReCutFpsOverlay";
        static bool registered = false;
        if (!registered) {
            WNDCLASSW window_class{};
            window_class.lpfnWndProc = fps_overlay_proc;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = class_name;
            window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassW(&window_class);
            registered = true;
        }

        fps_overlay_window = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            class_name, L"", WS_POPUP, 0, 0, fps_overlay_width, fps_overlay_height,
            main_window, nullptr, GetModuleHandleW(nullptr), nullptr);

        if (fps_overlay_window) {
            SetLayeredWindowAttributes(fps_overlay_window, 0, 210, LWA_ALPHA);
        }
    }

    void position_fps_overlay() {
        if (!fps_overlay_window || !main_window) {
            return;
        }

        RECT client_rect{};
        POINT client_origin{0, 0};
        if (!GetClientRect(main_window, &client_rect) || !ClientToScreen(main_window, &client_origin)) {
            return;
        }

        SetWindowPos(
            fps_overlay_window, HWND_TOPMOST,
            client_origin.x + 8, client_origin.y + 8,
            fps_overlay_width, fps_overlay_height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void set_fps_overlay_enabled(bool enabled) {
        fps_overlay_enabled = enabled;

        if (enabled) {
            fps_vi_ticks.store(0, std::memory_order_relaxed);
            fps_last_presented_frames = ultramodern::get_presented_frame_count();
            fps_last_sample = std::chrono::steady_clock::now();
            std::snprintf(fps_overlay_text, sizeof(fps_overlay_text), "VI --.-\nFPS --.-");
            ensure_fps_overlay_window();
            position_fps_overlay();
            if (fps_overlay_window) {
                ShowWindow(fps_overlay_window, SW_SHOWNOACTIVATE);
                InvalidateRect(fps_overlay_window, nullptr, FALSE);
            }
        }
        else if (fps_overlay_window) {
            ShowWindow(fps_overlay_window, SW_HIDE);
        }
    }

    void update_fps_overlay() {
        if (!fps_overlay_enabled) {
            return;
        }

        ensure_fps_overlay_window();
        position_fps_overlay();

        auto now = std::chrono::steady_clock::now();
        double elapsed_seconds = std::chrono::duration<double>(now - fps_last_sample).count();
        if (elapsed_seconds < 0.5) {
            return;
        }

        uint32_t vi_frames = fps_vi_ticks.exchange(0, std::memory_order_relaxed);
        uint64_t presented_frames = ultramodern::get_presented_frame_count();
        uint64_t presented_delta = presented_frames - fps_last_presented_frames;
        double vi_fps = vi_frames / elapsed_seconds;
        double present_fps = presented_delta / elapsed_seconds;
        fps_last_presented_frames = presented_frames;
        fps_last_sample = now;
        std::snprintf(fps_overlay_text, sizeof(fps_overlay_text), "VI %4.1f\nFPS %4.1f", vi_fps, present_fps);

        if (fps_overlay_window) {
            InvalidateRect(fps_overlay_window, nullptr, FALSE);
        }
    }

    void destroy_fps_overlay() {
        if (fps_overlay_window) {
            DestroyWindow(fps_overlay_window);
            fps_overlay_window = nullptr;
        }
    }
#endif

    void show_message(const char* msg) {
        std::fprintf(stderr, "%s\n", msg);
#ifdef _WIN32
        if (window == nullptr && SDL_WasInit(SDL_INIT_VIDEO) == 0) {
            MessageBoxA(nullptr, msg, "Paper Mario ReCut", MB_OK | MB_ICONERROR | MB_TASKMODAL);
            return;
        }
#endif
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Paper Mario ReCut", msg, window);
    }

    void show_info_message(const char* msg) {
        std::fprintf(stderr, "%s\n", msg);
#ifdef _WIN32
        if (window == nullptr && SDL_WasInit(SDL_INIT_VIDEO) == 0) {
            MessageBoxA(nullptr, msg, "Paper Mario ReCut", MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
            return;
        }
#endif
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Paper Mario ReCut", msg, window);
    }

    void open_first_controller() {
        if (controller != nullptr) {
            return;
        }

        const int count = SDL_NumJoysticks();
        for (int i = 0; i < count; i++) {
            if (SDL_IsGameController(i)) {
                controller = SDL_GameControllerOpen(i);
                if (controller != nullptr) {
                    break;
                }
            }
        }
    }

    ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
            show_message(SDL_GetError());
            std::exit(EXIT_FAILURE);
        }

        open_first_controller();
        return nullptr;
    }

    ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
        uint32_t flags = SDL_WINDOW_RESIZABLE;
#if defined(__APPLE__)
        flags |= SDL_WINDOW_METAL;
#elif defined(RT64_SDL_WINDOW_VULKAN)
        flags |= SDL_WINDOW_VULKAN;
#endif

        window = SDL_CreateWindow("Paper Mario ReCut", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960, flags);
        if (window == nullptr) {
            show_message(SDL_GetError());
            std::exit(EXIT_FAILURE);
        }

        SDL_SysWMinfo wm_info;
        SDL_VERSION(&wm_info.version);
        SDL_GetWindowWMInfo(window, &wm_info);

#if defined(_WIN32)
        main_window = wm_info.info.win.window;
        return ultramodern::renderer::WindowHandle{ main_window, GetCurrentThreadId() };
#elif defined(__linux__) || defined(__ANDROID__)
        return window;
#elif defined(__APPLE__)
        SDL_MetalView view = SDL_Metal_CreateView(window);
        return ultramodern::renderer::WindowHandle{ wm_info.info.cocoa.window, SDL_Metal_GetLayer(view) };
#endif
    }

    void update_gfx(void*) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                ultramodern::quit();
            }
            else if (event.type == SDL_CONTROLLERDEVICEADDED) {
                open_first_controller();
            }
            else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (controller != nullptr && event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                    open_first_controller();
                }
            }
        }

#ifdef _WIN32
        static bool f10_was_down = false;
        const bool foreground = GetForegroundWindow() == main_window;
        bool f10_down = foreground && (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        if (f10_down && !f10_was_down) {
            set_fps_overlay_enabled(!fps_overlay_enabled);
        }
        f10_was_down = f10_down;
        update_fps_overlay();
#endif
    }

    void update_audio_converter() {
        int ret = SDL_BuildAudioCVT(
            &audio_convert,
            AUDIO_F32,
            input_channels,
            static_cast<int>(sample_rate),
            AUDIO_F32,
            static_cast<Uint8>(output_channels),
            static_cast<int>(output_sample_rate));
        if (ret < 0) {
            std::fprintf(stderr, "Error creating SDL audio converter: %s\n", SDL_GetError());
            std::exit(EXIT_FAILURE);
        }

        discarded_output_frames = duplicated_input_frames * output_sample_rate / sample_rate;
    }

    void reset_audio(uint32_t output_freq) {
        if (audio_device != 0) {
            SDL_CloseAudioDevice(audio_device);
            audio_device = 0;
        }

        SDL_AudioSpec desired{};
        desired.freq = static_cast<int>(output_freq);
        desired.format = AUDIO_F32;
        desired.channels = static_cast<Uint8>(output_channels);
        desired.samples = 0x100;
        desired.callback = nullptr;

        audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        if (audio_device == 0) {
            std::fprintf(stderr, "SDL error opening audio device: %s\n", SDL_GetError());
            std::exit(EXIT_FAILURE);
        }

        SDL_PauseAudioDevice(audio_device, 0);
        output_sample_rate = output_freq;
        update_audio_converter();
    }

    void set_frequency(uint32_t freq) {
        sample_rate = freq == 0 ? 48000 : freq;

        if (audio_device == 0) {
            reset_audio(48000);
            return;
        }

        update_audio_converter();
    }

    void queue_samples(int16_t* audio_data, size_t sample_count) {
        if (audio_device == 0 || sample_count == 0) {
            return;
        }

        static std::vector<float> swap_buffer;
        static std::array<float, duplicated_input_frames * input_channels> duplicated_sample_buffer{};

        size_t converted_input_samples = sample_count + duplicated_input_frames * input_channels;
        size_t max_sample_count = std::max(converted_input_samples, converted_input_samples * audio_convert.len_mult);
        if (max_sample_count > swap_buffer.size()) {
            swap_buffer.resize(max_sample_count);
        }

        for (size_t i = 0; i < duplicated_sample_buffer.size(); i++) {
            swap_buffer[i] = duplicated_sample_buffer[i];
        }

        constexpr float output_gain = 0.5f / 32768.0f;
        for (size_t i = 0; i + 1 < sample_count; i += input_channels) {
            swap_buffer[i + 0 + duplicated_input_frames * input_channels] = audio_data[i + 1] * output_gain;
            swap_buffer[i + 1 + duplicated_input_frames * input_channels] = audio_data[i + 0] * output_gain;
        }

        if (sample_count >= duplicated_sample_buffer.size()) {
            for (size_t i = 0; i < duplicated_sample_buffer.size(); i++) {
                duplicated_sample_buffer[i] = swap_buffer[i + sample_count];
            }
        }

        audio_convert.buf = reinterpret_cast<Uint8*>(swap_buffer.data());
        audio_convert.len = static_cast<int>(converted_input_samples * sizeof(float));

        int ret = SDL_ConvertAudio(&audio_convert);
        if (ret < 0) {
            std::fprintf(stderr, "Error converting audio: %s\n", SDL_GetError());
            return;
        }

        constexpr uint32_t bytes_per_output_frame = input_channels * sizeof(float);
        uint64_t queued_input_us =
            uint64_t(SDL_GetQueuedAudioSize(audio_device)) /
            bytes_per_output_frame * 1000000 / output_sample_rate;

        uint32_t discard_bytes = output_channels * discarded_output_frames * sizeof(float);
        uint32_t queue_bytes = audio_convert.len_cvt > discard_bytes ? audio_convert.len_cvt - discard_bytes : 0;
        float* samples_to_queue = swap_buffer.data() + (output_channels * discarded_output_frames / 2);

        uint32_t skip_factor = static_cast<uint32_t>(queued_input_us / 100000);
        if (skip_factor != 0 && queue_bytes >= output_channels * sizeof(float)) {
            uint32_t skip_ratio = 1u << std::min<uint32_t>(skip_factor, 4);
            uint32_t output_frame_count = queue_bytes / (output_channels * sizeof(float));
            output_frame_count /= skip_ratio;
            for (uint32_t i = 0; i < output_frame_count; i++) {
                samples_to_queue[2 * i + 0] = samples_to_queue[2 * skip_ratio * i + 0];
                samples_to_queue[2 * i + 1] = samples_to_queue[2 * skip_ratio * i + 1];
            }
            queue_bytes = output_frame_count * output_channels * sizeof(float);
        }

        if (queue_bytes != 0) {
            SDL_QueueAudio(audio_device, samples_to_queue, queue_bytes);
        }
    }

    size_t get_frames_remaining() {
        if (audio_device == 0) {
            return 0;
        }

        uint64_t buffered_byte_count = SDL_GetQueuedAudioSize(audio_device);
        buffered_byte_count = buffered_byte_count * input_channels * sample_rate / output_sample_rate / output_channels;
        return static_cast<size_t>(buffered_byte_count / bytes_per_input_frame);
    }

    void poll_input() {
        SDL_PumpEvents();
    }

    float normalize_axis(Sint16 value) {
        if (std::abs(value) < 8000) {
            return 0.0f;
        }
        return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
    }

    bool get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
        if (controller_num != 0) {
            return false;
        }

        uint16_t out_buttons = 0;
        float out_x = 0.0f;
        float out_y = 0.0f;

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_Z]) out_buttons |= A_BUTTON;
        if (keys[SDL_SCANCODE_X]) out_buttons |= B_BUTTON;
        if (keys[SDL_SCANCODE_RETURN]) out_buttons |= START_BUTTON;
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) out_buttons |= Z_BUTTON;
        if (keys[SDL_SCANCODE_Q]) out_buttons |= L_TRIG;
        if (keys[SDL_SCANCODE_E]) out_buttons |= R_TRIG;
        if (keys[SDL_SCANCODE_I]) out_buttons |= U_CBUTTONS;
        if (keys[SDL_SCANCODE_K]) out_buttons |= D_CBUTTONS;
        if (keys[SDL_SCANCODE_J]) out_buttons |= L_CBUTTONS;
        if (keys[SDL_SCANCODE_L]) out_buttons |= R_CBUTTONS;
        if (keys[SDL_SCANCODE_W]) out_buttons |= U_JPAD;
        if (keys[SDL_SCANCODE_S]) out_buttons |= D_JPAD;
        if (keys[SDL_SCANCODE_A]) out_buttons |= L_JPAD;
        if (keys[SDL_SCANCODE_D]) out_buttons |= R_JPAD;

        if (keys[SDL_SCANCODE_LEFT]) out_x -= 1.0f;
        if (keys[SDL_SCANCODE_RIGHT]) out_x += 1.0f;
        if (keys[SDL_SCANCODE_DOWN]) out_y -= 1.0f;
        if (keys[SDL_SCANCODE_UP]) out_y += 1.0f;

        if (controller != nullptr) {
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) out_buttons |= A_BUTTON;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X)) out_buttons |= B_BUTTON;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) out_buttons |= START_BUTTON;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) out_buttons |= L_TRIG;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) out_buttons |= R_TRIG;
            if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) out_buttons |= Z_BUTTON;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) out_buttons |= U_JPAD;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) out_buttons |= D_JPAD;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) out_buttons |= L_JPAD;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) out_buttons |= R_JPAD;

            float pad_x = normalize_axis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
            float pad_y = -normalize_axis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
            if (pad_x != 0.0f || pad_y != 0.0f) {
                out_x = pad_x;
                out_y = pad_y;
            }

            float right_x = normalize_axis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX));
            float right_y = -normalize_axis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
            if (right_y > 0.5f) out_buttons |= U_CBUTTONS;
            if (right_y < -0.5f) out_buttons |= D_CBUTTONS;
            if (right_x < -0.5f) out_buttons |= L_CBUTTONS;
            if (right_x > 0.5f) out_buttons |= R_CBUTTONS;
        }

        *buttons = out_buttons;
        *x = std::clamp(out_x, -1.0f, 1.0f);
        *y = std::clamp(out_y, -1.0f, 1.0f);
        return true;
    }

    void set_rumble(int, bool) {
    }

    ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
        if (controller_num != 0) {
            return { ultramodern::input::Device::None, ultramodern::input::Pak::None };
        }
        return { ultramodern::input::Device::Controller, ultramodern::input::Pak::RumblePak };
    }

    RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
        if (task->t.type == M_AUDTASK) {
            return n_aspMain;
        }
        std::fprintf(stderr, "Unknown non-graphics RSP task type: %u\n", task->t.type);
        return nullptr;
    }

    std::string get_game_thread_name(const OSThread* thread) {
        return "PM " + std::to_string(thread ? thread->id : 0);
    }

    std::filesystem::path app_base_path() {
#ifdef _WIN32
        std::array<wchar_t, MAX_PATH> executable_path{};
        const DWORD length = GetModuleFileNameW(nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
        if (length != 0 && length < executable_path.size()) {
            return std::filesystem::path(executable_path.data()).parent_path();
        }
#endif
        return std::filesystem::current_path();
    }

    std::filesystem::path app_config_path() {
        std::filesystem::path base = app_base_path() / "user";
        std::filesystem::create_directories(base);
        return base;
    }

    std::filesystem::path installed_rom_path() {
        return app_config_path() / "pm.n64.us.z64";
    }

    const char* rom_validation_message(recomp::RomValidationError error) {
        switch (error) {
        case recomp::RomValidationError::FailedToOpen:
            return "Could not open the selected file. Please choose your legally dumped Paper Mario (U) ROM.";
        case recomp::RomValidationError::NotARom:
            return "That file does not look like an N64 ROM. Please choose your legally dumped Paper Mario (U) ROM.";
        case recomp::RomValidationError::IncorrectRom:
            return "That is not the supported Paper Mario (U) ROM. Please choose your legally dumped US Paper Mario ROM.";
        case recomp::RomValidationError::NotYet:
            return "That Paper Mario ROM is not supported by this build yet. Please choose the supported US Paper Mario ROM.";
        case recomp::RomValidationError::IncorrectVersion:
            return "That is a Paper Mario ROM, but not the supported US version. Please choose Paper Mario (U).";
        case recomp::RomValidationError::OtherError:
        default:
            return "Paper Mario ReCut could not validate that ROM. Please choose your legally dumped Paper Mario (U) ROM.";
        }
    }

#ifdef _WIN32
    std::optional<std::filesystem::path> prompt_for_rom_path() {
        std::array<wchar_t, MAX_PATH> file_name{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = main_window;
        dialog.lpstrFile = file_name.data();
        dialog.nMaxFile = static_cast<DWORD>(file_name.size());
        dialog.lpstrFilter = L"N64 ROMs (*.z64;*.n64;*.v64)\0*.z64;*.n64;*.v64\0All Files (*.*)\0*.*\0";
        dialog.lpstrTitle = L"Select your legally dumped Paper Mario (U) ROM";
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (!GetOpenFileNameW(&dialog)) {
            return std::nullopt;
        }

        return std::filesystem::path(file_name.data());
    }
#else
    std::optional<std::filesystem::path> prompt_for_rom_path() {
        return std::nullopt;
    }
#endif

    bool install_rom_from_path(const std::filesystem::path& rom_path, std::u8string& game_id) {
        auto rom_result = recomp::select_rom(rom_path, game_id);
        if (rom_result == recomp::RomValidationError::Good) {
            if (recomp::load_stored_rom(game_id)) {
                return true;
            }

            show_message("The ROM validated, but Paper Mario ReCut could not save it into this installation's user folder.");
            return false;
        }

        std::fprintf(stderr, "ROM validation failed for %ls with error %d\n", rom_path.c_str(), static_cast<int>(rom_result));
        show_message(rom_validation_message(rom_result));
        return false;
    }

    bool ensure_rom_installed(int argc, char** argv, std::u8string& game_id) {
        if (std::filesystem::exists(installed_rom_path()) && recomp::load_stored_rom(game_id)) {
            return true;
        }

        if (argc > 1 && install_rom_from_path(std::filesystem::path(argv[1]), game_id)) {
            return true;
        }

#ifdef _WIN32
        show_info_message(
            "Paper Mario ReCut needs your legally dumped Paper Mario (U) ROM to finish local setup.\n\n"
            "Choose the ROM in the next window. It will be validated and installed into this copy of Paper Mario ReCut.");

        while (true) {
            std::optional<std::filesystem::path> selected_rom = prompt_for_rom_path();
            if (!selected_rom.has_value()) {
                show_message("Paper Mario ReCut cannot start until a legally dumped Paper Mario (U) ROM is installed.");
                return false;
            }

            if (install_rom_from_path(selected_rom.value(), game_id)) {
                return true;
            }
        }
#else
        show_message(
            "Paper Mario ReCut cannot find an installed ROM. Place your legally dumped Paper Mario (U) ROM at user/pm.n64.us.z64 "
            "or pass the ROM path as the first command-line argument.");
        return false;
#endif
    }

    void fps_vi_callback() {
#ifdef _WIN32
        fps_vi_ticks.fetch_add(1, std::memory_order_relaxed);
#endif
    }

#ifdef _WIN32
    void print_symbol(HANDLE process, DWORD64 address) {
        char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, address, &displacement, symbol)) {
            std::fprintf(stderr, "  %p %s+0x%llX\n", reinterpret_cast<void*>(address), symbol->Name, displacement);
        }
        else {
            std::fprintf(stderr, "  %p\n", reinterpret_cast<void*>(address));
        }
    }

    LONG WINAPI crash_handler(EXCEPTION_POINTERS* info) {
        static LONG handling_crash = 0;
        if (InterlockedExchange(&handling_crash, 1) == 0) {
            HANDLE process = GetCurrentProcess();
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize(process, nullptr, TRUE);

            std::fprintf(stderr, "Unhandled exception 0x%08lX at %p\n",
                info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress);

            print_symbol(process, reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress));

            void* stack[64] = {};
            USHORT frames = CaptureStackBackTrace(0, 64, stack, nullptr);
            std::fprintf(stderr, "Stack trace:\n");
            for (USHORT i = 0; i < frames; i++) {
                print_symbol(process, reinterpret_cast<DWORD64>(stack[i]));
            }
            std::fflush(stderr);
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
    timeBeginPeriod(1);
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif

    recomp::Version version{};
    if (!recomp::Version::from_string("0.1.0", version)) {
        return EXIT_FAILURE;
    }

    std::u8string game_id = u8"pm.n64.us";
    recomp::GameEntry paper_mario_us{
        .rom_hash = paper_mario_us_xxh3,
        .internal_name = "PAPER MARIO",
        .game_id = game_id,
        .mod_game_id = "",
        .save_type = recomp::SaveType::Flashram,
        .is_enabled = true,
        .decompression_routine = nullptr,
        .has_compressed_code = false,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = recomp_entrypoint,
    };

    recomp::register_config_path(app_config_path());
    recomp::register_game(paper_mario_us);
    paper_mario::register_overlays();

    if (!ensure_rom_installed(argc, argv, game_id)) {
        return EXIT_FAILURE;
    }

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = get_rsp_microcode,
    };

    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context = paper_mario::renderer::create_render_context,
    };

    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = queue_samples,
        .get_frames_remaining = get_frames_remaining,
        .set_frequency = set_frequency,
    };

    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = poll_input,
        .get_input = get_input,
        .set_rumble = set_rumble,
        .get_connected_device_info = get_connected_device_info,
    };

    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
    };

    ultramodern::events::callbacks_t events_callbacks{
        .vi_callback = fps_vi_callback,
        .gfx_init_callback = nullptr,
    };
    ultramodern::error_handling::callbacks_t error_callbacks{
        .message_box = show_message,
    };
    ultramodern::threads::callbacks_t thread_callbacks{
        .get_game_thread_name = get_game_thread_name,
    };

    std::thread([game_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        recomp::start_game(game_id);
    }).detach();

    recomp::start(
        version,
        {},
        rsp_callbacks,
        renderer_callbacks,
        audio_callbacks,
        input_callbacks,
        gfx_callbacks,
        events_callbacks,
        error_callbacks,
        thread_callbacks);

    if (controller != nullptr) {
        SDL_GameControllerClose(controller);
        controller = nullptr;
    }
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
#ifdef _WIN32
    destroy_fps_overlay();
#endif
    SDL_Quit();

#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return EXIT_SUCCESS;
}
