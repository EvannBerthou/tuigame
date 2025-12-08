all: build/main_game

$(shell mkdir -p build)

build/main_game: machines_builder src/main.c
	gcc -Wall -Wextra -Warray-bounds -Wno-override-init-side-effects -Wno-initializer-overrides \
		src/main.c -o build/main_game \
		-I./include -L ./lib/linux -lraylib -lm -ggdb

machines_builder:
	python tools/machines_builder.py > assets/machines

run: build/main_game
	./build/main_game

clean:
	-rm -rf build

.PHONY: all run clean machines_builder
