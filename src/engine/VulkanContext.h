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

#define VK_CHECK_NON_HPP(expr)                                                                                         \
  do {                                                                                                                 \
    VkResult result = (expr);                                                                                          \
    if (result != VK_SUCCESS) {                                                                                        \
      EG_FATAL("{} failed with {}", #expr, vk::to_string(vk::Result(result)));                                         \
    }                                                                                                                  \
  } while (0)

// C++ bullshit that I have little desire to understand.
#define VK_CHECK(expr)                                                                                                 \
  [&]() -> decltype(auto) {                                                                                            \
    auto _result = (expr);                                                                                             \
    if (_result.result != vk::Result::eSuccess) {                                                                      \
      EG_FATAL("{} failed with {}", #expr, vk::to_string(_result.result));                                             \
    }                                                                                                                  \
    return _result.value;                                                                                              \
  }()

namespace engine {

class VulkanContext {
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

  /// Waits for the previous frame fence and then acquires the
  /// next image in the swapchain.
  void acquire_next_image(GLFWwindow *window);

  /// Submits the command buffer to the graphics queue and presents the current swapchain image.
  void present(GLFWwindow *window, vk::CommandBuffer cmd);

  // Delete all the stupid c++ constructors.
  VulkanContext() = default;
  ~VulkanContext() = default;
  VulkanContext(const VulkanContext &) = delete;
  VulkanContext &operator=(const VulkanContext &) = delete;
  VulkanContext(VulkanContext &&) = delete;
  VulkanContext &operator=(VulkanContext &&) = delete;

public:
  template <typename T> void set_debug_name(T object, const char *name) {
    if (!m_validation_enabled)
      return;

    auto name_info = vk::DebugUtilsObjectNameInfoEXT()
                         .setPObjectName(name)
                         .setObjectHandle(u64(static_cast<T::CType>(object)))
                         .setObjectType(object.objectType);
  }

  /// Returns the index of the current frame. Cycles between [0, frames_in_flight]
  u32 frame_index() const { return m_frame_index; }
  u32 graphics_queue_index() const { return m_graphics_queue_index; }
  vk::Device vulkan_device() const { return m_device; }
  vk::Image current_image() const { return m_swapchain.images[m_swapchain.image_index]; }
  TracyVkCtx tracy_ctx() const { return m_tracy_ctx; }

private:
  struct Swapchain {
    // This is seperate from the present mode since
    // multiple present modes provide vsync
    // and some are better than others
    // so we dynamically decide which to use
    bool vsync;
    u32 image_index;
    vk::SwapchainKHR swapchain;
    vk::PresentModeKHR present_mode;
    vk::SurfaceFormatKHR format;
    vk::Extent2D extent;

    std::vector<vk::Image> images;
    std::vector<vk::ImageView> srgb_views;
    std::vector<vk::ImageView> linear_views;
  };

private:
  bool m_validation_enabled;
  u32 m_frames_in_flight;
  u32 m_frame_index;

  vk::Device m_device;
  vk::PhysicalDevice m_physical_device;
  vk::Instance m_instance;
  vk::SurfaceKHR m_surface;
  vk::DebugUtilsMessengerEXT m_debug_messenger;
  vk::Queue m_graphics_queue;
  vk::Queue m_present_queue;

  Swapchain m_swapchain;
  std::vector<vk::Semaphore> m_submit_semaphores;
  std::vector<vk::Semaphore> m_acquire_semaphores;
  std::vector<vk::Fence> m_frame_fences;

  u32 m_graphics_queue_index;
  u32 m_present_queue_index;

  TracyVkCtx m_tracy_ctx;

private:
  void init_vulkan(const Options &options, GLFWwindow *window);
  void init_tracy_context();
  void init_swapchain(GLFWwindow *window);
  vk::PresentModeKHR select_present_mode();
  void destroy_swapchain();
  void init_frame_sync_structures();
  void deinit_frame_sync_structures();
};

}; // namespace engine
