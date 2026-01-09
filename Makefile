CC=gcc
CFLAGS=#-Wall -Wextra -Werror -Warray-bounds -Wno-initializer-overrides -std=c23 -pedantic

all: build/main_game

$(shell mkdir -p build)

build/main_game: machines_builder build_docs src/main.c src/basic.c
	$(CC) $(CFLAGS) src/main.c src/basic.c src/arena.c src/bootseq.c -o build/main_game -I./include -L ./lib/linux -lraylib -lm -ggdb

build_docs:
	sh tools/build_help_pages.sh

machines_builder:
	python tools/machines_builder.py > assets/machines

run: build/main_game
	./build/main_game

clean:
	-rm -rf build

analysis:
	cppcheck --std=c23 --suppress=missingIncludeSystem --suppress=staticFunction --check-level=exhaustive src/main.c src/basic.c src/arena.c src/bootseq.c

basic-build: src/basic.c
	$(CC) $(CFLAGS) -I./include src/basic.c src/arena.c -o build/basic -ggdb -DBASIC_TEST

test: basic-build
	python tools/basic-test.py

debug: basic-build
	gf2 ./build/basic

basic: basic-build
	./build/basic

.PHONY: all run clean machines_builder build_docs analysis
