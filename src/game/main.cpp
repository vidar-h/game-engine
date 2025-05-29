#include <engine/VulkanDevice.h>
#include <engine/core.h>

#include <thread>

using namespace std::chrono_literals;

static void error_callback(int error, const char *description) {
  SPDLOG_ERROR("GLFW error with code {}: {}", error, description);
}

int main(void) {
  if (!glfwInit()) {
    EG_FATAL("Failed to initiliaze glfw");
  }
  SPDLOG_INFO("Initialized GLFW");
  glfwSetErrorCallback(error_callback);

  f32 content_x_scale;
  f32 content_y_scale;
  glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &content_x_scale,
                             &content_y_scale);
  SPDLOG_INFO("Monitor content scale is ({}, {})", content_x_scale,
              content_y_scale);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  f32 content_scale = std::max(content_x_scale, content_y_scale);
  auto window = glfwCreateWindow(1920 / content_scale, 1080 / content_scale,
                                 "Game", nullptr, nullptr);
  if (!window) {
    EG_FATAL("Failed to create glfw window");
  }

  engine::VulkanDevice device;
  engine::VulkanDevice::Options options = {
      .app_name = "Game",
      .request_validation = true,
      .request_calibrated_timestamps = true,
      .frames_in_flight = 2,
  };
  device.init(window, options);

  SPDLOG_INFO("Entering main loop");

  u32 frame_counter = 0;
  while (!glfwWindowShouldClose(window)) {
    FrameMark;
    glfwPollEvents();

    if (frame_counter == 100) {
      break;
    }

    std::this_thread::sleep_for(10ms);
    ++frame_counter;
  }
  device.deinit();
}
