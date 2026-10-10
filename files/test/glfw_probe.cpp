#include <cstdio>
#include <gglfw3/glfw3.h>
int main() {
    int ok = glfwInit();
    std::fprintf(stderr, "[probe] glfwInit=%d\n", ok);
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    std::fprintf(stderr, "[probe] primaryMonitor=%p\n", (void*)mon);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* w = glfwCreateWindow(640, 360, "probe", nullptr, nullptr);
    std::fprintf(stderr, "[probe] window=%p\n", (void*)w);
    if (w) {
        glfwMakeContextCurrent(w);
        std::fprintf(stderr, "[probe] context ok, glVer=%s\n", glGetString(GL_VERSION) ? (const char*)glGetString(GL_VERSION) : "null");
        glfwDestroyWindow(w);
    }
    glfwTerminate();
    std::fprintf(stderr, "[probe] done\n");
    return 0;
}
