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
///
/// -   Move to bit pattern rather than bools.
///
/// Ideas:
/// -   Two threads start at opposite sides and different
///     rows, and work their way towards a shared middle,
///     the upper thread always takes the middle path.
///     - Problems:
///       -  Won't get rid of buffering current and above/below
///          as that is inherent in the problem.
///       -  Also will still have to buffer the borders
/// -   Use a bit flag to check if the cell has changed.
///     - Problems:
///       - Effectively doubles the amount of memory needed,
///         for a grid,
//        - Adds extra computation (might be worth it)
///
///////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdatomic.h>

//#define SLOW_UPDATE

#define BORDER_WIDTH 1
#ifdef SLOW_UPDATE
#define CELL_WIDTH 100
#define CELL_HEIGHT 200
#else
#define CELL_WIDTH 5
#define CELL_HEIGHT 5
#endif
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define CELL_COL_COUNT (WINDOW_WIDTH / CELL_WIDTH)
#define CELL_ROW_COUNT (WINDOW_HEIGHT / CELL_HEIGHT)
#define CELL_COL_OFFSET 1
#define CELL_ROW_OFFSET 1

#ifdef SLOW_UPDATE
#define THREAD_COUNT 1
#else
#define THREAD_COUNT 4
#endif

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

    (*out_window) = SDL_CreateWindow("non_double_buffered_conways",
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

size_t
calc_cell_idx(const int row, // Which y position
              const int col) // Which x position
{
    const size_t tot_col_count = (CELL_COL_COUNT + CELL_COL_OFFSET * 2);
    return (tot_col_count * (row + CELL_ROW_OFFSET)) + CELL_COL_OFFSET + col;
}

bool
handle_events(cell* grid,
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
            const int selected_col = event.button.x / CELL_WIDTH;
            const int selected_row = event.button.y / CELL_HEIGHT;

            if (selected_col >= 0 && selected_col < (int)cols &&
                selected_row >= 0 && selected_row < (int)rows)
            {
                const size_t idx = calc_cell_idx(selected_row, selected_col);
                grid[idx] = !grid[idx];
            }
        }
    }
    return true;
}

cell*
create_grid(const size_t rows,
            const size_t cols)
{
    // Creating an outer layer for the grid,
    // allowing us to drop the bounds checking.
    const size_t outer_rows = rows + CELL_ROW_OFFSET * 2;
    const size_t outer_cols = cols + CELL_COL_OFFSET * 2;

    cell* grid = calloc(outer_rows * outer_cols, sizeof(cell));
    // Set an initial state
    for (size_t i = 0; i != rows; ++i)
    {
        for (size_t j = 0; j != cols; ++j)
        {
            size_t idx = calc_cell_idx(i, j);
            grid[idx] = (i - 1) % 2 == 0;
        }
    }

    return grid;
}

void
copy_grid(cell* restrict dest,
          cell* restrict src,
          const size_t rows,
          const size_t cols)
{
    int count = (rows + CELL_ROW_OFFSET * 2) * (cols + CELL_COL_OFFSET * 2);
    memcpy(dest, src, count * sizeof(cell));
}

void
draw_grid(cell* grid,
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
            size_t idx = calc_cell_idx(i, j);
            if (grid[idx])
            {
                SDL_Rect rect =
                {
                    .x = j * CELL_WIDTH + BORDER_WIDTH,
                    .y = i * CELL_HEIGHT + BORDER_WIDTH,
                    .w = CELL_WIDTH - (BORDER_WIDTH * 2),
                    .h = CELL_HEIGHT - (BORDER_WIDTH * 2),
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
sub_update(cell* restrict curr,
           cell* restrict prev,
           size_t row_begin,
           size_t row_end,
           size_t cols)
{
    // Rules from: https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life
    // Any live cell with fewer than two live neighbours dies, as if caused by underpopulation.
    // Any live cell with two or three live neighbours lives on to the next generation.
    // Any live cell with more than three live neighbours dies, as if by overpopulation.
    // Any dead cell with exactly three live neighbours becomes a live cell, as if by reproduction.

    // Make sure that on last iteration, we check the border buffer

    // Need to stop one before, or at least change to the border buffer for checking below
    for (size_t i = row_begin; i != row_end; ++i)
    {
        for (size_t j = 0; j != cols; ++j)
        {
            int alive_neighbors = 0;

            // Check form above buffer
            // Above
            alive_neighbors += prev[calc_cell_idx(i - 1, j - 1)];
            alive_neighbors += prev[calc_cell_idx(i - 1, j)];
            alive_neighbors += prev[calc_cell_idx(i - 1, j + 1)];

            // Check from current buffer
            // Side
            alive_neighbors += prev[calc_cell_idx(i, j - 1)];
            alive_neighbors += prev[calc_cell_idx(i, j + 1)];

            // Check from the actual memory area
            // Below
            alive_neighbors += prev[calc_cell_idx(i + 1, j - 1)];
            alive_neighbors += prev[calc_cell_idx(i + 1, j)];
            alive_neighbors += prev[calc_cell_idx(i + 1, j + 1)];

            size_t idx = calc_cell_idx(i, j);
            if (prev[idx])
            {
                if (alive_neighbors < 2)
                    curr[idx] = false;
                if (alive_neighbors == 2 || alive_neighbors == 3)
                    curr[idx] = true;
                if (alive_neighbors > 3)
                    curr[idx] = false;
            }
            else if (alive_neighbors == 3)
            {
                curr[idx] = true;
            }

            // Set above to point to current
            // Set current to point to above
            // memcpy new_current into current
        }
    }

    return NULL;
}

void
print_buffers(cell* above,
              cell* curr,
              cell* border,
              size_t id)
{
    char buffer[4096];
    char* info_buffer = buffer;
    info_buffer += sprintf(info_buffer, "Id: %zu\nAbove: ", id);

    for (size_t col = 0; col != CELL_COL_COUNT + CELL_COL_OFFSET * 2; ++col)
        info_buffer += sprintf(info_buffer, "%d", above[col]);

    info_buffer += sprintf(info_buffer, "\nCurr:  ");

    for (size_t col = 0; col != CELL_COL_COUNT + CELL_COL_OFFSET * 2; ++col)
        info_buffer += sprintf(info_buffer, "%d", curr[col]);

    info_buffer += sprintf(info_buffer, "\nBelow: ");
    for (size_t col = 0; col != CELL_COL_COUNT + CELL_COL_OFFSET * 2; ++col)
        info_buffer += sprintf(info_buffer, "%d", border[col]);

    info_buffer += sprintf(info_buffer, "\n");

    printf("%s\n", buffer);
}

void*
sub_update_2(cell* restrict grid,
             cell* restrict above,
             cell* restrict curr,
             cell* restrict border,
             size_t row_begin,
             size_t row_end,
             size_t cols,
             size_t id)
{
    // Rules from: https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life
    // Any live cell with fewer than two live neighbours dies, as if caused by underpopulation.
    // Any live cell with two or three live neighbours lives on to the next generation.
    // Any live cell with more than three live neighbours dies, as if by overpopulation.
    // Any dead cell with exactly three live neighbours becomes a live cell, as if by reproduction.

    // Make sure that on last iteration, we check the border buffer

    // Need to stop one before, or at least change to the border buffer for checking below

    //printf("Printing\n");

    for (size_t i = row_begin; i != row_end; ++i)
    {
        memcpy(curr, &grid[calc_cell_idx(i, -1)], sizeof(cell) * (CELL_COL_COUNT + CELL_COL_OFFSET * 2));
        #ifdef SLOW_UPDATE
        system("clear");
        print_buffers(above, curr, border, id);
        #endif
        for (size_t j = 0; j != cols; ++j)
        {
            int alive_neighbors = 0;

            // Check form above buffer
            // Above
            alive_neighbors += above[j + CELL_COL_OFFSET - 1];
            alive_neighbors += above[j + CELL_COL_OFFSET];
            alive_neighbors += above[j + CELL_COL_OFFSET + 1];
            int above_neighbors = alive_neighbors;

            // Check from current buffer
            // Side

            alive_neighbors += curr[j + CELL_COL_OFFSET - 1];
            alive_neighbors += curr[j + CELL_COL_OFFSET + 1];
            int side_neighbors = alive_neighbors - above_neighbors;

            // Check from the actual memory area
            // Below
            if (i < row_end - 1)
            {
                alive_neighbors += grid[calc_cell_idx(i + 1, j - 1)];
                alive_neighbors += grid[calc_cell_idx(i + 1, j)];
                alive_neighbors += grid[calc_cell_idx(i + 1, j + 1)];
            }
            else
            {
                alive_neighbors += border[j + CELL_COL_OFFSET - 1];
                alive_neighbors += border[j + CELL_COL_OFFSET];
                alive_neighbors += border[j + CELL_COL_OFFSET + 1];
            }
            int below_neighbors = alive_neighbors - side_neighbors - above_neighbors;

            size_t idx = calc_cell_idx(i, j);
            #ifndef SLOW_UPDATE
            if (grid[idx])
            {
                if (alive_neighbors < 2)
                    grid[idx] = false;
                if (alive_neighbors == 2 || alive_neighbors == 3)
                    grid[idx] = true;
                if (alive_neighbors > 3)
                    grid[idx] = false;
            }
            else if (alive_neighbors == 3)
            {
                grid[idx] = true;
            }
            #else
            printf("cols: %zu, row: %zu, col: %zu\n", cols, i, j);
            printf("above_neighbors: %d\n", above_neighbors);
            printf("side_neighbors: %d\n", side_neighbors);
            printf("below_neighbors: %d\n", below_neighbors);
            #endif

            // Set above to point to current
            // Set current to point to above
            // memcpy new_current into current
        }

            memcpy(above, curr, (CELL_COL_COUNT + CELL_COL_OFFSET * 2) * sizeof(cell));
        #ifdef SLOW_UPDATE
        getc(stdin);
        #endif
    }

    return NULL;
}

// Contains all information needed by a single thread to run.
typedef struct
{
    cell* restrict grid;
    cell* restrict prev;
    size_t row_begin;
    size_t row_end;
    size_t cols;

    cell* restrict above_buffer;
    cell* restrict current_buffer;
    cell* restrict border_buffer;

    // Synchronization vars
    atomic_bool* running;
    atomic_int* signal;
    pthread_cond_t* cv;
    pthread_mutex_t* cv_mtx;

    size_t id;
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

//        sub_update(args.curr, args.prev,
//                   args.row_begin, args.row_end,
//                   args.cols);

        sub_update_2(args.grid, args.above_buffer,
                     args.current_buffer, args.border_buffer,
                     args.row_begin, args.row_end,
                     args.cols, args.id);

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
create_threads(cell* restrict grid,
               cell* restrict prev,
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
        info.params[i].grid = grid;
        info.params[i].prev = prev;
        info.params[i].row_begin = (rows / THREAD_COUNT) * i;
        info.params[i].row_end = (rows / THREAD_COUNT) * (i + 1);
        info.params[i].cols = cols;
        info.params[i].running = info.running;
        info.params[i].signal = info.signal;
        info.params[i].cv = info.cv;
        info.params[i].cv_mtx = info.cv_mtx;
        info.params[i].id = i;

        info.params[i].above_buffer = malloc(CELL_COL_COUNT + CELL_COL_OFFSET * 2);
        info.params[i].current_buffer = malloc(CELL_COL_COUNT + CELL_COL_OFFSET * 2);
        info.params[i].border_buffer = malloc(CELL_COL_COUNT + CELL_COL_OFFSET * 2);
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

    for (size_t i = 0; i != THREAD_COUNT - 1; ++i)
        pthread_join(info->threads[i], NULL);

    for (size_t i = 0; i != THREAD_COUNT; ++i)
    {
        free(info->params[i].above_buffer);
        free(info->params[i].current_buffer);
        free(info->params[i].border_buffer);

    }

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
    /// Memcpy in all sub buffers

    for (size_t i = 0; i != THREAD_COUNT; ++i)
    {
        const int above_row = calc_cell_idx((int)info->params[i].row_begin - 1, -1);
        //printf("i: %zu, above_row: %i\n", i, above_row);
        memcpy(info->params[i].above_buffer, &info->params[0].grid[above_row], (CELL_COL_COUNT + CELL_COL_OFFSET * 2) * sizeof(cell));

        const int curr_row = calc_cell_idx((int)info->params[i].row_begin, -1);
        //printf("i: %zu, curr_row: %d\n", i, curr_row);
        memcpy(info->params[i].current_buffer, &info->params[0].grid[curr_row], (CELL_COL_COUNT + CELL_COL_OFFSET * 2) * sizeof(cell));


        const int border_row = calc_cell_idx((int)info->params[i].row_end, -1);
        //printf("i: %zu, border_row: %d\n", i, border_row);
        memcpy(info->params[i].border_buffer, &info->params[0].grid[border_row], (CELL_COL_COUNT + CELL_COL_OFFSET * 2) * sizeof(cell));
    }

    // for (size_t row = 0; row != CELL_ROW_COUNT; ++row)
    // {
    //     for (size_t col = 0; col != CELL_COL_COUNT; ++col)
    //     {
    //         printf("%d", info->params[0].grid[calc_cell_idx(row, col)]);
    //     }
    //     printf("\n");
    // }
    // printf("\n");

    // printf("Above: \n");
    // for (size_t col = 0; col != CELL_COL_COUNT + CELL_COL_OFFSET * 2; ++col)
    //     printf("%d", info->params[0].above_buffer[col]);

    // printf("\nCurr: \n");
    // for (size_t col = 0; col != CELL_COL_COUNT + CELL_COL_OFFSET * 2; ++col)
    //     printf("%d", info->params[0].current_buffer[col]);

    // printf("\nBelow: \n");
    // for (size_t col = 0; col != CELL_COL_COUNT + CELL_COL_OFFSET * 2; ++col)
    //     printf("%d", info->params[0].border_buffer[col]);
    // printf("\n");

    system("clear");

    /// Awake all threads
    pthread_cond_broadcast(info->cv);

    //sub_update(info->params[0].curr, info->params[0].prev,
    //           info->params[0].row_begin, info->params[0].row_end,
    //           info->params[0].cols);

    sub_update_2(info->params[0].grid, info->params[0].above_buffer,
                 info->params[0].current_buffer, info->params[0].border_buffer,
                 info->params[0].row_begin, info->params[0].row_end,
                 info->params[0].cols, info->params[0].id);

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

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    cell* prev_grid = create_grid(CELL_ROW_COUNT, CELL_COL_COUNT);
    cell* curr_grid = create_grid(CELL_ROW_COUNT, CELL_COL_COUNT);
    copy_grid(prev_grid, curr_grid, CELL_ROW_COUNT, CELL_COL_COUNT);

    thread_info threads = create_threads(curr_grid,
                                         prev_grid,
                                         CELL_ROW_COUNT,
                                         CELL_COL_COUNT);

    bool should_continue = true;
    bool iterate = false;
    while (should_continue)
    {
        should_continue = handle_events(curr_grid, CELL_ROW_COUNT,
                                        CELL_COL_COUNT, &iterate);

        if (iterate)
            update_grid(&threads);

        copy_grid(prev_grid, curr_grid, CELL_ROW_COUNT, CELL_COL_COUNT);



        SDL_RenderClear(renderer);
        draw_grid(prev_grid,
                  CELL_ROW_COUNT,
                  CELL_COL_COUNT,
                  renderer);

        SDL_RenderPresent(renderer);

        SDL_Delay(60);
    }

    destroy_threads(&threads);

    free(prev_grid);
    free(curr_grid);

    sdl_shutdown(window, renderer);

    printf("rows: %d, cols: %d, tot_size: %d\n",
           CELL_ROW_COUNT + CELL_ROW_OFFSET * 2,
           CELL_COL_COUNT + CELL_COL_OFFSET * 2,
           (CELL_ROW_COUNT + CELL_ROW_OFFSET * 2) * (CELL_COL_COUNT + CELL_COL_OFFSET * 2));

    return 0;
}