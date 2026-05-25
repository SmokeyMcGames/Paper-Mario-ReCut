#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_syswm.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#endif

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "builtin_texture_pack.h"
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
    std::atomic_bool texture_dump_input_paused = false;

    enum class InputAction : int {
        N64A,
        N64B,
        Start,
        Z,
        L,
        R,
        CUp,
        CDown,
        CLeft,
        CRight,
        DPadUp,
        DPadDown,
        DPadLeft,
        DPadRight,
        StickUp,
        StickDown,
        StickLeft,
        StickRight,
        Count
    };

    constexpr int input_action_count = static_cast<int>(InputAction::Count);

    struct InputActionDescriptor {
        InputAction action;
        const wchar_t* label;
        uint16_t button;
        int axis_x;
        int axis_y;
    };

    constexpr std::array<InputActionDescriptor, input_action_count> input_actions{ {
        { InputAction::N64A, L"A Button", A_BUTTON, 0, 0 },
        { InputAction::N64B, L"B Button", B_BUTTON, 0, 0 },
        { InputAction::Start, L"Start", START_BUTTON, 0, 0 },
        { InputAction::Z, L"Z Trigger", Z_BUTTON, 0, 0 },
        { InputAction::L, L"L Trigger", L_TRIG, 0, 0 },
        { InputAction::R, L"R Trigger", R_TRIG, 0, 0 },
        { InputAction::CUp, L"C Up", U_CBUTTONS, 0, 0 },
        { InputAction::CDown, L"C Down", D_CBUTTONS, 0, 0 },
        { InputAction::CLeft, L"C Left", L_CBUTTONS, 0, 0 },
        { InputAction::CRight, L"C Right", R_CBUTTONS, 0, 0 },
        { InputAction::DPadUp, L"D-Pad Up", U_JPAD, 0, 0 },
        { InputAction::DPadDown, L"D-Pad Down", D_JPAD, 0, 0 },
        { InputAction::DPadLeft, L"D-Pad Left", L_JPAD, 0, 0 },
        { InputAction::DPadRight, L"D-Pad Right", R_JPAD, 0, 0 },
        { InputAction::StickUp, L"Stick Up", 0, 0, 1 },
        { InputAction::StickDown, L"Stick Down", 0, 0, -1 },
        { InputAction::StickLeft, L"Stick Left", 0, -1, 0 },
        { InputAction::StickRight, L"Stick Right", 0, 1, 0 }
    } };

    enum class GamepadBindingKind {
        Button,
        AxisPositive,
        AxisNegative
    };

    struct GamepadBinding {
        GamepadBindingKind kind = GamepadBindingKind::Button;
        int code = SDL_CONTROLLER_BUTTON_INVALID;
    };

    struct AudioSettings {
        int volume_percent = 50;
        bool muted = false;
        uint32_t output_rate = 48000;
        uint16_t buffer_samples = 256;
    };

    struct AppInputSettings {
        int preferred_controller_index = 0;
        bool mouse_click_to_move = false;
        std::array<SDL_Scancode, input_action_count> keyboard_bindings{};
        std::array<GamepadBinding, input_action_count> gamepad_bindings{};
    };

    AppInputSettings make_default_input_settings() {
        AppInputSettings settings{};
        settings.keyboard_bindings = {
            SDL_SCANCODE_Z,
            SDL_SCANCODE_X,
            SDL_SCANCODE_RETURN,
            SDL_SCANCODE_LSHIFT,
            SDL_SCANCODE_Q,
            SDL_SCANCODE_E,
            SDL_SCANCODE_I,
            SDL_SCANCODE_K,
            SDL_SCANCODE_J,
            SDL_SCANCODE_L,
            SDL_SCANCODE_W,
            SDL_SCANCODE_S,
            SDL_SCANCODE_A,
            SDL_SCANCODE_D,
            SDL_SCANCODE_UP,
            SDL_SCANCODE_DOWN,
            SDL_SCANCODE_LEFT,
            SDL_SCANCODE_RIGHT
        };
        settings.gamepad_bindings = {
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_A },
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_X },
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_START },
            GamepadBinding{ GamepadBindingKind::AxisPositive, SDL_CONTROLLER_AXIS_TRIGGERLEFT },
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
            GamepadBinding{ GamepadBindingKind::AxisNegative, SDL_CONTROLLER_AXIS_RIGHTY },
            GamepadBinding{ GamepadBindingKind::AxisPositive, SDL_CONTROLLER_AXIS_RIGHTY },
            GamepadBinding{ GamepadBindingKind::AxisNegative, SDL_CONTROLLER_AXIS_RIGHTX },
            GamepadBinding{ GamepadBindingKind::AxisPositive, SDL_CONTROLLER_AXIS_RIGHTX },
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_DPAD_UP },
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_DPAD_DOWN },
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_DPAD_LEFT },
            GamepadBinding{ GamepadBindingKind::Button, SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
            GamepadBinding{ GamepadBindingKind::AxisNegative, SDL_CONTROLLER_AXIS_LEFTY },
            GamepadBinding{ GamepadBindingKind::AxisPositive, SDL_CONTROLLER_AXIS_LEFTY },
            GamepadBinding{ GamepadBindingKind::AxisNegative, SDL_CONTROLLER_AXIS_LEFTX },
            GamepadBinding{ GamepadBindingKind::AxisPositive, SDL_CONTROLLER_AXIS_LEFTX }
        };
        return settings;
    }

    void sanitize_recut_graphics_config(ultramodern::renderer::GraphicsConfig& config) {
        config.wm_option = ultramodern::renderer::WindowMode::Windowed;
        config.rr_option = ultramodern::renderer::RefreshRate::Original;
        config.rr_manual_value = 60;
    }

    std::mutex settings_mutex;
    AudioSettings audio_settings{};
    AppInputSettings input_settings = make_default_input_settings();
    int active_controller_device_index = -1;

    void show_message(const char* msg);
    void show_info_message(const char* msg);
    std::filesystem::path app_base_path();
    std::filesystem::path app_executable_path();
    std::filesystem::path texture_root_path();
    std::filesystem::path texture_builtin_path();
    std::filesystem::path texture_replacement_path();
    std::filesystem::path texture_dump_path();
    bool ensure_texture_folder_layout();
    bool ensure_texture_replacement_database(const std::filesystem::path& directory);
    std::filesystem::path app_settings_path();
    void load_recut_settings();
    void save_recut_settings();
    void reset_audio(uint32_t output_freq);
    void select_controller_index(int device_index);

#ifdef _WIN32
    HWND main_window = nullptr;
    WNDPROC previous_window_proc = nullptr;
    HMENU app_menu_bar = nullptr;
    bool app_menu_bar_visible = false;
    HWND texture_replacement_window = nullptr;
    HWND texture_live_replacement_checkbox = nullptr;
    HWND texture_dump_button = nullptr;
    HWND texture_dump_progress = nullptr;
    HWND graphics_options_window = nullptr;
    HWND graphics_resolution_combo = nullptr;
    HWND graphics_aspect_combo = nullptr;
    HWND graphics_filter_combo = nullptr;
    HWND graphics_anisotropic_combo = nullptr;
    HWND graphics_msaa_combo = nullptr;
    HWND graphics_downsample_combo = nullptr;
    HWND graphics_framebuffer_combo = nullptr;
    HWND graphics_refresh_combo = nullptr;
    HWND graphics_upscale_2d_combo = nullptr;
    HWND graphics_hardware_resolve_combo = nullptr;
    HWND graphics_fullscreen_checkbox = nullptr;
    HWND graphics_three_point_checkbox = nullptr;
    HWND audio_options_window = nullptr;
    HWND audio_volume_combo = nullptr;
    HWND audio_rate_combo = nullptr;
    HWND audio_buffer_combo = nullptr;
    HWND audio_mute_checkbox = nullptr;
    HWND input_options_window = nullptr;
    HWND input_tab_control = nullptr;
    HWND input_gamepad_page = nullptr;
    HWND input_keyboard_page = nullptr;
    HWND input_controller_combo = nullptr;
    HWND input_profile_label = nullptr;
    HWND input_mouse_checkbox = nullptr;
    HWND gamepad_rebind_window = nullptr;
    std::array<HWND, input_action_count> gamepad_rebind_combos{};
    std::array<HWND, input_action_count> input_keyboard_action_labels{};
    std::array<HWND, input_action_count> input_keyboard_binding_fields{};
    int input_keyboard_capture_action = -1;
    int input_keyboard_scroll_offset = 0;
    WNDPROC keyboard_binding_field_proc_prev = nullptr;
    bool texture_live_replacement_enabled = false;
    bool texture_dump_pass_active = false;
    AppInputSettings input_window_pending_settings = make_default_input_settings();
    bool input_window_pending_valid = false;
    std::chrono::steady_clock::time_point texture_dump_pass_started = std::chrono::steady_clock::now();

    constexpr UINT_PTR menu_command_restart = 40001;
    constexpr UINT_PTR menu_command_save_state = 40002;
    constexpr UINT_PTR menu_command_load_state = 40003;
    constexpr UINT_PTR menu_command_exit = 40004;
    constexpr UINT_PTR menu_command_graphics_options = 40020;
    constexpr UINT_PTR menu_command_fullscreen = 40021;
    constexpr UINT_PTR menu_command_resolution = 40022;
    constexpr UINT_PTR menu_command_texture_replacement = 40023;
    constexpr UINT_PTR menu_command_controller_setup = 40040;
    constexpr UINT_PTR menu_command_rebind_keys = 40041;
    constexpr UINT_PTR menu_command_input_profiles = 40042;
    constexpr UINT_PTR menu_command_audio_options = 40060;
    constexpr UINT_PTR texture_command_live_replacement = 40100;
    constexpr UINT_PTR texture_command_reload = 40101;
    constexpr UINT_PTR texture_command_open_folder = 40102;
    constexpr UINT_PTR texture_command_dump_textures = 40103;
    constexpr UINT_PTR texture_dump_timer = 40200;
    constexpr UINT_PTR graphics_command_apply = 40300;
    constexpr UINT_PTR graphics_command_close = 40301;
    constexpr UINT_PTR audio_command_apply = 40400;
    constexpr UINT_PTR audio_command_close = 40401;
    constexpr UINT_PTR input_command_apply = 40500;
    constexpr UINT_PTR input_command_close = 40501;
    constexpr UINT_PTR input_command_apply_profile = 40502;
    constexpr UINT_PTR input_command_open_gamepad_rebind = 40505;
    constexpr UINT_PTR input_keyboard_binding_field_base = 40520;
    constexpr UINT_PTR gamepad_rebind_command_apply = 40600;
    constexpr UINT_PTR gamepad_rebind_command_close = 40601;
    constexpr UINT_PTR gamepad_rebind_combo_base = 40620;

    constexpr double texture_dump_pass_seconds = 8.0;
    constexpr int input_keyboard_view_height = 248;
    constexpr int input_keyboard_content_height = 462;

    void set_app_menu_bar_visible(bool visible);
    void show_texture_replacement_window();
    void show_graphics_options_window();
    void show_audio_options_window();
    void show_input_options_window();
    void fill_gamepad_rebind_controls();
    void destroy_texture_replacement_window();
    void destroy_graphics_options_window();
    void destroy_audio_options_window();
    void destroy_input_options_window();
    void refresh_texture_replacement_window();
    void update_texture_dump_progress();
    bool handle_menu_command(UINT_PTR command, HWND hwnd);

    void restart_application() {
        const std::filesystem::path executable = app_executable_path();
        if (executable.empty()) {
            show_message("Paper Mario ReCut could not find its executable path to restart.");
            return;
        }

        std::wstring command_line = L"\"" + executable.wstring() + L"\"";
        std::wstring working_directory = app_base_path().wstring();

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(
                executable.c_str(),
                command_line.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                working_directory.empty() ? nullptr : working_directory.c_str(),
                &startup_info,
                &process_info)) {
            show_message("Paper Mario ReCut could not restart itself.");
            return;
        }

        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        ultramodern::quit();
    }

    bool handle_menu_command(UINT_PTR command, HWND hwnd) {
        switch (command) {
        case menu_command_restart:
            restart_application();
            return true;
        case menu_command_save_state:
            show_info_message("Save states are not implemented yet. This menu item is reserved for the save-state system.");
            return true;
        case menu_command_load_state:
            show_info_message("Load states are not implemented yet. This menu item is reserved for the save-state system.");
            return true;
        case menu_command_exit:
            ultramodern::quit();
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return true;
        case menu_command_graphics_options:
            show_graphics_options_window();
            return true;
        case menu_command_fullscreen:
            show_graphics_options_window();
            return true;
        case menu_command_resolution:
            show_graphics_options_window();
            return true;
        case menu_command_texture_replacement:
            show_texture_replacement_window();
            return true;
        case menu_command_controller_setup:
            show_input_options_window();
            return true;
        case menu_command_rebind_keys:
            show_input_options_window();
            return true;
        case menu_command_input_profiles:
            show_input_options_window();
            return true;
        case menu_command_audio_options:
            show_audio_options_window();
            return true;
        default:
            return false;
        }
    }

    LRESULT CALLBACK app_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_COMMAND && handle_menu_command(LOWORD(wparam), hwnd)) {
            return 0;
        }

        if (previous_window_proc) {
            return CallWindowProcW(previous_window_proc, hwnd, message, wparam, lparam);
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void install_app_window_proc() {
        if (!main_window || previous_window_proc) {
            return;
        }

        previous_window_proc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(main_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(app_window_proc)));
    }

    void ensure_app_menu_bar() {
        if (app_menu_bar) {
            return;
        }

        app_menu_bar = CreateMenu();

        HMENU file_menu = CreatePopupMenu();
        AppendMenuW(file_menu, MF_STRING, menu_command_restart, L"&Restart Game");
        AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file_menu, MF_STRING, menu_command_save_state, L"&Save State...");
        AppendMenuW(file_menu, MF_STRING, menu_command_load_state, L"&Load State...");
        AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file_menu, MF_STRING, menu_command_exit, L"E&xit");
        AppendMenuW(app_menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"&File");

        HMENU graphics_menu = CreatePopupMenu();
        AppendMenuW(graphics_menu, MF_STRING, menu_command_graphics_options, L"&Graphics Options...");
        AppendMenuW(graphics_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(graphics_menu, MF_STRING, menu_command_texture_replacement, L"&Texture Replacement...");
        AppendMenuW(app_menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(graphics_menu), L"&Graphics");

        HMENU audio_menu = CreatePopupMenu();
        AppendMenuW(audio_menu, MF_STRING, menu_command_audio_options, L"&Audio Options...");
        AppendMenuW(app_menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(audio_menu), L"&Audio");

        HMENU controls_menu = CreatePopupMenu();
        AppendMenuW(controls_menu, MF_STRING, menu_command_controller_setup, L"&Controller Setup...");
        AppendMenuW(app_menu_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(controls_menu), L"&Controls");
    }

    void set_app_menu_bar_visible(bool visible) {
        if (!main_window) {
            return;
        }

        if (visible) {
            ensure_app_menu_bar();
        }
        SetMenu(main_window, visible ? app_menu_bar : nullptr);
        DrawMenuBar(main_window);
        app_menu_bar_visible = visible;
    }

    void toggle_app_menu_bar() {
        set_app_menu_bar_visible(!app_menu_bar_visible);
    }

    void destroy_app_menu_bar() {
        if (!main_window) {
            return;
        }

        SetMenu(main_window, nullptr);
        DrawMenuBar(main_window);
        if (app_menu_bar) {
            DestroyMenu(app_menu_bar);
            app_menu_bar = nullptr;
        }
        app_menu_bar_visible = false;
    }

    void stop_texture_dump_pass() {
        if (!texture_dump_pass_active) {
            return;
        }

        texture_dump_pass_active = false;
        texture_dump_input_paused.store(false, std::memory_order_relaxed);
        ultramodern::stop_texture_dumping();

        if (texture_dump_progress) {
            SendMessageW(texture_dump_progress, PBM_SETPOS, 100, 0);
            ShowWindow(texture_dump_progress, SW_HIDE);
        }
        if (texture_dump_button) {
            EnableWindow(texture_dump_button, TRUE);
            SetWindowTextW(texture_dump_button, L"Dump Textures");
        }
        if (texture_replacement_window) {
            KillTimer(texture_replacement_window, texture_dump_timer);
        }
    }

    void start_texture_dump_pass() {
        if (texture_dump_pass_active) {
            return;
        }

        if (!ensure_texture_folder_layout()) {
            show_message("Paper Mario ReCut could not prepare the texture dump folder.");
            return;
        }

        texture_dump_pass_active = true;
        texture_dump_input_paused.store(true, std::memory_order_relaxed);
        texture_dump_pass_started = std::chrono::steady_clock::now();
        ultramodern::start_texture_dumping(texture_dump_path());

        if (texture_dump_progress) {
            SendMessageW(texture_dump_progress, PBM_SETRANGE32, 0, 100);
            SendMessageW(texture_dump_progress, PBM_SETPOS, 0, 0);
            ShowWindow(texture_dump_progress, SW_SHOW);
        }
        if (texture_dump_button) {
            EnableWindow(texture_dump_button, FALSE);
            SetWindowTextW(texture_dump_button, L"Dumping...");
        }
        if (texture_replacement_window) {
            SetTimer(texture_replacement_window, texture_dump_timer, 100, nullptr);
        }
    }

    void update_texture_dump_progress() {
        if (!texture_dump_pass_active) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - texture_dump_pass_started).count();
        const int percent = std::clamp(static_cast<int>((elapsed / texture_dump_pass_seconds) * 100.0), 0, 100);
        if (texture_dump_progress) {
            SendMessageW(texture_dump_progress, PBM_SETPOS, percent, 0);
        }

        if (elapsed >= texture_dump_pass_seconds) {
            stop_texture_dump_pass();
        }
    }

    void reload_texture_replacement_folder() {
        const std::filesystem::path directory = texture_replacement_path();
        if (!ensure_texture_folder_layout()) {
            show_message("Paper Mario ReCut could not prepare the textures folder.");
            return;
        }

        if (texture_live_replacement_enabled) {
            ultramodern::load_texture_replacements(directory);
        }
        refresh_texture_replacement_window();
    }

    void set_live_texture_replacement_enabled(bool enabled) {
        texture_live_replacement_enabled = enabled;

        if (enabled) {
            if (!ensure_texture_folder_layout()) {
                texture_live_replacement_enabled = false;
                show_message("Paper Mario ReCut could not prepare the texture replacement folder.");
            }
            else {
                ultramodern::load_texture_replacements(texture_replacement_path());
            }
        }
        else {
            ultramodern::clear_texture_replacements();
        }

        refresh_texture_replacement_window();
    }

    void open_texture_replacement_folder() {
        const std::filesystem::path directory = texture_root_path();
        ensure_texture_folder_layout();

        std::wstring command_line = L"explorer.exe \"" + directory.wstring() + L"\"";
        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};
        if (CreateProcessW(
                nullptr,
                command_line.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                directory.c_str(),
                &startup_info,
                &process_info)) {
            CloseHandle(process_info.hThread);
            CloseHandle(process_info.hProcess);
        }
    }

    void refresh_texture_replacement_window() {
        if (!texture_replacement_window) {
            return;
        }

        if (texture_live_replacement_checkbox) {
            CheckDlgButton(
                texture_replacement_window,
                static_cast<int>(texture_command_live_replacement),
                texture_live_replacement_enabled ? BST_CHECKED : BST_UNCHECKED);
        }

    }

    struct ResolutionChoice {
        std::wstring label;
        ultramodern::renderer::Resolution option;
        double multiplier;
    };

    struct ComboIntChoice {
        const wchar_t* label;
        int value;
    };

    std::vector<ResolutionChoice> graphics_resolution_choices;
    std::vector<int> input_controller_choices;
    std::vector<GamepadBinding> gamepad_binding_choices;

    const std::array<ComboIntChoice, 5> anisotropic_choices{ {
        { L"Off", 1 },
        { L"2x", 2 },
        { L"4x", 4 },
        { L"8x", 8 },
        { L"16x", 16 }
    } };

    const std::array<ComboIntChoice, 5> audio_volume_choices{ {
        { L"0%", 0 },
        { L"25%", 25 },
        { L"50%", 50 },
        { L"100%", 100 },
        { L"200%", 200 }
    } };

    const std::array<ComboIntChoice, 3> audio_rate_choices{ {
        { L"44,100 Hz", 44100 },
        { L"48,000 Hz", 48000 },
        { L"96,000 Hz", 96000 }
    } };

    const std::array<ComboIntChoice, 5> audio_buffer_choices{ {
        { L"128 samples", 128 },
        { L"256 samples", 256 },
        { L"512 samples", 512 },
        { L"1024 samples", 1024 },
        { L"2048 samples", 2048 }
    } };

    HFONT ui_font() {
        static HFONT font = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        return font;
    }

    void use_ui_font(HWND hwnd) {
        if (hwnd) {
            SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font()), TRUE);
            SetWindowTheme(hwnd, L"Explorer", nullptr);
        }
    }

    HWND create_label(HWND parent, const wchar_t* text, int x, int y, int width, int height = 20) {
        HWND label = CreateWindowExW(
            0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            x, y, width, height,
            parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        use_ui_font(label);
        return label;
    }

    HWND create_combo(HWND parent, UINT_PTR id, int x, int y, int width) {
        HWND combo = CreateWindowExW(
            0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            x, y, width, 200,
            parent, reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
        use_ui_font(combo);
        return combo;
    }

    HWND create_button(HWND parent, const wchar_t* text, UINT_PTR id, int x, int y, int width, int height = 30) {
        HWND button = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            x, y, width, height,
            parent, reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
        use_ui_font(button);
        return button;
    }

    HWND create_checkbox(HWND parent, const wchar_t* text, UINT_PTR id, int x, int y, int width) {
        HWND checkbox = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            x, y, width, 24,
            parent, reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
        use_ui_font(checkbox);
        return checkbox;
    }

    void combo_add(HWND combo, const wchar_t* label) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }

    int combo_selection(HWND combo) {
        return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    }

    void combo_select_clamped(HWND combo, int index) {
        const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
        if (count > 0) {
            SendMessageW(combo, CB_SETCURSEL, std::clamp(index, 0, count - 1), 0);
        }
    }

    template <size_t Count>
    int find_choice_index(const std::array<ComboIntChoice, Count>& choices, int value, int fallback = 0) {
        for (size_t i = 0; i < choices.size(); i++) {
            if (choices[i].value == value) {
                return static_cast<int>(i);
            }
        }
        return fallback;
    }

    std::wstring utf8_to_wide(const char* text) {
        if (text == nullptr || text[0] == '\0') {
            return L"";
        }

        const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (length <= 1) {
            return L"";
        }

        std::wstring wide(static_cast<size_t>(length - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
        return wide;
    }

    void center_dialog_on_main(HWND hwnd, int width, int height) {
        RECT owner_rect{};
        GetWindowRect(main_window, &owner_rect);
        const int owner_width = static_cast<int>(owner_rect.right - owner_rect.left);
        const int owner_height = static_cast<int>(owner_rect.bottom - owner_rect.top);
        const int x = static_cast<int>(owner_rect.left) + std::max(0, (owner_width - width) / 2);
        const int y = static_cast<int>(owner_rect.top) + std::max(0, (owner_height - height) / 2);
        SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    std::vector<ResolutionChoice> enumerate_resolution_choices() {
        std::vector<ResolutionChoice> choices{
            { L"Auto (window integer scale)", ultramodern::renderer::Resolution::Auto, 2.0 },
            { L"Original (320 x 240)", ultramodern::renderer::Resolution::Original, 1.0 },
            { L"2x (640 x 480)", ultramodern::renderer::Resolution::Original2x, 2.0 }
        };

        HMONITOR monitor = MonitorFromWindow(main_window, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!GetMonitorInfoW(monitor, &monitor_info)) {
            return choices;
        }

        std::vector<std::pair<DWORD, DWORD>> modes;
        for (DWORD mode_index = 0;; mode_index++) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsW(monitor_info.szDevice, mode_index, &mode)) {
                break;
            }

            if (mode.dmPelsWidth < 320 || mode.dmPelsHeight < 240) {
                continue;
            }

            const auto mode_pair = std::make_pair(mode.dmPelsWidth, mode.dmPelsHeight);
            if (std::find(modes.begin(), modes.end(), mode_pair) == modes.end()) {
                modes.push_back(mode_pair);
            }
        }

        std::sort(modes.begin(), modes.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.first != rhs.first) {
                return lhs.first < rhs.first;
            }
            return lhs.second < rhs.second;
        });

        for (const auto& mode : modes) {
            wchar_t label[96] = {};
            const double multiplier = std::clamp(static_cast<double>(mode.second) / 240.0, 1.0, 32.0);
            swprintf_s(label, L"%lu x %lu (%.2fx internal)", mode.first, mode.second, multiplier);
            choices.push_back({ label, ultramodern::renderer::Resolution::Manual, multiplier });
        }

        return choices;
    }

    int selected_resolution_index(const ultramodern::renderer::GraphicsConfig& config) {
        for (size_t i = 0; i < graphics_resolution_choices.size(); i++) {
            const ResolutionChoice& choice = graphics_resolution_choices[i];
            if (choice.option == config.res_option) {
                if (choice.option != ultramodern::renderer::Resolution::Manual || std::abs(choice.multiplier - config.resolution_multiplier) < 0.05) {
                    return static_cast<int>(i);
                }
            }
        }
        return 0;
    }

    void fill_graphics_options_controls() {
        if (!graphics_options_window || !graphics_resolution_combo || !graphics_aspect_combo ||
            !graphics_filter_combo || !graphics_anisotropic_combo || !graphics_msaa_combo ||
            !graphics_downsample_combo || !graphics_framebuffer_combo || !graphics_refresh_combo ||
            !graphics_upscale_2d_combo || !graphics_hardware_resolve_combo) {
            return;
        }

        const auto config = ultramodern::renderer::get_graphics_config();
        graphics_resolution_choices = enumerate_resolution_choices();

        SendMessageW(graphics_resolution_combo, CB_RESETCONTENT, 0, 0);
        for (const ResolutionChoice& choice : graphics_resolution_choices) {
            combo_add(graphics_resolution_combo, choice.label.c_str());
        }
        combo_select_clamped(graphics_resolution_combo, selected_resolution_index(config));

        SendMessageW(graphics_aspect_combo, CB_RESETCONTENT, 0, 0);
        combo_add(graphics_aspect_combo, L"Original 4:3");
        combo_add(graphics_aspect_combo, L"Expand to Window");
        combo_select_clamped(graphics_aspect_combo, config.ar_option == ultramodern::renderer::AspectRatio::Expand ? 1 : 0);

        SendMessageW(graphics_filter_combo, CB_RESETCONTENT, 0, 0);
        combo_add(graphics_filter_combo, L"Anti-aliased Pixel Scaling");
        combo_add(graphics_filter_combo, L"Linear");
        combo_add(graphics_filter_combo, L"Nearest");
        int filter_index = 0;
        if (config.filtering_option == ultramodern::renderer::TextureFiltering::Linear) {
            filter_index = 1;
        }
        else if (config.filtering_option == ultramodern::renderer::TextureFiltering::Nearest) {
            filter_index = 2;
        }
        combo_select_clamped(graphics_filter_combo, filter_index);

        SendMessageW(graphics_anisotropic_combo, CB_RESETCONTENT, 0, 0);
        for (const ComboIntChoice& choice : anisotropic_choices) {
            combo_add(graphics_anisotropic_combo, choice.label);
        }
        combo_select_clamped(graphics_anisotropic_combo, find_choice_index(anisotropic_choices, std::clamp(config.anisotropic_filtering, 1, 16), 4));

        SendMessageW(graphics_msaa_combo, CB_RESETCONTENT, 0, 0);
        combo_add(graphics_msaa_combo, L"Off");
        combo_add(graphics_msaa_combo, L"2x MSAA");
        combo_add(graphics_msaa_combo, L"4x MSAA");
        combo_add(graphics_msaa_combo, L"8x MSAA");
        combo_select_clamped(graphics_msaa_combo, static_cast<int>(config.msaa_option));

        SendMessageW(graphics_downsample_combo, CB_RESETCONTENT, 0, 0);
        combo_add(graphics_downsample_combo, L"Off");
        combo_add(graphics_downsample_combo, L"2x");
        combo_add(graphics_downsample_combo, L"4x");
        combo_select_clamped(graphics_downsample_combo, config.ds_option >= 4 ? 2 : (config.ds_option >= 2 ? 1 : 0));

        SendMessageW(graphics_framebuffer_combo, CB_RESETCONTENT, 0, 0);
        combo_add(graphics_framebuffer_combo, L"Automatic");
        combo_add(graphics_framebuffer_combo, L"High Precision");
        combo_add(graphics_framebuffer_combo, L"Standard");
        combo_select_clamped(graphics_framebuffer_combo, static_cast<int>(config.hpfb_option));

        SendMessageW(graphics_refresh_combo, CB_RESETCONTENT, 0, 0);
        combo_add(graphics_refresh_combo, L"Original 60 VI");
        combo_select_clamped(graphics_refresh_combo, 0);
        EnableWindow(graphics_refresh_combo, FALSE);

        SendMessageW(graphics_upscale_2d_combo, CB_RESETCONTENT, 0, 0);
        combo_add(graphics_upscale_2d_combo, L"Scaled 2D Only");
        combo_add(graphics_upscale_2d_combo, L"All 2D");
        combo_add(graphics_upscale_2d_combo, L"Original 2D");
        int upscale_index = 0;
        if (config.upscale_2d == ultramodern::renderer::Upscale2D::All) {
            upscale_index = 1;
        }
        else if (config.upscale_2d == ultramodern::renderer::Upscale2D::Original) {
            upscale_index = 2;
        }
        combo_select_clamped(graphics_upscale_2d_combo, upscale_index);

        SendMessageW(graphics_hardware_resolve_combo, CB_RESETCONTENT, 0, 0);
        combo_add(graphics_hardware_resolve_combo, L"Automatic");
        combo_add(graphics_hardware_resolve_combo, L"Enabled");
        combo_add(graphics_hardware_resolve_combo, L"Disabled");
        int hardware_index = 0;
        if (config.hardware_resolve == ultramodern::renderer::HardwareResolve::On) {
            hardware_index = 1;
        }
        else if (config.hardware_resolve == ultramodern::renderer::HardwareResolve::Off) {
            hardware_index = 2;
        }
        combo_select_clamped(graphics_hardware_resolve_combo, hardware_index);

        CheckDlgButton(graphics_options_window, static_cast<int>(menu_command_fullscreen), BST_UNCHECKED);
        if (graphics_fullscreen_checkbox) {
            ShowWindow(graphics_fullscreen_checkbox, SW_HIDE);
            EnableWindow(graphics_fullscreen_checkbox, FALSE);
        }
        CheckDlgButton(graphics_options_window, static_cast<int>(graphics_command_apply + 20), config.three_point_filtering ? BST_CHECKED : BST_UNCHECKED);
    }

    void apply_graphics_options() {
        if (!graphics_options_window) {
            return;
        }

        auto config = ultramodern::renderer::get_graphics_config();
        const int resolution_index = combo_selection(graphics_resolution_combo);
        if (resolution_index >= 0 && resolution_index < static_cast<int>(graphics_resolution_choices.size())) {
            const ResolutionChoice& choice = graphics_resolution_choices[resolution_index];
            config.res_option = choice.option;
            config.resolution_multiplier = choice.multiplier;
        }

        config.wm_option = ultramodern::renderer::WindowMode::Windowed;
        config.ar_option = combo_selection(graphics_aspect_combo) == 1
            ? ultramodern::renderer::AspectRatio::Expand
            : ultramodern::renderer::AspectRatio::Original;

        switch (combo_selection(graphics_filter_combo)) {
        case 1:
            config.filtering_option = ultramodern::renderer::TextureFiltering::Linear;
            break;
        case 2:
            config.filtering_option = ultramodern::renderer::TextureFiltering::Nearest;
            break;
        default:
            config.filtering_option = ultramodern::renderer::TextureFiltering::PixelScaling;
            break;
        }

        const int anisotropic_index = combo_selection(graphics_anisotropic_combo);
        if (anisotropic_index >= 0 && anisotropic_index < static_cast<int>(anisotropic_choices.size())) {
            config.anisotropic_filtering = anisotropic_choices[anisotropic_index].value;
        }

        switch (combo_selection(graphics_msaa_combo)) {
        case 1:
            config.msaa_option = ultramodern::renderer::Antialiasing::MSAA2X;
            break;
        case 2:
            config.msaa_option = ultramodern::renderer::Antialiasing::MSAA4X;
            break;
        case 3:
            config.msaa_option = ultramodern::renderer::Antialiasing::MSAA8X;
            break;
        default:
            config.msaa_option = ultramodern::renderer::Antialiasing::None;
            break;
        }

        switch (combo_selection(graphics_downsample_combo)) {
        case 1:
            config.ds_option = 2;
            break;
        case 2:
            config.ds_option = 4;
            break;
        default:
            config.ds_option = 1;
            break;
        }

        config.hpfb_option = static_cast<ultramodern::renderer::HighPrecisionFramebuffer>(std::clamp(combo_selection(graphics_framebuffer_combo), 0, 2));

        config.rr_option = ultramodern::renderer::RefreshRate::Original;
        config.rr_manual_value = 60;

        switch (combo_selection(graphics_upscale_2d_combo)) {
        case 1:
            config.upscale_2d = ultramodern::renderer::Upscale2D::All;
            break;
        case 2:
            config.upscale_2d = ultramodern::renderer::Upscale2D::Original;
            break;
        default:
            config.upscale_2d = ultramodern::renderer::Upscale2D::ScaledOnly;
            break;
        }

        switch (combo_selection(graphics_hardware_resolve_combo)) {
        case 1:
            config.hardware_resolve = ultramodern::renderer::HardwareResolve::On;
            break;
        case 2:
            config.hardware_resolve = ultramodern::renderer::HardwareResolve::Off;
            break;
        default:
            config.hardware_resolve = ultramodern::renderer::HardwareResolve::Auto;
            break;
        }

        config.three_point_filtering = IsDlgButtonChecked(graphics_options_window, static_cast<int>(graphics_command_apply + 20)) == BST_CHECKED;
        sanitize_recut_graphics_config(config);
        ultramodern::renderer::set_graphics_config(config);
        save_recut_settings();
    }

    LRESULT CALLBACK graphics_options_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            graphics_options_window = hwnd;
            EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);
            create_label(hwnd, L"Internal resolution", 18, 18, 150);
            graphics_resolution_combo = create_combo(hwnd, menu_command_resolution, 178, 14, 270);
            create_label(hwnd, L"Aspect ratio", 18, 54, 150);
            graphics_aspect_combo = create_combo(hwnd, menu_command_resolution + 1, 178, 50, 270);
            create_label(hwnd, L"Texture filtering", 18, 90, 150);
            graphics_filter_combo = create_combo(hwnd, menu_command_resolution + 2, 178, 86, 270);
            create_label(hwnd, L"Anisotropic filtering", 18, 126, 150);
            graphics_anisotropic_combo = create_combo(hwnd, menu_command_resolution + 3, 178, 122, 270);
            create_label(hwnd, L"Anti-aliasing", 18, 162, 150);
            graphics_msaa_combo = create_combo(hwnd, menu_command_resolution + 4, 178, 158, 270);
            create_label(hwnd, L"Downsample", 18, 198, 150);
            graphics_downsample_combo = create_combo(hwnd, menu_command_resolution + 5, 178, 194, 270);
            create_label(hwnd, L"Framebuffer", 18, 234, 150);
            graphics_framebuffer_combo = create_combo(hwnd, menu_command_resolution + 6, 178, 230, 270);
            create_label(hwnd, L"Game timing", 18, 270, 150);
            graphics_refresh_combo = create_combo(hwnd, menu_command_resolution + 7, 178, 266, 270);
            create_label(hwnd, L"2D upscaling", 18, 306, 150);
            graphics_upscale_2d_combo = create_combo(hwnd, menu_command_resolution + 8, 178, 302, 270);
            create_label(hwnd, L"Hardware resolve", 18, 342, 150);
            graphics_hardware_resolve_combo = create_combo(hwnd, menu_command_resolution + 9, 178, 338, 270);
            graphics_fullscreen_checkbox = create_checkbox(hwnd, L"Fullscreen", menu_command_fullscreen, 178, 374, 140);
            graphics_three_point_checkbox = create_checkbox(hwnd, L"Three-point filtering", graphics_command_apply + 20, 318, 374, 170);
            create_button(hwnd, L"Apply", graphics_command_apply, 286, 416, 88);
            create_button(hwnd, L"Close", graphics_command_close, 384, 416, 88);
            fill_graphics_options_controls();
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case graphics_command_apply:
                apply_graphics_options();
                return 0;
            case graphics_command_close:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            graphics_options_window = nullptr;
            graphics_resolution_combo = nullptr;
            graphics_aspect_combo = nullptr;
            graphics_filter_combo = nullptr;
            graphics_anisotropic_combo = nullptr;
            graphics_msaa_combo = nullptr;
            graphics_downsample_combo = nullptr;
            graphics_framebuffer_combo = nullptr;
            graphics_refresh_combo = nullptr;
            graphics_upscale_2d_combo = nullptr;
            graphics_hardware_resolve_combo = nullptr;
            graphics_fullscreen_checkbox = nullptr;
            graphics_three_point_checkbox = nullptr;
            return 0;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void show_graphics_options_window() {
        if (!main_window) {
            return;
        }

        if (!graphics_options_window) {
            const wchar_t* class_name = L"PaperMarioReCutGraphicsOptions";
            static bool registered = false;
            if (!registered) {
                WNDCLASSW window_class{};
                window_class.lpfnWndProc = graphics_options_window_proc;
                window_class.hInstance = GetModuleHandleW(nullptr);
                window_class.lpszClassName = class_name;
                window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
                window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
                RegisterClassW(&window_class);
                registered = true;
            }

            graphics_options_window = CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                class_name,
                L"Graphics Options",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 500, 500,
                main_window,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            center_dialog_on_main(graphics_options_window, 500, 500);
        }
        else {
            fill_graphics_options_controls();
        }

        ShowWindow(graphics_options_window, SW_SHOWNORMAL);
        SetForegroundWindow(graphics_options_window);
    }

    void destroy_graphics_options_window() {
        if (graphics_options_window) {
            DestroyWindow(graphics_options_window);
            graphics_options_window = nullptr;
        }
    }

    void fill_audio_options_controls() {
        if (!audio_options_window || !audio_volume_combo || !audio_rate_combo || !audio_buffer_combo) {
            return;
        }

        AudioSettings settings_snapshot{};
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            settings_snapshot = audio_settings;
        }

        SendMessageW(audio_volume_combo, CB_RESETCONTENT, 0, 0);
        for (const ComboIntChoice& choice : audio_volume_choices) {
            combo_add(audio_volume_combo, choice.label);
        }
        combo_select_clamped(audio_volume_combo, find_choice_index(audio_volume_choices, settings_snapshot.volume_percent, 2));

        SendMessageW(audio_rate_combo, CB_RESETCONTENT, 0, 0);
        for (const ComboIntChoice& choice : audio_rate_choices) {
            combo_add(audio_rate_combo, choice.label);
        }
        combo_select_clamped(audio_rate_combo, find_choice_index(audio_rate_choices, static_cast<int>(settings_snapshot.output_rate), 1));

        SendMessageW(audio_buffer_combo, CB_RESETCONTENT, 0, 0);
        for (const ComboIntChoice& choice : audio_buffer_choices) {
            combo_add(audio_buffer_combo, choice.label);
        }
        combo_select_clamped(audio_buffer_combo, find_choice_index(audio_buffer_choices, static_cast<int>(settings_snapshot.buffer_samples), 1));
        CheckDlgButton(audio_options_window, static_cast<int>(audio_command_apply + 10), settings_snapshot.muted ? BST_CHECKED : BST_UNCHECKED);
    }

    void apply_audio_options() {
        if (!audio_options_window) {
            return;
        }

        AudioSettings new_settings{};
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            new_settings = audio_settings;
        }

        int selection = combo_selection(audio_volume_combo);
        if (selection >= 0 && selection < static_cast<int>(audio_volume_choices.size())) {
            new_settings.volume_percent = audio_volume_choices[selection].value;
        }

        selection = combo_selection(audio_rate_combo);
        if (selection >= 0 && selection < static_cast<int>(audio_rate_choices.size())) {
            new_settings.output_rate = static_cast<uint32_t>(audio_rate_choices[selection].value);
        }

        selection = combo_selection(audio_buffer_combo);
        if (selection >= 0 && selection < static_cast<int>(audio_buffer_choices.size())) {
            new_settings.buffer_samples = static_cast<uint16_t>(audio_buffer_choices[selection].value);
        }

        new_settings.muted = IsDlgButtonChecked(audio_options_window, static_cast<int>(audio_command_apply + 10)) == BST_CHECKED;

        bool restart_audio = false;
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            restart_audio = new_settings.output_rate != audio_settings.output_rate || new_settings.buffer_samples != audio_settings.buffer_samples;
            audio_settings = new_settings;
        }

        if (restart_audio && audio_device != 0) {
            reset_audio(new_settings.output_rate);
        }
        save_recut_settings();
    }

    LRESULT CALLBACK audio_options_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            audio_options_window = hwnd;
            EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);
            create_label(hwnd, L"Volume", 18, 20, 120);
            audio_volume_combo = create_combo(hwnd, audio_command_apply + 1, 150, 16, 190);
            audio_mute_checkbox = create_checkbox(hwnd, L"Mute", audio_command_apply + 10, 150, 52, 120);
            create_label(hwnd, L"Output rate", 18, 90, 120);
            audio_rate_combo = create_combo(hwnd, audio_command_apply + 2, 150, 86, 190);
            create_label(hwnd, L"Buffer size", 18, 126, 120);
            audio_buffer_combo = create_combo(hwnd, audio_command_apply + 3, 150, 122, 190);
            create_button(hwnd, L"Apply", audio_command_apply, 156, 172, 88);
            create_button(hwnd, L"Close", audio_command_close, 254, 172, 88);
            fill_audio_options_controls();
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case audio_command_apply:
                apply_audio_options();
                return 0;
            case audio_command_close:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            audio_options_window = nullptr;
            audio_volume_combo = nullptr;
            audio_rate_combo = nullptr;
            audio_buffer_combo = nullptr;
            audio_mute_checkbox = nullptr;
            return 0;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void show_audio_options_window() {
        if (!main_window) {
            return;
        }

        if (!audio_options_window) {
            const wchar_t* class_name = L"PaperMarioReCutAudioOptions";
            static bool registered = false;
            if (!registered) {
                WNDCLASSW window_class{};
                window_class.lpfnWndProc = audio_options_window_proc;
                window_class.hInstance = GetModuleHandleW(nullptr);
                window_class.lpszClassName = class_name;
                window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
                window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
                RegisterClassW(&window_class);
                registered = true;
            }

            audio_options_window = CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                class_name,
                L"Audio Options",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 370, 250,
                main_window,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            center_dialog_on_main(audio_options_window, 370, 250);
        }
        else {
            fill_audio_options_controls();
        }

        ShowWindow(audio_options_window, SW_SHOWNORMAL);
        SetForegroundWindow(audio_options_window);
    }

    void destroy_audio_options_window() {
        if (audio_options_window) {
            DestroyWindow(audio_options_window);
            audio_options_window = nullptr;
        }
    }

    std::wstring gamepad_binding_label(const GamepadBinding& binding) {
        if (binding.kind == GamepadBindingKind::Button) {
            const char* name = SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(binding.code));
            std::wstring wide = utf8_to_wide(name);
            return wide.empty() ? L"Unknown Button" : L"Button " + wide;
        }

        const char* name = SDL_GameControllerGetStringForAxis(static_cast<SDL_GameControllerAxis>(binding.code));
        std::wstring wide = utf8_to_wide(name);
        if (wide.empty()) {
            wide = L"Unknown Axis";
        }
        return wide + (binding.kind == GamepadBindingKind::AxisPositive ? L" +" : L" -");
    }

    void build_gamepad_binding_choices() {
        gamepad_binding_choices.clear();
        const SDL_GameControllerButton buttons[] = {
            SDL_CONTROLLER_BUTTON_A,
            SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_X,
            SDL_CONTROLLER_BUTTON_Y,
            SDL_CONTROLLER_BUTTON_BACK,
            SDL_CONTROLLER_BUTTON_START,
            SDL_CONTROLLER_BUTTON_LEFTSTICK,
            SDL_CONTROLLER_BUTTON_RIGHTSTICK,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
            SDL_CONTROLLER_BUTTON_DPAD_UP,
            SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_DPAD_LEFT,
            SDL_CONTROLLER_BUTTON_DPAD_RIGHT
        };
        for (SDL_GameControllerButton button : buttons) {
            gamepad_binding_choices.push_back({ GamepadBindingKind::Button, button });
        }

        const SDL_GameControllerAxis axes[] = {
            SDL_CONTROLLER_AXIS_LEFTX,
            SDL_CONTROLLER_AXIS_LEFTY,
            SDL_CONTROLLER_AXIS_RIGHTX,
            SDL_CONTROLLER_AXIS_RIGHTY,
            SDL_CONTROLLER_AXIS_TRIGGERLEFT,
            SDL_CONTROLLER_AXIS_TRIGGERRIGHT
        };
        for (SDL_GameControllerAxis axis : axes) {
            gamepad_binding_choices.push_back({ GamepadBindingKind::AxisPositive, axis });
            gamepad_binding_choices.push_back({ GamepadBindingKind::AxisNegative, axis });
        }
    }

    int find_gamepad_binding_index(const GamepadBinding& binding) {
        for (size_t i = 0; i < gamepad_binding_choices.size(); i++) {
            if (gamepad_binding_choices[i].kind == binding.kind && gamepad_binding_choices[i].code == binding.code) {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    std::wstring keyboard_binding_label(SDL_Scancode scancode) {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES) {
            return L"";
        }

        std::wstring label = utf8_to_wide(SDL_GetScancodeName(scancode));
        return label.empty() ? L"" : label;
    }

    void set_keyboard_binding_field_text(int action_index, const wchar_t* override_text = nullptr) {
        if (action_index < 0 || action_index >= input_action_count) {
            return;
        }

        HWND field = input_keyboard_binding_fields[action_index];
        if (!field) {
            return;
        }

        if (override_text != nullptr) {
            SetWindowTextW(field, override_text);
            return;
        }

        const std::wstring label = keyboard_binding_label(input_window_pending_settings.keyboard_bindings[action_index]);
        SetWindowTextW(field, label.c_str());
    }

    void refresh_keyboard_binding_fields() {
        for (int i = 0; i < input_action_count; i++) {
            set_keyboard_binding_field_text(i);
        }
    }

    int max_keyboard_scroll_offset() {
        return std::max(0, input_keyboard_content_height - input_keyboard_view_height);
    }

    void update_keyboard_page_scrollbar() {
        if (!input_keyboard_page) {
            return;
        }

        SCROLLINFO scroll_info{};
        scroll_info.cbSize = sizeof(scroll_info);
        scroll_info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        scroll_info.nMin = 0;
        scroll_info.nMax = input_keyboard_content_height - 1;
        scroll_info.nPage = input_keyboard_view_height;
        scroll_info.nPos = input_keyboard_scroll_offset;
        SetScrollInfo(input_keyboard_page, SB_VERT, &scroll_info, TRUE);
    }

    void layout_keyboard_page_controls() {
        for (int i = 0; i < input_action_count; i++) {
            const int y = 8 + (i * 23) - input_keyboard_scroll_offset;
            if (input_keyboard_action_labels[i]) {
                SetWindowPos(input_keyboard_action_labels[i], nullptr, 0, y + 2, 144, 18, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (input_keyboard_binding_fields[i]) {
                SetWindowPos(input_keyboard_binding_fields[i], nullptr, 154, y, 230, 22, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        if (input_mouse_checkbox) {
            SetWindowPos(input_mouse_checkbox, nullptr, 0, 430 - input_keyboard_scroll_offset, 220, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        update_keyboard_page_scrollbar();
    }

    void scroll_keyboard_page_to(int offset) {
        input_keyboard_scroll_offset = std::clamp(offset, 0, max_keyboard_scroll_offset());
        layout_keyboard_page_controls();
    }

    void scroll_keyboard_page_by(int delta) {
        scroll_keyboard_page_to(input_keyboard_scroll_offset + delta);
    }

    SDL_Scancode scancode_from_windows_key(WPARAM key, LPARAM key_info) {
        const bool extended = (key_info & (1 << 24)) != 0;

        if (key >= 'A' && key <= 'Z') {
            return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (key - 'A'));
        }
        if (key >= '1' && key <= '9') {
            return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (key - '1'));
        }
        if (key == '0') {
            return SDL_SCANCODE_0;
        }
        if (key >= VK_F1 && key <= VK_F12) {
            return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (key - VK_F1));
        }

        switch (key) {
        case VK_BACK: return SDL_SCANCODE_BACKSPACE;
        case VK_TAB: return SDL_SCANCODE_TAB;
        case VK_RETURN: return extended ? SDL_SCANCODE_KP_ENTER : SDL_SCANCODE_RETURN;
        case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
        case VK_SPACE: return SDL_SCANCODE_SPACE;
        case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
        case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
        case VK_END: return SDL_SCANCODE_END;
        case VK_HOME: return SDL_SCANCODE_HOME;
        case VK_LEFT: return SDL_SCANCODE_LEFT;
        case VK_UP: return SDL_SCANCODE_UP;
        case VK_RIGHT: return SDL_SCANCODE_RIGHT;
        case VK_DOWN: return SDL_SCANCODE_DOWN;
        case VK_INSERT: return SDL_SCANCODE_INSERT;
        case VK_DELETE: return SDL_SCANCODE_DELETE;
        case VK_SHIFT: return ((key_info >> 16) & 0xff) == 0x36 ? SDL_SCANCODE_RSHIFT : SDL_SCANCODE_LSHIFT;
        case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
        case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
        case VK_CONTROL: return extended ? SDL_SCANCODE_RCTRL : SDL_SCANCODE_LCTRL;
        case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
        case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
        case VK_MENU: return extended ? SDL_SCANCODE_RALT : SDL_SCANCODE_LALT;
        case VK_LMENU: return SDL_SCANCODE_LALT;
        case VK_RMENU: return SDL_SCANCODE_RALT;
        case VK_LWIN: return SDL_SCANCODE_LGUI;
        case VK_RWIN: return SDL_SCANCODE_RGUI;
        case VK_APPS: return SDL_SCANCODE_APPLICATION;
        case VK_CAPITAL: return SDL_SCANCODE_CAPSLOCK;
        case VK_NUMPAD0: return SDL_SCANCODE_KP_0;
        case VK_NUMPAD1: return SDL_SCANCODE_KP_1;
        case VK_NUMPAD2: return SDL_SCANCODE_KP_2;
        case VK_NUMPAD3: return SDL_SCANCODE_KP_3;
        case VK_NUMPAD4: return SDL_SCANCODE_KP_4;
        case VK_NUMPAD5: return SDL_SCANCODE_KP_5;
        case VK_NUMPAD6: return SDL_SCANCODE_KP_6;
        case VK_NUMPAD7: return SDL_SCANCODE_KP_7;
        case VK_NUMPAD8: return SDL_SCANCODE_KP_8;
        case VK_NUMPAD9: return SDL_SCANCODE_KP_9;
        case VK_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY;
        case VK_ADD: return SDL_SCANCODE_KP_PLUS;
        case VK_SUBTRACT: return SDL_SCANCODE_KP_MINUS;
        case VK_DECIMAL: return SDL_SCANCODE_KP_PERIOD;
        case VK_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
        case VK_OEM_1: return SDL_SCANCODE_SEMICOLON;
        case VK_OEM_PLUS: return SDL_SCANCODE_EQUALS;
        case VK_OEM_COMMA: return SDL_SCANCODE_COMMA;
        case VK_OEM_MINUS: return SDL_SCANCODE_MINUS;
        case VK_OEM_PERIOD: return SDL_SCANCODE_PERIOD;
        case VK_OEM_2: return SDL_SCANCODE_SLASH;
        case VK_OEM_3: return SDL_SCANCODE_GRAVE;
        case VK_OEM_4: return SDL_SCANCODE_LEFTBRACKET;
        case VK_OEM_5: return SDL_SCANCODE_BACKSLASH;
        case VK_OEM_6: return SDL_SCANCODE_RIGHTBRACKET;
        case VK_OEM_7: return SDL_SCANCODE_APOSTROPHE;
        default: return SDL_SCANCODE_UNKNOWN;
        }
    }

    void start_keyboard_binding_capture(int action_index) {
        if (action_index < 0 || action_index >= input_action_count) {
            return;
        }

        if (input_keyboard_capture_action >= 0 && input_keyboard_capture_action != action_index) {
            set_keyboard_binding_field_text(input_keyboard_capture_action);
        }

        input_keyboard_capture_action = action_index;
        set_keyboard_binding_field_text(action_index, L"Press a key...");
        SetFocus(input_keyboard_binding_fields[action_index]);
    }

    void finish_keyboard_binding_capture(int action_index, WPARAM key, LPARAM key_info) {
        const SDL_Scancode scancode = scancode_from_windows_key(key, key_info);
        if (scancode == SDL_SCANCODE_UNKNOWN) {
            MessageBeep(MB_ICONWARNING);
            return;
        }

        if (!input_window_pending_valid) {
            std::lock_guard<std::mutex> lock(settings_mutex);
            input_window_pending_settings = input_settings;
            input_window_pending_valid = true;
        }

        input_window_pending_settings.keyboard_bindings[action_index] = scancode;
        input_keyboard_capture_action = -1;
        set_keyboard_binding_field_text(action_index);
    }

    LRESULT CALLBACK keyboard_binding_field_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        const int action_index = static_cast<int>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (message) {
        case WM_LBUTTONDOWN:
            start_keyboard_binding_capture(action_index);
            return 0;
        case WM_GETDLGCODE:
            if (input_keyboard_capture_action == action_index) {
                return DLGC_WANTALLKEYS;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (input_keyboard_capture_action == action_index) {
                finish_keyboard_binding_capture(action_index, wparam, lparam);
                return 0;
            }
            break;
        case WM_CHAR:
        case WM_SYSCHAR:
            if (input_keyboard_capture_action == action_index) {
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (input_keyboard_page) {
                SendMessageW(input_keyboard_page, message, wparam, lparam);
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            if (input_keyboard_capture_action == action_index) {
                input_keyboard_capture_action = -1;
                set_keyboard_binding_field_text(action_index);
            }
            break;
        }

        return keyboard_binding_field_proc_prev
            ? CallWindowProcW(keyboard_binding_field_proc_prev, hwnd, message, wparam, lparam)
            : DefWindowProcW(hwnd, message, wparam, lparam);
    }

    HWND create_keyboard_binding_field(HWND parent, int action_index, int x, int y, int width) {
        HWND field = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_READONLY | ES_AUTOHSCROLL,
            x, y, width, 22,
            parent, reinterpret_cast<HMENU>(input_keyboard_binding_field_base + action_index), GetModuleHandleW(nullptr), nullptr);
        use_ui_font(field);
        SetWindowTheme(field, L"Explorer", nullptr);
        SetWindowLongPtrW(field, GWLP_USERDATA, action_index);
        WNDPROC previous_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(field, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(keyboard_binding_field_proc)));
        if (!keyboard_binding_field_proc_prev) {
            keyboard_binding_field_proc_prev = previous_proc;
        }
        return field;
    }

    LRESULT CALLBACK keyboard_input_page_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            input_keyboard_scroll_offset = 0;
            update_keyboard_page_scrollbar();
            return 0;
        case WM_MOUSEWHEEL: {
            const int wheel_delta = GET_WHEEL_DELTA_WPARAM(wparam);
            if (wheel_delta != 0) {
                scroll_keyboard_page_by(-(wheel_delta / WHEEL_DELTA) * 48);
                return 0;
            }
            break;
        }
        case WM_VSCROLL: {
            SCROLLINFO scroll_info{};
            scroll_info.cbSize = sizeof(scroll_info);
            scroll_info.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &scroll_info);

            int target = input_keyboard_scroll_offset;
            switch (LOWORD(wparam)) {
            case SB_LINEUP: target -= 24; break;
            case SB_LINEDOWN: target += 24; break;
            case SB_PAGEUP: target -= input_keyboard_view_height; break;
            case SB_PAGEDOWN: target += input_keyboard_view_height; break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK: target = scroll_info.nTrackPos; break;
            case SB_TOP: target = 0; break;
            case SB_BOTTOM: target = max_keyboard_scroll_offset(); break;
            default: break;
            }

            scroll_keyboard_page_to(target);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    std::wstring controller_profile_name(const char* controller_name) {
        std::string lower = controller_name ? controller_name : "";
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower.find("playstation") != std::string::npos ||
            lower.find("dualsense") != std::string::npos ||
            lower.find("dualshock") != std::string::npos ||
            lower.find("ps4") != std::string::npos ||
            lower.find("ps5") != std::string::npos) {
            return L"Automatic profile: PlayStation";
        }
        if (lower.find("xbox") != std::string::npos ||
            lower.find("xinput") != std::string::npos) {
            return L"Automatic profile: Xbox";
        }
        return L"Automatic profile: SDL Gamepad";
    }

    void update_input_profile_label() {
        if (!input_profile_label || !input_controller_combo) {
            return;
        }

        const int selection = combo_selection(input_controller_combo);
        if (selection >= 0 && selection < static_cast<int>(input_controller_choices.size())) {
            const int device_index = input_controller_choices[selection];
            const char* name = SDL_GameControllerNameForIndex(device_index);
            std::wstring label = controller_profile_name(name);
            SetWindowTextW(input_profile_label, label.c_str());
        }
        else {
            SetWindowTextW(input_profile_label, L"No controller selected");
        }
    }

    void fill_input_options_controls() {
        if (!input_options_window || !input_controller_combo) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            input_window_pending_settings = input_settings;
        }
        input_window_pending_valid = true;

        input_controller_choices.clear();
        SendMessageW(input_controller_combo, CB_RESETCONTENT, 0, 0);
        const int joystick_count = SDL_NumJoysticks();
        int preferred_selection = 0;
        for (int i = 0; i < joystick_count; i++) {
            if (!SDL_IsGameController(i)) {
                continue;
            }

            input_controller_choices.push_back(i);
            std::wstring label = utf8_to_wide(SDL_GameControllerNameForIndex(i));
            if (label.empty()) {
                label = L"Gamepad";
            }
            combo_add(input_controller_combo, label.c_str());
            if (i == input_window_pending_settings.preferred_controller_index) {
                preferred_selection = static_cast<int>(input_controller_choices.size()) - 1;
            }
        }
        if (input_controller_choices.empty()) {
            combo_add(input_controller_combo, L"No SDL gamepad found");
        }
        combo_select_clamped(input_controller_combo, preferred_selection);
        update_input_profile_label();

        refresh_keyboard_binding_fields();
        CheckDlgButton(input_options_window, static_cast<int>(input_command_apply + 20), input_window_pending_settings.mouse_click_to_move ? BST_CHECKED : BST_UNCHECKED);
    }

    void show_input_page(int page_index) {
        ShowWindow(input_gamepad_page, page_index == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(input_keyboard_page, page_index == 1 ? SW_SHOW : SW_HIDE);
    }

    void apply_automatic_gamepad_profile() {
        input_window_pending_settings.gamepad_bindings = make_default_input_settings().gamepad_bindings;
        fill_gamepad_rebind_controls();
    }

    void apply_input_options() {
        if (!input_options_window || !input_window_pending_valid) {
            return;
        }

        input_window_pending_settings.mouse_click_to_move =
            IsDlgButtonChecked(input_options_window, static_cast<int>(input_command_apply + 20)) == BST_CHECKED;

        const int controller_selection = combo_selection(input_controller_combo);
        if (controller_selection >= 0 && controller_selection < static_cast<int>(input_controller_choices.size())) {
            input_window_pending_settings.preferred_controller_index = input_controller_choices[controller_selection];
        }

        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            input_settings = input_window_pending_settings;
        }
        select_controller_index(input_window_pending_settings.preferred_controller_index);
        save_recut_settings();
    }

    std::wstring selected_controller_name() {
        if (input_controller_combo) {
            const int selection = combo_selection(input_controller_combo);
            if (selection >= 0 && selection < static_cast<int>(input_controller_choices.size())) {
                std::wstring label = utf8_to_wide(SDL_GameControllerNameForIndex(input_controller_choices[selection]));
                if (!label.empty()) {
                    return label;
                }
            }
        }

        if (controller != nullptr) {
            std::wstring label = utf8_to_wide(SDL_GameControllerName(controller));
            if (!label.empty()) {
                return label;
            }
        }

        return L"SDL Gamepad";
    }

    POINT gamepad_rebind_position(int action_index) {
        static constexpr POINT positions[input_action_count] = {
            { 586, 246 }, // A
            { 492, 246 }, // B
            { 345, 248 }, // Start
            { 345, 306 }, // Z
            { 122, 86 },  // L
            { 556, 86 },  // R
            { 620, 164 }, // C Up
            { 620, 314 }, // C Down
            { 524, 224 }, // C Left
            { 672, 224 }, // C Right
            { 122, 164 }, // D-Pad Up
            { 122, 314 }, // D-Pad Down
            { 38, 236 },  // D-Pad Left
            { 206, 236 }, // D-Pad Right
            { 292, 150 }, // Stick Up
            { 292, 326 }, // Stick Down
            { 216, 236 }, // Stick Left
            { 358, 236 }  // Stick Right
        };
        return positions[std::clamp(action_index, 0, input_action_count - 1)];
    }

    void populate_gamepad_rebind_combo(HWND combo, const GamepadBinding& binding) {
        if (!combo) {
            return;
        }

        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const GamepadBinding& choice : gamepad_binding_choices) {
            const std::wstring label = gamepad_binding_label(choice);
            combo_add(combo, label.c_str());
        }
        combo_select_clamped(combo, find_gamepad_binding_index(binding));
    }

    void fill_gamepad_rebind_controls() {
        if (!gamepad_rebind_window) {
            return;
        }

        if (!input_window_pending_valid) {
            std::lock_guard<std::mutex> lock(settings_mutex);
            input_window_pending_settings = input_settings;
            input_window_pending_valid = true;
        }

        build_gamepad_binding_choices();
        for (int i = 0; i < input_action_count; i++) {
            populate_gamepad_rebind_combo(gamepad_rebind_combos[i], input_window_pending_settings.gamepad_bindings[i]);
        }
    }

    void apply_gamepad_rebind_window() {
        if (!gamepad_rebind_window) {
            return;
        }

        build_gamepad_binding_choices();
        for (int i = 0; i < input_action_count; i++) {
            const int binding_index = combo_selection(gamepad_rebind_combos[i]);
            if (binding_index >= 0 && binding_index < static_cast<int>(gamepad_binding_choices.size())) {
                input_window_pending_settings.gamepad_bindings[i] = gamepad_binding_choices[binding_index];
            }
        }
        input_window_pending_valid = true;

        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            input_settings = input_window_pending_settings;
        }
        save_recut_settings();

    }

    void draw_controller_shape(HDC dc) {
        HPEN outline = CreatePen(PS_SOLID, 3, GetSysColor(COLOR_WINDOWTEXT));
        HBRUSH body = CreateSolidBrush(RGB(235, 238, 242));
        HBRUSH accent = CreateSolidBrush(RGB(218, 224, 232));
        HPEN old_pen = reinterpret_cast<HPEN>(SelectObject(dc, outline));
        HBRUSH old_brush = reinterpret_cast<HBRUSH>(SelectObject(dc, body));

        RoundRect(dc, 132, 122, 680, 374, 92, 92);
        RoundRect(dc, 62, 180, 260, 374, 72, 72);
        RoundRect(dc, 552, 180, 750, 374, 72, 72);
        RoundRect(dc, 132, 70, 286, 134, 24, 24);
        RoundRect(dc, 526, 70, 680, 134, 24, 24);

        SelectObject(dc, accent);
        Ellipse(dc, 276, 208, 364, 296);
        RoundRect(dc, 110, 232, 214, 280, 10, 10);
        RoundRect(dc, 138, 204, 186, 308, 10, 10);
        Ellipse(dc, 566, 206, 612, 252);
        Ellipse(dc, 624, 206, 670, 252);
        Ellipse(dc, 566, 274, 612, 320);
        Ellipse(dc, 624, 274, 670, 320);
        RoundRect(dc, 360, 240, 452, 284, 18, 18);

        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(accent);
        DeleteObject(body);
        DeleteObject(outline);
    }

    LRESULT CALLBACK gamepad_rebind_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE: {
            gamepad_rebind_window = hwnd;
            EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);
            std::fill(gamepad_rebind_combos.begin(), gamepad_rebind_combos.end(), nullptr);

            const std::wstring title = L"Controller: " + selected_controller_name();
            create_label(hwnd, title.c_str(), 18, 14, 680, 22);

            for (int i = 0; i < input_action_count; i++) {
                POINT position = gamepad_rebind_position(i);
                create_label(hwnd, input_actions[i].label, position.x, position.y - 18, 132, 18);
                gamepad_rebind_combos[i] = create_combo(hwnd, gamepad_rebind_combo_base + i, position.x, position.y, 142);
            }

            create_button(hwnd, L"Apply", gamepad_rebind_command_apply, 548, 478, 88);
            create_button(hwnd, L"Close", gamepad_rebind_command_close, 646, 478, 88);
            fill_gamepad_rebind_controls();
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect{};
            GetClientRect(hwnd, &rect);
            FillRect(dc, &rect, GetSysColorBrush(COLOR_WINDOW));
            draw_controller_shape(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case gamepad_rebind_command_apply:
                apply_gamepad_rebind_window();
                return 0;
            case gamepad_rebind_command_close:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            gamepad_rebind_window = nullptr;
            std::fill(gamepad_rebind_combos.begin(), gamepad_rebind_combos.end(), nullptr);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void show_gamepad_rebind_window() {
        if (!main_window) {
            return;
        }

        if (!gamepad_rebind_window) {
            const wchar_t* class_name = L"PaperMarioReCutGamepadRebind";
            static bool registered = false;
            if (!registered) {
                WNDCLASSW window_class{};
                window_class.lpfnWndProc = gamepad_rebind_window_proc;
                window_class.hInstance = GetModuleHandleW(nullptr);
                window_class.lpszClassName = class_name;
                window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
                window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
                RegisterClassW(&window_class);
                registered = true;
            }

            gamepad_rebind_window = CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                class_name,
                L"Gamepad Rebind",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 780, 560,
                input_options_window ? input_options_window : main_window,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            center_dialog_on_main(gamepad_rebind_window, 780, 560);
        }
        else {
            fill_gamepad_rebind_controls();
        }

        ShowWindow(gamepad_rebind_window, SW_SHOWNORMAL);
        InvalidateRect(gamepad_rebind_window, nullptr, TRUE);
        SetForegroundWindow(gamepad_rebind_window);
    }

    LRESULT CALLBACK input_options_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE: {
            input_options_window = hwnd;
            EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);
            const wchar_t* keyboard_page_class_name = L"PaperMarioReCutKeyboardInputPage";
            static bool keyboard_page_registered = false;
            if (!keyboard_page_registered) {
                WNDCLASSW keyboard_page_class{};
                keyboard_page_class.lpfnWndProc = keyboard_input_page_proc;
                keyboard_page_class.hInstance = GetModuleHandleW(nullptr);
                keyboard_page_class.lpszClassName = keyboard_page_class_name;
                keyboard_page_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
                keyboard_page_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
                RegisterClassW(&keyboard_page_class);
                keyboard_page_registered = true;
            }

            input_tab_control = CreateWindowExW(
                0, WC_TABCONTROLW, nullptr,
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TCS_FIXEDWIDTH,
                12, 12, 500, 292,
                hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            use_ui_font(input_tab_control);

            TCITEMW gamepad_tab{};
            gamepad_tab.mask = TCIF_TEXT;
            gamepad_tab.pszText = const_cast<wchar_t*>(L"Gamepad");
            SendMessageW(input_tab_control, TCM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&gamepad_tab));
            TCITEMW keyboard_tab{};
            keyboard_tab.mask = TCIF_TEXT;
            keyboard_tab.pszText = const_cast<wchar_t*>(L"Keyboard");
            SendMessageW(input_tab_control, TCM_INSERTITEMW, 1, reinterpret_cast<LPARAM>(&keyboard_tab));
            SendMessageW(input_tab_control, TCM_SETITEMSIZE, 0, MAKELPARAM(126, 28));

            input_gamepad_page = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE, 24, 44, 476, 248, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            input_keyboard_page = CreateWindowExW(0, keyboard_page_class_name, nullptr, WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 24, 44, 476, input_keyboard_view_height, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

            create_label(input_gamepad_page, L"Controller", 0, 4, 110);
            input_controller_combo = create_combo(input_gamepad_page, input_command_apply + 1, 128, 0, 310);
            input_profile_label = create_label(input_gamepad_page, L"Automatic profile: SDL Gamepad", 128, 36, 310);
            create_button(input_gamepad_page, L"Apply Automatic Profile", input_command_apply_profile, 128, 64, 180);
            create_button(input_gamepad_page, L"Rebind", input_command_open_gamepad_rebind, 318, 64, 120);

            for (int i = 0; i < input_action_count; i++) {
                const int y = 8 + (i * 23);
                input_keyboard_action_labels[i] = create_label(input_keyboard_page, input_actions[i].label, 0, y + 2, 144, 18);
                input_keyboard_binding_fields[i] = create_keyboard_binding_field(input_keyboard_page, i, 154, y, 230);
            }
            input_mouse_checkbox = create_checkbox(input_keyboard_page, L"Mouse Click-To-Move", input_command_apply + 20, 0, 430, 220);
            scroll_keyboard_page_to(0);

            create_button(hwnd, L"Apply", input_command_apply, 326, 318, 88);
            create_button(hwnd, L"Close", input_command_close, 424, 318, 88);
            fill_input_options_controls();
            show_input_page(0);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == input_tab_control &&
                reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE) {
                show_input_page(TabCtrl_GetCurSel(input_tab_control));
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (input_tab_control && input_keyboard_page && TabCtrl_GetCurSel(input_tab_control) == 1) {
                SendMessageW(input_keyboard_page, message, wparam, lparam);
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case input_command_apply:
                apply_input_options();
                return 0;
            case input_command_close:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            case input_command_apply_profile:
                apply_automatic_gamepad_profile();
                return 0;
            case input_command_open_gamepad_rebind:
                show_gamepad_rebind_window();
                return 0;
            default:
                if (reinterpret_cast<HWND>(lparam) == input_controller_combo && HIWORD(wparam) == CBN_SELCHANGE) {
                    update_input_profile_label();
                    return 0;
                }
                break;
            }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            input_options_window = nullptr;
            input_tab_control = nullptr;
            input_gamepad_page = nullptr;
            input_keyboard_page = nullptr;
            input_controller_combo = nullptr;
            input_profile_label = nullptr;
            input_mouse_checkbox = nullptr;
            input_keyboard_action_labels.fill(nullptr);
            input_keyboard_binding_fields.fill(nullptr);
            input_keyboard_capture_action = -1;
            input_keyboard_scroll_offset = 0;
            input_window_pending_valid = false;
            return 0;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void show_input_options_window() {
        if (!main_window) {
            return;
        }

        if (!input_options_window) {
            const wchar_t* class_name = L"PaperMarioReCutInputOptions";
            static bool registered = false;
            if (!registered) {
                WNDCLASSW window_class{};
                window_class.lpfnWndProc = input_options_window_proc;
                window_class.hInstance = GetModuleHandleW(nullptr);
                window_class.lpszClassName = class_name;
                window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
                window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
                RegisterClassW(&window_class);
                registered = true;
            }

            input_options_window = CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                class_name,
                L"Input Options",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 540, 400,
                main_window,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            center_dialog_on_main(input_options_window, 540, 400);
        }
        else {
            fill_input_options_controls();
        }

        ShowWindow(input_options_window, SW_SHOWNORMAL);
        SetForegroundWindow(input_options_window);
    }

    void destroy_input_options_window() {
        if (gamepad_rebind_window) {
            DestroyWindow(gamepad_rebind_window);
            gamepad_rebind_window = nullptr;
        }
        if (input_options_window) {
            DestroyWindow(input_options_window);
            input_options_window = nullptr;
        }
    }

    LRESULT CALLBACK texture_replacement_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE: {
            texture_replacement_window = hwnd;
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);

            texture_live_replacement_checkbox = CreateWindowExW(
                0, L"BUTTON", L"Live Texture Replacement",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                20, 18, 240, 24,
                hwnd, reinterpret_cast<HMENU>(texture_command_live_replacement), GetModuleHandleW(nullptr), nullptr);

            texture_dump_button = CreateWindowExW(
                0, L"BUTTON", L"Dump Textures",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                20, 56, 122, 30,
                hwnd, reinterpret_cast<HMENU>(texture_command_dump_textures), GetModuleHandleW(nullptr), nullptr);

            HWND reload_button = CreateWindowExW(
                0, L"BUTTON", L"Reload Folder",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                152, 56, 122, 30,
                hwnd, reinterpret_cast<HMENU>(texture_command_reload), GetModuleHandleW(nullptr), nullptr);

            HWND open_button = CreateWindowExW(
                0, L"BUTTON", L"Open Folder",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                284, 56, 104, 30,
                hwnd, reinterpret_cast<HMENU>(texture_command_open_folder), GetModuleHandleW(nullptr), nullptr);

            texture_dump_progress = CreateWindowExW(
                0, PROGRESS_CLASSW, nullptr,
                WS_CHILD | PBS_SMOOTH,
                20, 98, 368, 18,
                hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

            SetWindowTheme(texture_live_replacement_checkbox, L"Explorer", nullptr);
            SetWindowTheme(texture_dump_button, L"Explorer", nullptr);
            SetWindowTheme(reload_button, L"Explorer", nullptr);
            SetWindowTheme(open_button, L"Explorer", nullptr);
            SetWindowTheme(texture_dump_progress, L"Explorer", nullptr);
            SendMessageW(texture_live_replacement_checkbox, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(texture_dump_button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(reload_button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(open_button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(texture_dump_progress, PBM_SETRANGE32, 0, 100);
            ShowWindow(texture_dump_progress, texture_dump_pass_active ? SW_SHOW : SW_HIDE);
            refresh_texture_replacement_window();
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_TIMER:
            if (wparam == texture_dump_timer) {
                update_texture_dump_progress();
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case texture_command_live_replacement:
                if (HIWORD(wparam) == BN_CLICKED) {
                    const bool enabled = IsDlgButtonChecked(hwnd, static_cast<int>(texture_command_live_replacement)) == BST_CHECKED;
                    set_live_texture_replacement_enabled(enabled);
                    return 0;
                }
                break;
            case texture_command_dump_textures:
                start_texture_dump_pass();
                return 0;
            case texture_command_reload:
                reload_texture_replacement_folder();
                return 0;
            case texture_command_open_folder:
                open_texture_replacement_folder();
                return 0;
            }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, texture_dump_timer);
            texture_replacement_window = nullptr;
            texture_live_replacement_checkbox = nullptr;
            texture_dump_button = nullptr;
            texture_dump_progress = nullptr;
            return 0;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void show_texture_replacement_window() {
        if (!main_window) {
            return;
        }

        if (!texture_replacement_window) {
            const wchar_t* class_name = L"PaperMarioReCutTextureReplacement";
            static bool registered = false;
            if (!registered) {
                WNDCLASSW window_class{};
                window_class.lpfnWndProc = texture_replacement_window_proc;
                window_class.hInstance = GetModuleHandleW(nullptr);
                window_class.lpszClassName = class_name;
                window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
                window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
                RegisterClassW(&window_class);
                registered = true;
            }

            RECT owner_rect{};
            GetWindowRect(main_window, &owner_rect);
            constexpr int dialog_width = 420;
            constexpr int dialog_height = 170;
            const int owner_width = static_cast<int>(owner_rect.right - owner_rect.left);
            const int owner_height = static_cast<int>(owner_rect.bottom - owner_rect.top);
            const int x = static_cast<int>(owner_rect.left) + std::max(0, (owner_width - dialog_width) / 2);
            const int y = static_cast<int>(owner_rect.top) + std::max(0, (owner_height - dialog_height) / 2);

            texture_replacement_window = CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                class_name,
                L"Texture Replacement",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                x, y, dialog_width, dialog_height,
                main_window,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
        }

        refresh_texture_replacement_window();
        ShowWindow(texture_replacement_window, SW_SHOWNORMAL);
        SetForegroundWindow(texture_replacement_window);
    }

    void destroy_texture_replacement_window() {
        if (texture_replacement_window) {
            DestroyWindow(texture_replacement_window);
            texture_replacement_window = nullptr;
        }
        texture_live_replacement_checkbox = nullptr;
        texture_dump_button = nullptr;
        texture_dump_progress = nullptr;
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

    bool open_controller_index(int device_index) {
        if (device_index < 0 || device_index >= SDL_NumJoysticks() || !SDL_IsGameController(device_index)) {
            return false;
        }

        SDL_GameController* opened_controller = SDL_GameControllerOpen(device_index);
        if (opened_controller == nullptr) {
            return false;
        }

        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
        }
        controller = opened_controller;
        active_controller_device_index = device_index;
        return true;
    }

    void open_first_controller() {
        if (controller != nullptr) {
            return;
        }

        const int count = SDL_NumJoysticks();
        int preferred_controller_index = 0;
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            preferred_controller_index = input_settings.preferred_controller_index;
        }

        if (open_controller_index(preferred_controller_index)) {
            return;
        }

        for (int i = 0; i < count; i++) {
            if (i != preferred_controller_index && open_controller_index(i)) {
                return;
            }
        }
    }

    void select_controller_index(int device_index) {
        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
            controller = nullptr;
            active_controller_device_index = -1;
        }

        if (!open_controller_index(device_index)) {
            open_first_controller();
        }
    }

    ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
        SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT, "0");
        SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
            show_message(SDL_GetError());
            std::exit(EXIT_FAILURE);
        }
#ifdef _WIN32
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_TAB_CLASSES;
        InitCommonControlsEx(&controls);
        SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS | STAP_ALLOW_WEBCONTENT);
        SDL_EventState(SDL_SYSWMEVENT, SDL_DISABLE);
        SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
        SDL_EventState(SDL_MOUSEWHEEL, SDL_IGNORE);
#endif

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
        install_app_window_proc();
        set_app_menu_bar_visible(false);
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
                    active_controller_device_index = -1;
                    open_first_controller();
                }
            }
        }

#ifdef _WIN32
        static bool f1_was_down = false;
        static bool f2_was_down = false;
        static bool f8_was_down = false;
        const bool foreground = GetForegroundWindow() == main_window;
        bool f1_down = foreground && (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        if (f1_down && !f1_was_down) {
            toggle_app_menu_bar();
        }
        f1_was_down = f1_down;

        bool f2_down = foreground && (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        if (f2_down && !f2_was_down) {
            set_live_texture_replacement_enabled(!texture_live_replacement_enabled);
        }
        f2_was_down = f2_down;

        bool f8_down = foreground && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        if (f8_down && !f8_was_down) {
            show_texture_replacement_window();
        }
        f8_was_down = f8_down;

        refresh_texture_replacement_window();
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

        uint16_t buffer_samples = 256;
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            buffer_samples = audio_settings.buffer_samples;
        }

        SDL_AudioSpec desired{};
        desired.freq = static_cast<int>(output_freq);
        desired.format = AUDIO_F32;
        desired.channels = static_cast<Uint8>(output_channels);
        desired.samples = buffer_samples;
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
            uint32_t configured_output_rate = 48000;
            {
                std::lock_guard<std::mutex> lock(settings_mutex);
                configured_output_rate = audio_settings.output_rate;
            }
            reset_audio(configured_output_rate);
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

        int volume_percent = 50;
        bool muted = false;
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            volume_percent = audio_settings.volume_percent;
            muted = audio_settings.muted;
        }

        const float output_gain = (muted ? 0.0f : std::clamp(volume_percent, 0, 200) / 100.0f) / 32768.0f;
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
        // SDL event pumping belongs to the host window thread. The game input
        // callback can run on a game/runtime thread, and pumping Win32 messages
        // from there makes foreground-window input handling fight the desktop.
    }

    float normalize_axis(Sint16 value) {
        if (std::abs(value) < 8000) {
            return 0.0f;
        }
        return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
    }

    float gamepad_binding_strength(const GamepadBinding& binding) {
        if (controller == nullptr) {
            return 0.0f;
        }

        if (binding.kind == GamepadBindingKind::Button) {
            if (binding.code < 0 || binding.code >= SDL_CONTROLLER_BUTTON_MAX) {
                return 0.0f;
            }
            return SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(binding.code)) ? 1.0f : 0.0f;
        }

        if (binding.code < 0 || binding.code >= SDL_CONTROLLER_AXIS_MAX) {
            return 0.0f;
        }

        const float axis_value = normalize_axis(SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(binding.code)));
        if (binding.kind == GamepadBindingKind::AxisPositive) {
            return std::max(axis_value, 0.0f);
        }
        return std::max(-axis_value, 0.0f);
    }

    void apply_input_action(int action_index, float strength, uint16_t& out_buttons, float& out_x, float& out_y) {
        if (action_index < 0 || action_index >= input_action_count || strength <= 0.0f) {
            return;
        }

        const InputActionDescriptor& action = input_actions[action_index];
        if (action.button != 0 && strength >= 0.5f) {
            out_buttons |= action.button;
        }
        out_x += action.axis_x * strength;
        out_y += action.axis_y * strength;
    }

    bool get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
        if (controller_num != 0) {
            return false;
        }

        if (texture_dump_input_paused.load(std::memory_order_relaxed)) {
            *buttons = 0;
            *x = 0.0f;
            *y = 0.0f;
            return true;
        }

        uint16_t out_buttons = 0;
        float out_x = 0.0f;
        float out_y = 0.0f;

        AppInputSettings input_snapshot{};
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            input_snapshot = input_settings;
        }

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        for (int i = 0; i < input_action_count; i++) {
            const SDL_Scancode scancode = input_snapshot.keyboard_bindings[i];
            if (scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES && keys[scancode]) {
                apply_input_action(i, 1.0f, out_buttons, out_x, out_y);
            }

            apply_input_action(i, gamepad_binding_strength(input_snapshot.gamepad_bindings[i]), out_buttons, out_x, out_y);
        }

        if (input_snapshot.mouse_click_to_move && window != nullptr) {
            bool mouse_controls_active = true;
#ifdef _WIN32
            mouse_controls_active = GetForegroundWindow() == main_window;
#endif
            if (mouse_controls_active) {
                int mouse_x = 0;
                int mouse_y = 0;
                const Uint32 mouse_state = SDL_GetMouseState(&mouse_x, &mouse_y);
                int window_width = 1;
                int window_height = 1;
                SDL_GetWindowSize(window, &window_width, &window_height);
                if ((mouse_state & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0) {
                    out_x = std::clamp((mouse_x - window_width * 0.5f) / std::max(window_width * 0.5f, 1.0f), -1.0f, 1.0f);
                    out_y = std::clamp((window_height * 0.5f - mouse_y) / std::max(window_height * 0.5f, 1.0f), -1.0f, 1.0f);
                }
                if ((mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0) {
                    out_buttons |= A_BUTTON;
                }
                if ((mouse_state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0) {
                    out_buttons |= B_BUTTON;
                }
            }
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

    std::filesystem::path app_executable_path() {
#ifdef _WIN32
        std::array<wchar_t, MAX_PATH> executable_path{};
        const DWORD length = GetModuleFileNameW(nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
        if (length != 0 && length < executable_path.size()) {
            return std::filesystem::path(executable_path.data());
        }
#endif
        return {};
    }

    std::filesystem::path app_base_path() {
        const std::filesystem::path executable = app_executable_path();
        if (!executable.empty()) {
            return executable.parent_path();
        }

        return std::filesystem::current_path();
    }

    std::filesystem::path app_config_path() {
        std::filesystem::path base = app_base_path() / "user";
        std::filesystem::create_directories(base);
        return base;
    }

    std::filesystem::path texture_root_path() {
        return app_base_path() / "textures";
    }

    std::filesystem::path texture_builtin_path() {
        return app_config_path() / "builtin_textures";
    }

    std::filesystem::path texture_replacement_path() {
        return texture_root_path() / "replacements";
    }

    std::filesystem::path texture_dump_path() {
        return texture_root_path() / "dumps";
    }

    bool ensure_texture_folder_layout() {
        std::error_code error;
        const std::filesystem::path root = texture_root_path();
        const std::filesystem::path starter_folders[] = {
            texture_replacement_path() / "sprites" / "unassigned",
            texture_replacement_path() / "models" / "unassigned",
            texture_replacement_path() / "masks" / "unassigned",
            texture_replacement_path() / "misc" / "unassigned",
            texture_dump_path() / "sprites" / "unassigned",
            texture_dump_path() / "models" / "unassigned",
            texture_dump_path() / "masks" / "unassigned",
            texture_dump_path() / "misc" / "unassigned",
            texture_dump_path() / "_debug" / "raw"
        };

        std::filesystem::create_directories(root, error);
        if (error) {
            return false;
        }

        for (const std::filesystem::path& starter_folder : starter_folders) {
            std::filesystem::create_directories(starter_folder, error);
            if (error) {
                return false;
            }
        }

        return ensure_texture_replacement_database(texture_replacement_path());
    }

    bool ensure_texture_replacement_database(const std::filesystem::path& directory) {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return false;
        }

        const std::filesystem::path database_path = directory / "rt64.json";
        if (std::filesystem::exists(database_path)) {
            return true;
        }

        std::ofstream database(database_path, std::ios::binary);
        if (!database.good()) {
            return false;
        }

        database <<
            "{\n"
            "    \"configuration\": {\n"
            "        \"configurationVersion\": 3,\n"
            "        \"autoPath\": \"rt64\",\n"
            "        \"defaultOperation\": \"stream\",\n"
            "        \"defaultShift\": \"half\",\n"
            "        \"hashVersion\": 5\n"
            "    },\n"
            "    \"textures\": [],\n"
            "    \"operationFilters\": [],\n"
            "    \"shiftFilters\": [],\n"
            "    \"extraFiles\": []\n"
            "}\n";
        return true;
    }

    std::filesystem::path app_settings_path() {
        return app_config_path() / "recut_settings.json";
    }

    int input_action_index(InputAction action) {
        return static_cast<int>(action);
    }

    void to_json(nlohmann::json& json, const GamepadBinding& binding) {
        json = nlohmann::json{
            { "kind", binding.kind == GamepadBindingKind::Button ? "Button" : (binding.kind == GamepadBindingKind::AxisPositive ? "AxisPositive" : "AxisNegative") },
            { "code", binding.code }
        };
    }

    GamepadBinding gamepad_binding_from_json(const nlohmann::json& json, const GamepadBinding& fallback) {
        if (!json.is_object()) {
            return fallback;
        }

        GamepadBinding binding = fallback;
        const std::string kind = json.value("kind", std::string{});
        if (kind == "Button") {
            binding.kind = GamepadBindingKind::Button;
        }
        else if (kind == "AxisPositive") {
            binding.kind = GamepadBindingKind::AxisPositive;
        }
        else if (kind == "AxisNegative") {
            binding.kind = GamepadBindingKind::AxisNegative;
        }
        binding.code = json.value("code", binding.code);
        return binding;
    }

    void load_recut_settings() {
        std::ifstream settings_file(app_settings_path(), std::ios::binary);
        if (!settings_file.good()) {
            return;
        }

        try {
            nlohmann::json settings_json{};
            settings_file >> settings_json;

            auto graphics_config = ultramodern::renderer::get_graphics_config();
            if (settings_json.contains("graphics") && settings_json["graphics"].is_object()) {
                const nlohmann::json& graphics = settings_json["graphics"];
                graphics_config.api_option = graphics.value("graphicsApi", graphics_config.api_option);
                graphics_config.res_option = graphics.value("resolution", graphics_config.res_option);
                graphics_config.resolution_multiplier = graphics.value("resolutionMultiplier", graphics_config.resolution_multiplier);
                graphics_config.wm_option = graphics.value("windowMode", graphics_config.wm_option);
                graphics_config.ar_option = graphics.value("aspectRatio", graphics_config.ar_option);
                graphics_config.msaa_option = graphics.value("antialiasing", graphics_config.msaa_option);
                graphics_config.hpfb_option = graphics.value("framebuffer", graphics_config.hpfb_option);
                graphics_config.filtering_option = graphics.value("textureFiltering", graphics_config.filtering_option);
                graphics_config.upscale_2d = graphics.value("upscale2D", graphics_config.upscale_2d);
                graphics_config.hardware_resolve = graphics.value("hardwareResolve", graphics_config.hardware_resolve);
                graphics_config.three_point_filtering = graphics.value("threePointFiltering", graphics_config.three_point_filtering);
                graphics_config.rr_option = graphics.value("refreshRate", graphics_config.rr_option);
                graphics_config.rr_manual_value = graphics.value("refreshRateTarget", graphics_config.rr_manual_value);
                graphics_config.ds_option = graphics.value("downsampleMultiplier", graphics_config.ds_option);
                graphics_config.anisotropic_filtering = graphics.value("anisotropicFiltering", graphics_config.anisotropic_filtering);
                sanitize_recut_graphics_config(graphics_config);
                ultramodern::renderer::set_graphics_config(graphics_config);
            }

            std::lock_guard<std::mutex> lock(settings_mutex);
            if (settings_json.contains("audio") && settings_json["audio"].is_object()) {
                const nlohmann::json& audio = settings_json["audio"];
                audio_settings.volume_percent = std::clamp(audio.value("volumePercent", audio_settings.volume_percent), 0, 200);
                audio_settings.muted = audio.value("muted", audio_settings.muted);
                audio_settings.output_rate = audio.value("outputRate", audio_settings.output_rate);
                audio_settings.buffer_samples = static_cast<uint16_t>(std::clamp(audio.value("bufferSamples", static_cast<int>(audio_settings.buffer_samples)), 128, 2048));
            }

            if (settings_json.contains("input") && settings_json["input"].is_object()) {
                const nlohmann::json& input = settings_json["input"];
                input_settings.preferred_controller_index = input.value("preferredControllerIndex", input_settings.preferred_controller_index);
                input_settings.mouse_click_to_move = input.value("mouseClickToMove", input_settings.mouse_click_to_move);

                if (input.contains("keyboard") && input["keyboard"].is_array()) {
                    const nlohmann::json& keyboard = input["keyboard"];
                    for (size_t i = 0; i < input_settings.keyboard_bindings.size() && i < keyboard.size(); i++) {
                        input_settings.keyboard_bindings[i] = static_cast<SDL_Scancode>(keyboard[i].get<int>());
                    }
                }

                if (input.contains("gamepad") && input["gamepad"].is_array()) {
                    const nlohmann::json& gamepad = input["gamepad"];
                    for (size_t i = 0; i < input_settings.gamepad_bindings.size() && i < gamepad.size(); i++) {
                        input_settings.gamepad_bindings[i] = gamepad_binding_from_json(gamepad[i], input_settings.gamepad_bindings[i]);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "Could not load ReCut settings: %s\n", e.what());
        }
    }

    void save_recut_settings() {
        AudioSettings audio_snapshot{};
        AppInputSettings input_snapshot{};
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            audio_snapshot = audio_settings;
            input_snapshot = input_settings;
        }

        auto graphics_config = ultramodern::renderer::get_graphics_config();
        sanitize_recut_graphics_config(graphics_config);
        nlohmann::json settings_json{};
        settings_json["graphics"] = {
            { "graphicsApi", graphics_config.api_option },
            { "resolution", graphics_config.res_option },
            { "resolutionMultiplier", graphics_config.resolution_multiplier },
            { "windowMode", graphics_config.wm_option },
            { "aspectRatio", graphics_config.ar_option },
            { "antialiasing", graphics_config.msaa_option },
            { "framebuffer", graphics_config.hpfb_option },
            { "textureFiltering", graphics_config.filtering_option },
            { "upscale2D", graphics_config.upscale_2d },
            { "hardwareResolve", graphics_config.hardware_resolve },
            { "threePointFiltering", graphics_config.three_point_filtering },
            { "refreshRate", graphics_config.rr_option },
            { "refreshRateTarget", graphics_config.rr_manual_value },
            { "downsampleMultiplier", graphics_config.ds_option },
            { "anisotropicFiltering", graphics_config.anisotropic_filtering }
        };
        settings_json["audio"] = {
            { "volumePercent", audio_snapshot.volume_percent },
            { "muted", audio_snapshot.muted },
            { "outputRate", audio_snapshot.output_rate },
            { "bufferSamples", audio_snapshot.buffer_samples }
        };
        settings_json["input"]["preferredControllerIndex"] = input_snapshot.preferred_controller_index;
        settings_json["input"]["mouseClickToMove"] = input_snapshot.mouse_click_to_move;
        settings_json["input"]["keyboard"] = nlohmann::json::array();
        settings_json["input"]["gamepad"] = nlohmann::json::array();
        for (SDL_Scancode binding : input_snapshot.keyboard_bindings) {
            settings_json["input"]["keyboard"].push_back(static_cast<int>(binding));
        }
        for (const GamepadBinding& binding : input_snapshot.gamepad_bindings) {
            nlohmann::json binding_json{};
            to_json(binding_json, binding);
            settings_json["input"]["gamepad"].push_back(binding_json);
        }

        std::error_code error;
        std::filesystem::create_directories(app_config_path(), error);
        if (error) {
            return;
        }

        std::ofstream settings_file(app_settings_path(), std::ios::binary | std::ios::trunc);
        if (settings_file.good()) {
            settings_file << settings_json.dump(4);
        }
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
    SetProcessPriorityBoost(GetCurrentProcess(), TRUE);
    SetThreadPriorityBoost(GetCurrentThread(), TRUE);
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
    load_recut_settings();
    recomp::register_game(paper_mario_us);
    paper_mario::register_overlays();

    if (!ensure_rom_installed(argc, argv, game_id)) {
        return EXIT_FAILURE;
    }

    const std::filesystem::path builtin_textures_path = texture_builtin_path();
    if (!paper_mario::ensure_builtin_texture_pack(builtin_textures_path)) {
        show_message("Paper Mario ReCut could not prepare its built-in texture pack.");
        return EXIT_FAILURE;
    }
    ultramodern::set_startup_texture_replacement_directory(builtin_textures_path);

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
        .vi_callback = nullptr,
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

    const std::filesystem::path textures_path = texture_root_path();
    if (std::filesystem::is_directory(textures_path)) {
        ensure_texture_folder_layout();
    }

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
    destroy_input_options_window();
    destroy_audio_options_window();
    destroy_graphics_options_window();
    destroy_texture_replacement_window();
    destroy_app_menu_bar();
#endif
    SDL_Quit();

    return EXIT_SUCCESS;
}
