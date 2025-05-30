#include <engine/Renderer.h>
#include <engine/core.h>

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
  glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &content_x_scale, &content_y_scale);
  SPDLOG_INFO("Monitor content scale is ({}, {})", content_x_scale, content_y_scale);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  f32 content_scale = std::max(content_x_scale, content_y_scale);
  auto window = glfwCreateWindow(1920 / content_scale, 1080 / content_scale, "Game", nullptr, nullptr);
  if (!window) {
    EG_FATAL("Failed to create glfw window");
  }

  engine::Renderer renderer;
  renderer.init(window, 2);

  SPDLOG_INFO("Entering main loop");
  while (!glfwWindowShouldClose(window)) {
    FrameMark;
    glfwPollEvents();
    renderer.render(window);
  }
  renderer.deinit();
}
