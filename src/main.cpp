#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "../toy2d/toy2d.hpp"

#include <exception>
#include <iostream>
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

    try {
        toy2d::Init();
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
