# InfinityOS Updater (C11 Branch)

C11 target branch of InfinityOS Updater.

- **Core logic (`steel-core/*.steel`, 1,652 lines)**: Compiled directly to C11 (`c-core/generated.c`) by the Steel compiler (`steel compile steel-core/main.steel c --bundle --out c-core/generated.c --force`).
- **Thin C11 OS/GUI adapters (`c-src/*.c`, `Makefile`)**: Rewritten from the Rust adapter layer using **Gemini (AI-assisted)**.

## Build & Test

```bash
make test
make all
# On Debian/Linux with GTK4 + Libadwaita dev packages installed:
make gui-gtk
```
