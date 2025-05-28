#pragma once

#include <string_view>

#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define TRACY_VK_USE_SYMBOL_TABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include "core.h"

namespace engine {

class VulkanDevice {
public:
  struct Options {
    /// The app name given to the Vulkan instance.
    std::string_view app_name;

    /// Requests the Khronos validation layer for
    /// the Vulkan device. High performance impact.
    /// Layers will not be loaded if unavailable.
    bool request_validation;

    /// Requests the VK_EXT_calibrated_timestamps
    /// extensions which enhances the GPU timestamps
    /// by also providing a CPU timestamp for the
    /// timing point.
    bool request_calibrated_timestamps;

    /// Number of frames that are allowed to be worked on simultaneously.
    /// Recommended values are 2-3. Increasing this will lead to higher latency
    /// but might increase overall smoothness and alleviate stutters.
    u32 frames_in_flight;
  };

public:
  void init(GLFWwindow *window, const Options &options);
  void deinit();

private:
  vk::Instance m_instance;
  vk::DebugUtilsMessengerEXT m_debug_messenger;

private:
  void init_vulkan(const Options &options, GLFWwindow *window);
};

}; // namespace engine
