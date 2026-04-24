#include "toy2d.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <array>
#include <glm/glm.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("toy3d",
                                          1024, 720,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    Uint32 count = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!sdlExtensions || count == 0) {
        std::cerr << "SDL_Vulkan_GetInstanceExtensions failed:  " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + count);

    toy2d::Init(extensions,
        [&](VkInstance instance){
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
                throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
            }
            return surface;
        }, 1024, 720);
    {
        auto renderer = toy2d::GetRenderer();
        auto& camera = renderer->GetCamera();
        auto cubeMesh = toy2d::CreateCubeMesh();
        constexpr std::array cubePositions = {
            glm::vec3{-2.8f,  1.5f,  0.0f},
            glm::vec3{-0.8f,  1.3f, -1.4f},
            glm::vec3{ 1.2f,  1.0f, -2.8f},
            glm::vec3{ 3.0f,  1.4f, -4.0f},
            glm::vec3{-2.5f, -1.0f, -1.0f},
            glm::vec3{-0.2f, -1.3f, -2.3f},
            glm::vec3{ 2.0f, -1.1f, -3.6f},
            glm::vec3{ 3.6f, -0.7f, -5.0f},
        };
        constexpr std::array cubeSizes = {
            glm::vec3{0.8f, 0.8f, 0.8f},
            glm::vec3{1.1f, 0.7f, 0.9f},
            glm::vec3{0.9f, 1.3f, 0.8f},
            glm::vec3{1.4f, 0.6f, 1.0f},
            glm::vec3{0.7f, 1.0f, 1.2f},
            glm::vec3{1.0f, 1.0f, 0.6f},
            glm::vec3{1.3f, 0.8f, 1.3f},
            glm::vec3{0.9f, 1.5f, 0.7f},
        };
        constexpr std::array cubeColors = {
            glm::vec3{1.0f, 0.4f, 0.4f},
            glm::vec3{0.4f, 1.0f, 0.4f},
            glm::vec3{0.4f, 0.6f, 1.0f},
            glm::vec3{1.0f, 0.8f, 0.3f},
            glm::vec3{0.8f, 0.4f, 1.0f},
            glm::vec3{0.3f, 1.0f, 0.9f},
            glm::vec3{1.0f, 0.6f, 0.7f},
            glm::vec3{0.9f, 0.9f, 0.9f},
        };

        std::array<toy2d::Material, cubePositions.size()> materials;
        std::array<toy2d::GameObject, cubePositions.size()> cubes;
        for (std::size_t i = 0; i < cubes.size(); ++i) {
            materials[i].color = cubeColors[i];
            materials[i].createTexture("../../../assets/opaque.png");

            cubes[i].SetMaterial(&materials[i]);
            cubes[i].SetMesh(&cubeMesh);
            cubes[i].SetSize(cubeSizes[i]);
            cubes[i].SetPosition(cubePositions[i]);
        }

        camera.SetPosition(0.0f, 0.0f, 8.5f);

        bool shouldClose = false;
        bool rotateCamera = false;
        SDL_Event event;
        Uint64 lastCounter = SDL_GetPerformanceCounter();

        while (!shouldClose) {
            Uint64 currentCounter = SDL_GetPerformanceCounter();
            float deltaTime = static_cast<float>(currentCounter - lastCounter) /
                              static_cast<float>(SDL_GetPerformanceFrequency());
            lastCounter = currentCounter;

            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    shouldClose = true;
                }
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) {
                        shouldClose = true;
                    }
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
                    rotateCamera = true;
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
                    rotateCamera = false;
                }
                if (event.type == SDL_EVENT_MOUSE_MOTION && rotateCamera) {
                    camera.ProcessMouseMovement(static_cast<float>(event.motion.xrel),
                                                static_cast<float>(-event.motion.yrel));
                }
                if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    camera.ProcessMouseScroll(static_cast<float>(event.wheel.y));
                }
            }

            const bool* keyboardState = SDL_GetKeyboardState(nullptr);
            if (keyboardState[SDL_SCANCODE_W]) {
                camera.ProcessKeyboard(toy2d::CameraMovement::Forward, deltaTime);
            }
            if (keyboardState[SDL_SCANCODE_S]) {
                camera.ProcessKeyboard(toy2d::CameraMovement::Backward, deltaTime);
            }
            if (keyboardState[SDL_SCANCODE_A]) {
                camera.ProcessKeyboard(toy2d::CameraMovement::Left, deltaTime);
            }
            if (keyboardState[SDL_SCANCODE_D]) {
                camera.ProcessKeyboard(toy2d::CameraMovement::Right, deltaTime);
            }
            if (keyboardState[SDL_SCANCODE_Q]) {
                camera.ProcessKeyboard(toy2d::CameraMovement::Up, deltaTime);
            }
            if (keyboardState[SDL_SCANCODE_E]) {
                camera.ProcessKeyboard(toy2d::CameraMovement::Down, deltaTime);
            }

            renderer->BeginFrame();
            for (const auto& cube : cubes) {
                renderer->Draw(cube);
            }
            renderer->EndFrame();
        }

        toy2d::Context::Instance().device.waitIdle();
    }
    toy2d::Quit();

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
