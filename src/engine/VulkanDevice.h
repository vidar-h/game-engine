#pragma once

#include <string_view>

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif
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

    /// Sync presentation with the vertical blank of the monitor.
    /// Can be changed later.
    bool vsync;
  };

public:
  void init(GLFWwindow *window, const Options &options);
  void deinit();

public:
  template <typename T> void set_debug_name(T object, const char *name) {
    if (!m_validation_enabled)
      return;

    auto name_info = vk::DebugUtilsObjectNameInfoEXT()
                         .setPObjectName(name)
                         .setObjectHandle(u64(static_cast<T::CType>(object)))
                         .setObjectType(object.objectType);
  }

private:
  struct Swapchain {
    // This is seperate from the present mode since
    // multiple present modes provide vsync
    // and some are better than others
    // so we dynamically decide which to use
    bool vsync;
    vk::SwapchainKHR swapchain;
    vk::PresentModeKHR present_mode;
    vk::SurfaceFormatKHR format;
    vk::Extent2D extent;
  };

private:
  bool m_validation_enabled;
  u32 frames_in_flight;

  vk::Device m_device;
  vk::PhysicalDevice m_physical_device;
  vk::Instance m_instance;
  vk::SurfaceKHR m_surface;
  vk::DebugUtilsMessengerEXT m_debug_messenger;
  vk::Queue m_graphics_queue;
  vk::Queue m_present_queue;
  Swapchain m_swapchain;

  u32 m_graphics_queue_index;
  u32 m_present_queue_index;

  TracyVkCtx m_tracy_ctx;

private:
  void init_vulkan(const Options &options, GLFWwindow *window);
  void init_tracy_context();
  void init_swapchain(u32 width, u32 height);
  vk::PresentModeKHR select_present_mode();
};

}; // namespace engine
