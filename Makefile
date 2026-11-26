CXX ?= g++
CXXFLAGS ?= -O3 -std=c++20

all: build/infinity-updater build/infinity-helper build/infinity-pkg build/infinity-gui

build:
	mkdir -p build

build/infinity-updater: cpp-src/updater.cpp cpp-core/generated.cpp | build
	$(CXX) $(CXXFLAGS) cpp-src/updater.cpp -o $@

build/infinity-helper: cpp-src/helper.cpp cpp-core/generated.cpp | build
	$(CXX) $(CXXFLAGS) cpp-src/helper.cpp -o $@

build/infinity-pkg: cpp-src/pkg.cpp cpp-core/generated.cpp | build
	$(CXX) $(CXXFLAGS) cpp-src/pkg.cpp -o $@

build/infinity-gui: cpp-src/gui.cpp cpp-core/generated.cpp | build
	$(CXX) $(CXXFLAGS) cpp-src/gui.cpp -o $@

gui-gtk: cpp-src/gui.cpp cpp-core/generated.cpp | build
	$(CXX) $(CXXFLAGS) -DUSE_GTK cpp-src/gui.cpp $$(pkg-config --cflags --libs gtk4 libadwaita-1) -o build/infinity-gui

test: cpp-src/test_core.cpp cpp-core/generated.cpp | build
	$(CXX) $(CXXFLAGS) cpp-src/test_core.cpp -o build/test_core
	./build/test_core

regen:
	steel compile steel-core/main.steel cpp --bundle --out cpp-core/generated.cpp --force

clean:
	rm -rf build

.PHONY: all gui-gtk test regen clean
