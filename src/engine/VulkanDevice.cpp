#include "VulkanDevice.h"
#include "engine/core.h"

#include <VkBootstrap.h>

namespace engine {

void VulkanDevice::init(GLFWwindow *window, const Options &options) {
  ZoneScopedN("Vulkan context creation");
  init_vulkan(options, window);
}

void VulkanDevice::deinit() {
  m_instance.destroyDebugUtilsMessengerEXT(m_debug_messenger);
  m_instance.destroy();
}

void VulkanDevice::init_vulkan(const Options &options, GLFWwindow *window) {
  ZoneScopedN("Vulkan initilization");
  SPDLOG_INFO("Initializing Vulkan");

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
      SPDLOG_ERROR("Validation layers were request but we were unable to "
                   "create the debug messenger!");
    }

    vkb_inst = ret.value();
    m_instance = vkb_inst.instance;
    m_debug_messenger = vkb_inst.debug_messenger;
    SPDLOG_INFO("Create Vulkan instance");
  }
}

}; // namespace engine
