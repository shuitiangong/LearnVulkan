#include "toy2d.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

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
        auto quadMesh = toy2d::CreateQuadMesh();
        toy2d::Material materialA;
        materialA.color = toy2d::Color{0, 1, 0};
        materialA.createTexture("../../../assets/opaque.png");

        toy2d::Material materialB;
        materialB.color = toy2d::Color{1, 1, 1};
        materialB.createTexture("../../../assets/opaque.png");

        toy2d::GameObject objectA;
        objectA.SetMaterial(&materialA);
        objectA.SetMesh(&quadMesh);
        objectA.SetSize(toy2d::Size{200, 300});

        toy2d::GameObject objectB;
        objectB.SetMaterial(&materialB);
        objectB.SetMesh(&quadMesh);
        objectB.SetSize(toy2d::Size{120, 120});

        bool shouldClose = false;
        SDL_Event event;

        float x = 100, y = 100;

        while (!shouldClose) {
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    shouldClose = true;
                }
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_A) {
                        x -= 10;
                    }
                    if (event.key.key == SDLK_D) {
                        x += 10;
                    }
                    if (event.key.key == SDLK_W) {
                        y -= 10;
                    }
                    if (event.key.key == SDLK_S) {
                        y += 10;
                    }
                    if (event.key.key == SDLK_0) {
                        objectA.SetColor(toy2d::Color{1, 0, 0});
                    }
                    if (event.key.key == SDLK_1) {
                        objectA.SetColor(toy2d::Color{0, 1, 0});
                    }
                    if (event.key.key == SDLK_2) {
                        objectA.SetColor(toy2d::Color{0, 0, 1});
                    }
                }
            }
            objectA.SetPosition(toy2d::Vec{x, y});
            objectB.SetPosition(toy2d::Vec{x + 260.0f, y + 80.0f});

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
