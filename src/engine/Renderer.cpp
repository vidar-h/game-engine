#include "Renderer.h"
#include "VulkanContext.h"
#include <vulkan/vulkan_enums.hpp>

namespace engine {

void Renderer::init(GLFWwindow *window, u32 frames_in_flight) {
  ZoneScopedN("Renderer init");
  m_frames_in_flight = frames_in_flight;

  engine::VulkanContext::Options options = {
      .app_name = "Game",
#ifndef NDEBUG
      .request_validation = true,
      .request_calibrated_timestamps = true,
#endif
      .frames_in_flight = frames_in_flight,
      .vsync = true,
  };

  m_context.init(window, options);
  m_device = m_context.vulkan_device();

  init_frame_data();
}

void Renderer::deinit() {
  ZoneScopedN("Renderer deinit");
  SPDLOG_INFO("Destroying renderer");
  (void)m_device.waitIdle();
  deinit_frame_data();
  m_context.deinit();
}

static vk::ImageMemoryBarrier2 transition_image_layout(vk::Image image, vk::ImageLayout oldLayout,
                                                       vk::ImageLayout newLayout, vk::AccessFlags2 srcAccessMask,
                                                       vk::AccessFlags2 dstAccessMask, vk::PipelineStageFlags2 srcStage,
                                                       vk::PipelineStageFlags2 dstStage) {
  bool is_depth_attachment = (newLayout == vk::ImageLayout::eDepthAttachmentOptimal);
  auto barrier = vk::ImageMemoryBarrier2()
                     .setSrcStageMask(srcStage)
                     .setSrcAccessMask(srcAccessMask)
                     .setDstStageMask(dstStage)
                     .setDstAccessMask(dstAccessMask)
                     .setOldLayout(oldLayout)
                     .setNewLayout(newLayout)
                     .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                     .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                     .setImage(image)
                     .setSubresourceRange(vk::ImageSubresourceRange()
                                              .setAspectMask(is_depth_attachment ? vk::ImageAspectFlagBits::eDepth
                                                                                 : vk::ImageAspectFlagBits::eColor)
                                              .setLevelCount(1)
                                              .setLayerCount(1));
  return barrier;
}

void Renderer::render(GLFWwindow *window) {
  ZoneScopedN("render");
  m_context.acquire_next_image(window);

  auto frame_data = m_frame_data[m_context.frame_index()];
  auto cmd = frame_data.cmd;
  auto image = m_context.current_image();
  {
    ZoneScopedN("Collect tracy data");
    TracyVkCollectHostQueryReset(m_context.tracy_ctx());
    m_device.resetCommandPool(frame_data.pool);
  }

  auto begin_info = vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
  auto begin_result = cmd.begin(begin_info);
  if (begin_result != vk::Result::eSuccess) {
    SPDLOG_ERROR("Failed to begin command buffer recording: {}", vk::to_string(begin_result));
    return;
  }
  {
    TracyVkZone(m_context.tracy_ctx(), cmd, "Frame render");
    {
      auto color_attachment_barrier =
          transition_image_layout(image, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                                  vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eTransferWrite,
                                  vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eClear);

      auto barriers = std::array<vk::ImageMemoryBarrier2, 1>{
          color_attachment_barrier,
      };
      auto dep_info = vk::DependencyInfo().setImageMemoryBarriers(barriers);
      cmd.pipelineBarrier2(dep_info);
    }

    auto clear_range = vk::ImageSubresourceRange()
                           .setAspectMask(vk::ImageAspectFlagBits::eColor)
                           .setLayerCount(vk::RemainingArrayLayers)
                           .setLevelCount(vk::RemainingMipLevels);
    auto clear_value = vk::ClearColorValue().setFloat32({0.3f, 0.6f, 0.3f, 1.0f});
    cmd.clearColorImage(image, vk::ImageLayout::eGeneral, clear_value, clear_range);

    auto present_transition_barrier = transition_image_layout(
        image, vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR, vk::AccessFlagBits2::eTransferWrite,
        vk::AccessFlagBits2::eNone, vk::PipelineStageFlagBits2::eClear, vk::PipelineStageFlagBits2::eBottomOfPipe);

    cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(present_transition_barrier));
  }

  if (cmd.end() != vk::Result::eSuccess) {
    SPDLOG_ERROR("Failed to end vulkan command buffer recording.");
    return;
  }
  m_context.present(window, cmd);
}

void Renderer::init_frame_data() {
  ZoneScopedN("FrameData creation");
  auto pool_info = vk::CommandPoolCreateInfo()
                       .setFlags(vk::CommandPoolCreateFlagBits::eTransient)
                       .setQueueFamilyIndex(m_context.graphics_queue_index());
  for (size_t i = 0; i < m_frames_in_flight; ++i) {
    auto pool = VK_CHECK(m_device.createCommandPool(pool_info));
    auto cmd_info = vk::CommandBufferAllocateInfo().setCommandPool(pool).setCommandBufferCount(1).setLevel(
        vk::CommandBufferLevel::ePrimary);
    auto cmd = VK_CHECK(m_device.allocateCommandBuffers(cmd_info));
    m_frame_data.push_back(FrameData{
        .pool = pool,
        .cmd = cmd[0],
    });
  }
}

void Renderer::deinit_frame_data() {
  ZoneScopedN("FrameData destruction");
  SPDLOG_INFO("Destroying renderer frame data");
  for (const auto &data : m_frame_data) {
    m_device.destroyCommandPool(data.pool);
  }
}

}; // namespace engine