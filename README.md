# InfinityOS Updater (C++20 Branch)

C++20 target branch of InfinityOS Updater.

- **Core logic (`steel-core/*.steel`, 1,652 lines)**: Compiled directly to C++20 (`cpp-core/generated.cpp`) by the Steel compiler (`steel compile steel-core/main.steel cpp --bundle --out cpp-core/generated.cpp --force`).
- **Thin C++20 OS/GUI adapters (`cpp-src/*.cpp`, `Makefile`)**: Rewritten from the Rust adapter layer using **Gemini (AI-assisted)**.

## Build & Test

```bash
make test
make all
# On Debian/Linux with GTK4 + Libadwaita dev packages installed:
make gui-gtk
```
