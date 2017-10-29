/////////////////////////////////////////////////////////////////////
/// Non double buffered solution.
/// Line of thought:
/// - Buffering:
///     Cellular automata requires some sort of buffering,
///     as the new state depends on the last one.
///     However, theoretically you only need to buffer
///     the row above you, the cells to the left of
///     current cell, assuming traversal left to right.
///     In practice this essentially means two row buffers,
///     One for above, and one for current.
///     When introducing multiple threads, each thread
///     needs 3 buffers.
///     The two normal ones (above and current),
///     and 1 for the border of the thread below.
///     This is so that the potentially updated updated
///     border cannot affect the result of the last
///     row in a threads working area.
/// - Bounds checking:
///     Decided to avoid bounds checking by adding a border
///     around the entire grid.
///
/// Thoughts/Reflections:
/// -   Can probably shrink or get rid of thread_params
///     structure.
///     Can calculate begin, end for example.
///     However, we then need an id.
///     Getting rid of the thread_params would also
///     mean that we could just use the thread_info
///     and would not need to point back into it with
///     thread_params.
/// -   Can probably add some attributes to help compiler,
///     for example, most cells will not be alive, so can
///     do a expects solution.
/// -   Should decide on using int or size_t, unlike C++
///     size_t doesn't actually give me anything int
///     doesn't. I had to semi convert to int to get copying
///     of borders to work, so might as well go all the way.
/// -   Should be consistent on what is macros and what is
///     parameters?
///     Threads are decided by macro, solutions in functions
///     while grid size is sent as parameters to functions.
///
/// Other Ideas:
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
///       - Adds extra computation (might be worth it)
///
/// - Restriction to make bit patterns work
///     Total columns per row
///     (i.e. CELL_COL_COUNT + CELL_COL_OFFSET * 2),
///     must be multiple of 8.
///     This is to ensure proper row copying
///     as we cannot address one bit.
///     Solution to avoiding this could be to pad each
///     row on both sides.
///     But currently I just go for multiple of 8 solution
/////////////////////////////////////////////////////////////////////

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <pthread.h>
#include <SDL2/SDL.h>

#define BORDER_WIDTH 1
#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#define CELL_TOT_COL 128
#define CELL_TOT_ROW 130
#define CELL_COL_OFFSET 1
#define CELL_ROW_OFFSET 1
#define CELL_COL_COUNT (CELL_TOT_COL - CELL_COL_OFFSET * 2)
#define CELL_ROW_COUNT (CELL_TOT_ROW - CELL_ROW_OFFSET * 2)
#define CELL_WIDTH (WINDOW_WIDTH / CELL_COL_COUNT)
#define CELL_HEIGHT (WINDOW_HEIGHT / CELL_ROW_COUNT)

#define THREAD_COUNT 8

#if ((CELL_TOT_COL) % 8 != 0)
#error "CELL_TOT_COL is not multiple of 8"
#endif

#if ((CELL_ROW_COUNT) % THREAD_COUNT != 0)
#error "CELL_ROW_COUNT is not multiple of THREAD_COUNT"
#endif

///////////////////////////////////////////////////////////
/// Grid
///////////////////////////////////////////////////////////
typedef uint8_t cell;

size_t
get_byte_idx(const int row,
             const int col)
{
    const int row_idx = (row + CELL_ROW_OFFSET) * (CELL_TOT_COL / 8);
    const int row_byte = (col + CELL_COL_OFFSET) / 8;
    return row_idx + row_byte;
}

void
set_cell(cell* grid,
         const int row,
         const int col,
         bool val)
{
    const int row_idx = (row + CELL_ROW_OFFSET) * (CELL_TOT_COL / 8);
    const int row_byte = (col + CELL_COL_OFFSET) / 8;
    const int byte_idx = row_idx + row_byte;

    // Conditionally skipping one bit if we are in byte which has border
    const int bit_idx = (col + (row_byte != 0)) % 8 + (row_byte == 0);

    if (val)
        grid[byte_idx] |= (1 << bit_idx);
    else
        grid[byte_idx] &= ~(1 << bit_idx);
}

bool
get_cell(const cell* grid,
         const int row,
         const int col)
{
    const int row_idx = (row + CELL_ROW_OFFSET) * (CELL_TOT_COL / 8);
    const int row_byte = (col + CELL_COL_OFFSET) / 8;
    const int byte_idx = row_idx + row_byte;
    const int bit_idx = (col + (row_byte != 0)) % 8 + (row_byte == 0);

    return grid[byte_idx] & (1 << bit_idx);
}

bool
get_cell_from_row(const cell* row,
                  const int col)
{
    const int row_byte = (col + CELL_COL_OFFSET) / 8;
    const int bit_idx = (col + (row_byte != 0)) % 8 + (row_byte == 0);
    return row[row_byte] & (1 << bit_idx);
}

void
copy_row(cell* restrict dst,
         const cell* restrict src,
         const size_t cols)
{
    int size = (cols + CELL_COL_OFFSET * 2) / 8;
    memcpy(dst, src, size);
}


cell*
create_row(const size_t cols)
{
    const size_t outer_cols = (cols + CELL_COL_OFFSET * 2) / 8;
    return calloc(outer_cols, sizeof(cell));
}

// Kept for debug purposes.
void
print_row(cell* row,
          const size_t col_count)
{
    for (size_t i = 0; i < col_count; ++i)
    {
        for (size_t j = 0; j < 8; ++j)
        {
            printf("%d", (row[i] & (1 << j)) >> j);
        }
        printf(" ");
    }
    printf("\n");
}

void*
sub_update(cell* restrict grid,
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

    for (size_t i = row_begin; i != row_end; ++i)
    {
        copy_row(curr, &grid[get_byte_idx(i, -1)], cols);

        for (size_t j = 0; j != cols; ++j)
        {
            int alive_neighbors = 0;

            // Check form above buffer
            // Above
            alive_neighbors += get_cell_from_row(above, j - 1);
            alive_neighbors += get_cell_from_row(above, j);
            alive_neighbors += get_cell_from_row(above, j + 1);

            // Check from current buffer
            // Side
            alive_neighbors += get_cell_from_row(curr, j - 1);
            alive_neighbors += get_cell_from_row(curr, j + 1);

            // Need to use buffer if we are closing in on row_end
            // as that position in grid has probably been updated by thread below.
            // Below
            if (i < row_end - 1)
            {
                alive_neighbors += get_cell(grid, i + 1, j - 1);
                alive_neighbors += get_cell(grid, i + 1, j);
                alive_neighbors += get_cell(grid, i + 1, j + 1);
            }
            else
            {
                alive_neighbors += get_cell_from_row(border, j - 1);
                alive_neighbors += get_cell_from_row(border, j);
                alive_neighbors += get_cell_from_row(border, j + 1);
            }

            bool val = (alive_neighbors == 3 ||
                        (get_cell(grid, i, j) && alive_neighbors == 2));
            set_cell(grid, i, j, val);

        }

        copy_row(above, curr, cols);
    }

    return NULL;
}


cell*
create_grid(const size_t rows,
            const size_t cols)
{
    // Creating an outer layer for the grid,
    // allowing us to drop the bounds checking.
    const size_t outer_rows = rows + CELL_ROW_OFFSET * 2;
    const size_t outer_cols = (cols + CELL_COL_OFFSET * 2) / 8;

    cell* grid = calloc(outer_rows * outer_cols, sizeof(cell));

    // Set an initial state
    for (size_t i = 0; i != rows; ++i)
    {
        for (size_t j = 0; j != cols; ++j)
        {
            set_cell(grid, i, j, ((i - 1) % 2 == 0));
        }
    }

    return grid;
}

///////////////////////////////////////////////////////////
/// SDL
///////////////////////////////////////////////////////////
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
                bool val = get_cell(grid, selected_row, selected_col);
                set_cell(grid, selected_row, selected_col, !val);
            }
        }
    }
    return true;
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
            if (get_cell(grid, i, j))
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

///////////////////////////////////////////////////////////
/// Threads
///////////////////////////////////////////////////////////
// Contains all information needed by a single thread to run.
typedef struct
{
    cell* restrict grid;
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
    thread_params* args = (thread_params*)params;
    while (atomic_load_explicit(args->running,
                                memory_order_relaxed))
    {
        pthread_mutex_lock(args->cv_mtx);
        pthread_cond_wait(args->cv, args->cv_mtx);
        pthread_mutex_unlock(args->cv_mtx);

        sub_update(args->grid, args->above_buffer,
                   args->current_buffer, args->border_buffer,
                   args->row_begin, args->row_end,
                   args->cols, args->id);

        atomic_fetch_add_explicit(args->signal, 1, memory_order_release);
    }

    return NULL;
}

// Holds all variables that needs to be deallocated,
// and that is used to communicate between threads.
// This structure should be merged together with thread_params.
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
        info.params[i].row_begin = (rows / THREAD_COUNT) * i;
        info.params[i].row_end = (rows / THREAD_COUNT) * (i + 1);
        info.params[i].cols = cols;
        info.params[i].running = info.running;
        info.params[i].signal = info.signal;
        info.params[i].cv = info.cv;
        info.params[i].cv_mtx = info.cv_mtx;
        info.params[i].id = i;

        info.params[i].above_buffer = create_row(CELL_COL_COUNT);
        info.params[i].current_buffer = create_row(CELL_COL_COUNT);
        info.params[i].border_buffer = create_row(CELL_COL_COUNT);
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
    // memcpy in all buffers where races might occur
    for (size_t i = 0; i != THREAD_COUNT; ++i)
    {
        const int above_row = get_byte_idx((int)info->params[i].row_begin - 1, -1);
        copy_row(info->params[i].above_buffer,
                 &info->params[0].grid[above_row],
                 info->params[i].cols);

        const int border_row = get_byte_idx((int)info->params[i].row_end, -1);
        copy_row(info->params[i].border_buffer,
                 &info->params[0].grid[border_row],
                 info->params[i].cols);
    }

    // Awake all threads
    pthread_cond_broadcast(info->cv);

    sub_update(info->params[0].grid, info->params[0].above_buffer,
               info->params[0].current_buffer, info->params[0].border_buffer,
               info->params[0].row_begin, info->params[0].row_end,
               info->params[0].cols, info->params[0].id);

    // Just spinning in place, as the threads are given the same amount of work
    // they should not be that far away from each other in terms of time.
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

    cell* curr_grid = create_grid(CELL_ROW_COUNT, CELL_COL_COUNT);

    thread_info threads = create_threads(curr_grid,
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

        SDL_RenderClear(renderer);
        draw_grid(curr_grid,
                  CELL_ROW_COUNT,
                  CELL_COL_COUNT,
                  renderer);

        SDL_RenderPresent(renderer);

        SDL_Delay(60);
    }

    destroy_threads(&threads);

    free(curr_grid);

    sdl_shutdown(window, renderer);

    printf("rows: %d, cols: %d, tot_size: %d\n",
           CELL_TOT_ROW,
           CELL_TOT_COL,
           CELL_TOT_COL * CELL_TOT_ROW);

    return 0;
}
