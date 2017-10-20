#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define BORDER_WIDTH 1
#define CELL_WIDTH 10
#define CELL_HEIGHT 10
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define CELL_COUNT (WINDOW_WIDTH / CELL_WIDTH)

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

typedef bool cell;

cell**
create_grid(const size_t rows,
            const size_t cols)
{
    const size_t outer_rows = rows + 2;
    const size_t outer_cols = cols + 2;

    cell** cells = calloc(outer_rows, sizeof(cell*));
    for (size_t i = 0; i != outer_rows; ++i)
    {
        cells[i] = calloc(outer_cols, sizeof(cell));
        cells[i]++;
    }


    // Increment outer to inner layer, allows us to go out of bounds.
    cells++;

    // Randomize
    for (size_t i = 0; i != rows; ++i)
        for (size_t j = 0; j != cols; ++j)
            cells[i][j] = (j + 1) % 2 == 0;

    return cells;
}

void
destroy_grid(cell** cells,
             const size_t rows)
{
    --cells;
    for (size_t i = 0; i != rows; ++i)
        free(--cells[i]);
    free(cells);
}

void
copy_grid(cell** restrict dest,
          cell** restrict src,
          const size_t rows,
          const size_t cols)
{
    for (size_t i = 0; i != rows; ++i)
        memcpy(dest[i], src[i], sizeof(cell) * cols);
}

void
draw_grid(cell** grid,
          const size_t rows,
          const size_t cols,
          SDL_Renderer* renderer)
{
    SDL_Color prev_color;
    SDL_GetRenderDrawColor(renderer,
                           &prev_color.r,
                           &prev_color.g,
                           &prev_color.b,
                           &prev_color.a);

    SDL_SetRenderDrawColor(renderer, 0, 128, 255, 255);
    for (size_t i = 0; i != rows; ++i)
    {
        for (size_t j = 0; j != cols; ++j)
        {
            SDL_Rect rect =
            {
                .x = i * CELL_WIDTH + BORDER_WIDTH,
                .y = j * CELL_HEIGHT + BORDER_WIDTH,
                .w = CELL_WIDTH - (BORDER_WIDTH * 2),
                .h = CELL_WIDTH - (BORDER_WIDTH * 2),
            };

            if (grid[i][j])
                SDL_RenderFillRect(renderer, &rect);
        }
    }

    SDL_SetRenderDrawColor(renderer,
                           prev_color.r,
                           prev_color.g,
                           prev_color.b,
                           prev_color.a);
}

int
main(int argc, char** argv)
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    if (!sdl_init(&window, &renderer, WINDOW_WIDTH, WINDOW_HEIGHT))
        return 1;

    cell** grid = create_grid(CELL_COUNT, CELL_COUNT);

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
        draw_grid(grid,
                  CELL_COUNT,
                  CELL_COUNT,
                  renderer);

        SDL_RenderPresent(renderer);

        SDL_Delay(30);
    }


    destroy_grid(grid, CELL_COUNT);
    sdl_shutdown(window, renderer);

    return 0;
}
