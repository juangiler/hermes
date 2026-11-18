# C Core Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a headless, SDL-free `hermes_core` C library with a small public C ABI and a passing headless test suite, while the existing SDL binary keeps building and behaving identically.

**Architecture:** Hermes is currently one C translation unit: `src/main.c` includes the engine files, then `src/app.c`, which includes the UI files. The engine files already avoid `A` (the UI state) and rarely call `main_post`; the exceptions are `rclone.c` and the SDL wake event in `pool.c`. This plan removes those two dependencies, wraps the engine in a C ABI, adds a CMake target and headless tests, and leaves the SDL app untouched in behaviour. Kirigami, portals and packaging changes belong to later PRs.

**Tech Stack:** C11, gcc/clang, pthreads, CMake >= 3.20, pkg-config libs `libarchive fuse3 openssl` (no SDL, Freetype, Harfbuzz, fontconfig or vterm in the core). Standalone test pattern from `tools/appearance-selftest.c`.

**Spec:** `docs/superpowers/specs/2026-11-18-qt-kirigami-port-design.md` (phase 1 / PR 1). All later-phase requirements in that spec remain in force but are not implemented here.

## Global Constraints

- PR-only `main`: branch `build/port-core-abi`, atomic conventional commits, all seven CI checks green, branch up to date.
- Source must be pure ASCII: `src/*.c` and `src/*.h` are checked by `build.sh` with `LC_ALL=C grep -nP '[^\x00-\x7F]'`.
- Warnings fail CI. Keep `-Wunused-function`, `-Wunused-parameter`, `-Wformat-truncation` suppressed only as today.
- `./build.sh` release/debug/package/demo and every existing self-test must keep working throughout this PR.
- Headless suites run as non-root with `XDG_CONFIG_HOME=/tmp/fe-selftest-config`; cross-device tests use `/dev/shm` and CI needs `--shm-size=2g`.
- Build dependencies may be installed in the `fedora-build` Distrobox, never on the host.
- No commit, push or tag without explicit authorization; the commit steps below are gated by that rule.

## Review Focus

- **Permission-denied directory during scan** (non-root): expect `LST_ERROR` with `Listing.err` set, no hang or crash -- test in Task 2.
- **Filenames with spaces, UTF-8 bytes and a 255-byte name**: expect exact `name_len` and byte-for-byte names in the listing -- test in Task 2.
- **Empty directory and a self-referencing symlink**: expect a completed listing with `count == 0` and no infinite loop -- test in Task 2.
- **Cross-device copy via `/dev/shm`**: expect byte-identical result and `OP_DONE` -- test in Task 2.
- **Worker callback arriving after `hermes_core_destroy`**: expect no use-after-free or assertion under ASan -- test in Task 4.

---

### Task 1: Remove SDL from the job wake path

**Files:**
- Modify: `src/pool.c:20`, `src/pool.c:63-74`, `src/app.c:15782-15783`, `src/app.c:16277-16278`
- Test: `tools/pool-selftest.c` (new)

**Interfaces:**
- Consumes: `JobFn`, `jq_push`, `main_drain`, `g_wake_event` (current `src/pool.c`, `src/app.c`).
- Produces: `typedef void (*CoreWakeFn)(void *user); static void pool_set_wake(CoreWakeFn fn, void *user);` in `src/pool.c`. `main_post()` invokes the hook instead of `SDL_PushEvent`. `g_wake_event` moves to `src/app.c`; app sets the hook to a function that pushes that SDL event.

- [ ] **Step 1: Write the failing test**

Create `tools/pool-selftest.c` with the exact compile command in its first comment line:

```c
/* cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
   tools/pool-selftest.c -o /tmp/hermes-pool-test -lm -pthread */
#include "../src/base.h"
#include <assert.h>
#include "../src/pool.c"
static int wakes;
static void wake(void *user) { (void)user; wakes++; }
static int ran;
static void job(void *arg) { (void)arg; ran++; }
int main(void) {
    pool_set_wake(wake, NULL);
    pool_init();
    pool_submit(job, NULL);
    for (int i = 0; i < 1000 && !ran; i++) main_drain();
    assert(ran == 1);
    assert(wakes >= 1);
    pool_set_wake(NULL, NULL);
    return 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter tools/pool-selftest.c -o /tmp/hermes-pool-test -lm -pthread && /tmp/hermes-pool-test`
Expected: compile error, `pool_set_wake` undeclared.

- [ ] **Step 3: Implement the wake hook in `src/pool.c`**

Add `CoreWakeFn`, a `static CoreWakeFn g_wake_fn` and `static void *g_wake_user`; define `pool_set_wake(fn, user)`; change `main_wake()` to call `g_wake_fn(g_wake_user)` when set and do nothing otherwise; remove `g_wake_event` and all SDL includes/includes' dependencies from `pool.c`. Keep `main_post`, `main_drain`, `pool_init` and `pool_submit` signatures unchanged.

- [ ] **Step 4: Wire the SDL side in `src/app.c`**

Make `g_wake_event` a file-static in `app.c`; add `static void sdl_wake(void *user) { (void)user; SDL_Event ev; SDL_zero(ev); ev.type = g_wake_event; SDL_PushEvent(&ev); }`; at both current assignment sites call `g_wake_event = SDL_RegisterEvents(1); pool_set_wake(sdl_wake, NULL); pool_init();`.

- [ ] **Step 5: Run the test and the existing build**

Run the Task 1 compile command; expect PASS.
Run: `./build.sh debug && ./build/debug/hermes --selftest-ops`
Expected: build succeeds with no `warning:`, suite exits 0.

- [ ] **Step 6: Commit**

`git add src/pool.c src/app.c tools/pool-selftest.c && git commit -m "refactor(core): decouple job wake from SDL"`

---

### Task 2: Headless engine harness with stubs

**Files:**
- Create: `tools/core-selftest.c`

**Interfaces:**
- Consumes: `pool_set_wake` (Task 1), engine files `pool.c`, `fs.c`, `archive.c`, `ops.c`, `search.c`, `diskuse.c`, `compare.c`, `snapshots.c`, `preview.c`, `svg_preview.c`, `apps.c`, `devices.c`, `usb.c`, `iphone.c`, `mtp.c`, `devfs.c`, `plist.c`, `local_shares.c`, `cloud_identity.c`, `credentials.c`, `ipc.c` from `src/`.
- Produces: headless test binary covering scan, fuzzy match, hash, copy and the Review Focus cases; no new production symbols.

- [ ] **Step 1: Write the failing test**

Create `tools/core-selftest.c` with the compile command in its first comment line, including `../src/base.h`, `<assert.h>`, then the engine files in dependency order (`pool.c`, `archive.c`, `search.c`, `fs.c`, `ops.c`, ...). Before the includes define stubs with these exact signatures:

```c
static struct { bool selftest; bool screenshot; } A = {.selftest = true};
static void op_finished(void *arg) { (void)arg; }
static void search_batch_cb(void *arg) { (void)arg; }
static void content_batch_cb(void *arg) { (void)arg; }
```

Add these assertions as separate `test_*` functions called from `main`:
- `assert(fuzzy_match("README.md", 9, "rmd", 3, pos) > 0);`
- `assert(natcmp("file2", "file10") < 0);`
- XXH64 of `"abc"` using `xxh64_init`/`xxh64_update`/`xxh64_final` equals `0x44BC2CF5AD770999`.
- Create a temp directory with `a.txt`, `b b.txt`, a UTF-8-named file and a 255-byte name; `Listing L` zeroed with `L.path` set, `listing_start(&L)`, loop `main_drain()` up to 10000 times with `sched_yield()` until `atomic_load(&L.state) >= LST_READY`; assert `L.count == 4` and each expected `Entry.name_len` matches `strlen(name)`.
- An empty directory plus a self-referencing symlink: same scan flow, assert `L.count == 0` and the loop terminates.
- A mode-0000 directory scanned as non-root: assert `atomic_load(&L.state) == LST_ERROR && L.err != 0`.
- Copy `a.txt` to a path under `/dev/shm`: `char *srcs[] = {src_path}; Op *op = op_new(OP_COPY, srcs, 1, shm_dir); op_launch(op);` then loop `main_drain()` until `atomic_load(&op->phase) == OPS_DONE`; assert byte equality of source and destination. `OP_COPY` and `OPS_DONE` are enums in `src/ops.c`; `op_launch` takes no extra setup.

- [ ] **Step 2: Run it to verify it fails**

Run: `cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation tools/core-selftest.c -o /tmp/hermes-core-test -lm -ldl -pthread $(pkg-config --cflags --libs libarchive fuse3 openssl) && /tmp/hermes-core-test`
Expected: compile/link failure from missing stubs or unresolved app callbacks, then once it compiles, assertion failure on the first unimplemented expectation.

- [ ] **Step 3: Implement the harness stubs and include order**

Add the minimum stubs needed by the included engine files; keep each stub a no-op unless the test above asserts on it. If a file references an app-only symbol not listed above, add a no-op stub rather than including UI files. Copy the `mkdtemp`/`unlink` cleanup pattern from `tools/appearance-selftest.c` so no fixture survives the run.

- [ ] **Step 4: Run the test under ASan as a non-root user**

Run: `cc -std=gnu11 -g -fsanitize=address,undefined -fno-omit-frame-pointer [same flags as Step 2] tools/core-selftest.c -o /tmp/hermes-core-test-asan && /tmp/hermes-core-test-asan`
Expected: PASS with no sanitizer report. If run as root, use `su nobody -s /bin/sh -c /tmp/hermes-core-test-asan` for the permission case.

- [ ] **Step 5: Commit**

`git add tools/core-selftest.c && git commit -m "test(core): add headless engine harness"`

---

### Task 3: Replace rclone's direct UI-state access with host hooks

**Files:**
- Create: `src/core_host.h`
- Modify: `src/rclone.c` (12 `A.` sites from the recon), `src/app.c` (install the SDL host)

**Interfaces:**
- Consumes: `A` fields in `app.c`.
- Produces: `typedef struct { bool (*is_selftest)(void); bool (*is_screenshot)(void); bool (*window_hidden)(void); void (*mark_settings_dirty)(void); void (*request_redraw)(void); void (*open_tab)(const char *path, int index, bool focus); int (*current_tab)(void); } HermesCoreHost;` and `static const HermesCoreHost *g_core_host`; defaults in the header are false/no-op. Each translation unit sets `g_core_host` once at startup.

- [ ] **Step 1: Write the failing test**

In `tools/core-selftest.c`, add `test_core_host_defaults()` asserting `g_core_host->is_selftest() == false`, `g_core_host->current_tab() == 0`, and that `g_core_host->open_tab("/tmp", 0, true)` does not crash. Include `../src/core_host.h`.

- [ ] **Step 2: Run it to verify it fails**

Run the Task 2 compile command.
Expected: compile error, `core_host.h` missing.

- [ ] **Step 3: Define the host hooks in `src/core_host.h`**

Declare the struct above, static-inline default implementations, and `static const HermesCoreHost *g_core_host = &hermes_core_host_defaults;`. Keep the header C++-safe with `#ifdef __cplusplus extern "C" { #endif` around declarations only.

- [ ] **Step 4: Convert `src/rclone.c` to use `g_core_host`**

Replace each `A.selftest`, `A.screenshot`, `A.win_hidden`, `A.settings_dirty`, `A.redraw`, `A.cur` and `tab_new(...)` call with the corresponding hook. Preserve argument order and side effects exactly; no behaviour change in the SDL app.

- [ ] **Step 5: Install the SDL host in `src/app.c`**

Define a file-static `HermesCoreHost sdl_host` whose functions read/write `A` and call `tab_new`; set `g_core_host = &sdl_host;` at the start of both entry paths (the ones at `app.c:15782` and `app.c:16277`).

- [ ] **Step 6: Run both harnesses and the SDL self-test**

Run the Task 2 command and `./build.sh debug && ./build/debug/hermes --selftest-m10`.
Expected: all PASS, no `warning:`, no regression in cloud selftests.

- [ ] **Step 7: Commit**

`git add src/core_host.h src/rclone.c src/app.c tools/core-selftest.c && git commit -m "refactor(core): inject host hooks into rclone"`

---

### Task 4: Public C ABI and core implementation

**Files:**
- Create: `src/core_api.h`, `src/core_api.c`, `src/hermes_core.c`
- Test: `tools/core-selftest.c` (extend)

**Interfaces:**
- Consumes: engine files and `pool_set_wake` (Tasks 1-3).
- Produces: `HermesCore`, `HermesWakeFn`, `HermesEntry`, `HermesListing`, `hermes_core_create`, `hermes_core_destroy`, `hermes_core_drain`, `hermes_core_scan`, `hermes_listing_free` with the exact signatures below.

```c
typedef struct HermesCore HermesCore;
typedef void (*HermesWakeFn)(void *user);
typedef struct { const char *name; size_t name_len; unsigned char flags;
                 uint64_t size; int64_t mtime; } HermesEntry;
typedef struct { HermesEntry *items; size_t count; } HermesListing;
HermesCore *hermes_core_create(HermesWakeFn wake, void *user);
void hermes_core_destroy(HermesCore *core);
void hermes_core_drain(HermesCore *core);
int hermes_core_scan(HermesCore *core, const char *path, HermesListing *out);
void hermes_listing_free(HermesListing *listing);
```

`HermesEntry.name` is owned by the listing and valid until `hermes_listing_free`.

- [ ] **Step 1: Write the failing test**

In `tools/core-selftest.c`, add `test_public_abi()`:
- `HermesCore *core = hermes_core_create(wake, NULL);` with the same wake counter as Task 1.
- `HermesListing out; assert(hermes_core_scan(core, fixture_dir, &out) == 0);` and `assert(out.count == 4)`.
- Verify one entry's `name_len` and `strcmp(name, expected) == 0`.
- `hermes_listing_free(&out);`
- Submit a job through the public drain path, destroy the core while a job is queued, then call `hermes_core_drain(core)` -- the ASan build must report no use-after-free.

- [ ] **Step 2: Run it to verify it fails**

Run the Task 3 compile command.
Expected: compile error, `core_api.h` missing or functions undeclared.

- [ ] **Step 3: Define `src/core_api.h`**

Exactly the declarations above, `#pragma once`, `#include <stddef.h>` and `#include <stdint.h>`, and `extern "C"` guards.

- [ ] **Step 4: Implement `src/core_api.c`**

`struct HermesCore` holds the wake callback/user and a `bool destroyed` flag. `hermes_core_create` calls `pool_set_wake(wake, user)` and `pool_init()` once (guard with a static bool); `hermes_core_drain` calls `main_drain()` and is a no-op after destroy; `hermes_core_scan` calls `listing_start` on a zeroed `Listing`, drains until `LST_READY` or `LST_ERROR`, copies `Entry.name`, `name_len`, `flags`, `size`, `mtime` into a `HermesListing`, then `listing_release`; `hermes_listing_free` frees the `items` array. `hermes_core_destroy` sets the flag, calls `pool_set_wake(NULL, NULL)` and drains once.

- [ ] **Step 5: Add `src/hermes_core.c` unity source**

Include `src/base.h`, `src/core_host.h` and the engine files in the same order as Task 2, then `#include "core_api.c"`. This file is compiled only into the core library, never into the SDL binary.

- [ ] **Step 6: Run the ABI test in C and C++**

Run the Task 4 compile command; expect PASS.
Then compile a 6-line C++ probe that does `#include "core_api.h"`, `HermesCore *c = hermes_core_create(nullptr, nullptr); hermes_core_destroy(c);` and link it against the core objects:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -I src /tmp/cpp-probe.cpp /tmp/core-objs/*.o -o /tmp/hermes-cpp-probe -lm -ldl -pthread $(pkg-config --libs libarchive fuse3 openssl) && /tmp/hermes-cpp-probe
```

Expected: PASS, proving the header is usable from the Qt/C++ side.

- [ ] **Step 7: Commit**

`git add src/core_api.h src/core_api.c src/hermes_core.c tools/core-selftest.c && git commit -m "feat(core): add C ABI for the engine"`

---

### Task 5: CMake target, CI check and documentation

**Files:**
- Create: `CMakeLists.txt`
- Modify: `.github/workflows/ci.yml:8-16`, `docs/architecture.md`, `docs/building.md`

**Interfaces:**
- Consumes: `src/hermes_core.c`, `src/core_api.h`, `tools/core-selftest.c`.
- Produces: targets `hermes_core` (static library) and `hermes-core-selftest` (executable); the headless suite is runnable as `cmake --build build/cmake --target hermes-core-selftest`.

- [ ] **Step 1: Write the failing check**

In `tools/build-install-selftest.py` add a test that reads `CMakeLists.txt` and asserts it contains `add_library(hermes_core STATIC src/hermes_core.c)` and `add_executable(hermes-core-selftest tools/core-selftest.c)`.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 tools/build-install-selftest.py`
Expected: FAIL on the new assertion.

- [ ] **Step 3: Add the CMake project**

`cmake_minimum_required(VERSION 3.20)`, `project(hermes_core C)`, `set(CMAKE_C_STANDARD 11)`, `find_package(PkgConfig REQUIRED)`, `pkg_check_modules(CORE REQUIRED IMPORTED_TARGET libarchive fuse3 openssl)`, `add_library(hermes_core STATIC src/hermes_core.c)`, `target_link_libraries(hermes_core PUBLIC PkgConfig::CORE m dl pthread)`, `add_executable(hermes-core-selftest tools/core-selftest.c)`, `target_link_libraries(hermes-core-selftest PRIVATE hermes_core)`. Keep `build.sh` unchanged and authoritative for the SDL app.

- [ ] **Step 4: Run the check and a Linux build**

Run: `python3 tools/build-install-selftest.py`
Expected: PASS.
Run in the `fedora-build` Distrobox: `cmake -S . -B build/cmake && cmake --build build/cmake --target hermes-core-selftest && ./build/cmake/hermes-core-selftest`
Expected: configure and build with no `warning:`, test exits 0.

- [ ] **Step 5: Wire CI**

Add `cmake` to the install lists in the `build` matrix job and add a new `core-selftest` job on `archlinux:latest` with `--shm-size=2g`, running the configure/build/test commands as a non-root `tester` user. Do not add it to the seven required checks yet; leave it informational until phase 5.

- [ ] **Step 6: Update docs**

In `docs/architecture.md` add a paragraph naming `src/hermes_core.c`, `src/core_api.h`, `src/core_host.h` and the core target. In `docs/building.md` add the CMake commands and note that `build.sh` still builds the SDL app. Add a `CHANGELOG.md` Unreleased bullet under `refactor(core)`.

- [ ] **Step 7: Run the full local suite**

Run: `python3 tools/build-install-selftest.py && python3 tools/branding-selftest.py && python3 tools/legacy-purge-selftest.py && ./build.sh debug && ./build/debug/hermes --selftest-ops && git diff --check`
Expected: all PASS, no warnings, no whitespace errors.

- [ ] **Step 8: Commit**

`git add CMakeLists.txt .github/workflows/ci.yml docs/architecture.md docs/building.md CHANGELOG.md tools/build-install-selftest.py && git commit -m "build(core): add headless core library and CI check"`
