# Hermes: Qt 6 + Kirigami port with native desktop portal integration

Date: 2026-11-18
Status: proposed

## Intent and why

Hermes is currently a native C11 + SDL3 file explorer with a hand-drawn UI. The
user wants it to become a first-class KDE Plasma 6 application: a Kirigami
frontend written with C++ controllers and QML pages, the C engine preserved,
and the freedesktop portal layer integrated properly so Hermes participates in
normal Plasma desktop workflows. The motivation is native look, behaviour and
interoperability on Plasma, not a different widget set for its own sake.

Hermes remains a host application. It is explicitly **not** being prepared for
Flatpak or any other sandboxed distribution in this work, and interacting with
sandboxed applications is not a goal of this round. Its existing direct access
to devices, FUSE mounts, arbitrary paths and the pkexec helper stays intact.

## Goals

- Replace the SDL3-rendered shell with a Kirigami `ApplicationWindow` using
  C++ model/controller classes and QML pages, styled by Breeze/QQC2 Desktop.
- Keep the filesystem, transfer, search, preview, device, cloud, archive, git
  and snapshot logic in C, exposed through a narrow C ABI.
- Provide native portal and D-Bus integration: Settings, FileManager1, OpenURI,
  a FileChooser *implementation* and document-portal drag targets.
- Preserve GPU acceleration: the runtime UI must render through Qt Quick's
  hardware scene graph on Plasma, never a software fallback.
- Show folder-view thumbnails for every image type Hermes can decode (PNG,
  JPEG, ICO, SVG and others), report source resolution in the preview panel
  when known, and never paint an opaque background behind transparent images.
- Keep every integration opt-in and reversible; never silently take over a
  desktop role or overwrite portal configuration.
- Keep all four CI distribution legs green and preserve the existing test
  coverage, including the headless self-tests that cannot use SDL anymore.

## Non-goals

- No Flatpak/Snap packaging, sandbox confinement or sandboxed-app support.
- No AppImage distribution of Hermes: remove the recipe, self-updater path,
  settings rows, docs, selftest cases and demo text. Showing `.AppImage` files
  as a file type in lists stays.
- No port of the custom atlas renderer, Freetype/HarfBuzz text stack or custom
  icon rasteriser to Qt drawing code.
- No GNOME-first decisions, no FileManager-equivalent for other desktops.
- No change to the Hermes identity, app id, config paths or package names.
- No rewrite of the C engine's algorithms in C++ in the same change.

## Anti-goals

- A C++/QML frontend that bypasses the C engine and duplicates its logic.
- Making Hermes the system's default FileChooser backend without an explicit
  user action; a file dialog is a desktop-wide dependency, not an app setting.
- Losing the worker-thread/isolation behaviour that keeps scans and transfers
  off the UI thread, or losing cancellation and conflict handling in `ops.c`.

## Constraints

- `main` is PR-only; each staging step below is its own branch, atomic commits
  and PR with all seven required CI checks passing.
- C engine source stays pure ASCII and warning-clean; the existing
  `-Wunused-function`, `-Wunused-parameter` and `-Wformat-truncation`
  exceptions remain until the unity build is replaced.
- KDE Plasma 6.6-6.8 is the target. Verified means builds and headless tests on
  this Fedora host; live Plasma behaviour is target-only until tested there.
- Host build dependencies are currently incomplete. Qt6/KF6/ECM packages must
  be available in each CI image before the relevant PR can be green.
- No commit, push, tag or package-identity change without explicit approval.

## Approaches

### A. Kirigami (QML + C++) frontend, C engine preserved -- chosen

Kirigami has no C API: the UI is QML, with C++ for the application, models and
controllers. The C files stay C and are exposed through `extern "C"`. Kirigami
gives native Plasma styling, Breeze integration, `Kirigami.Theme`, `GlobalDrawer`
and adaptive pages, plus KDE i18n and icon-theme handling. Cost: the largest UI
rewrite, a QML runtime dependency, CMake/KF6 deps in CI, and a new model layer.

### B. Qt Widgets (C++) frontend, same C engine

A Dolphin-like desktop application with no QML/JS. Simpler to build and test,
and arguably the more traditional Plasma desktop fit. Rejected because the
user explicitly named Kirigami as the ultimate choice.

### C. Keep SDL3/C, add portals only

Extend the existing `dbus.c` loader with FileManager1, OpenURI, FileChooser impl
and document drag targets. Preserves all 58k lines and every test, but leaves
the UI custom-rendered rather than native. Rejected for the same reason.

### Rejected: GTK4/libadwaita

Pure C, but GNOME-first and contrary to the repository's Plasma-first target.
Third-party C bindings for Qt do not cover Qt Quick/Kirigami.

## Architecture

### 1. C engine boundary

Today `build.sh` compiles only `src/main.c`, and every `src/*.c` is included
into it. Static names are shared across all files and include order encodes
dependencies. The C engine will stay as a small number of translation units
compiled by CMake into a `hermes_core` library; the existing unity-style
includes are kept inside the core where they still work, so no mass renaming
of statics is required to begin. A generated/exported `hermes_core.h` C ABI
gives the Qt side opaque handles plus init/shutdown, event subscription and
typed query/callback functions.

Approximate classification (the builder confirms each file):

- **Engine (stay C):** `fs.c`, `ops.c`, `archive.c`, `search.c`, `diskuse.c`,
  `compare.c`, `snapshots.c`, `preview.c`, `svg_preview.c`, `git.c`, `apps.c`,
  `devices.c`, `usb.c`, `iphone.c`, `mtp.c`, `devfs.c`, `plist.c`, `rclone.c`,
  `local_shares.c`, `cloud_identity.c`, `credentials.c`, `pool.c`, `term.c`,
  `dragout.c`.
- **Logic with a C++ adapter:** `settings.c`, `keys.c`, `options.c`, `ipc.c`,
  `integration.c`, `system_appearance.c`, `dbus.c`. Their behaviour survives;
  their presentation-facing entry points change.
- **Replaced by Qt/Kirigami:** `app.c`, `input.c`, `render.c`, `draw.c`,
  `font.c`, `shape.c`, `icons.c`, `system_icon_raster.c`, `theme.c`,
  `ui_tools.c`, `text_caret.c`, `text_pointer.c`, `icon_contrast.c`,
  `header_preferences.c`, `tab_preferences.c`, `desktop_config.c`,
  `desktop_palette.c`.

Worker threads in C report results through a registered C callback. The Qt side
turns those callbacks into queued signals so all model mutation happens on the
GUI thread. `main_post()` and SDL event pumping disappear from the UI path.

### 2. C++ bridge and QML frontend

`src/app/` holds `main.cpp`, the `HermesApplication` class, `QAbstractListModel`
subclasses for directory entries, tabs, transfers and search results, and
`QSortFilterProxyModel` where sorting/filtering is cheap. QML pages live under
`src/qml/` and are registered with `ECMQmlModule` / `ecm_find_qmlmodule`.

The shell uses `Kirigami.ApplicationWindow`, a `GlobalDrawer` for the sidebar,
`PageRow` for navigation and Kirigami dialogs for conflicts, settings and
progress. Breeze icons come from `KIconTheme`; `Kirigami.Theme` replaces the
custom palette and the text-atlas/renderer stack is deleted. Every current
keyboard shortcut remains rebindable through `keys.c` data and `docs/keys.md`.

**Visual parity baseline.** The current SDL UI is the visual reference: the
Kirigami port must look identical at parity, down to layout, spacing, icons,
colours and typography. Screenshots from the SDL build (`--screenshot`) and the
Qt build are compared before the old sources are deleted. Deliberate tuning of
the look is a separate, later round of work; it must not be smuggled into the
port itself.

Minimal CMake skeleton (versions from current KDE documentation):

```cmake
cmake_minimum_required(VERSION 3.20)
find_package(ECM 6.0.0 REQUIRED NO_MODULE)
find_package(Qt6 REQUIRED COMPONENTS Core Quick Gui QuickControls2 Widgets Test)
find_package(KF6 REQUIRED COMPONENTS
    Kirigami I18n CoreAddons QQC2DesktopStyle IconThemes DBusAddons)
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)
```

`build.sh` remains as a thin compatibility wrapper during the migration so
`install.sh`, CI and existing docs keep working until step 5 replaces them.

**GPU acceleration.** Kirigami is Qt Quick, whose scene graph is hardware
accelerated via OpenGL/Vulkan on Plasma; this replaces SDL's
`SDL_RenderGeometry` atlas path without losing acceleration. The app must not
force `QT_QUICK_BACKEND=software` or ship a software-rendered runtime path.
Custom visualisations such as the disk-use squarified treemap and sunburst must
be `QQuickItem` subclasses using `QSGGeometryNode` (GPU geometry), not
`QPainter`/`Canvas`, so their GPU behaviour survives the port. Headless tests
may select the offscreen/software backend explicitly; that choice stays test
only. Qt Quick also handles bidi text shaping and glyph rendering on the GPU,
so the Freetype/HarfBuzz/fontconfig stack can be removed rather than ported.

### 3. Portal and desktop integration layer

Portal backend discovery facts (from xdg-desktop-portal): a backend is a
D-Bus-activatable executable, installs a `foo.portal` file with `DBusName` and
`Interfaces`, and is selected through `xdg-desktop-portal/portals.conf`.
Hermes will install its `.portal` file but will **not** write itself as the
preferred backend by default.

| Interface | Direction | Design |
|---|---|---|
| `org.freedesktop.portal.Settings` | client | Existing accent/colour-scheme/icon-theme reader survives; move to Qt D-Bus or keep the C loader behind the core API. |
| `org.freedesktop.FileManager1` | service | `ShowItems()`, opt-in via Options > Integration, opens tabs and focuses the given files. Never claimed by installers. |
| `org.freedesktop.portal.OpenURI` | client | Use the portal for `OpenURI`/`OpenFile` when available; keep the current launcher as fallback. |
| `org.freedesktop.impl.portal.FileChooser` | backend service | D-Bus-activatable Hermes component implementing `Open`/`Save` request handles, filters, multi-select and parent windows. Available only after the user opts in and writes the portal config; no default takeover. |
| `org.freedesktop.portal.Documents` | client | Export dragged paths with `Add()` and offer `application/vnd.portal.filetransfer` alongside existing clipboard/DND targets. |
| Notifications, Gateway, Secret, ScreenCast, Camera | none | Out of scope. |

All D-Bus work stays asynchronous; a missing or unavailable portal must degrade
to the current behaviour rather than blocking startup or a file operation.

### 4. Build, tests and CI

- CMake project with `hermes_core` (C), the Qt/C++ app target, QML resources
  and a separate portal-backend executable.
- Standalone C suites (`tools/*-selftest.c`) keep compiling against the C core.
- `src/selftest.c` (~9k lines) is moved to a headless C-core test executable;
  its SDL-specific window/mouse cases are replaced with direct core calls, and
  Qt-side tests use `Qt Test` for models and QML loading.
- Portal tests run under `dbus-run-session` with a stub portal service as a
  non-root user; no real desktop config is touched.
- Add Qt6/KF6/ECM packages to the four CI images; keep all seven required
  checks. Add a check that QML loading and the C ABI header compile.

### 5. Staging into independent PRs

1. `build/port-core-abi`: CMake build, `hermes_core` library, C ABI header,
   headless test harness, no UI change.
2. `feat(portals-desktop-integration)`: headless D-Bus plumbing only --
   Settings/FileManager1/OpenURI clients, document-portal `Add()` export and a
   FileChooser backend speaking the full request/response protocol against the
   C core. UI-visible effects (focusing tabs, drag payloads, the picker window)
   are wired in steps 3-4, so no SDL UI work is written and discarded. Opt-in
   only, with D-Bus tests.
3. `feat(ui/kirigami-shell)`: Kirigami window, sidebar, tabs, directory model,
   navigation and basic operations; a build flag selects old/new UI until
   parity (see "Settled by the designer").
4. `feat(ui/kirigami-parity)`: search, previews, transfers, devices/cloud/git/
   snapshot panels, settings and keys UI.
5. `build/cmake-packaging-ci`: remove the SDL build path and all AppImage
   distribution code, update install/packaging/docs/CI/AGENTS/architecture,
   and delete the old UI sources. Full parity from step 4 is the gate for
   deleting SDL sources.

## Preview and thumbnail requirements

Today `src/preview.c` decodes images with stb_image, reads freedesktop
thumbnails and runs installed `.thumbnailer`s; `src/svg_preview.c` rasterises
SVG directly, and `src/render.c` already prints `src_w x src_h` in the panel
when dimensions are known. The port must preserve and strengthen this:

- Folder/grid view: thumbnails for all supported image formats, including ICO
  and SVG, via `QImageReader`/Qt image plugins, keeping the existing thumbnail
  cache and thumbnailer fallback for exotic formats. Requests stay
  asynchronous, latest-request-wins and at background priority.
- Preview panel: show the source resolution (W x H) whenever dimensions can be
  read, not the scaled thumbnail size; omit it rather than guess.
- Transparency: transparent PNG, ICO and SVG pixels stay transparent in both
  thumbnails and the panel. No opaque fill, backdrop rectangle or checkerboard
  is composited behind them; only the surrounding view background shows
  through.
- Regression tests cover transparent PNG, ICO and SVG thumbnails plus
  resolution reporting, asserting that no background pixel is introduced.

## AppImage removal and verification environment

AppImage reaches beyond packaging: `src/integration.c` implements the
self-updater, `src/base.h` detects `$APPIMAGE` and changes the pkexec command,
`src/options.c` hides update rows, `src/selftest.c` tests the updater, and
`packaging/appimage/build.sh`, README, install, settings, MAINTENANCE and the
build-install selftest all reference it. All of that is removed. Icon and
disk-use mappings for `.AppImage` files stay: that is browsing a file type,
not distributing Hermes as one.

Build dependencies may be installed in the existing `fedora-build` Distrobox,
not on the host. CI dependency steps for the four images are updated as part of
the work. Live Plasma behaviour remains target-only until tested in a real
session; headless runs only verify builds and tests.

## Risks

- Qt6/KF6/Kirigami/ECM availability in the Debian, Ubuntu, Fedora and Arch CI
  images is not yet verified; this can block steps 3-5.
- The C core's one-namespace/order assumptions may hide coupling that only
  appears when files are compiled separately.
- The in-app selftest suite is large and SDL-coupled; moving it could lose
  coverage if done carelessly.
- Becoming a FileChooser backend is a permanent session service when enabled;
  the opt-in gate and a clean disable path are mandatory.

## Settled by the designer (check these while reading)

- **Portal staging:** step 2 is headless D-Bus plumbing; UI-visible portal
  behaviour is wired in steps 3-4. This keeps the staging independent.
- **FileChooser presentation:** a dedicated D-Bus-activatable picker mode of
  the same binary with its own Kirigami window, not a general `--pick` CLI.
  It shares the C core, models and theme.
- **Feature flag:** a CMake option builds both UIs during steps 3-4, with the
  legacy UI as default until step 4 is complete; removed in step 5. CI builds
  both variants during the migration.
- **Debug surfaces:** `--screenshot` and `HERMES_RECORD` get Qt equivalents
  (offscreen grab and `QQuickWindow::grabWindow()`); the intro demo build stays
  as a Qt-based mode, with only its AppImage text removed.
- **Selftests:** all engine assertions survive as core-level tests; UI/window
  cases become Qt Test equivalents where they cover user-visible behaviour;
  only renderer-specific drawing assertions are dropped, listed explicitly in
  docs.
- **Portal configuration:** install `.portal` and D-Bus service files only;
  never write system-wide config. Opt-in writes a user-level config under
  `$XDG_CONFIG_HOME/xdg-desktop-portal/`, with backup and restore on disable.
- **Library baseline:** target Qt/KF6 as shipped by the four CI images
  (ECM >= 6.0, Qt 6, KF6); no separate older baseline. Verify in step 3.

## What is left to the builder

Detailed API signatures, the exact per-file keep/replace decision, QML page
structure, signal/slot contracts, portal D-Bus method signatures and error
handling, the CMake target layout, and the exact CI package names are all
implementation decisions to be settled in the plan for each staging step.

## Provenance of decisions

The user chose approach A, Kirigami with a C++ frontend layer, full portal
integration as native Plasma support, no Flatpak/sandbox target, preservation
of GPU acceleration, full feature parity before old sources are deleted,
dependency installation in the `fedora-build` Distrobox, and complete removal
of AppImage distribution. Everything else above is the author's proposed
call, not a user decision.
