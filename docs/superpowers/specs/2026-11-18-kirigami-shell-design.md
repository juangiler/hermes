# Kirigami shell (step 3 of the Qt/Kirigami port)

Date: 2026-11-18
Status: proposed

## Intent and why

This is PR 3 of the five-step port described in
`2026-11-18-qt-kirigami-port-design.md`. PRs 1-2 landed the C ABI and the
headless portal plumbing. This PR makes Hermes actually run as a Kirigami
application for the first time: a native Plasma window with a sidebar, tabs,
a directory view, navigation and basic file operations. The legacy SDL UI stays
usable as the default until feature parity is reached in PR 4; both are removed
from the build in PR 5.

## Goals

- A new `hermes-qt` executable built with Qt 6 Quick, Kirigami and KF6, using
  C++ controllers and QML pages rather than any SDL rendering code.
- `Kirigami.ApplicationWindow` with a `GlobalDrawer` for places, a tab strip,
  and a directory page backed by a `QAbstractListModel` over the C ABI.
- Navigation: up/back/forward, path entry, opening folders, and opening files
  through the existing engine/portal path.
- Basic operations: create folder, rename, remove/trash, copy and move,
  implemented in the C engine and exposed through minimal C ABI calls; the
  frontend only renders and invokes them.
- A build/install option that keeps the legacy UI as the default during steps
  3-4 while still building the Kirigami UI in CI.
- Headless verification: Qt Test model/navigation cases plus a QML load test on
  the offscreen platform, with all existing C/portal suites still green.

## Non-goals

- No search, previews, transfers, devices/cloud/git/snapshot panels, settings
  or keybinding editor: those belong to PR 4.
- No FileChooser picker window or portal-visible tab focus/drag payload wiring:
  the portal backend from PR 2 stays headless in this PR.
- No SDL UI changes, no deletion of SDL sources, no AppImage removal: PR 5.
- No packaging/default-role changes and no desktop-wide configuration writes.

## Anti-goals

- No frontend reimplementation of engine operations with KIO/QFile APIs.
- No software renderer or `QPainter`/`Canvas` in the runtime UI; offscreen
  software backends are only for headless tests.
- No second event loop: the Qt frontend uses the same Qt 6 event loop as the
  portal library from PR 2.

## Decisions made here

- **Two binaries during migration.** One CMake tree builds `hermes-qt`
  (Kirigami) and keeps the legacy SDL executable; a CMake option selects which
  one is installed as `hermes`. CI builds both variants. PR 5 removes the
  option and the legacy target entirely.
- **Directory model.** A C++ `QAbstractListModel` wraps `hermes_core_scan()`;
  the existing core wake callback triggers a model reset. QML binds to typed
  roles and never scans the filesystem itself.
- **Operations.** Extend `core_api.h` with the smallest set of operation
  functions needed for mkdir/rename/remove/copy/move, delegating to the
  existing engine code so cancellation and conflict handling stay in C.
- **Tabs and places** are C++-owned controller state exposed to QML; tab focus
  hooks from `core_host.h` are wired to the controller so PR 2's portal clients
  can target a tab later without a redesign.
- **GPU path** is the Qt Quick default hardware scene graph. No QML `Canvas`,
  `QPainter`, or forced software backend in the shipped application; headless
  tests may use `-platform offscreen`.
- **Tests.** New Qt Test cases cover the model and navigation/operation
  controller using temporary directories. A QML load test constructs the main
  component offscreen. Existing core, portal and SDL self-tests remain in CI.

## Left to the builder (settled in the plan)

- Exact CMake target names, flags, install rules and the legacy/default-UI
  switch, including how the unity-built SDL source is compiled or reused.
- Concrete C ABI signatures and host callbacks for operations and progress.
- QML page/component structure, roles, signals and slot contracts.
- Qt/KF6/ECM package names for the four CI images and the CI job layout.
- Test fixture layout and which existing self-test cases are duplicated by Qt
  Test versus kept at the C level.
- Verification that no required dependency is installed without approval.
