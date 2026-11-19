# Architecture

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

- `src/fs.c` – directory scan on worker threads: `getdents64` for names/types, then parallel `statx`
  for size/date with on-screen rows prioritized. Sorting runs off the UI thread.
- `src/draw.c`, `src/font.c` – all shapes and glyphs come from one atlas texture, so a frame is a few
  `SDL_RenderGeometry` calls. The app sleeps when idle (no redraw without events).
- `src/shape.c` – text in right-to-left scripts (Persian, Arabic, Hebrew) or scripts that need shaping
  (Indic, combining marks) is reordered (Unicode bidi) and shaped with HarfBuzz; other text skips both.
- `src/ops.c` – copy/move/trash/delete/restore on dedicated threads: `copy_file_range` (read/write
  fallback), cross-device moves, atomic replace via temp file, conflicts found up front and asked about together,
  cancel removes partial files. Transfers wait for their turn after counting (the UI schedules them in queue order),
  pause between chunks, and can verify each copy (flushed, dropped from the cache, read back, XXH64 compared) before it
  replaces anything.
- `src/search.c` – fuzzy scoring, background folder + file index for Ctrl+P, parallel recursive search
  (stays on one filesystem), search inside files (ripgrep when installed - it also skips what `.gitignore` excludes -
  otherwise a built-in parallel search that skips `.git`, binary files and files over 50 MB; at most 100 lines per file
  and 20,000 in all), recursive folder sizes (per-folder cache checked by mtime, so unchanged subtrees cost one statx per folder;
  hidden folders only while shown; in network folders one gentle thread: `du` on the server over the shared ssh
  connection for SFTP, walking the mount for the rest), inotify thread.
  Background work runs at idle CPU/IO priority.
- Folders refresh automatically when their contents change (inotify, debounced and throttled by scan cost);
  refreshes load in the background and swap in, so the list never flickers.
- `src/preview.c` – preview worker (latest request wins): stb_image decode + downscale, thumbnail lookup
  (own cache, `~/.cache/thumbnails`), installed `.thumbnailer`s with an 8 s timeout, text and folder summaries.
- `src/diskuse.c` – disk usage map: measures a folder tree on 4 threads into a tree of folders and their biggest files
  (the rest summed), staying on one drive (btrfs subvolumes of the same disk included); the view in `app.c` / `render.c`
  lays it out as a squarified treemap or a sunburst every frame.
- `src/compare.c` – folder compare: walks both trees together on a worker thread and records only what differs, by path
  relative to the two roots; syncs run as one operation copying each item to its own place (`OP_SYNC` in `ops.c`).
- `src/snapshots.c` – previous versions: finds the filesystem a path is on and looks for it in Snapper / Timeshift / ZFS
  snapshots on a worker thread (or as root through the pkexec helper).
- `src/git.c` – finds a folder's repository, runs `git status --porcelain=v2` for the repository on its own threads (on the server over ssh in SFTP folders) and parses it into per-path states, plus remotes, last commit and stashes; reads branch + remote of the subfolders that are repositories.
- `src/app.c` – layout, input, virtualized list, navigation, tabs, dialogs, menus, progress cards, transfer queue, undo.
  `src/input.c` (keyboard/mouse), `src/render.c` (drawing), `src/settings.c` (config files) and
  `src/selftest.c` (self-tests) are included from it.
- `src/identity.h` – the single source of the Hermes identity (`HERMES_NAME`, `HERMES_SLUG`,
  `HERMES_APP_ID`) and `identity_env()`, a `getenv()` wrapper. Runtime overrides use `HERMES_*`
  names only. Included from `base.h`, so every file sees it.
- `src/cloud_identity.c` – cloud unit naming (`hermes-cloud-<escaped>.service`) and file-provenance
  ownership (`CLOUD_HERMES` / `CLOUD_EXTERNAL`); units with a legacy header are external.
- `src/credentials.c` – Hermes-only keyring namespace with a private deletion-intent directory;
  no legacy fallback.

- `src/theme.c` owns the effective palette and scrollbar colors; `src/tab_preferences.c` shares tab settings and
  geometry across rendering and input; `src/header_preferences.c` owns the label and control-visibility settings. `src/desktop_config.c` / `src/desktop_palette.c` read desktop colors without
  GTK/Qt build dependencies. `src/system_appearance.c` monitors the optional Settings portal off the UI thread and
  posts updates to the existing main queue; the optional libdbus loader it uses lives in `src/dbus.c`.
- `src/system_icons.c` resolves installed freedesktop icon themes and inheritance; `src/system_icon_raster.c` converts
  SVG/PNG icons into the existing atlas/texture caches in `icons.c`. Missing icons keep bundled fallbacks.

### Headless core library

- `src/hermes_core.c` is the unity source for the SDL-free core: it includes
  most of the same engine files as `src/main.c` (scan, ops, search, previews,
  devices, cloud identity and so on) but none of the UI, SDL or text-rendering
  files. Cloud *remote management* (`src/rclone.c`) is still SDL-only in this
  PR: it needs UI callbacks that have not been ported to the core yet. CMake
  builds the core as the static `hermes_core` library; `build.sh` continues to
  build the SDL app.
- `src/core_api.h` is the small C ABI (`hermes_core_create`, `hermes_core_scan`,
  `hermes_core_drain`, `hermes_core_destroy`, `hermes_listing_free`,
  `hermes_core_set_host`) that a non-SDL frontend links against. Scans are
  exposed both as a synchronous wrapper (`hermes_core_scan_sorted()`, for
  unchanged callers) and as an async handle: `hermes_core_scan_start()` takes
  `HermesScanOptions` (sort key, direction, hidden entries, folder sizes) and
  returns a `HermesScan` that the frontend polls with `hermes_core_scan_poll()`
  and releases with `hermes_core_scan_cancel()` / `hermes_core_scan_free()`;
  `hermes_core_scan_start_folder_sizes()` starts the engine's recursive size
  pass on a completed scan. The eight `HermesSortKey` values map to the legacy
  sort fields, and `HERMES_ENTRY_SIZE_UNKNOWN` marks a folder whose recursive
  size is not known yet. `src/core_host.h` carries the host callbacks the engine
  uses for UI-side decisions, with inert defaults for headless use. A C++
  frontend installs its table through `hermes_core_set_host()`; the SDL app
  assigns its own table directly at startup.
- `tools/core-selftest.c` exercises the engine and the public ABI without SDL
  or a display server, including permission-denied scans, unusual filenames
  and cross-device copies. It is built by the `hermes-core-selftest` target.
  `tools/core-abi-selftest.c` is the ABI-level suite: it includes no engine
  sources and links the `hermes_core` archive itself. `tools/core-api-probe.cpp`
  is a C++17 consumer that links the same archive, built by the
  `hermes-cpp-probe` target.

### Qt portal plumbing

- `portals/` owns the Qt 6 / D-Bus desktop integration. It is C++ behind the
  narrow `src/core_api.h` and `src/core_host.h` C ABI; the C engine has no Qt
  dependency.
- `portals/settings_client.cpp`, `portals/open_uri_client.cpp` and
  `portals/documents_client.cpp` are async clients for Settings `ReadOne`,
  OpenURI `OpenURI` (outgoing `file://` URIs) and Documents `Add`.
- `portals/file_manager1_service.cpp` and `portals/file_chooser_backend.cpp`
  own the headless D-Bus services. Their `QDBusAbstractAdaptor` classes are
  hand-written because the build images have no `qdbusxml2cpp`; the method
  signatures are documented as XML contracts under `portals/dbus-interfaces/`.
- `portals/portal_config.cpp` implements the opt-in role CLI: it edits only
  `$XDG_CONFIG_HOME/xdg-desktop-portal/portals.conf`, with byte-for-byte
  backup/restore, and never writes at install time.
- `portals/hermes-portal-backend.cpp` is the D-Bus-activatable host: with no
  flags it owns the services for the session; the config flags edit the user's
  preference and exit. `portals/hermes.portal` and
  `portals/org.freedesktop.impl.portal.desktop.hermes.service` declare the
  backend and its FileChooser interface as data files, not system-wide config.
- `tools/portal-selftest.cpp` drives the clients, services and config through
  a private session bus, built as `hermes-portal-selftest`; see
  [Building](building.md). The QML shell lives in `qtui/`; no picker window
  exists yet.

### Qt/Kirigami shell

- `qtui/main.cpp` is the `hermes-qt` entry point: a `QGuiApplication` and
  `QQmlApplicationEngine` that loads `qrc:/qtui/qml/main.qml`, installs the
  controller as the `hermes` context property, and handles `--version`/`--help`.
- `qtui/shell_controller.*` owns migration state: places, tab list and focus,
  per-tab history, view mode (list/details/grid), the column set and zoom, plus
  clipboard, conflict policy and operation completion. It installs the
  `core_host.h` hooks so engine callbacks (including open-tab requests off the
  GUI thread) reach QML-safe slots, exposes invokable
  create/rename/remove/copy/move calls that delegate to the C ABI, and owns the
  directory watcher so an on-disk change asks the model for a refresh.
- `qtui/directory_watcher.*` watches the current directory on disk with a
  debounced `QFileSystemWatcher` and reports changes to the controller; the
  controller owns it so the model can still be used headless.
- `qtui/directory_model.*` is a `QAbstractListModel` over the asynchronous
  `hermes_core_scan_start()` / `hermes_core_scan_poll()` handle: `navigate()`
  and `refresh()` start a scan and return, and `pollScan()` applies a completed
  result on the GUI thread while the rows on screen stay until the new ones
  arrive. A generation guard drops a superseded result, and a failed scan keeps
  the previous rows and emits `navigateFailed`. It exposes typed
  name/path/size/date roles, the hidden-entry and folder-size toggles, the sort
  key and direction (including the legacy key policy), the eight sort keys and
  the ordered path list; `scanFinished()` fires once a result is applied. QML
  never scans disks.
- `qtui/places_model.*` supplies the GlobalDrawer labels and paths.
- `qtui/qml/main.qml` owns the `Kirigami.ApplicationWindow`, places drawer, tab
  strip, navigation/operation controls and dialogs; `qtui/qml/DirectoryPage.qml`
  owns the directory list, selection and single/double-click behaviour. Both
  bind to C++ models/controller only and use the hardware scene graph, with no
  `Canvas` or `QPainter`.
- `tools/qtui-selftest.cpp` is the Qt Test suite for the model, controller and
  offscreen QML load, built as `hermes-qt-selftest`. Offscreen runs prove
  plumbing, not live Plasma behaviour; see [Building](building.md).

## Repository layout

- `assets/branding/` owns Hermes's editable pixel-art helmet and generated SVG/PNG
  assets. `tools/gen_logo.py` also generates the embedded `src/logo_data.h`.
- `assets/desktop/` and `assets/udev/` hold shared installation assets.
- `packaging/debian/`, `packaging/rpm/`, `packaging/arch/` and
  `packaging/appimage/` own distribution-specific recipes.
- `build/<mode>/` holds separate debug/release/package/demo outputs;
  `build/packaging/` holds isolated native-package workspaces and `dist/`
  holds generated distributable artifacts.
- `build.sh` remains the unity-build entry point. `install.sh` consumes an
  explicitly selected mode/binary and supports `DESTDIR` staging.
- `CMakeLists.txt` builds `hermes_core`, the portal library and, when
  `HERMES_BUILD_QT_UI=ON`, the `qtui/` frontend plus the legacy SDL target.
  `HERMES_DEFAULT_UI` is a temporary install alias for `hermes`, removed with
  the SDL UI in PR 5.
- `tools/source-archive.sh` packages an explicit source allowlist, including
  working-tree edits but not local Git metadata, build products or home data.
