#include <iostream>
#include <SDL2/SDL.h>

int main(int argc, char **argv)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cout << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    std::cout << "All Ok, proceed with SDL" << std::endl;

    SDL_Quit();

    return 0;
}
