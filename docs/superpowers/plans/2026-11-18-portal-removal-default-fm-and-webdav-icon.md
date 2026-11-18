# Portal removal, default-FM verification and WebDAV sidebar icon Implementation Plan

> **Historical design:** implemented in early Hermes, later fully purged; see
> `CHANGELOG.md`. Do not treat as current behavior.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> Execution method for this session: **native** (no subagents — user asked not to spawn them until limits refresh). All work stays uncommitted; skip every commit step.

**Goal:** Remove all portal integrations and the `--pick` window, verify the default-file-manager Options toggle, and make the WebDAV sidebar icon follow the system icon theme.

**Architecture:** The project is a C11 unity build (`src/main.c` → `src/app.c` → sub-files); all edits are in `src/` plus docs and tests. Removals delete functions and enum entries rather than adding shims; the shared D-Bus loader stays for the KDE accent reader and is renamed. One small predicate in `render.c` routes default network rows through the same themed folder icon Cloud WebDAV already uses.

**Tech Stack:** C11, SDL3, nanovg/svg, libdbus (dynamic), in-app headless selftests, standalone `tools/*-selftest.c`, `build.sh`, `install.sh`.

**Spec:** `docs/superpowers/specs/2026-11-18-portal-removal-default-fm-and-webdav-icon-design.md`

## Global Constraints

- Work in place on `main`; **do not commit, push, tag, fetch or merge**.
- KDE Plasma 6.x is the only target. Report headless results as **verified**, live Plasma behavior as **target-only**.
- C sources pure ASCII; warnings fail CI; unity build means unique `static` names and declaration-before-use.
- Every behavior change uses TDD (failing test first). Tests are headless fixtures; never touch the real `$HOME`, keyring, mounts, socket or desktop defaults.
- In-app suites run as a non-root user in `fedora-build`, serially, with `XDG_DATA_HOME=/tmp/opencode/hermes-suite-data`.
- Do not spawn review/implementation subagents in this session.

## Review Focus

- A user toggles "Default file manager" off: it must restore the previous handler, not leave Hermes set — Task 1 test.
- A WebDAV/network row without a custom icon must use the system theme icon, including when the icon theme is changed — Task 2 test.
- Legacy portal/FileManager1 files on a user's disk must be left untouched by migration; only Hermes-owned files are adopted — Task 5 adoption test.
- Removing options must not break persisted settings that still contain `integration_show_in_folder=` — Task 5 settings test.
- `--pick` must be gone from CLI, app hooks, docs and tests with no dangling declarations — Task 3 build + grep check.

---

### Task 1: Verify the default-file-manager Options toggle

**Files:**
- Modify: `src/selftest.c` (grep suite, after the setter assertions at ~6947)

**Interfaces:**
- Consumes: `opt_bool_toggle(int)`, `ig_default_fm_on()`, `A.ig_prev_fm`, `mp`, `text` already in scope in `run_selftest_grep()`.
- Produces: no new production symbols.

- [ ] **Step 1: Write the failing/regression test**

Append to the default-FM block in `run_selftest_grep()` (state is off after the existing setter assertions):

```c
opt_bool_toggle(OPT_IG_DEFAULT_FM);
st_read_text(mp, text, sizeof text);
ST_CHECK(ig_default_fm_on() && strstr(text, "inode/directory=" HERMES_APP_ID ".desktop;") &&
         !strcmp(A.ig_prev_fm, "fake-files.desktop"));
opt_bool_toggle(OPT_IG_DEFAULT_FM);
st_read_text(mp, text, sizeof text);
ST_CHECK(!ig_default_fm_on() && strstr(text, "inode/directory=fake-files.desktop;") &&
         !strstr(text, HERMES_APP_ID));
```

- [ ] **Step 2: Run the suite**

Run: `distrobox enter fedora-build -- env XDG_DATA_HOME=/tmp/opencode/hermes-suite-data ./build/debug/hermes --selftest-grep`
Expected: PASS. If it fails, the switch is broken; continue to Step 3.

- [ ] **Step 3: Fix only if Step 2 failed**

In `src/options.c` make `case OPT_IG_DEFAULT_FM:` call `ig_default_fm_set(!ig_default_fm_on())`, and in `src/integration.c` make `ig_default_fm_set(false)` restore `A.ig_prev_fm` when that desktop file still exists. Do not change the test to match a broken implementation.

- [ ] **Step 4: Re-run Step 2 and record the evidence**

Expected: PASS. Keep the exact command and result for the final report.
No commit.

---

### Task 2: Theme the WebDAV / default network sidebar icon

**Files:**
- Modify: `src/render.c` (sidebar place drawing at ~842-844)
- Modify: `src/selftest.c` (icon fixtures in `run_selftest_m7()` at ~1930)

**Interfaces:**
- Consumes: `draw_icon_id()`, `icon_find()`, `draw_net_icon()`, `system_icon_name()` (all already in scope).
- Produces: `static bool net_row_uses_theme_icon(const char *key)` in `src/render.c`, declared before `draw_sidebar_places()`.

- [ ] **Step 1: Write the failing test**

Add to the icon-theme block in `run_selftest_m7()`:

```c
ST_CHECK(net_row_uses_theme_icon(NULL) && net_row_uses_theme_icon("") && net_row_uses_theme_icon("network"));
ST_CHECK(!net_row_uses_theme_icon("custom/Homelab/rack") && !net_row_uses_theme_icon("aws"));
ST_CHECK(!strcmp(system_icon_name(icon_find("folder-shared")), "folder-remote"));
```

- [ ] **Step 2: Run the suite to verify RED**

Run: `distrobox enter fedora-build -- env XDG_DATA_HOME=/tmp/opencode/hermes-suite-data ./build/debug/hermes --selftest-m7`
Expected: build fails with `net_row_uses_theme_icon` undefined (the initial RED).

- [ ] **Step 3: Implement the predicate and route the rows**

In `src/render.c`, define `net_row_uses_theme_icon()` returning true for `NULL`, `""` or `"network"`. In the `PK_NETWORK || PK_NETSAVED || PK_NETSYSTEM` branch, read the server key once, then call `draw_icon_id(ix, iy, is, icon_find("folder-shared"), dim)` when the predicate is true and `draw_net_icon(ix, iy, is, key, dim)` otherwise. Preserve the existing faded alpha (`0.5f` for saved/system rows). Do not touch `draw_net_icon()` itself or the Connect icon picker.

- [ ] **Step 4: Rebuild and verify GREEN**

Run: `./build.sh debug`, then Step 2. Expected: PASS.
No commit.

---

### Task 3: Remove the `--pick` window and `picker.c`

**Files:**
- Delete: `src/picker.c`
- Modify: `src/app.c` (forward declarations ~1595-1604, `picker_active()` call sites, `--pick` parsing at ~16202-16217, `#include "picker.c"` at ~15280)
- Modify: `src/selftest.c` (picker suite block ~6002-6077, picker screenshots/messages)
- Modify: `src/base.h` (unused picker ids, if any)
- Test: build + `--selftest-m9`

**Interfaces:**
- Consumes: nothing from Tasks 1-2.
- Produces: no `picker_*` symbol remains.

- [ ] **Step 1: Delete the file and its include**

Remove `src/picker.c` and the `#include "picker.c"` line in `src/app.c`.

- [ ] **Step 2: Remove declarations, CLI and call sites in `src/app.c`**

Delete the `picker_*` forward declarations, all `picker_active()` guards (including those in startup/single-instance/screenshot paths), `--pick` argument parsing and `picker_setup()`. Where a guard was `!picker_active()`, keep the remaining conditions; where `if (picker_active()) { ... }` wrapped picker-only work, delete the block. Remove `HERMES_PICKER_*` definitions from `src/base.h` if now unused.

- [ ] **Step 3: Remove picker tests**

Delete the picker block in `run_selftest_m9()` and any `st_shot("picker-...")` / `connect-icon-picker` assertions that only exercise the removed window. Keep unrelated connect-sheet tests.

- [ ] **Step 4: Build warning- and ASCII-clean**

Run: `./build.sh debug` and `./build.sh release`. Expected: no `warning:`, no non-ASCII error.

- [ ] **Step 5: Verify no dangling references**

Run: `grep -rn 'picker_\|--pick\|picker_active' src/ || true`
Expected: no matches. Run `--selftest-m9` in `fedora-build`; expected PASS.
No commit.

---

### Task 4: Remove FileManager1 and documents-portal FileTransfer

**Files:**
- Modify: `src/integration.c` (`IgShow`, `ig_show_*`, `ig_fm1_*`, `g_pft`/`pft_*`, PFT defines)
- Modify: `src/dbus.c` (rename `g_fm1` → `g_dbus`, drop FM1-only state/enum)
- Modify: `src/system_appearance.c` (update `g_fm1` → `g_dbus` references)
- Modify: `src/dragout.c` (PFT atom, target list, offer and probe)
- Modify: `src/app.c` (`ig_fm1_start()` call (~16544), `A.ig_fm1` field, picker guards in integration entry points)
- Modify: `src/settings.c` (drop `integration_show_in_folder` read/write and reset)
- Modify: `src/selftest.c` (remove FM1 block ~6996-7010 and dragout PFT assertions)
- Test: `--selftest-grep`, `--selftest` (dragout fixtures)

**Interfaces:**
- Consumes: nothing from Tasks 1-3.
- Produces: `g_dbus` is the shared D-Bus loader state used by `system_appearance.c`.

- [ ] **Step 1: Remove FM1 from `integration.c`**

Delete `IgShow`, `ig_show_apply()`, `ig_show_parse()`, `g_fm1_xml`, `ig_fm1_thread()`, `ig_fm1_service_path()`, `ig_fm1_stop()`, `ig_fm1_start()`, `ig_fm1_set()` and every `g_fm1` use in that file. Delete `PFT_*` defines, `g_pft`, `pft_*` functions and the `ig_default_fm_*` / first-run ask code remains untouched. Remove `picker_active()` from the first-run guard at ~575.

- [ ] **Step 2: Rename the shared loader**

In `src/dbus.c` rename the struct variable `g_fm1` → `g_dbus` everywhere, delete the `state` field and `enum { IG_FM1_* }`, and update all `g_fm1.` uses in `src/system_appearance.c`. Keep all loaded symbols used by the accent reader.

- [ ] **Step 3: Remove PFT from `dragout.c`**

Delete `pft_probe`, `pft_available`, `pft_key` declarations, `#define PFT_MIME`, `g_xd.a_pft` and its target-list entry, the PFT receive branch (~604), the Wayland offer line (~764), and the `if (g_do.sys) pft_probe()` call.

- [ ] **Step 4: Remove app/settings state and startup call**

Delete `ig_fm1_start()` from `src/app.c`, the `A.ig_fm1` field, its reset in `src/settings.c:48`, and the `integration_show_in_folder=` write in `settings_save()` plus its read in `settings_load()`. Persisted files from older versions containing that key must still load (unknown keys ignored).

- [ ] **Step 5: Remove FM1/PFT tests and verify**

Delete the FM1 assertions in `run_selftest_grep()` (~6996-7010) and any `pft` assertions in the plain suite. Build, then run `--selftest-grep` and `--selftest` in `fedora-build`; expected PASS.
No commit.

---

### Task 5: Remove the file-chooser portal and clean migration adoption

**Files:**
- Modify: `src/options.c` (enum at ~12, `OptDef` rows ~82-83, `opt_desc` ~166-171, `opt_bool_value` ~345-346, `opt_bool_toggle` ~399-404)
- Modify: `src/integration.c` (`ig_picker_*`, `ig_portal_restart`, FM1/portal adoption branches at ~526-563)
- Modify: `src/integration_identity.c` (`identity_legacy_fm1`, `identity_legacy_portal`)
- Modify: `tools/integration-identity-selftest.c`
- Modify: `src/selftest.c` (picker block ~6948-6960, adoption fixture ~7011-7096)
- Test: `--selftest-grep`, standalone integration-identity test

**Interfaces:**
- Consumes: `g_dbus` from Task 4 (not needed here).
- Produces: `identity_legacy_desktop()` only; `ig_identity_adopt()` keeps launcher + legacy-default adoption.

- [ ] **Step 1: Remove the Options entries**

Delete `OPT_IG_FM1` and `OPT_IG_PICKER` from the enum, their two `{OC_INTEGRATION, OK_BOOL, ...}` rows, their `opt_desc()` cases, `opt_bool_value()` cases and `opt_bool_toggle()` cases. Verify no persisted setting stores numeric option ids (settings use string keys).

- [ ] **Step 2: Remove portal code in `integration.c`**

Delete `ig_picker_portal_installed()`, `ig_picker_paths()`, `ig_picker_on()`, `ig_portal_restart()`, `ig_picker_set()` and their D-Bus/`systemctl` helpers. In `ig_identity_adopt()` delete the FM1 branch (~526-535) and the portal wrapper/config branch (~537-563). Keep only the legacy `.desktop` adoption and the `ig_folder_app()` legacy-default claim.

- [ ] **Step 3: Trim identity provenance helpers**

In `src/integration_identity.c` delete `identity_legacy_fm1()` and `identity_legacy_portal()`. In `tools/integration-identity-selftest.c` delete their assertions and add an assertion that a legacy FM1/portal file is **not** adopted:

```c
assert(!identity_legacy_desktop("[D-BUS Service]\nName=org.freedesktop.FileManager1\n", "/fixture/hermes"));
```

Run the standalone test with the compile command in its first line; expected PASS.

- [ ] **Step 4: Update grep-suite fixtures**

Delete the file-chooser assertions (~6948-6960). In the adoption fixture, stop writing FM1/portal files and stop asserting them; keep the launcher adoption, the legacy-default claim and the "other app's default is never claimed" case. Add: create a legacy FM1 service file and a legacy portal config, run `ig_identity_adopt()`, and assert both are byte-for-byte unchanged.

- [ ] **Step 5: Verify no dangling references**

Run: `grep -rn 'ig_picker\|ig_fm1\|FileManager1\|termfilechooser\|portals.conf\|PFT_\|pft_' src/ tools/ || true`
Expected: no matches outside historical design docs. Build and run `--selftest-grep`; expected PASS.
No commit.

---

### Task 6: Docs, CHANGELOG and the full verification battery

**Files:**
- Modify: `docs/integration.md`, `docs/architecture.md`, `docs/migration.md`, `docs/settings.md`, `README.md`, `CHANGELOG.md`
- Modify: `docs/superpowers/specs/2026-11-17-hermes-identity-design.md` (superseded-note only)

**Interfaces:**
- Consumes: verified behavior from Tasks 1-5.
- Produces: no new code symbols.

- [ ] **Step 1: Update docs**

Rewrite `docs/integration.md` to document only the default-FM switch and state that no portal/FileManager1 integration is provided. Remove stale picker/FileManager1/portal claims from `docs/architecture.md`, `docs/migration.md`, `docs/settings.md` and `README.md`. Add a one-line superseded note to the earlier identity spec about its picker app-id row.

- [ ] **Step 2: Update CHANGELOG**

Under `## Unreleased`, add a **Changed** bullet (portal integrations and picker window removed; KDE Plasma-native roles only) and a **Fixed** bullet (WebDAV/network sidebar row follows the system icon theme). Do not create a version section.

- [ ] **Step 3: Run the full battery**

Run: `./build.sh debug`, `./build.sh release`, `./build.sh package`; all 15 in-app suites serially in `fedora-build` as non-root with `XDG_DATA_HOME=/tmp/opencode/hermes-suite-data`; all standalone `tools/*-selftest.c`; staged `install.sh`; `python3 tools/branding-selftest.py`; `python3 tools/build-install-selftest.py`; doc-link check; `sh tools/check-release-version.sh`; `git diff --check`; ASCII/warning checks.

- [ ] **Step 4: Report**

Record verified (headless) vs target-only (live Plasma), note the unavailable review subagents, and confirm the tree is uncommitted. No commit.

## Self-review

- Spec coverage: every spec goal maps to Tasks 1-6; non-goals are enforced by the no-touch and no-commit constraints.
- Step scan: each step names exact files, symbols and assertions; no TBDs.
- Type consistency: `net_row_uses_theme_icon(const char *)` and `g_dbus` are used consistently in the tasks that define and consume them.
- Review Focus: all five items are pinned to a test in Tasks 1, 2, 5 and 5/3.
- Proportion: the plan is shorter than the code it removes; code blocks are only the exact test assertions.
