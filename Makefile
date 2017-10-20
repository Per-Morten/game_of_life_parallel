.PHONY: clean
clean:
	rm -f ./double_buffer ./double_buffer_leak

double_buffer: double_buffer.c
	gcc double_buffer.c -o double_buffer -std=c11 -O3 -Wall -Wextra -lSDL2

.PHONY: run_double_buffer
run_double_buffer: clean double_buffer
	./double_buffer

.PHONY: clean leak_double_buffer
leak_double_buffer:
	gcc -g3 -fno-omit-frame-pointer -std=c11 -fsanitize=address double_buffer.c -o double_buffer_leak -lSDL2; ASAN_OPTIONS=detect_leaks=1; ./double_buffer_leak
