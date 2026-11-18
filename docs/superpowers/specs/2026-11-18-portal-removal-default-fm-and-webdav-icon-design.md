# Design: remove portal integrations, verify default-FM switch, theme the WebDAV sidebar icon

> **Historical design:** implemented in early Hermes, later fully purged; see
> `CHANGELOG.md`. Do not treat as current behavior.

Date: 2026-11-18
Status: **awaiting approval** (nothing implemented yet)

## Intent and why

Hermes targets KDE Plasma 6.x. Three opt-in integrations are portals / non-native
roles and are being removed: the termfilechooser file chooser, FileManager1
"Show in folder", and the documents-portal FileTransfer used for dragging into
sandboxed apps. The `--pick` picker window exists only to serve the file
chooser and goes with it. The default-file-manager switch stays, and the user
asked to confirm it actually works from the UI, not just from its setter.
Finally, the WebDAV row in the left sidebar draws a hard-coded blue globe that
does not follow the icon theme, while the Cloud WebDAV row already does.

## Goals

1. Remove every portal integration and the `--pick` window, including their
   Options switches, generated files, startup hooks, drag paths, migration
   adoption branches, tests and docs.
2. Keep the "Default file manager" switch and prove the real Options toggle
   writes and restores `mimeapps.list`.
3. Make the sidebar's default network/WebDAV row draw the system theme icon.
4. Keep migration safe and idempotent; never touch the user's real config.

## Non-goals / anti-goals

- Do not remove `src/dbus.c` or the KDE accent-colour reader that shares it.
- Do not change what "Default file manager" does; only verify it.
- Do not remove the built-in globe from the Connect-to-server icon picker; it
  stays as a fallback for custom/network themes.
- Do not migrate, delete or rewrite legacy portal/FileManager1 files on disk.
  They are left untouched; only files Hermes itself owns are ever removed.
- No commits, pushes, tags or upstream fetch/merge.

## Constraints

- C11 unity build (`main.c` → `app.c` → sub-files); every new `static` name must
  be unique across all sources and defined before use.
- Sources must be pure ASCII; warnings fail CI.
- TDD RED → GREEN for behavior changes; headless fixture tests only, run as a
  non-root user; never touch the real `$HOME`, keyring, mount or socket.
- KDE Plasma is the only target; headless results are **verified**, live Plasma
  behavior remains **target-only**.

## Approaches (decided in conversation)

| Area | Chosen | Rejected |
|---|---|---|
| Portal scope | Full removal, including `picker.c` and `--pick` | Keep `--pick` as a standalone CLI (leaves dead code whose only caller is gone) |
| WebDAV icon | Sidebar default network rows draw the themed `folder-remote` icon, same as Cloud WebDAV | Recolour the built-in globe's hard-coded blue with the theme marker (colour only, custom artwork remains) |
| Default-FM | Add a regression test through `opt_bool_toggle(OPT_IG_DEFAULT_FM)` | Trust the existing setter-only test |

## The change, in detail

### 1. Default-FM switch verification (first, TDD)

In the grep suite (`src/selftest.c`), alongside the existing setter assertions:

- `opt_bool_toggle(OPT_IG_DEFAULT_FM)` from a fixture where
  `inode/directory=fake-files.desktop;` → assert `ig_default_fm_on()`,
  `A.ig_prev_fm == "fake-files.desktop"`, and the Hermes desktop id in
  `/tmp/fe-selftest-config/mimeapps.list`.
- Toggle back → assert `!ig_default_fm_on()`, the original id restored and no
  Hermes id left.

No production change is expected. If the test fails, fix the toggle path in
`src/options.c` / `src/integration.c`, not the test.

### 2. WebDAV / default network sidebar icon (TDD)

`src/render.c` draws `PK_NETWORK` / `PK_NETSAVED` / `PK_NETSYSTEM` rows with
`draw_net_icon(key)`. An empty or `"network"` key selects the built-in globe
(`src/net_icons.h`), whose stroke is hard-coded `#7FB2FF`. The Cloud WebDAV
branch a few lines below already uses `draw_icon_id(..., icon_find("folder-shared"), ...)`,
which `system_icon_name()` maps to the freedesktop `folder-remote` icon.

Change: add one small predicate (e.g. `net_row_uses_theme_icon(key)`, true for
`NULL`, `""` and `"network"`) and use `draw_icon_id(... icon_find("folder-shared") ...)`
for those rows. Explicit/custom keys keep going through `draw_net_icon()`,
so user-chosen artwork and the Connect icon picker are unaffected. This applies
to all default network rows, not just WebDAV, which removes the special case
and matches Cloud WebDAV exactly.

Test the predicate (default keys themed, custom keys not) and assert that
`system_icon_name(icon_find("folder-shared")) == "folder-remote"` so a future
mapping change cannot silently drop theme compliance.

### 3. Portal removal

- `src/options.c`: drop `OPT_IG_FM1`, `OPT_IG_PICKER` from the enum, their two
  `OptDef` rows, their `opt_desc`, `opt_bool_value` and `opt_bool_toggle` cases.
  Verify no persisted setting stores numeric option ids.
- `src/integration.c`: drop `ig_fm1_*`, `g_pft` / `pft_*`, `ig_picker_*`,
  `ig_portal_restart`, and the FM1/portal branches of `ig_identity_adopt()`.
  Keep the desktop-launcher adoption, the legacy-default-FM adoption and
  `ig_default_fm_*`. Rename the shared D-Bus loader state `g_fm1` → `g_dbus` in
  `src/dbus.c` and `src/system_appearance.c` so the name no longer implies a
  removed role.
- `src/integration_identity.c`: drop `identity_legacy_fm1()` and
  `identity_legacy_portal()`; keep `identity_legacy_desktop()`. Update
  `tools/integration-identity-selftest.c`.
- `src/dragout.c`: drop all PFT/FileTransfer offers, target atoms and probes.
- `src/app.c`: drop `#include "picker.c"`, the picker forward declarations and
  call sites, `--pick` parsing, `picker_setup()`, and `ig_fm1_start()`.
- Delete `src/picker.c`; drop now-unused picker/FM1 ids and `App` fields from
  `src/base.h`.
- `src/selftest.c`: remove the picker suite block, the picker screenshot, and
  the picker/FM1/portal assertions and adoption fixtures. Keep the default-FM
  block and add the toggle test from step 1.

### 4. Docs and release notes

- `docs/integration.md`: reduce to the single default-FM switch and state that
  no portal / FileManager1 integration is provided by design.
- `docs/architecture.md`, `docs/migration.md`, `docs/settings.md`, `README.md`:
  remove stale portal/picker/FileManager1 claims; add a note to the earlier
  identity spec that its picker id row is superseded.
- `CHANGELOG.md` under `## Unreleased`: **Changed** (portals and picker removed)
  and **Fixed** (WebDAV sidebar icon follows the icon theme).

## Verification

1. `./build.sh debug` (warning + ASCII clean), then the in-app suites serially
   in `fedora-build` as non-root with `XDG_DATA_HOME=/tmp/opencode/hermes-suite-data`,
   including the grep suite (default-FM toggle) and the icon suite.
2. All standalone `tools/*-selftest.c`, especially `integration-identity-selftest.c`.
3. `./build.sh release`, `./build.sh package`, staged `install.sh`,
   `python3 tools/branding-selftest.py`, `python3 tools/build-install-selftest.py`,
   doc-link check, `sh tools/check-release-version.sh`, `git diff --check`.
4. Report verified (headless) separately from target-only (live Plasma).

## Calls made here (builder may treat as decided)

- Apply the themed icon to all default network rows, not only WebDAV, to avoid
  a protocol special case.
- Leave the built-in globe in the Connect icon picker as a fallback.
- Rename the shared D-Bus loader state to `g_dbus` rather than leave a stale
  FileManager1 name.
- Rewrite `docs/integration.md` rather than delete it.

## Builder check

The two independent review subagents are unavailable (free usage limit), so I
read this document as the builder myself. Open questions: none that change the
plan. All choices above are settled by the conversation or by project
constraints; remaining details are mechanical and are covered in the change list.
