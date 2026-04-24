#include "toy2d.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
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

    SDL_Window* window = SDL_CreateWindow("toy2d",
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
        auto quadMesh = toy2d::CreateQuadMesh();
        toy2d::Material materialA;
        materialA.color = glm::vec3(0.0f, 1.0f, 0.0f);
        materialA.createTexture("../../../assets/opaque.png");

        toy2d::Material materialB;
        materialB.color = glm::vec3(1.0f, 1.0f, 1.0f);
        materialB.createTexture("../../../assets/opaque.png");

        toy2d::GameObject objectA;
        objectA.SetMaterial(&materialA);
        objectA.SetMesh(&quadMesh);
        objectA.SetSize(glm::vec2(1.8f, 2.2f));
        objectA.SetPosition(glm::vec3(-1.4f, 0.0f, 0.0f));

        toy2d::GameObject objectB;
        objectB.SetMaterial(&materialB);
        objectB.SetMesh(&quadMesh);
        objectB.SetSize(glm::vec2(1.1f, 1.1f));
        objectB.SetPosition(glm::vec3(1.4f, 0.35f, -1.2f));

        camera.SetPosition(0.0f, 0.0f, 5.0f);

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
            renderer->Draw(objectA);
            renderer->Draw(objectB);
            renderer->EndFrame();
        }
    }
    toy2d::Quit();

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
