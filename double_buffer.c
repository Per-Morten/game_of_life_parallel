#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <pthread.h>

#define BORDER_WIDTH 1
#define CELL_WIDTH 10
#define CELL_HEIGHT 10
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define CELL_COUNT (WINDOW_WIDTH / CELL_WIDTH)
#define THREAD_COUNT 4

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

typedef bool cell;

bool
handle_events(cell** grid,
              const size_t rows,
              const size_t cols,
              bool* iterate)
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

        if (event.type == SDL_KEYUP &&
            event.key.keysym.sym == SDLK_SPACE)
        {
            (*iterate) = !(*iterate);
        }

        if (event.type == SDL_MOUSEBUTTONUP)
        {
            const int selected_x = event.button.x / CELL_WIDTH;
            const int selected_y = event.button.y / CELL_HEIGHT;

            if (selected_x >= 0 && selected_x < (int)rows &&
                selected_y >= 0 && selected_y < (int)cols)
            {
                grid[selected_x][selected_y] = !grid[selected_x][selected_y];
            }
        }
    }
    return true;
}

cell**
create_grid(const size_t rows,
            const size_t cols)
{
    // Creating an outer layer for the grid,
    // allowing us to drop the bounds checking.
    const size_t outer_rows = rows + 2;
    const size_t outer_cols = cols + 2;

    cell** cells = calloc(outer_rows, sizeof(cell*));
    for (size_t i = 0; i != outer_rows; ++i)
    {
        cells[i] = calloc(outer_cols, sizeof(cell));
        cells[i]++;
    }

    cells++;

    // Set an initial state
    for (size_t i = 0; i != rows; ++i)
        for (size_t j = 0; j != cols; ++j)
            cells[i][j] = (j + 1) % 2 == 0;

    return cells;
}

void
destroy_grid(cell** cells,
             const size_t rows)
{
    // Move out to outer layer to delete properly.
    --cells;
    for (size_t i = 0; i != rows + 2; ++i)
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
            if (grid[i][j])
            {
                SDL_Rect rect =
                {
                    .x = i * CELL_WIDTH + BORDER_WIDTH,
                    .y = j * CELL_HEIGHT + BORDER_WIDTH,
                    .w = CELL_WIDTH - (BORDER_WIDTH * 2),
                    .h = CELL_WIDTH - (BORDER_WIDTH * 2),
                };

                SDL_RenderFillRect(renderer, &rect);
            }

        }
    }

    SDL_SetRenderDrawColor(renderer,
                           prev_color.r,
                           prev_color.g,
                           prev_color.b,
                           prev_color.a);
}

typedef struct
{
    cell** restrict curr;
    cell** restrict prev;
    size_t row_begin;
    size_t row_end;
    size_t cols;
} thread_params;

void*
sub_update(void* params)
{
    // Rules from: https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life
    // Any live cell with fewer than two live neighbours dies, as if caused by underpopulation.
    // Any live cell with two or three live neighbours lives on to the next generation.
    // Any live cell with more than three live neighbours dies, as if by overpopulation.
    // Any dead cell with exactly three live neighbours becomes a live cell, as if by reproduction.
    thread_params args = *(thread_params*)params;
    for (size_t i = args.row_begin; i != args.row_end; ++i)
    {
        for (size_t j = 0; j != args.cols; ++j)
        {
            int alive_neighbors = 0;

            // left side
            alive_neighbors += args.prev[i - 1][j - 1];
            alive_neighbors += args.prev[i - 1][j];
            alive_neighbors += args.prev[i - 1][j + 1];

            // above below
            alive_neighbors += args.prev[i][j - 1];
            alive_neighbors += args.prev[i][j + 1];

            // right side
            alive_neighbors += args.prev[i + 1][j - 1];
            alive_neighbors += args.prev[i + 1][j];
            alive_neighbors += args.prev[i + 1][j + 1];

            if (args.prev[i][j])
            {
                if (alive_neighbors < 2)
                    args.curr[i][j] = false;
                if (alive_neighbors == 2 || alive_neighbors == 3)
                    args.curr[i][j] = true;
                if (alive_neighbors > 3)
                    args.curr[i][j] = false;
            }
            else if (alive_neighbors == 3)
            {
                args.curr[i][j] = true;
            }
        }
    }

    return NULL;
}

void
update_grid(cell** restrict curr,
            cell** restrict prev,
            const size_t rows,
            const size_t cols)
{
    // Using main thread as well for calculations hence -1
    pthread_t threads[THREAD_COUNT - 1];
    thread_params params[THREAD_COUNT];

    for (size_t i = 0; i != THREAD_COUNT; ++i)
    {
        params[i].curr = curr;
        params[i].prev = prev;
        params[i].row_begin = (rows / 4) * i;
        params[i].row_end = (rows / 4) * (i + 1);
        params[i].cols = cols;
    }

    for (size_t i = 0; i < THREAD_COUNT - 1; ++i)
        pthread_create(&threads[i], NULL, sub_update, &params[i + 1]);

    sub_update(&params[0]);

    for (size_t i = 0; i < THREAD_COUNT - 1; ++i)
        pthread_join(threads[i], NULL);
}

int
main(int argc, char** argv)
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    if (!sdl_init(&window, &renderer, WINDOW_WIDTH, WINDOW_HEIGHT))
        return 1;

    cell** prev_grid = create_grid(CELL_COUNT, CELL_COUNT);
    cell** curr_grid = create_grid(CELL_COUNT, CELL_COUNT);
    copy_grid(prev_grid, curr_grid, CELL_COUNT, CELL_COUNT);

    bool should_continue = true;
    bool iterate = false;
    while (should_continue)
    {
        should_continue = handle_events(curr_grid, CELL_COUNT,
                                        CELL_COUNT, &iterate);

        if (iterate)
            update_grid(curr_grid, prev_grid, CELL_COUNT, CELL_COUNT);

        copy_grid(prev_grid, curr_grid, CELL_COUNT, CELL_COUNT);

        SDL_RenderClear(renderer);
        draw_grid(prev_grid,
                  CELL_COUNT,
                  CELL_COUNT,
                  renderer);

        SDL_RenderPresent(renderer);

        SDL_Delay(60);
    }


    destroy_grid(prev_grid, CELL_COUNT);
    destroy_grid(curr_grid, CELL_COUNT);

    sdl_shutdown(window, renderer);

    return 0;
}
