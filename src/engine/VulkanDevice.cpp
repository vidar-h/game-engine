#include "VulkanDevice.h"
#include "engine/core.h"

#include <VkBootstrap.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

#define VK_CHECK_NON_HPP(expr)                                                 \
  do {                                                                         \
    VkResult result = (expr);                                                  \
    if (result != VK_SUCCESS) {                                                \
      EG_FATAL("{} failed with {}", #expr, vk::to_string(vk::Result(result))); \
    }                                                                          \
  } while (0)

// C++ bullshit that I have little desire to understand.
#define VK_CHECK(expr)                                                         \
  [&]() -> decltype(auto) {                                                    \
    auto _result = (expr);                                                     \
    if (_result.result != vk::Result::eSuccess) {                              \
      EG_FATAL("{} failed with {}", #expr, vk::to_string(_result.result));     \
    }                                                                          \
    return _result.value;                                                      \
  }()

namespace engine {

void VulkanDevice::init(GLFWwindow *window, const Options &options) {
  ZoneScopedN("Vulkan context creation");
  EG_ASSERT(window != nullptr);
  EG_ASSERT(options.frames_in_flight > 0 &&
            "Frames in flights must be greater than 0");
  EG_ASSERT(options.frames_in_flight < 6 && "Please be a sane person.");

  init_vulkan(options, window);
  init_tracy_context();
}

void VulkanDevice::deinit() {
  TracyVkDestroy(m_tracy_ctx);
  m_device.destroy();

  m_instance.destroySurfaceKHR(m_surface);
  m_instance.destroyDebugUtilsMessengerEXT(m_debug_messenger);
  m_instance.destroy();
}

void VulkanDevice::init_tracy_context() {
  ZoneScopedN("Tracy Vulkan context creation");
  vk::detail::DynamicLoader dl;
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");

  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr =
      dl.getProcAddress<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");

  m_tracy_ctx =
      TracyVkContextHostCalibrated(m_instance, m_physical_device, m_device,
                                   vkGetInstanceProcAddr, vkGetDeviceProcAddr);
}

void VulkanDevice::init_vulkan(const Options &options, GLFWwindow *window) {
  ZoneScopedN("Vulkan initilization");
  SPDLOG_INFO("Initializing Vulkan");

  m_validation_enabled = options.request_validation;

  auto vkb_inst = vkb::Instance();
  {
    ZoneScopedN("Instance creation");
    auto inst_builder = vkb::InstanceBuilder();
    inst_builder.set_app_name(options.app_name.data())
        .require_api_version(1, 3, 0)
        .build();

    if (options.request_validation) {
      inst_builder.request_validation_layers();
      inst_builder.use_default_debug_messenger();
    }

    auto ret = inst_builder.build();
    if (!ret) {
      EG_FATAL("Failed to create Vulkan instance: {}", ret.error().message());
    }

    if (options.request_validation && !ret->debug_messenger) {
      m_validation_enabled = false;
      SPDLOG_ERROR("Validation layers were request but we were unable to "
                   "create the debug messenger!");
    }

    vkb_inst = ret.value();
    m_instance = vkb_inst.instance;
    m_debug_messenger = vkb_inst.debug_messenger;
    SPDLOG_INFO("Created Vulkan instance");
  }

  {
    ZoneScopedN("Load instance dispatch");
    vk::detail::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
        dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    vk::detail::defaultDispatchLoaderDynamic.init(m_instance,
                                                  vkGetInstanceProcAddr);
    SPDLOG_INFO("Loaded Vulkan instance procedures");
  }

  {
    ZoneScopedN("Vulkan surface creation");
    VkSurfaceKHR surface;
    VK_CHECK_NON_HPP(
        glfwCreateWindowSurface(m_instance, window, nullptr, &surface));
    m_surface = surface;
  }

  auto vkb_pdev = vkb::PhysicalDevice();
  {
    ZoneScopedN("Physical device selection");
    auto vulkan_features =
        vk::PhysicalDeviceFeatures().setMultiDrawIndirect(true);

    auto vulkan12_features =
        vk::PhysicalDeviceVulkan12Features()
            .setBufferDeviceAddress(true)
            .setDescriptorIndexing(true)
            .setHostQueryReset(options.request_calibrated_timestamps);

    auto vulkan13_features = vk::PhysicalDeviceVulkan13Features()
                                 .setDynamicRendering(true)
                                 .setSynchronization2(true);

    auto selector = vkb::PhysicalDeviceSelector(vkb_inst, m_surface);
    selector.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .set_minimum_version(1, 3)
        .set_required_features(vulkan_features)
        .set_required_features_12(vulkan12_features)
        .set_required_features_13(vulkan13_features)
        .add_required_extension(vk::KHRSwapchainMutableFormatExtensionName);

    if (options.request_calibrated_timestamps) {
      selector.add_required_extension(vk::EXTCalibratedTimestampsExtensionName);
      SPDLOG_INFO("Enabling {}", vk::EXTCalibratedTimestampsExtensionName);
    }

    auto ret = selector.select();
    if (!ret) {
      EG_FATAL("Failed to find a compatible Vulkan device: {}",
               ret.error().message());
    }
    vkb_pdev = ret.value();
    m_physical_device = vkb_pdev.physical_device;
    SPDLOG_INFO("Selected {} as Vulkan device", vkb_pdev.name);
  }

  auto vkb_device = vkb::Device();
  {
    ZoneScopedN("Vulkan device creation");
    auto ret = vkb::DeviceBuilder(vkb_pdev).build();
    if (!ret) {
      EG_FATAL("Failed to create Vulkan device: {}", ret.error().message());
    }
    vkb_device = ret.value();
    m_device = vkb_device.device;
    SPDLOG_INFO("Created Vulkan device");
  }

  {
    ZoneScopedN("Load device dispatch");
    vk::detail::DynamicLoader dl;
    vk::detail::defaultDispatchLoaderDynamic.init(m_instance, m_device, dl);
    SPDLOG_INFO("Loaded Vulkan device procedures");
  }

  {
    ZoneScopedN("Vulkan queue retrieval");
    auto graphics_queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
      EG_FATAL(
          "Failed to find a Vulkan queue that supports graphics operations: {}",
          graphics_queue_ret.error().message());
    }

    auto graphics_queue_index =
        vkb_device.get_queue_index(vkb::QueueType::graphics);
    if (!graphics_queue_index) {
      EG_FATAL("Failed to get the queue index for the graphics queue: {}",
               graphics_queue_index.error().message());
    }

    m_graphics_queue = graphics_queue_ret.value();
    m_present_queue_index = graphics_queue_index.value();

    auto present_queue_ret = vkb_device.get_queue(vkb::QueueType::present);
    if (!present_queue_ret) {
      EG_FATAL(
          "Failed to find a Vulkan queue that supports present operations: {}",
          graphics_queue_ret.error().message());
    }

    auto present_queue_index =
        vkb_device.get_queue_index(vkb::QueueType::present);
    if (!present_queue_index) {
      EG_FATAL("Failed to get the queue index for the present queue: {}",
               present_queue_index.error().message());
    }

    m_present_queue = graphics_queue_ret.value();
    m_present_queue_index = graphics_queue_index.value();
    SPDLOG_INFO("Retrieved Vulkan queues");
  }
}

vk::PresentModeKHR VulkanDevice::select_present_mode() {
  auto present_modes =
      VK_CHECK(m_physical_device.getSurfacePresentModesKHR(m_surface));
  for (auto mode : present_modes) {
    SPDLOG_INFO("Present mode {} is available", vk::to_string(mode));
  }

  return {};
}

void VulkanDevice::init_swapchain(u32 width, u32 height) {
  m_swapchain.format = vk::SurfaceFormatKHR(vk::Format::eB8G8R8A8Snorm,
                                            vk::ColorSpaceKHR::eSrgbNonlinear);
  m_swapchain.extent = vk::Extent2D(width, height);
}

}; // namespace engine
