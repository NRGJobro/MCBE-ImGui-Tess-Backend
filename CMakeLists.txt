cmake_minimum_required(VERSION 3.24)
project(MCBEImGuiTessBackend VERSION 0.2.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This project builds a Windows x64 DLL and must be configured for Windows.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Minecraft Bedrock requires an x64 build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(minhook)

add_library(MCBEImGuiTess SHARED src/DllMain.cpp)

target_sources(MCBEImGuiTess PRIVATE
    src/backend/ImGuiTessellatorBackend.hpp
    src/demo/WorldPanelDemo.hpp
    src/diagnostics/CrashDiagnostics.hpp
    src/mcbe/BedrockSDK.hpp
    src/mcbe/Core/Types.hpp
    src/mcbe/Memory/PatternScanner.hpp
    src/mcbe/Version/Signatures.hpp
    src/mcbe/Input/MouseDevice.hpp
    src/mcbe/Render/CameraComponent.hpp
    src/mcbe/Render/LevelRenderer.hpp
    src/mcbe/Render/Resources.hpp
    src/mcbe/Render/Mesh.hpp
    src/mcbe/Render/Textures.hpp
    src/mcbe/Render/Tessellator.hpp
    src/mcbe/Render/UIContext.hpp
)

target_include_directories(MCBEImGuiTess PRIVATE src)
target_link_libraries(MCBEImGuiTess PRIVATE imgui minhook Dbghelp user32)
target_compile_definitions(MCBEImGuiTess PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(MCBEImGuiTess PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_options(imgui PRIVATE /W0)
endif()

set_target_properties(MCBEImGuiTess PROPERTIES
    OUTPUT_NAME "MCBE-ImGui-Tess"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
