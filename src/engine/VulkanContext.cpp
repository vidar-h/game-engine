#include "VulkanContext.h"

#include <VkBootstrap.h>
#include <limits>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

namespace engine {

void VulkanContext::init(GLFWwindow *window, const Options &options) {
  ZoneScopedN("Vulkan context creation");
  EG_ASSERT(window != nullptr);
  EG_ASSERT(options.frames_in_flight > 0 && "Frames in flights must be greater than 0");
  EG_ASSERT(options.frames_in_flight < 6 && "Please be a sane person.");

  init_vulkan(options, window);
  init_tracy_context();

  m_swapchain.vsync = options.vsync;
  m_frames_in_flight = options.frames_in_flight;
  init_swapchain(window);
  init_frame_sync_structures();
}

void VulkanContext::deinit() {
  ZoneScopedN("Vulkan context destruction");
  destroy_swapchain();
  deinit_frame_sync_structures();

  TracyVkDestroy(m_tracy_ctx);
  m_device.destroy();

  m_instance.destroySurfaceKHR(m_surface);
  m_instance.destroyDebugUtilsMessengerEXT(m_debug_messenger);
  m_instance.destroy();
}

void VulkanContext::init_tracy_context() {
  ZoneScopedN("Tracy Vulkan context creation");
  vk::detail::DynamicLoader dl;
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");

  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = dl.getProcAddress<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");

  m_tracy_ctx =
      TracyVkContextHostCalibrated(m_instance, m_physical_device, m_device, vkGetInstanceProcAddr, vkGetDeviceProcAddr);
}

void VulkanContext::init_vulkan(const Options &options, GLFWwindow *window) {
  ZoneScopedN("Vulkan initilization");
  SPDLOG_INFO("Initializing Vulkan");

  m_validation_enabled = options.request_validation;

  auto vkb_inst = vkb::Instance();
  {
    ZoneScopedN("Instance creation");
    auto inst_builder = vkb::InstanceBuilder();
    inst_builder.set_app_name(options.app_name.data()).require_api_version(1, 3, 0).build();

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
    vk::detail::defaultDispatchLoaderDynamic.init(m_instance, vkGetInstanceProcAddr);
    SPDLOG_INFO("Loaded Vulkan instance procedures");
  }

  {
    ZoneScopedN("Vulkan surface creation");
    VkSurfaceKHR surface;
    VK_CHECK_NON_HPP(glfwCreateWindowSurface(m_instance, window, nullptr, &surface));
    m_surface = surface;
  }

  auto vkb_pdev = vkb::PhysicalDevice();
  {
    ZoneScopedN("Physical device selection");
    auto vulkan_features = vk::PhysicalDeviceFeatures().setMultiDrawIndirect(true);

    auto vulkan12_features =
        vk::PhysicalDeviceVulkan12Features().setBufferDeviceAddress(true).setDescriptorIndexing(true).setHostQueryReset(
            options.request_calibrated_timestamps);

    auto vulkan13_features = vk::PhysicalDeviceVulkan13Features().setDynamicRendering(true).setSynchronization2(true);

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
      EG_FATAL("Failed to find a compatible Vulkan device: {}", ret.error().message());
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
      EG_FATAL("Failed to find a Vulkan queue that supports graphics operations: {}",
               graphics_queue_ret.error().message());
    }

    auto graphics_queue_index = vkb_device.get_queue_index(vkb::QueueType::graphics);
    if (!graphics_queue_index) {
      EG_FATAL("Failed to get the queue index for the graphics queue: {}", graphics_queue_index.error().message());
    }

    m_graphics_queue = graphics_queue_ret.value();
    m_graphics_queue_index = graphics_queue_index.value();

    auto present_queue_ret = vkb_device.get_queue(vkb::QueueType::present);
    if (!present_queue_ret) {
      EG_FATAL("Failed to find a Vulkan queue that supports present operations: {}",
               graphics_queue_ret.error().message());
    }

    auto present_queue_index = vkb_device.get_queue_index(vkb::QueueType::present);
    if (!present_queue_index) {
      EG_FATAL("Failed to get the queue index for the present queue: {}", present_queue_index.error().message());
    }

    m_present_queue = graphics_queue_ret.value();
    m_present_queue_index = graphics_queue_index.value();
    SPDLOG_INFO("Retrieved Vulkan queues");
  }
}

vk::PresentModeKHR VulkanContext::select_present_mode() {
  auto present_modes = VK_CHECK(m_physical_device.getSurfacePresentModesKHR(m_surface));

  auto get_score_vsync = [](vk::PresentModeKHR mode) {
    switch (mode) {
    case vk::PresentModeKHR::eFifo:
      return 0;
    case vk::PresentModeKHR::eFifoRelaxed:
      return 1;
    case vk::PresentModeKHR::eFifoLatestReadyEXT:
      return 2;
    case vk::PresentModeKHR::eMailbox:
      return 3;
    default:
      return 0;
    };
  };

  u32 score = 0;
  auto immediate_found = false;
  auto selected = vk::PresentModeKHR::eFifo;

  for (auto mode : present_modes) {
    SPDLOG_INFO("Present mode {} is available", vk::to_string(mode));
    if (m_swapchain.vsync) {
      u32 mode_score = get_score_vsync(mode);
      if (mode_score > score) {
        score = mode_score;
        selected = mode;
      }
    } else {
      immediate_found = mode == vk::PresentModeKHR::eImmediate;
      if (immediate_found && !m_swapchain.vsync) {
        selected = mode;
        break;
      }
    }
  }

  if (!immediate_found && !m_swapchain.vsync) {
    SPDLOG_INFO("Vsync was not requested but immediate mode presentation is "
                "not supported");
  }

  selected = vk::PresentModeKHR::eFifo;
  return selected;
}

void VulkanContext::acquire_next_image(GLFWwindow *window) {
  {
    ZoneScopedN("Wait for frame fence");
    auto result = m_device.waitForFences(m_frame_fences[m_frame_index], true, std::numeric_limits<u64>::max());
    if (result != vk::Result::eSuccess) {
      SPDLOG_ERROR("Failed to wait for frame fence: {}", vk::to_string(result));
    }
    m_device.resetFences(m_frame_fences[m_frame_index]);
  }

  {
    ZoneScopedN("Acquire next swapchain image");
    auto index_or_error = m_device.acquireNextImageKHR(m_swapchain.swapchain, std::numeric_limits<u64>::max(),
                                                       m_acquire_semaphores[m_frame_index]);
    if (index_or_error.result == vk::Result::eSuboptimalKHR ||
        index_or_error.result == vk::Result::eErrorOutOfDateKHR) {
      SPDLOG_WARN("Swapchain suboptimal/out-of-date, recreating.");
      init_swapchain(window);
    } else if (index_or_error.result != vk::Result::eSuccess) {
      EG_FATAL("Failed to acquire next swapchain image: {}", vk::to_string(index_or_error.result));
    }

    m_swapchain.image_index = index_or_error.value;
  }
}

void VulkanContext::present(GLFWwindow *window, vk::CommandBuffer cmd) {
  ZoneScopedN("Present");
  auto wait_info = vk::SemaphoreSubmitInfo()
                       .setSemaphore(m_acquire_semaphores[m_frame_index])
                       .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);
  auto signal_info = vk::SemaphoreSubmitInfo()
                         .setSemaphore(m_submit_semaphores[m_swapchain.image_index])
                         .setStageMask(vk::PipelineStageFlagBits2::eAllGraphics);
  auto cmd_buf_Info = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
  auto submit_desc = vk::SubmitInfo2()
                         .setWaitSemaphoreInfos(wait_info)
                         .setSignalSemaphoreInfos(signal_info)
                         .setCommandBufferInfos(cmd_buf_Info);

  {
    ZoneScopedN("Queue submit");
    auto res = m_graphics_queue.submit2(submit_desc, m_frame_fences[m_frame_index]);
    if (res != vk::Result::eSuccess) {
      SPDLOG_ERROR("Failed to submit command buffer to graphics queue: {}", vk::to_string(res));
    }
  }

  {
    ZoneScopedN("Queue present");
    auto present_info = vk::PresentInfoKHR()
                            .setWaitSemaphores(m_submit_semaphores[m_swapchain.image_index])
                            .setSwapchains(m_swapchain.swapchain)
                            .setImageIndices(m_swapchain.image_index);
    auto present_result = m_present_queue.presentKHR(&present_info);
    if (present_result == vk::Result::eSuboptimalKHR || present_result == vk::Result::eErrorOutOfDateKHR) {
      SPDLOG_WARN("Swapchain in suboptimal/out-of-date state during presentaiton. Recreating.");
      init_swapchain(window);
    } else if (present_result != vk::Result::eSuccess) {
      EG_FATAL("Failed to present to swapchain image: {}", vk::to_string(present_result));
    }
  }

  m_frame_index = (m_frame_index + 1) % m_frames_in_flight;
}

void VulkanContext::init_swapchain(GLFWwindow *window) {
  int width, height;
  glfwGetFramebufferSize(window, &width, &height);

  ZoneScopedN("Vulkan swapchain creation");
  m_swapchain.format = vk::SurfaceFormatKHR(vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear);
  m_swapchain.extent = vk::Extent2D(width, height);
  m_swapchain.present_mode = select_present_mode();
  SPDLOG_INFO("Selected swapchain present mode: {}", vk::to_string(m_swapchain.present_mode));

  auto caps = VK_CHECK(m_physical_device.getSurfaceCapabilitiesKHR(m_surface));
  SPDLOG_INFO("minImageCount: {}", caps.minImageCount);
  SPDLOG_INFO("maxImageCount: {}", caps.maxImageCount);
  SPDLOG_INFO("currentExtent: {}x{}", caps.currentExtent.width, caps.currentExtent.height);
  SPDLOG_INFO("minImageExtent: {}x{}", caps.minImageExtent.width, caps.minImageExtent.height);
  SPDLOG_INFO("maxImageExtent: {}x{}", caps.maxImageExtent.width, caps.maxImageExtent.height);
  SPDLOG_INFO("maxImageArrayLayers: {}", caps.maxImageArrayLayers);
  SPDLOG_INFO("supportedTransforms: {}", vk::to_string(caps.supportedTransforms));
  SPDLOG_INFO("currentTransform: {}", vk::to_string(caps.currentTransform));
  SPDLOG_INFO("supportedCompositeAlpha: {}", vk::to_string(caps.supportedCompositeAlpha));
  SPDLOG_INFO("supportedUsageFlags: {}", vk::to_string(caps.supportedUsageFlags));

  auto queue_sharing =
      m_present_queue_index == m_graphics_queue_index ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;

  auto queues = std::array<u32, 2>{m_graphics_queue_index, m_present_queue_index};
  SPDLOG_INFO("Swapchain queue sharing: {}", vk::to_string(queue_sharing));

  if (caps.maxImageCount == 0) {
    caps.maxImageCount = std::numeric_limits<u32>::max();
  }

  if (caps.maxImageCount < m_frames_in_flight) {
    SPDLOG_ERROR("Swapchain max image count is smaller than the requested number of frames in flight.");
    SPDLOG_ERROR("Using {} frames in flight instead of {}", caps.maxImageCount, m_frames_in_flight);
    m_frames_in_flight = caps.maxImageCount;
  }

  auto min_swapchain_images = std::max(m_frames_in_flight, caps.minImageCount + 1);
  auto image_formats = std::array<vk::Format, 2>{m_swapchain.format.format, vk::Format::eB8G8R8A8Unorm};
  auto image_format_list_create_info = vk::ImageFormatListCreateInfo().setViewFormats(image_formats);

  auto swapchain_desc =
      vk::SwapchainCreateInfoKHR()
          .setSurface(m_surface)
          .setMinImageCount(min_swapchain_images)
          .setImageFormat(m_swapchain.format.format)
          .setImageColorSpace(m_swapchain.format.colorSpace)
          .setImageExtent(m_swapchain.extent)
          .setImageArrayLayers(1)
          .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst)
          .setImageSharingMode(queue_sharing)
          .setFlags(vk::SwapchainCreateFlagBitsKHR::eMutableFormat)
          .setQueueFamilyIndices(queues)
          .setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
          .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
          .setPresentMode(m_swapchain.present_mode)
          .setClipped(true)
          .setPNext(&image_format_list_create_info)
          .setOldSwapchain(m_swapchain.swapchain);

  auto new_swapchain = VK_CHECK(m_device.createSwapchainKHR(swapchain_desc));
  destroy_swapchain();
  m_swapchain.swapchain = new_swapchain;

  m_swapchain.images = VK_CHECK(m_device.getSwapchainImagesKHR(m_swapchain.swapchain));
  for (auto image : m_swapchain.images) {
    auto subresource =
        vk::ImageSubresourceRange().setAspectMask(vk::ImageAspectFlagBits::eColor).setLevelCount(1).setLayerCount(1);

    auto view_create_info = vk::ImageViewCreateInfo()
                                .setImage(image)
                                .setViewType(vk::ImageViewType::e2D)
                                .setSubresourceRange(subresource)
                                .setComponents(vk::ComponentSwizzle::eIdentity);

    view_create_info.setFormat(vk::Format::eB8G8R8A8Srgb);
    m_swapchain.srgb_views.push_back(VK_CHECK(m_device.createImageView(view_create_info)));

    view_create_info.setFormat(vk::Format::eB8G8R8A8Unorm);
    m_swapchain.linear_views.push_back(VK_CHECK(m_device.createImageView(view_create_info)));
  }
  SPDLOG_INFO("Created Vulkan swapchain with:");
  SPDLOG_INFO("Image count: {}", m_swapchain.images.size());
  SPDLOG_INFO("Format: {}", vk::to_string(m_swapchain.format.format));
  SPDLOG_INFO("Colorspace: {}", vk::to_string(m_swapchain.format.colorSpace));
  SPDLOG_INFO("Present mode: {}", vk::to_string(m_swapchain.present_mode));
  SPDLOG_INFO("Dimensions: {}x{}", m_swapchain.extent.width, m_swapchain.extent.height);

  m_frame_index = 0;
}

void VulkanContext::destroy_swapchain() {
  if (m_swapchain.swapchain == nullptr)
    return;

  ZoneScopedN("Vulkan swapchain destruction");

  for (size_t i = 0; i < m_swapchain.images.size(); ++i) {
    m_device.destroyImageView(m_swapchain.srgb_views[i]);
    m_device.destroyImageView(m_swapchain.linear_views[i]);
  }
  m_swapchain.linear_views.clear();
  m_swapchain.srgb_views.clear();

  // This is technically wrong since it does not guarantee that presentation operations are finished
  // but currently there is no better way of doing it without the VK_EXT_swapchain_maintenance1 extension.
  // Apparently since it is the best way of doing even though it is wrong the validation errors have
  // a special carve out for it
  // See https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
  (void)m_device.waitIdle();
  m_device.destroySwapchainKHR(m_swapchain.swapchain);
  SPDLOG_INFO("Destroyed swapchain");
}

void VulkanContext::init_frame_sync_structures() {
  ZoneScopedN("Semaphore and fence creation");
  for (size_t i = 0; i < m_frames_in_flight; ++i) {
    m_acquire_semaphores.push_back(VK_CHECK(m_device.createSemaphore(vk::SemaphoreCreateInfo())));
    m_frame_fences.push_back(VK_CHECK(m_device.createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled))));
  }

  for (size_t i = 0; i < m_swapchain.images.size(); ++i) {
    m_submit_semaphores.push_back(VK_CHECK(m_device.createSemaphore(vk::SemaphoreCreateInfo())));
  }
}

void VulkanContext::deinit_frame_sync_structures() {
  ZoneScopedN("Semaphore and fence destruction");
  for (size_t i = 0; i < m_frames_in_flight; ++i) {
    m_device.destroySemaphore(m_acquire_semaphores[i]);
    m_device.destroyFence(m_frame_fences[i]);
  }

  for (size_t i = 0; i < m_swapchain.images.size(); ++i) {
    m_device.destroySemaphore(m_submit_semaphores[i]);
  }
}

}; // namespace engine
