# Building and testing Hermes

[README](../README.md) · [Install](install.md) · [Distrobox](immutable-distros.md) · [Architecture](architecture.md)

Hermes is a C11/SDL3 Linux application with a unity build, plus a Qt 6/Kirigami
shell under development. `build.sh` compiles only `src/main.c` for the legacy
SDL app; other C files are included into that translation unit. CMake builds
the headless core library and, during migration, both UI executables.
`build.sh` remains authoritative for the legacy application.

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

### Qt portal plumbing

Qt 6 (`Core`, `DBus`, `Test`) builds the headless portal library and its test
through the same CMake project, separately from the `build.sh` application:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake     # all targets, including the portal backend and suite
sh tools/portal-selftest.sh   # private dbus-run-session, Qt Test portal suite
./build/cmake/hermes-portal-backend --help
./build/cmake/hermes-core-abi-selftest
./build/cmake/hermes-core-selftest
./build/cmake/hermes-cpp-probe
```

`tools/portal-selftest.sh` builds `hermes-portal-backend` and
`hermes-portal-selftest` itself and runs them under `dbus-run-session`, so a
local run and CI use the same entry point. The suite needs a working session
bus; it does not open a picker window. CI's informational `portal-selftest`
job runs on `archlinux:latest` with `qt6-base` and `dbus` (plus the core build
dependencies) as a non-root user and configures with
`-DHERMES_BUILD_QT_UI=OFF` to keep that smaller package set. The four `build`
matrix legs install the Qt/Kirigami packages below and cover both UIs. These
headless runs prove D-Bus plumbing only, not live KDE Plasma portal behaviour.

### Qt/Kirigami shell (migration)

The `hermes-qt` executable is built by CMake from Qt 6 Quick, Kirigami and KF6;
it links the SDL-free `hermes_core` archive. `hermes-legacy` is a separate
unity compilation of `src/main.c` and does not link that archive. Install the
same packages the four CI build legs use:

```sh
# Arch (archlinux:latest)
pacman -Syu --noconfirm --needed base-devel git pkgconf cmake sdl3 freetype2 harfbuzz fontconfig libarchive libvterm fuse3 openssl qt6-base qt6-declarative kirigami extra-cmake-modules qqc2-desktop-style
# Fedora (fedora:latest)
dnf install -y gcc git pkgconf-pkg-config cmake SDL3-devel freetype-devel harfbuzz-devel fontconfig-devel libarchive-devel libvterm-devel fuse3-devel openssl-devel qt6-qtbase-devel qt6-qtdeclarative-devel kf6-kirigami-devel extra-cmake-modules kf6-qqc2-desktop-style
# Debian trixie / Ubuntu 26.04
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential git pkg-config cmake libsdl3-dev libfreetype-dev libharfbuzz-dev libfontconfig-dev libarchive-dev libvterm-dev libfuse3-dev libssl-dev qt6-base-dev qt6-declarative-dev libkirigami-dev extra-cmake-modules qml6-module-org-kde-kirigami qml6-module-org-kde-desktop
```

Build both UIs and run the Qt Test suite the way CI does:

```sh
cmake -S . -B build/cmake-ui -DHERMES_BUILD_QT_UI=ON -DHERMES_DEFAULT_UI=legacy
cmake --build build/cmake-ui --target hermes-legacy hermes-qt hermes-qt-selftest 2>&1 | tee cmake-ui-build.log
if grep -q 'warning:' cmake-ui-build.log; then echo "::error::Qt UI compiler warnings"; exit 1; fi
QT_QPA_PLATFORM=offscreen ./build/cmake-ui/hermes-qt-selftest
cmake -S . -B build/cmake-ui-qt -DHERMES_BUILD_QT_UI=ON -DHERMES_DEFAULT_UI=kirigami
cmake --build build/cmake-ui-qt --target hermes-qt
```

`HERMES_DEFAULT_UI` is a temporary migration install flag: `legacy` (the
default) installs the SDL binary as `hermes`, `kirigami` installs `hermes-qt`
as `hermes`. With `HERMES_BUILD_QT_UI=ON`, both executables are installed under
their own names too; the flag and the legacy target are removed in PR 5.
Core-only and portal-only CMake builds pass `-DHERMES_BUILD_QT_UI=OFF` and keep
their smaller package sets.

The Qt Test and QML load cases run with `QT_QPA_PLATFORM=offscreen`; they check
build and headless plumbing only and do **not** prove live KDE Plasma behaviour
(the target is Plasma 6.6-6.8). Runtime Qt Quick uses the hardware scene graph;
offscreen/software backends are for tests only. Qt equivalents of the legacy
`--screenshot` and `HERMES_RECORD` diagnostics are deferred to PR 4; those two
options still apply to `./build/*/hermes` only.

Known limitation: directory scans are synchronous in this shell.
`hermes_core_scan_sorted()` polls for up to 10,000 one-millisecond intervals,
and `DirectoryModel::applyScan()` calls it on the GUI thread. A large or slow
directory can therefore block the Qt event loop until the scan returns.
Asynchronous scanning is planned for PR 4.

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
