#pragma once

#include "VulkanContext.h"

namespace engine {

class Renderer {
public:
  void init(GLFWwindow *window, u32 frames_in_flight);
  void deinit();
  void render(GLFWwindow *window);

  // Delete all the stupid c++ constructors.
  Renderer() = default;
  ~Renderer() = default;
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) = delete;
  Renderer &operator=(Renderer &&) = delete;

private:
  struct FrameData {
    vk::CommandPool pool;
    vk::CommandBuffer cmd;
  };

private:
  u32 m_frames_in_flight;

  VulkanContext m_context;
  vk::Device m_device;
  std::vector<FrameData> m_frame_data;

private:
  void init_frame_data();
  void deinit_frame_data();
};
}; // namespace engine