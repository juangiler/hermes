# Portals and desktop integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land headless Qt 6 D-Bus portal plumbing (Settings, OpenURI, Documents, FileManager1, FileChooser) against the C core, opt-in and reversible, with no UI work.

**Architecture:** A new `hermes-portals` C++17 static library wraps Qt6 D-Bus clients and service adaptors around narrow additions to the C ABI. A single D-Bus-activatable `hermes-portal-backend` executable owns the service lifetime and later gains the Kirigami picker in step 4. All requests are asynchronous; with no picker attached, services answer with a defined cancellation error instead of hanging.

**Tech Stack:** C11, C++17, Qt 6 D-Bus, CMake 3.20+, Qt Test, `dbus-run-session`, pkg-config.

**Spec:** `docs/superpowers/specs/2026-11-18-portals-desktop-integration-design.md` (and the overall port design it references).

## Global Constraints

- Qt KF6 baseline: ECM >= 6.0, Qt 6 as shipped by the four CI images.
- `hermes_core` stays a C ABI; portal code includes only `core_api.h` / `core_host.h`.
- `src/*.c` and `src/*.h` stay pure ASCII; compiler warnings fail CI.
- No SDL UI changes; no Kirigami/QML UI; no default portal takeover.
- Opt-in writes only `$XDG_CONFIG_HOME/xdg-desktop-portal/portals.conf`, with backup/restore.
- Headless tests only; live Plasma behaviour is target-only.
- No packages installed on the host; build/test inside `distrobox enter fedora-build`.

## Review Focus

- **Portal service absent:** clients must degrade to existing behaviour and never block startup or a file operation — test each client with no service registered.
- **Pre-existing `portals.conf`:** unrelated keys must survive and disable must restore the exact original bytes — test with a fixture containing other interfaces.
- **Malformed URIs:** `file://` with percent-encoding works, other schemes and truncated URIs are rejected without crashing — test both in the core ABI suite.
- **Request with no picker attached:** FileChooser `OpenFile`/`SaveFile` must answer immediately with portal `Response(response=1)` (Cancelled), and `FileManager1.ShowItems` with the D-Bus error `org.freedesktop.portal.Error.Cancelled`, so callers never hang — test both.
- **Real user config isolation:** tests must force `XDG_CONFIG_HOME` under `/tmp` and never touch the user's desktop config — assert the path in the config tests.

---

### Task 1: Core ABI helpers for portals

**Files:**
- Modify: `src/core_api.h`, `src/core_api.c`
- Test: `tools/core-abi-selftest.c`

**Interfaces:**
- Consumes: existing `HermesCore`, `HermesCoreHost::open_tab(path, index, focus)`.
- Produces:
  - `int hermes_core_uri_to_path(const char *uri, char *out, size_t out_len);` — 0 on success, -1 otherwise; decodes `file://` URIs including `%20` and `%2F`; rejects other schemes.
  - `int hermes_core_open_uris(HermesCore *core, const char *const *uris, size_t count, bool focus);` — number of URIs handed to `open_tab`, -1 if any URI is invalid; passes index `-1` for each.

- [ ] **Step 1: Add failing tests to `tools/core-abi-selftest.c`**

```c
static void test_uri_to_path(void) {
    char out[256];
    assert(!hermes_core_uri_to_path("file:///tmp/a%20b.txt", out, sizeof out));
    assert(!strcmp(out, "/tmp/a b.txt"));
    assert(hermes_core_uri_to_path("https://example.com", out, sizeof out) == -1);
    assert(hermes_core_uri_to_path("file://", out, sizeof out) == -1);
}
```

Add a host table whose `open_tab` records calls, install it with `hermes_core_set_host()`, and assert `hermes_core_open_uris(core, uris, 2, true) == 2` and that both paths were recorded.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build/cmake --target hermes-core-abi-selftest && ./build/cmake/hermes-core-abi-selftest`
Expected: compile error for the two undeclared functions.

- [ ] **Step 3: Implement the two functions in `src/core_api.c`**

`uri_to_path` is a pure helper with no registry access. `open_uris` validates every URI first, then calls `g_core_host->open_tab(path, -1, focus)` under the registry lock only for handle validity. Add `#include <stdbool.h>` to `core_api.h`.

- [ ] **Step 4: Run the test to verify it passes**

Run: same as Step 2.
Expected: `CORE ABI SELFTEST OK`.

- [ ] **Step 5: Commit**

```bash
git add src/core_api.h src/core_api.c tools/core-abi-selftest.c
git commit -m "feat(core): add portal-facing URI and open helpers"
```

---

### Task 2: Build targets and headless portal harness

**Files:**
- Create: `portals/CMakeLists.txt`, `portals/hermes-portal-backend.cpp`, `portals/hermes.portal`, `portals/org.freedesktop.impl.portal.desktop.hermes.service`, `tools/portal-selftest.sh`
- Modify: `CMakeLists.txt`, `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `hermes_core` target; Qt6DBus from `pkg-config`.
- Produces: target `hermes-portals` (static library, later tasks add sources), target `hermes-portal-backend` (executable, links `hermes-portals` + `hermes_core`), target `hermes-portal-selftest` (Qt Test), script `tools/portal-selftest.sh` that runs it via `dbus-run-session`.

- [ ] **Step 1: Write a failing smoke test in `tools/portal-selftest.sh`**

The script asserts `dbus-run-session -- true` succeeds and that `hermes-portal-backend --help` exits 0.

- [ ] **Step 2: Run it to verify it fails**

Run: `sh tools/portal-selftest.sh`
Expected: fails because the targets do not exist.

- [ ] **Step 3: Add the CMake structure and minimal backend**

`find_package(Qt6 REQUIRED COMPONENTS Core DBus Test)`; `add_subdirectory(portals)`; in `portals/CMakeLists.txt` declare `hermes-portals` with `target_link_libraries(... PUBLIC Qt6::Core Qt6::DBus PRIVATE hermes_core)` and the two executables. `hermes-portal-backend.cpp` starts a `QCoreApplication`, parses `--enable-filechooser`, `--disable-filechooser`, `--status`, `--help` into a stub that prints the flag and exits. `hermes.portal` declares the backend's `DBusName` and `Interfaces`; the service file is D-Bus-activatable. Install them to `${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/` and `${CMAKE_INSTALL_DATADIR}/dbus-1/services/` respectively — files only, no system-wide config. Add an informational `portal-selftest` CI job on `archlinux:latest` installing `qt6-base dbus` plus the core deps, running the script as a non-root user.

- [ ] **Step 4: Run the smoke test to verify it passes**

Run: `cmake -S . -B build/cmake && cmake --build build/cmake --target hermes-portal-backend && sh tools/portal-selftest.sh`
Expected: script exits 0 with `--help` output.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt portals/ tools/portal-selftest.sh .github/workflows/ci.yml
git commit -m "build(portals): add Qt D-Bus targets and headless harness"
```

---

### Task 3: Portal clients

**Files:**
- Create: `portals/portal_common.h`, `portals/settings_client.h/.cpp`, `portals/open_uri_client.h/.cpp`, `portals/documents_client.h/.cpp`
- Test: `tools/portal-selftest.cpp` (add cases)
- Modify: `portals/CMakeLists.txt`

**Interfaces:**
- Consumes: Qt6::DBus, `hermes_core_uri_to_path`.
- Produces:
  - `struct PortalReply { enum class Status { Ok, Unavailable, Cancelled, Error } status; QString errorName; QVariantMap results; };`
  - `class SettingsClient : public QObject` with `void readOne(const QString &ns, const QString &key);` and signal `void settingRead(const QString &ns, const QString &key, const QVariant &value);` plus `void failed(const PortalReply &reply);`.
  - `class OpenUriClient : public QObject` with `void openFile(const QString &path, const QString &parentWindow);` and `void finished(const PortalReply &reply);`.
  - `class DocumentsClient : public QObject` with `void add(const QString &path, bool writable);` and `void added(const QString &id, const PortalReply &reply);`.

- [ ] **Step 1: Write failing Qt Test cases with stub services**

Register stub `org.freedesktop.portal.Desktop` objects on a private bus in the test process. Assert: `SettingsClient::readOne("org.freedesktop.appearance", "accent-color")` emits the stub's value; `OpenUriClient::openFile` receives `Response(0)`; `DocumentsClient::add` receives the stub's document ID. Add a case with no stub registered asserting `Status::Unavailable` within 2 seconds.

- [ ] **Step 2: Run the suite to verify the new cases fail**

Run: `cmake --build build/cmake --target hermes-portal-selftest && sh tools/portal-selftest.sh`
Expected: link/compile failure for the new classes.

- [ ] **Step 3: Implement the three clients**

Use `QDBusInterface`/`QDBusPendingCallWatcher` on `org.freedesktop.portal.Desktop`; call `Settings.ReadOne`, `OpenURI.OpenURI`, `Documents.Add`. Map `QDBusError` to `PortalReply::Unavailable` when the service/interface is absent and to `Error` otherwise. Never call `waitForFinished()`. (Corrected by ruling R9: `OpenURI.OpenFile` takes an fd, not a URI string.)

- [ ] **Step 4: Run the suite to verify all cases pass**

Run: same as Step 2.
Expected: all cases pass within the timeouts.

- [ ] **Step 5: Commit**

```bash
git add portals/ tools/portal-selftest.cpp
git commit -m "feat(portals): add Settings, OpenURI and Documents clients"
```

---

### Task 4: Portal services

**Files:**
- Create: `portals/file_manager1_service.h/.cpp`, `portals/file_chooser_backend.h/.cpp`
- Test: `tools/portal-selftest.cpp` (add cases)
- Modify: `portals/CMakeLists.txt`, `portals/hermes-portal-backend.cpp`

**Interfaces:**
- Consumes: `hermes_core_open_uris`, `hermes_core_scan`, `PortalReply` from Task 3.
- Produces:
  - `class FileManager1Service : public QObject` with `bool registerService(QDBusConnection bus, QString *error);` and signal `void itemsRequested(const QStringList &uris);`. `ShowItems(as uris, s startupId)` converts each URI with `hermes_core_uri_to_path` and calls `hermes_core_open_uris`; with no UI attached it returns D-Bus error `org.freedesktop.portal.Error.Cancelled`.
  - `class FileChooserBackend : public QObject` with `bool registerService(QDBusConnection bus, QString *error);`. Implements `org.freedesktop.impl.portal.FileChooser.OpenFile` and `SaveFile` with `(IN o handle, IN s app_id, IN s parent_window, IN s title, IN a{sv} options) -> (OUT u response, OUT a{sv} results)`, returning `response = 1` (Cancelled) and an empty results dict when no picker is attached. Supports filter, multiple and current_folder options in per-handle state. (Corrected by rulings R10 and R11: the impl methods return the response directly, and `1` means Cancelled while `2` means Other.)

- [ ] **Step 1: Write failing protocol tests**

Register a caller stub on the private bus. Assert `ShowItems` with two `file://` URIs records both paths through a test host hook; assert that with no UI attached `ShowItems` returns the Cancelled error; assert `OpenFile` and `SaveFile` each return `response = 1` immediately and do not block the caller for more than 2 seconds.

- [ ] **Step 2: Run the suite to verify the new cases fail**

Run: `cmake --build build/cmake --target hermes-portal-selftest && sh tools/portal-selftest.sh`
Expected: compile failure for the two new classes.

- [ ] **Step 3: Implement the two services**

Use `QDBusConnection::registerObject` with an adaptor generated from introspection XML kept in `portals/dbus-interfaces/`. Keep request state in a per-handle map in `FileChooserBackend`; the step-4 picker attaches by registering a callback on the same object.

- [ ] **Step 4: Run the suite to verify all cases pass**

Run: same as Step 2.
Expected: all cases pass; no caller waits longer than 2 seconds.

- [ ] **Step 5: Commit**

```bash
git add portals/ tools/portal-selftest.cpp
git commit -m "feat(portals): add FileManager1 and FileChooser services"
```

---

### Task 5: Opt-in configuration CLI

**Files:**
- Create: `portals/portal_config.h/.cpp`
- Test: `tools/portal-selftest.cpp` (add cases)
- Modify: `portals/CMakeLists.txt`, `portals/hermes-portal-backend.cpp`

**Interfaces:**
- Consumes: `QStandardPaths`, `QSettings` with IniFormat.
- Produces:
  - `class PortalConfig` with static `bool enableFileChooser(QString *error);`, `bool disableFileChooser(QString *error);`, `QString status();`.
  - Writes only `org.freedesktop.impl.portal.FileChooser=hermes` under `[preferred]` in `$XDG_CONFIG_HOME/xdg-desktop-portal/portals.conf`; keeps unrelated lines byte-identical; backs up to `portals.conf.hermes-backup` on first enable and restores/removes it on disable.

- [ ] **Step 1: Write failing config tests**

With `XDG_CONFIG_HOME` forced to a temp dir: assert enable creates the key and backup; assert a fixture with `[preferred]\ndefault=gtk\n` keeps `default=gtk`; assert disable restores the exact original bytes and removes the backup; assert no file is written under the real home.

- [ ] **Step 2: Run the suite to verify the new cases fail**

Run: `cmake --build build/cmake --target hermes-portal-selftest && sh tools/portal-selftest.sh`
Expected: compile failure for `PortalConfig`.

- [ ] **Step 3: Implement `PortalConfig` and wire the CLI flags**

Each backend CLI flag calls the matching static method and prints a one-line result; `--status` prints enabled/disabled per role. Never write outside the forced XDG dir.

- [ ] **Step 4: Run the suite to verify all cases pass**

Run: same as Step 2.
Expected: all cases pass; the real home config is unchanged.

- [ ] **Step 5: Commit**

```bash
git add portals/ tools/portal-selftest.cpp
git commit -m "feat(portals): add opt-in portal configuration CLI"
```

---

### Task 6: Documentation and CI completion

**Files:**
- Modify: `docs/integration.md`, `docs/architecture.md`, `docs/building.md`, `CHANGELOG.md`, `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: the CLI names and test commands from Tasks 2-5.

- [ ] **Step 1: Write a failing doc check**

Extend `tools/build-install-selftest.py` to assert the docs mention `--enable-filechooser`, `portals.conf.hermes-backup` and `hermes-portal-selftest`.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 tools/build-install-selftest.py`
Expected: assertions fail.

- [ ] **Step 3: Update the docs**

Replace the "does not implement FileManager1 / file-chooser portal / documents drag target" paragraph in `docs/integration.md` with the opt-in CLI table; note that the picker UI arrives in a later step. Update `docs/architecture.md` with the `portals/` ownership, `docs/building.md` with the portal test command and CI package names, and add a `CHANGELOG.md` Unreleased bullet.

- [ ] **Step 4: Run the full verification set**

Run inside `distrobox enter fedora-build`: the Python suites, a fresh CMake build of all targets, `hermes-portal-selftest`, `hermes-core-abi-selftest`, `./build.sh debug`, `--selftest-ops`, `src/` ASCII check and `git diff --check`.

Expected: all green, no `warning:` lines.

- [ ] **Step 5: Commit**

```bash
git add docs/ CHANGELOG.md tools/build-install-selftest.py .github/workflows/ci.yml
git commit -m "docs(portals): document opt-in roles and headless tests"
```
