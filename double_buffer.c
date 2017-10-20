#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

int
sdl_init(SDL_Window** out_window,
         SDL_Renderer** out_renderer,
         const int window_width,
         const int window_height)
{
    (*out_window) = NULL;
    (*out_renderer) = NULL;

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        goto failure;
    }

    (*out_window) = SDL_CreateWindow("double_buffered_conways",
                                     SDL_WINDOWPOS_UNDEFINED,
                                     SDL_WINDOWPOS_UNDEFINED,
                                     window_width,
                                     window_height,
                                     SDL_WINDOW_SHOWN);

    if ((*out_window) == NULL)
    {
        goto failure;
    }

    (*out_renderer) = SDL_CreateRenderer((*out_window),
                                         -1,
                                         SDL_RENDERER_ACCELERATED);

    if ((*out_renderer) == NULL)
    {
        goto failure;
    }

    return 1;

failure:
    fprintf(stderr, "SDL_Error: %s\n", SDL_GetError());
    SDL_DestroyRenderer((*out_renderer));
    SDL_DestroyWindow((*out_window));
    return 0;
}

void
sdl_shutdown(SDL_Window* window,
             SDL_Renderer* renderer)
{
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

bool
sdl_handle_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_KEYUP &&
             event.key.keysym.sym == SDLK_ESCAPE))
        {
            return false;
        }
    }
    return true;
}

int
main(int argc, char** argv)
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    if (!sdl_init(&window, &renderer, 640, 480))
        return 1;

    bool should_continue = true;
    while (should_continue)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYUP &&
                event.key.keysym.sym == SDLK_ESCAPE))
            {
                should_continue = false;
            }
        }

        SDL_RenderClear(renderer);


        SDL_RenderPresent(renderer);
    }


    sdl_shutdown(window, renderer);

    return 0;
}
