# Building and testing Hermes

[README](../README.md) · [Install](install.md) · [Distrobox](immutable-distros.md) · [Architecture](architecture.md)

Hermes is a C11/SDL3 Linux application with a unity build. `build.sh` compiles
only `src/main.c`; other C files are included into that translation unit.
CMake is only used for the headless core library; ordinary application builds
do not need it.

## Dependencies

A C compiler, pkg-config, and these pkg-config modules are required:

```text
sdl3 >= 3.2
freetype2 harfbuzz fontconfig libarchive
vterm >= 0.3
fuse3
openssl >= 3
```

Native distro packages must provide these versions. Debian/Ubuntu share one
recipe; Fedora and Arch use their own packaging conventions. See
[installation](install.md) for dependency commands and the target matrix.
Do not infer support for older Ubuntu/Debian releases that lack SDL3.

## Build modes

```sh
./build.sh             # build/release/hermes, optimized for this CPU
./build.sh debug       # build/debug/hermes, ASan + UBSan
./build.sh package     # build/package/hermes, portable CPU flags
./build.sh demo        # build/demo/hermes-demo
./build/release/hermes [PATH|FILE|URI]... [--tab PATH] [--split PATH] [-n|--new-window]
```

Modes no longer overwrite one executable. `HERMES_OUT` chooses an explicit
output and `HERMES_ADDON_SRC` selects optional add-on sources. Legacy aliases
are not accepted. `CC`, `CFLAGS_PKG`, and `LDFLAGS_PKG` remain supported.
Use package mode for distribution packages; `-march=native` is release-only.

`hermes --version` prints the application and SDL versions. Files open their
parent folder with the file selected; folders open as tabs. Single-instance
behavior and saved sessions remain configurable.

## Headless core library

`build.sh` remains authoritative for the SDL application. A separate CMake
project builds the SDL-free `hermes_core` static library and its self-test:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake --target hermes-core-selftest hermes-core-abi-selftest hermes-cpp-probe
./build/cmake/hermes-core-selftest
./build/cmake/hermes-core-abi-selftest
./build/cmake/hermes-cpp-probe
```

These targets link `libarchive`, `fuse3`, `openssl`, `m`, `dl` and `pthread`;
they are used by the Qt/Kirigami port and by CI as an early parity check. The
ABI self-test and C++ probe compile only against the public headers and link
the static archive, which is what a Qt frontend will do. The cross-device copy
case needs `/dev/shm`, and the permission-denied case must run as a normal user.

## Install and package

```sh
./build.sh package
DESTDIR="$PWD/build/stage" PREFIX=/usr ./install.sh
# Inspect build/stage; this does not alter desktop defaults or host services.
sh tools/build-package.sh rpm   # inside a prepared Fedora builder
sh tools/build-package.sh deb   # inside a prepared Debian/Ubuntu builder
```

The installer defaults to `build/package/hermes`; select another mode with
`HERMES_BUILD_MODE` or a specific binary with `HERMES_BINARY`.
Generated packages/source archives belong in `dist/`, not source directories.
Package helpers retain their isolated source/build workspaces in
`build/packaging/` for diagnosis. Arch uses `makepkg` in `packaging/arch/`.
Git-based Arch builds reflect committed source; the deb/rpm helpers include the
current working source. See [maintenance](../MAINTENANCE.md).

## Runtime helpers

Optional integrations use the existing desktop tools: `xdg-utils`, thumbnailers,
`udisks2`, GVfs, rclone, a polkit authentication agent, and libdecor for
system-managed Wayland title bars. Hermes does not install or enable these on
startup. Phone/camera access uses the built-in connector where supported.

## Headless self-tests

Rebuild debug after source changes. Tests use private config/cache/state
fixtures, the SDL offscreen driver, and software rendering. Do not run suites
concurrently: several use fixed fixture paths. Run as a non-root user;
cross-device tests need sufficient `/dev/shm` space (CI uses 2 GB).

```sh
./build.sh debug
sh tools/mkfiles.sh /tmp/hermes-selftest-files 5000
./build/debug/hermes --selftest /tmp/hermes-selftest-files
for suite in ops admin git grep du cmp m4 m5 m6 m7 m8 m9 m10 m11; do
  ./build/debug/hermes --selftest-"$suite" || break
done
```

| Suite | Coverage |
| --- | --- |
| plain | Tabs, splits, selection, navigation |
| ops / admin | File operations, cancellation, privilege retry, snapshots |
| git / grep | Git metadata and content search |
| du / cmp | Disk usage and folder comparison/sync |
| m4 / m5 | Search/refresh and previews/session restore |
| m6 / m7 | Panels/tab dragging and icons/views/grouping |
| m8 / m9 | Open with/properties/archives and settings/themes/shortcuts |
| m10 | IPC, devices/network, Cloud, settings, scaling, unusual paths |
| m11 | Terminal tabs, shell integration, input and scrollback |
| devices | USB/phone/camera FUSE integration; needs FUSE and is not in ordinary CI |

Some container security profiles block LeakSanitizer's ptrace checks. A run may
then print `WARNING: ptrace appears to be blocked (is seccomp enabled?).
LeakSanitizer may hang.` followed by `Child exited with signal 32`. That is a
sandbox limitation, not a test failure: the suite's `ok` / `OK` line and exit
code are authoritative. In a restricted container, prefix the test command with
`ASAN_OPTIONS=detect_leaks=0` to skip only leak detection; ASan/UBSan stay
enabled. Report that limitation, and do not disable leak detection in normal CI.

## Standalone regression checks

These need no SDL display. Each C source starts with its exact compiler command:

- `tools/appearance-selftest.c`
- `tools/credentials-selftest.c`
- `tools/cloud-identity-selftest.c`
- `tools/system-icons-selftest.c`
- `tools/app-desktop-icon-selftest.c`
- `tools/local-shares-selftest.c`
- `tools/apps-discovery-selftest.c`
- `tools/svg-preview-selftest.c`
- `tools/icon-contrast-selftest.c`
- `tools/text-pointer-selftest.c`
- `tools/text-caret-selftest.c`

Build/install and branding tests use Python's standard library:

```sh
python3 tools/build-install-selftest.py
python3 tools/branding-selftest.py
python3 tools/legacy-purge-selftest.py
python3 tools/gen_logo.py --check
sh tools/check-release-version.sh
```

## Diagnostics and demo

- `./build/release/hermes --bench DIR [QUERY]`: listing/search benchmark.
- `./build/release/hermes --screenshot out.bmp DIR`: render one frame.
- `HERMES_RECORD=/tmp/hermes-record ./build/release/hermes`: record UI frames.
- `HERMES_DEBUG_DND=1`: drag-and-drop diagnostics.
- `HERMES_VK_TARGET=0`: compare direct Vulkan swapchain rendering with the default
  offscreen composition.
- `sh tools/demo.sh [OUT_DIR]`: record the scripted demo with a made-up home;
  needs ffmpeg, ImageMagick, zip, and user/mount namespaces. No real home is shown.

Runtime overrides use `HERMES_*` names only; legacy aliases have no effect.

## Third-party credits

Preserve the original File Explorer MIT copyright in `LICENSE`.
Bundled `stb_image` is public domain/MIT; NanoSVG is zlib licensed.
Material Icon Theme, Catppuccin icons, and Symbols retain their MIT licences;
Monokai uses recolored Symbols artwork. `tools/fetch_icons.py` and
`tools/gen_icons.py` maintain the generated file icon data.
Network logos come from Simple Icons (CC0) and Material Icon Theme (MIT);
trademarks belong to their owners. `tools/gen_net_icons.py` maintains that data.

Hermes's own pixel-art helmet has one editable source:
`assets/branding/helmet.pixels`. `tools/gen_logo.py` deterministically generates
the shared SVG, PNG sizes, and `src/logo_data.h` without external image tools.
