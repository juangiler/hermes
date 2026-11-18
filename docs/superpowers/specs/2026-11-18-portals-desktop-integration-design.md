# Portals and desktop integration (step 2 of the Qt/Kirigami port)

## Intent and why

This is PR 2 of the five-step port described in
`2026-11-18-qt-kirigami-port-design.md`. It lands all D-Bus plumbing against
the headless C core before any Kirigami UI exists, so no SDL UI work is written
and then discarded. Every portal role stays opt-in and reversible: installation
never takes over a desktop role.

## Goals

- A `hermes-portals` C++ library using Qt 6 D-Bus, plus a D-Bus-activatable
  backend executable for the FileChooser role.
- Portal clients: `org.freedesktop.portal.Settings` (accent, colour scheme,
  icon theme), `org.freedesktop.portal.OpenURI` (`OpenURI`/`OpenFile`), and
  `org.freedesktop.portal.Documents.Add()` for exporting dragged paths.
- A `org.freedesktop.FileManager1` service implementing `ShowItems()`.
- An `org.freedesktop.impl.portal.FileChooser` backend implementing the full
  Open/Save request-response protocol (filters, multi-select, parent window,
  current folder) against the C core's scan/listing API.
- Opt-in config: install `.portal` and D-Bus service files only; write a
  user-level `portals.conf` under `$XDG_CONFIG_HOME/xdg-desktop-portal/` on
  opt-in, with backup and restore on disable.
- Headless tests under `dbus-run-session` with a stub portal service, as a
  non-root user, touching no real desktop config.

## Non-goals

- No Kirigami/QML UI, no picker window, no tab focusing or drag payloads: those
  are steps 3-4.
- No SDL UI changes; the legacy binary stays behaviourally unchanged.
- No system-wide portal config, no default backend selection by installers.
- Notifications, Gateway, Secret, ScreenCast, Camera are out of scope.

## Anti-goals

- A missing portal must never block startup or a file operation.
- No synchronous D-Bus calls on the UI path; all work is asynchronous.

## Constraints

- `hermes_core` remains a C ABI. Portal code consumes it via narrow additions
  (path resolution and open/focus requests routed through host hooks), never by
  including engine internals.
- Qt/KF6 as shipped by the four CI images, ECM >= 6.0.
- `src/*.c` and `src/*.h` stay pure ASCII; warnings fail CI.
- Headless runs verify builds and tests only; live Plasma behaviour is
  target-only until tested in a real session.

## Approaches

1. **Qt6 D-Bus (chosen).** Native asynchronous D-Bus on the same event loop the
   Kirigami frontend will use, with type-safe adaptors for the portal
   interfaces. Small C++ library plus activatable backend executable. Fits the
   target architecture and avoids a second event loop.
2. **GDBus in C.** Reuses GLib, already present, but introduces a GLib main loop
   beside SDL/Qt and needs hand-written async glue for every call.
3. **sd-bus.** Low-level and dependency-light, but the FileChooser protocol's
   request/session objects and option dictionaries would be a large amount of
   hand-written C.

## Decisions made here

- Qt 6 D-Bus, C++17, in a new `hermes-portals` CMake target; the backend is a
  separate D-Bus-activatable executable sharing the C core and the portals
  library.
- The backend is the **long-lived** process: step 4 adds the Kirigami picker
  window to this same executable. There is no handoff to a second process.
- Real requests that arrive while no picker is attached are answered immediately
  with a defined Cancelled-style portal error, so calling applications never
  hang. Tests assert this path.
- Opt-in is a CLI on the backend executable (for example
  `--enable-filechooser` / `--disable-filechooser`), with backup and restore of
  `portals.conf`; it is documented in `docs/integration.md`.
- Step 2 proves protocol behaviour with a stub service and a test queue; the
  real picker window and UI effects are wired in later steps.
- Portal availability is probed asynchronously; absence falls back to existing
  behaviour (current launcher, existing accent reader) without error dialogs.
- Opt-in writes only the specific interfaces Hermes implements into
  `portals.conf`, preserving unrelated keys, with a `.hermes-backup` file for
  exact restore.

## Left to the builder (settled in the plan)

- Exact D-Bus method signatures, object paths and error names per interface.
- Core API signatures for path resolution and open/focus requests.
- Class layout, signal/slot contracts and CMake target/install rules.
- CI package names for `dbus-run-session` on all four images.
- Test fixture layout and the exact stub-service behaviour.
