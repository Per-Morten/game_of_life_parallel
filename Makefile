.PHONY: clean
clean:
	rm -f ./double_buffer

double_buffer: double_buffer.c
	gcc double_buffer.c -o double_buffer -std=c11 -O3 -lSDL2

.PHONY: run_double_buffer
run_double_buffer: clean double_buffer
	./double_buffer

