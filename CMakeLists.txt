cmake_minimum_required(VERSION 3.20)
project(game_engine LANGUAGES CXX C)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TRACY "Enable support for the tracy profiler" OFF)

if(ENABLE_ASAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "AddressSanitizer enabled")
    set(ASAN_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
    add_compile_options(${ASAN_FLAGS})
    add_link_options(${ASAN_FLAGS})
endif()

set(SOURCE_FILES
    src/game/main.cpp
    src/engine/VulkanContext.cpp
    src/engine/Renderer.cpp
)

set(COMMON_COMPILE_FLAGS -Wall -Wextra -Wunused-result -Wno-missing-field-initializers -Wno-unused-function -fno-exceptions)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b-docking
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
)

FetchContent_Declare(
    VkBootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG v1.4.315
)

FetchContent_MakeAvailable(glm glfw imgui spdlog VkBootstrap)

# Game
add_executable(game_engine ${SOURCE_FILES})
set_target_properties(game_engine PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(game_engine PRIVATE src)
target_link_libraries(game_engine PRIVATE ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
target_compile_options(game_engine PRIVATE ${COMMON_COMPILE_FLAGS})
target_compile_options(game_engine PRIVATE "-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

# GLM
target_include_directories(game_engine PRIVATE ${glm_SOURCE_DIR})

# GLFW
target_include_directories(game_engine PRIVATE ${glfw_SOURCE_DIR}/include)
target_link_libraries(game_engine PRIVATE glfw)

# IMGUI
target_include_directories(game_engine PRIVATE ${imgui_SOURCE_DIR})
target_sources(game_engine PRIVATE ${imgui_SOURCE_DIR}/imgui_demo.cpp)
target_sources(game_engine PRIVATE ${imgui_SOURCE_DIR}/imgui_draw.cpp)
target_sources(game_engine PRIVATE ${imgui_SOURCE_DIR}/imgui_tables.cpp)
target_sources(game_engine PRIVATE ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_sources(game_engine PRIVATE ${imgui_SOURCE_DIR}/imgui.cpp)
target_sources(game_engine PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
target_sources(game_engine PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

# SPDLOG
target_compile_definitions(game_engine PRIVATE SPDLOG_NO_EXCEPTIONS)
target_link_libraries(game_engine PRIVATE spdlog)

# VkBootstrap
target_link_libraries(game_engine PRIVATE vk-bootstrap)

# Tracy (Vendored since codebase also includes the server which we don't care about)
target_sources(game_engine PRIVATE vendor/tracy/TracyClient.cpp)
set_source_files_properties(vendor/tracy/TracyClient.cpp PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter -Wno-empty-body")
target_include_directories(game_engine PRIVATE vendor/tracy)

if (ENABLE_TRACY)
  target_compile_definitions(game_engine PRIVATE TRACY_ENABLE)
endif()


