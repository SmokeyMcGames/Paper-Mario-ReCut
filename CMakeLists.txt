cmake_minimum_required(VERSION 3.20)

project(PaperMarioReCut LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PAPER_RECOMP_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(PM_RECOMP_OUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/paper_mario_recomp_out")
set(PAPER_MARIO_ROM_PATH "" CACHE FILEPATH "Optional path to a legally dumped Paper Mario (U) ROM for local generated build artifacts")

if (NOT EXISTS "${PM_RECOMP_OUT}/lookup.cpp")
    message(FATAL_ERROR
        "Missing generated Paper Mario recomp output at ${PM_RECOMP_OUT}. "
        "Generate it locally from your legally dumped Paper Mario (U) ROM before building.")
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (WIN32)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PAPER_RECOMP_LIB_DIR}")
    add_compile_definitions(NOMINMAX _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

set(RT64_STATIC TRUE CACHE BOOL "Build RT64 as a static library" FORCE)
if (WIN32)
    set(RT64_SDL_WINDOW_VULKAN OFF CACHE BOOL "Use SDL windows for RT64 Vulkan" FORCE)
    add_compile_definitions(HLSL_CPU)
else()
    set(RT64_SDL_WINDOW_VULKAN TRUE CACHE BOOL "Use SDL windows for RT64 Vulkan" FORCE)
    add_compile_definitions(HLSL_CPU RT64_SDL_WINDOW_VULKAN)
endif()

add_subdirectory("${PAPER_RECOMP_LIB_DIR}/rt64" "${CMAKE_BINARY_DIR}/rt64")
add_subdirectory("${PAPER_RECOMP_LIB_DIR}/N64ModernRuntime" "${CMAKE_BINARY_DIR}/N64ModernRuntime")

add_library(RecompiledFuncs STATIC)
file(GLOB PM_FUNC_SOURCES "${PM_RECOMP_OUT}/*.c" "${PM_RECOMP_OUT}/*.cpp")
target_sources(RecompiledFuncs PRIVATE ${PM_FUNC_SOURCES})
target_compile_options(RecompiledFuncs PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/bigobj>
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-fno-strict-aliasing -Wno-unused-variable -Wno-implicit-function-declaration>
)
target_include_directories(RecompiledFuncs PRIVATE
    "${PM_RECOMP_OUT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PAPER_RECOMP_LIB_DIR}/N64ModernRuntime/ultramodern/include"
    "${PAPER_RECOMP_LIB_DIR}/N64ModernRuntime/librecomp/include"
    "${PAPER_RECOMP_LIB_DIR}/N64ModernRuntime/N64Recomp/include"
)

set(PM_RSP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/rsp/n_aspMain.cpp")
if (PAPER_MARIO_ROM_PATH)
    set(PM_GENERATED_RSP_DIR "${CMAKE_BINARY_DIR}/generated_rsp")
    set(PM_GENERATED_RSP_CONFIG "${PM_GENERATED_RSP_DIR}/n_aspMain.toml")
    set(PM_RSP_SOURCE "${PM_GENERATED_RSP_DIR}/n_aspMain.cpp")
    file(MAKE_DIRECTORY "${PM_GENERATED_RSP_DIR}")
    string(REPLACE "\\" "/" PM_RSP_ROM_FILE_PATH "${PAPER_MARIO_ROM_PATH}")
    string(REPLACE "\"" "\\\"" PM_RSP_ROM_FILE_PATH "${PM_RSP_ROM_FILE_PATH}")
    string(REPLACE "\\" "/" PM_RSP_OUTPUT_FILE_PATH "${PM_RSP_SOURCE}")
    string(REPLACE "\"" "\\\"" PM_RSP_OUTPUT_FILE_PATH "${PM_RSP_OUTPUT_FILE_PATH}")
    file(GENERATE OUTPUT "${PM_GENERATED_RSP_CONFIG}" CONTENT
"text_offset = 0x4E5A0
text_size = 0xC60
text_address = 0x04001000
rom_file_path = \"${PM_RSP_ROM_FILE_PATH}\"
output_file_path = \"${PM_RSP_OUTPUT_FILE_PATH}\"
output_function_name = \"n_aspMain\"
extra_indirect_branch_targets = [
    0x10EC, 0x139C, 0x119C, 0x1A64, 0x11C8, 0x17EC, 0x1208, 0x127C,
    0x1348, 0x1248, 0x12D4, 0x12B0, 0x1384
]
")
    add_custom_command(
        OUTPUT "${PM_RSP_SOURCE}"
        COMMAND RSPRecomp "${PM_GENERATED_RSP_CONFIG}"
        DEPENDS RSPRecomp "${PAPER_MARIO_ROM_PATH}"
        COMMENT "Generating Paper Mario RSP microcode from local ROM"
        VERBATIM)
endif()

add_executable(PaperMarioReCut
    src/builtin_texture_pack.cpp
    src/main.cpp
    src/paper_rt64_context.cpp
    src/paper_stubs.cpp
    src/register_overlays.cpp
    src/PaperMarioReCut.rc
    "${PM_RSP_SOURCE}"
)

if (WIN32)
    set_target_properties(PaperMarioReCut PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(PaperMarioReCut PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PM_RECOMP_OUT}"
    "${PAPER_RECOMP_LIB_DIR}/N64ModernRuntime/ultramodern/include"
    "${PAPER_RECOMP_LIB_DIR}/N64ModernRuntime/librecomp/include"
    "${PAPER_RECOMP_LIB_DIR}/N64ModernRuntime/N64Recomp/include"
    "${PAPER_RECOMP_LIB_DIR}/rt64/src/contrib"
    "${PAPER_RECOMP_LIB_DIR}/rt64/src/contrib/hlslpp/include"
    "${PAPER_RECOMP_LIB_DIR}/rt64/src/contrib/dxc/inc"
    "${PAPER_RECOMP_LIB_DIR}/rt64/src"
    "${PAPER_RECOMP_LIB_DIR}/rt64/src/rhi"
    "${PAPER_RECOMP_LIB_DIR}/rt64/src/render"
    "${CMAKE_BINARY_DIR}/rt64/src"
)

target_compile_options(PaperMarioReCut PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
if (MSVC)
    target_link_options(PaperMarioReCut PRIVATE
        /SUBSYSTEM:WINDOWS
        /ENTRY:mainCRTStartup
        /DEBUG
        /MAP
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/PaperMarioReCut.exe.manifest"
    )
endif()

if (WIN32)
    include(FetchContent)
    set(SDL2_VERSION "2.30.3" CACHE STRING "SDL2 version")
    FetchContent_Declare(
        sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
    )
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(PaperMarioReCut PRIVATE "${sdl2_SOURCE_DIR}/include")
    target_link_directories(PaperMarioReCut PRIVATE "${sdl2_SOURCE_DIR}/lib/x64")
    target_link_libraries(PaperMarioReCut PRIVATE SDL2 Winmm.lib Dbghelp.lib Comdlg32.lib Comctl32.lib UxTheme.lib)
    add_custom_command(TARGET PaperMarioReCut POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${PAPER_RECOMP_LIB_DIR}/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${PAPER_RECOMP_LIB_DIR}/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:PaperMarioReCut>)
    find_program(DOTNET_EXECUTABLE dotnet)
    if (DOTNET_EXECUTABLE)
        add_custom_command(TARGET PaperMarioReCut POST_BUILD
            COMMAND "${DOTNET_EXECUTABLE}" publish
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/PaperAtlasTool/N64TextureAtlasEditor.csproj"
                --configuration Release
                --runtime win-x64
                --self-contained true
                -p:PublishSingleFile=true
                -p:EnableCompressionInSingleFile=true
                -p:IncludeNativeLibrariesForSelfExtract=true
                -p:DebugSymbols=false
                -p:DebugType=none
                -p:PublishReadyToRun=false
                --output $<TARGET_FILE_DIR:PaperMarioReCut>
            COMMENT "Publishing Paper Atlas Tool sidecar")
    else()
        message(WARNING "dotnet was not found. PaperAtlasTool.exe will not be built beside PaperMarioReCut.exe.")
    endif()
endif()

target_link_libraries(PaperMarioReCut PRIVATE
    RecompiledFuncs
    librecomp
    ultramodern
    rt64
)
