///////////////////////////////////////////////////////////
/// Double buffered solution using cond variables etc
/// to avoid recreating threads all the time.
///
/// Todo:
/// -   Can probably shrink or get rid of thread_params
///     structure.
///     Can calculate begin, end for example.
///     However, we then need an id.
///     Getting rid of the thread_params would also
///     mean that we could just use the thread_info
///     and would not need to point back into it with
///     thread_params.
///
/// -   Ensure that the memory ordering is actually
///     correct.
///////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdatomic.h>

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

    cell** grid = calloc(outer_rows, sizeof(cell*));
    for (size_t i = 0; i != outer_rows; ++i)
    {
        grid[i] = calloc(outer_cols, sizeof(cell));
        grid[i]++;
    }

    grid++;

    // Set an initial state
    for (size_t i = 0; i != rows; ++i)
        for (size_t j = 0; j != cols; ++j)
            grid[i][j] = (j + 1) % 2 == 0;

    return grid;
}

void
destroy_grid(cell** grid,
             const size_t rows)
{
    // Move out to outer layer to delete properly.
    --grid;
    for (size_t i = 0; i != rows + 2; ++i)
        free(--grid[i]);
    free(grid);
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


void*
sub_update(cell** restrict curr,
           cell** restrict prev,
           size_t row_begin,
           size_t row_end,
           size_t cols)
{
    // Rules from: https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life
    // Any live cell with fewer than two live neighbours dies, as if caused by underpopulation.
    // Any live cell with two or three live neighbours lives on to the next generation.
    // Any live cell with more than three live neighbours dies, as if by overpopulation.
    // Any dead cell with exactly three live neighbours becomes a live cell, as if by reproduction.
    for (size_t i = row_begin; i != row_end; ++i)
    {
        for (size_t j = 0; j != cols; ++j)
        {
            int alive_neighbors = 0;

            // left side
            alive_neighbors += prev[i - 1][j - 1];
            alive_neighbors += prev[i - 1][j];
            alive_neighbors += prev[i - 1][j + 1];

            // above below
            alive_neighbors += prev[i][j - 1];
            alive_neighbors += prev[i][j + 1];

            // right side
            alive_neighbors += prev[i + 1][j - 1];
            alive_neighbors += prev[i + 1][j];
            alive_neighbors += prev[i + 1][j + 1];

            if (prev[i][j])
            {
                if (alive_neighbors < 2)
                    curr[i][j] = false;
                if (alive_neighbors == 2 || alive_neighbors == 3)
                    curr[i][j] = true;
                if (alive_neighbors > 3)
                    curr[i][j] = false;
            }
            else if (alive_neighbors == 3)
            {
                curr[i][j] = true;
            }
        }
    }

    return NULL;
}

// Contains all information needed by a single thread to run.
typedef struct
{
    cell** restrict curr;
    cell** restrict prev;
    size_t row_begin;
    size_t row_end;
    size_t cols;

    // Synchronization vars
    atomic_bool* running;
    atomic_int* signal;
    pthread_cond_t* cv;
    pthread_mutex_t* cv_mtx;
} thread_params;

void*
thread_execution(void* params)
{
    thread_params args = *(thread_params*)params;
    while (atomic_load_explicit(args.running,
                                memory_order_relaxed))
    {
        pthread_mutex_lock(args.cv_mtx);
        pthread_cond_wait(args.cv, args.cv_mtx);
        pthread_mutex_unlock(args.cv_mtx);

        sub_update(args.curr, args.prev,
                   args.row_begin, args.row_end,
                   args.cols);

        atomic_fetch_add_explicit(args.signal, 1, memory_order_release);
    }

    return NULL;
}

// Holds all variables that needs to be deallocated,
// and that is used to communicate between threads.
typedef struct
{
    atomic_bool* running;
    atomic_int* signal;
    pthread_cond_t* cv;
    pthread_mutex_t* cv_mtx;
    pthread_t* threads;
    thread_params* params;
} thread_info;

thread_info
create_threads(cell** restrict curr,
               cell** restrict prev,
               const size_t rows,
               const size_t cols)
{
    thread_info info =
    {
        .running = malloc(sizeof(atomic_bool)),
        .signal = malloc(sizeof(atomic_int)),
        .cv = malloc(sizeof(pthread_cond_t)),
        .cv_mtx = malloc(sizeof(pthread_mutex_t)),
        .threads = malloc((THREAD_COUNT - 1) * sizeof(pthread_t)),
        .params = malloc((THREAD_COUNT) * sizeof(thread_params)),
    };

    atomic_init(info.running, true);
    atomic_init(info.signal, 0);
    pthread_cond_init(info.cv, NULL);
    pthread_mutex_init(info.cv_mtx, NULL);

    // Initialize threads and start execution
    for (size_t i = 0; i != THREAD_COUNT; ++i)
    {
        info.params[i].curr = curr;
        info.params[i].prev = prev;
        info.params[i].row_begin = (rows / THREAD_COUNT) * i;
        info.params[i].row_end = (rows / THREAD_COUNT) * (i + 1);
        info.params[i].cols = cols;
        info.params[i].running = info.running;
        info.params[i].signal = info.signal;
        info.params[i].cv = info.cv;
        info.params[i].cv_mtx = info.cv_mtx;
    }

    for (size_t i = 0; i != THREAD_COUNT - 1; ++i)
        pthread_create(&info.threads[i], NULL, thread_execution, &info.params[i + 1]);

    return info;
}

void
destroy_threads(thread_info* info)
{
    atomic_store_explicit(info->running, false, memory_order_release);
    pthread_cond_broadcast(info->cv);

    for (size_t i = 0; i < THREAD_COUNT - 1; ++i)
        pthread_join(info->threads[i], NULL);

    pthread_cond_destroy(info->cv);
    pthread_mutex_destroy(info->cv_mtx);
    free(info->running);
    free(info->signal);
    free(info->cv);
    free(info->cv_mtx);
    free(info->threads);
    free(info->params);
}

void
update_grid(thread_info* info)
{
    pthread_cond_broadcast(info->cv);
    sub_update(info->params[0].curr, info->params[0].prev,
               info->params[0].row_begin, info->params[0].row_end,
               info->params[0].cols);

    int expected = THREAD_COUNT - 1;
    while (!atomic_compare_exchange_weak_explicit(info->signal,
                                                  &expected,
                                                  0,
                                                  memory_order_acquire,
                                                  memory_order_relaxed))
    {
        expected = THREAD_COUNT - 1;
    }
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

    thread_info threads = create_threads(curr_grid,
                                         prev_grid,
                                         CELL_COUNT,
                                         CELL_COUNT);

    bool should_continue = true;
    bool iterate = false;
    while (should_continue)
    {
        should_continue = handle_events(curr_grid, CELL_COUNT,
                                        CELL_COUNT, &iterate);

        if (iterate)
            update_grid(&threads);

        copy_grid(prev_grid, curr_grid, CELL_COUNT, CELL_COUNT);

        SDL_RenderClear(renderer);
        draw_grid(prev_grid,
                  CELL_COUNT,
                  CELL_COUNT,
                  renderer);

        SDL_RenderPresent(renderer);

        SDL_Delay(60);
    }

    destroy_threads(&threads);

    destroy_grid(prev_grid, CELL_COUNT);
    destroy_grid(curr_grid, CELL_COUNT);

    sdl_shutdown(window, renderer);

    return 0;
}
