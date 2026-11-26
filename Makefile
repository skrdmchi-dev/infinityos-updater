CC ?= gcc
CFLAGS ?= -O3 -std=c11
LDFLAGS ?= -lm

all: build/infinity-updater build/infinity-helper build/infinity-pkg build/infinity-gui

build:
	mkdir -p build

build/infinity-updater: c-src/updater.c c-core/generated.c | build
	$(CC) $(CFLAGS) c-src/updater.c $(LDFLAGS) -o $@

build/infinity-helper: c-src/helper.c c-core/generated.c | build
	$(CC) $(CFLAGS) c-src/helper.c $(LDFLAGS) -o $@

build/infinity-pkg: c-src/pkg.c c-core/generated.c | build
	$(CC) $(CFLAGS) c-src/pkg.c $(LDFLAGS) -o $@

build/infinity-gui: c-src/gui.c c-core/generated.c | build
	$(CC) $(CFLAGS) c-src/gui.c $(LDFLAGS) -o $@

gui-gtk: c-src/gui.c c-core/generated.c | build
	$(CC) $(CFLAGS) -DUSE_GTK c-src/gui.c $$(pkg-config --cflags --libs gtk4 libadwaita-1) $(LDFLAGS) -o build/infinity-gui

test: c-src/test_core.c c-core/generated.c | build
	$(CC) $(CFLAGS) c-src/test_core.c $(LDFLAGS) -o build/test_core
	./build/test_core

regen:
	steel compile steel-core/main.steel c --bundle --out c-core/generated.c --force

clean:
	rm -rf build

.PHONY: all gui-gtk test regen clean
