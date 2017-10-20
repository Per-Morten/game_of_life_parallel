.PHONY: clean
clean:
	rm -f ./double_buffer ./double_buffer_leak ./single_threaded ./cond_double_buffer

cond_double_buffer: cond_double_buffer.c
	gcc -std=c11 -O3 -Wall -Wextra cond_double_buffer.c -o cond_double_buffer -lSDL2 -lpthread

double_buffer: double_buffer.c
	gcc -std=c11 -O3 -Wall -Wextra double_buffer.c -o double_buffer -lSDL2 -lpthread

single_threaded: single_threaded.c
	gcc -std=c11 -O3 -Wall -Wextra single_threaded.c -o single_threaded -lSDL2

.PHONY: run_single_threaded
run_single_threaded: clean single_threaded
	./single_threaded

.PHONY: run_double_buffer
run_double_buffer: clean double_buffer
	./double_buffer

.PHONY: run_cond_double_buffer
run_cond_double_buffer: clean cond_double_buffer
	./cond_double_buffer

.PHONY: clean leak_double_buffer
leak_double_buffer:
	gcc -g3 -std=c11 -Wall -Wextra -fno-omit-frame-pointer -fsanitize=address double_buffer.c -o double_buffer_leak -lSDL2 -lpthread; ASAN_OPTIONS=detect_leaks=1; ./double_buffer_leak
