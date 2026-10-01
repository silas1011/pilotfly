#include <cstdio>
#include <filesystem>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "app/Config.h"
#include "app/RunLoop.h"
#include "brain/TestPatternBrain.h"
#include "controller/SinkFactory.h"
#include "core/Tx12Layout.h"
#include "input/StopKeyFactory.h"
#include "ui/MainWindow.h"

int main(int, char** argv) {
    using namespace pilotfly;

    const std::filesystem::path configPath = std::filesystem::path(argv[0]).parent_path() / "pilotfly.ini";
    const Config config = loadConfig(configPath.string());

    if (!glfwInit()) {
        std::fprintf(stderr, "Could not start GLFW\n");
        return 1;
    }

#ifdef __APPLE__
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1000, 860, "PilotFly", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Could not create the window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    {
        const Tx12Layout& layout = tx12Layout();
        TestPatternBrain brain(layout);
        const auto sink = makeControllerSink(config);
        const auto stopKey = makeStopKey(config.stopKey, window);
        RunLoop runLoop(brain, *sink, layout, config.rateHz);
        const bool stopKeyOnWorker = stopKey->usableFromAnyThread();
        if (stopKeyOnWorker) {
            runLoop.setWorkerStopKey(stopKey.get());
        }
        runLoop.checkStatus();
        MainWindow mainWindow(runLoop, layout, stopKey->keyName());

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (!stopKeyOnWorker && stopKey->consumePress()) {
                runLoop.toggleHold();
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            mainWindow.draw(glfwGetTime());
            ImGui::Render();

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        runLoop.stop();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
