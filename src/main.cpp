#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "../toy2d/toy2d.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Toy2D",
        1024, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    bool shouldClose = false;
    SDL_Event event;
    uint32_t extensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!sdlExtensions || extensionCount == 0) {
        std::cerr << "SDL_Vulkan_GetInstanceExtensions failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + extensionCount);

    // std::cout << "SDL Vulkan instance extensions:\n";
    // for (const char* ext : extensions) {
    //     std::cout << "  - " << ext << '\n';
    // }
    /*
        查看拓宽，结果：
        SDL Vulkan instance extensions:
        - VK_KHR_surface
        - VK_KHR_win32_surface
    */

    try {
        toy2d::Init(extensions, [&](vk::Instance instance) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(instance), nullptr, &surface)) {
                throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
            }
            return vk::SurfaceKHR(surface);
        }, 1024, 720);
        
        while (!shouldClose) {
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    shouldClose = true;
                }
            }
        }
        toy2d::Quit();
    } catch (const std::exception& exception) {
        std::cerr << "Initialization failed: " << exception.what() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;

}
