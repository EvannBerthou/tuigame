all: build/main_game

$(shell mkdir -p build)

build/main_game: machines_builder build_docs src/main.c
	gcc -Wall -Wextra -Warray-bounds -Wno-override-init-side-effects -Wno-initializer-overrides \
		src/main.c -o build/main_game \
		-I./include -L ./lib/linux -lraylib -lm -ggdb

build_docs:
	sh tools/build_help_pages.sh

machines_builder:
	python tools/machines_builder.py > assets/machines

run: build/main_game
	./build/main_game

clean:
	-rm -rf build

.PHONY: all run clean machines_builder build_docs
