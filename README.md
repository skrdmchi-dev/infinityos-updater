# InfinityOS Updater

Component updater for InfinityOS with core logic written in **Steel** (`.steel`) and compiled to Rust, paired with a thin Rust shell for Ed25519 signing, `.tar.xz` archives, resumable HTTPS downloads, `pkexec`, and GTK4 / Libadwaita.

## Architecture

- **`steel-core/`** (13 Steel modules, ~78% of hand-written code):
  - `semver.steel`: SemVer 2.0 parser, prerelease comparison, and constraint matching (`>=`, `<=`, `^`, `~`, `=`)
  - `policy.steel`: Install path rules, archive zip-slip validation, SHA-256/Ed25519 format checks, privilege gates
  - `model.steel`: Catalogue schema validation, channel filtering (`stable`, `beta`, `nightly`), and search
  - `solver.steel`: DFS topological dependency resolver, cycle detection, and reverse-dependency lookup
  - `reconciler.steel`: Install/upgrade/downgrade/remove planner and full-system release snapshot transitions
  - `db.steel`: JSON state store, transaction journal, rollback engine, and configuration validation
  - `pkg.steel`: Package manifest builder, catalogue publisher, and helper request policy
  - `helper.steel`: Privileged `pkexec` helper logic with `dpkg -S` checks and on-disk transactional backup/restore
  - `cli.steel` & `runner.steel`: CLI flag parser, command dispatcher, output formatters, and atomic state saves
  - `ui_state.steel`: GUI view-model and controller state reducer
  - `tests.steel` & `main.steel`: Self-test suite and bundled FFI entrypoints
- **`crates/`** (6 thin Rust crates):
  - `infinity-steel-core`: Pre-compiled Steel core (`src/generated.rs`) + minimal serde FFI bridge
  - `infinity-runtime`: Ed25519 (`ed25519-dalek`), `.tar.xz` (`tar` + `xz2`), and resumable HTTPS (`reqwest`)
  - `infinity-updater`: CLI binary
  - `infinity-helper`: Privileged `pkexec` root helper binary
  - `infinity-pkg`: Component packager and catalogue publisher CLI
  - `infinity-gui`: GTK4 + Libadwaita desktop shell (`--features gtk`)

## Build & Test

`crates/infinity-steel-core/src/generated.rs` is checked in, so you can build directly with Cargo:

```bash
cargo test --release --workspace
cargo build --release --workspace
cargo build --release -p infinity-gui --features gtk
```

If you edit any `.steel` file in `steel-core/`, recompile it into Rust with:

```bash
steel compile steel-core/main.steel rust --bundle --out crates/infinity-steel-core/src/generated.rs --force
```
