# Legacy File Explorer Purge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove every File Explorer compatibility path from Hermes so startup can never be blocked by legacy migration, and document that early Hermes had this support and it was later purged.

**Architecture:** Delete the startup importer and all legacy keyring/cloud/launcher handover; rename every runtime override to `HERMES_*`; add a static regression checker that fails if legacy identifiers return. Unity build (`src/main.c` includes all `.c` files) means include order and unique static names are hard constraints.

**Tech Stack:** C11, SDL3, POSIX, shell/Python test scripts, Markdown docs.

**Spec:** `docs/superpowers/specs/2026-11-18-legacy-purge-design.md`

## Global Constraints

- SDL 3.2+, vterm 0.3+, OpenSSL 3.0+ are minimum build dependencies.
- Source must be pure ASCII; `build.sh` aborts on non-ASCII bytes.
- Compiler warnings fail CI; do not add new warnings.
- Self-tests must not touch the user's real config or home (`XDG_CONFIG_HOME`, `XDG_STATE_HOME`, IPC socket are overridden in fixtures).
- KDE Plasma 6.x is the primary target; headless/offscreen runs are not proof of live Plasma behavior.
- Host build dependencies are incomplete; ask before installing packages or provisioning a container.
- Do not change `APP_VERSION`, package IDs, or release tags; no version bump in this change.
- Do not delete or rewrite the user's legacy `file-explorer` data.
- Commit after each task. Push only after final verification; the user has explicitly authorized the final commit/push.

## Review Focus

1. **Stale `~/.local/state/hermes/startup.lock` or legacy legacy config dir** — app must still open; pinned by Task 1 static no-importer check plus a manual launch with a pre-made lock fixture.
2. **Keyring containing only the old `file-explorer` namespace secret** — Hermes must not read or revive it; pinned by Task 2 credentials fixture.
3. **Cloud unit with the legacy `file-explorer` header** — must be treated as external, never owned or deferred; pinned by Task 3 cloud fixture.
4. **A user sets `FE_*`-style overrides** — must have no effect; only `HERMES_*` works; pinned by Task 5 static checker and Hermes-only override test.
5. **Generated legacy launcher or default-FM role** — must not be adopted at startup; pinned by Task 4 static no-adoption check and removal of adoption assertions.

---

### Task 1: Remove the startup migration gate and importer

**Files:**
- Modify: `src/main.c:28`, `src/app.c:16220-16226`
- Delete: `src/migration.c`
- Create: `tools/legacy-purge-selftest.py`

**Interfaces:**
- Consumes: nothing.
- Produces: `tools/legacy-purge-selftest.py` with checks `test_no_migration_symbols_in_active_code` and `test_legacy_directory_not_referenced`. Exit 0 on pass, non-zero on fail.

- [ ] **Step 1: Write the failing static regression**

Create `tools/legacy-purge-selftest.py`. It must read active source files (`src/*.c`, `src/*.h`, `build.sh`, `tools/*.c`, `packaging/**`) and assert that these substrings are absent: `migration_startup`, `migration_finish`, `migration_import`, `migration_private_dir`, `startup.lock`, `HERMES_MIGRATION_RETRY`. It must also assert `src/main.c` does not include `migration.c` and `src/app.c` does not contain the migration error message. The checker must skip its own file so its literal patterns do not match itself.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 tools/legacy-purge-selftest.py`
Expected: FAIL, listing the migration symbols still present.

- [ ] **Step 3: Remove the importer**

Delete `src/migration.c`. Remove `#include "migration.c"` from `src/main.c`. In `src/app.c`, delete the `if (!any_selftest ... !migration_startup()) { ... return 1; }` block and the later `migration_finish()` call. Keep `settings_load()` and the single-instance block unchanged.

- [ ] **Step 4: Run the static regression to verify it passes**

Run: `python3 tools/legacy-purge-selftest.py`
Expected: PASS, exit 0.

- [ ] **Step 5: Commit**

```bash
git add -A src/migration.c src/main.c src/app.c tools/legacy-purge-selftest.py
git commit -m "refactor(startup): remove legacy migration gate and importer"
```

---

### Task 2: Remove legacy keyring migration

**Files:**
- Modify: `src/credentials.c:68-137`, `tools/credentials-selftest.c`
- Modify: `tools/legacy-purge-selftest.py` (add `HERMES_LEGACY_SLUG` check)

**Interfaces:**
- Consumes: `identity_xdg()` from `src/identity.h`, `tool()` from `src/devices.c`.
- Produces: `static int secret_private_dir(void)` in `src/credentials.c`; `secret_lookup()` uses only `HERMES_SLUG`.

- [ ] **Step 1: Write the failing test**

Rewrite `tools/credentials-selftest.c` so it:
- sets `XDG_STATE_HOME` to a temp dir and `HERMES_SECRET_TOOL` to its fake `secret-tool`;
- writes only `<tmp>/hermes.secret` and asserts `secret_lookup()` returns it;
- writes only `<tmp>/file-explorer.secret` in a fresh temp dir and asserts `secret_lookup()` returns false (legacy data ignored);
- keeps the existing store/clear/marker checks for the Hermes namespace only;
- removes `#include "../src/migration.c"`.

Compile command (first line of the file) must still be valid.

- [ ] **Step 2: Run it to verify it fails**

Run:
```sh
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/credentials-selftest.c -o /tmp/opencode/hermes-credentials-test -pthread -lm && /tmp/opencode/hermes-credentials-test
```
Expected: compile failure because `migration_private_dir` is no longer supplied after the include is removed.

- [ ] **Step 3: Implement Hermes-only credentials**

In `src/credentials.c`, add `static int secret_private_dir(void)` containing the old `migration_private_dir()` body, called with the existing `credential-deletions` path. Replace `migration_private_dir(...)` in `secret_intent_dir()` with it. In `secret_lookup()` delete the legacy namespace fallback and the "credential namespace migration deferred" message. In `secret_clear()` delete the second `secret_cmd()` and the `argv[3] = HERMES_LEGACY_SLUG` line; return only `current`.

- [ ] **Step 4: Run tests to verify they pass**

Run the compile command from Step 2.
Run: `python3 tools/legacy-purge-selftest.py`
Expected: both PASS.

- [ ] **Step 5: Commit**

```bash
git add src/credentials.c tools/credentials-selftest.c tools/legacy-purge-selftest.py
git commit -m "refactor(credentials): drop legacy keyring migration"
```

---

### Task 3: Remove legacy cloud-unit handover

**Files:**
- Modify: `src/cloud_identity.c:1-26`, `src/rclone.c:41-74,289-330,623`, `src/selftest.c:5779,5798`, `tools/cloud-identity-selftest.c`
- Modify: `tools/legacy-purge-selftest.py` (add `cloud_legacy_owned` check)

**Interfaces:**
- Consumes: `units_dir()` from `src/units.c`, `CLOUD_HERMES`/`CLOUD_EXTERNAL` from `src/cloud_identity.c`.
- Produces: `cloud_unit_owner()` returns only `CLOUD_HERMES` or `CLOUD_EXTERNAL`; no `CLOUD_LEGACY`.

- [ ] **Step 1: Write the failing test**

Rewrite `tools/cloud-identity-selftest.c` so it:
- asserts `cloud_unit_name("work s3", ...)` is `hermes-cloud-work_20s3.service`;
- writes a unit file with the legacy `file-explorer` header and asserts `cloud_unit_owner()` is `CLOUD_EXTERNAL`;
- writes a Hermes-header file and asserts `CLOUD_HERMES`;
- writes an unrelated file and asserts `CLOUD_EXTERNAL`;
- removes all legacy unit-name assertions.

- [ ] **Step 2: Run it to verify it fails**

Run:
```sh
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation tools/cloud-identity-selftest.c -o /tmp/opencode/hermes-cloud-identity-test -pthread -lm && /tmp/opencode/hermes-cloud-identity-test
```
Expected: runtime assertion failure on the legacy-header case (the current source still returns `CLOUD_LEGACY`; the new test expects `CLOUD_EXTERNAL`).

- [ ] **Step 3: Implement Hermes-only cloud ownership**

In `src/cloud_identity.c`, remove `CLOUD_LEGACY`, `cloud_legacy_unit_name()`, the `legacy` header string and its comparison. Keep `cloud_unit_name()` and a `cloud_unit_owner()` that checks only the Hermes header. In `src/rclone.c`, delete `cloud_legacy_owned()`, the legacy `identity_xdg(... HERMES_LEGACY_SLUG "/cloud" ...)` check in `path_in_cloud()`, the three early-return/toast blocks in `cloud_connect()`, `cloud_disconnect()`, and `cloud_set_login()`, and set `c->ownership = s->ours[k] ? CLOUD_HERMES : CLOUD_EXTERNAL`. In `src/selftest.c`, remove the two `CLOUD_LEGACY` assertions and keep the external/Hermes ones.

- [ ] **Step 4: Run tests to verify they pass**

Run the compile command from Step 2.
Run: `python3 tools/legacy-purge-selftest.py`
Expected: both PASS.

- [ ] **Step 5: Commit**

```bash
git add src/cloud_identity.c src/rclone.c src/selftest.c tools/cloud-identity-selftest.c tools/legacy-purge-selftest.py
git commit -m "refactor(cloud): remove legacy service handover"
```

---

### Task 4: Remove legacy launcher and default-FM adoption

**Files:**
- Modify: `src/integration.c:39,70-103`, `src/app.c:16225`, `src/selftest.c:6916-6970`
- Delete: `src/integration_identity.c`, `tools/integration-identity-selftest.c`
- Modify: `tools/legacy-purge-selftest.py` (add `ig_identity_adopt` check)

**Interfaces:**
- Consumes: nothing.
- Produces: no startup adoption hook; native `ig_first_run()` default-FM prompt remains.

- [ ] **Step 1: Write the failing test**

Add to `tools/legacy-purge-selftest.py` assertions that `ig_identity_adopt`, `identity_legacy_desktop`, and `HERMES_LEGACY_APP_ID` are absent from active source. Delete `tools/integration-identity-selftest.c` and remove the legacy adoption assertions from `src/selftest.c`.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 tools/legacy-purge-selftest.py`
Expected: FAIL on the adoption symbols.

- [ ] **Step 3: Remove the adoption code**

Delete `src/integration_identity.c` and its `#include` in `src/integration.c`. Delete `ig_identity_adopt()` and its call in `src/app.c`. Keep `ig_first_run()` and the native default-FM switch untouched.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 tools/legacy-purge-selftest.py`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A src/integration_identity.c src/integration.c src/app.c src/selftest.c tools/integration-identity-selftest.c tools/legacy-purge-selftest.py
git commit -m "refactor(integration): remove legacy launcher adoption"
```

---

### Task 5: Rename every active `FE_*` override to `HERMES_*`

**Files:**
- Modify: `src/*.c`, `src/*.h`, `build.sh`, `packaging/arch/PKGBUILD`, `tools/*.c`, `tools/*.py`, `docs/building.md`, `docs/settings.md`
- Modify: `tools/legacy-purge-selftest.py` (add `FE_` absence check)

**Interfaces:**
- Consumes: `identity_env()` simplified in `src/identity.h`.
- Produces: only `HERMES_*` runtime and build variables; `identity_env()` is a plain `getenv()` wrapper.

- [ ] **Step 1: Write the failing test**

Add to `tools/legacy-purge-selftest.py` an assertion that no active file in `src/`, `tools/*.c`, `tools/*.py`, `build.sh`, `packaging/**` contains the literal `FE_`. The checker must exclude its own file, and historical Markdown docs are excluded from this check.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 tools/legacy-purge-selftest.py`
Expected: FAIL with a list of active `FE_*` occurrences.

- [ ] **Step 3: Rename the identifiers**

In `src/identity.h`, remove `HERMES_LEGACY_SLUG`, `HERMES_LEGACY_APP_ID`, and the `FE_` alias branch; make `identity_env()` return `getenv(name)`. Rename every `FE_` override key to `HERMES_` in `src/*.c`, `src/*.h`, `tools/*.c`, `tools/*.py`, `build.sh`, and `packaging/arch/PKGBUILD`. Rename compile macros `FE_ADDON` → `HERMES_ADDON`, `FE_DEMO` → `HERMES_DEMO`, build inputs `FE_ADDON_SRC` → `HERMES_ADDON_SRC`, `FE_OUT` → `HERMES_OUT`, `FE_SRC` → `HERMES_SRC`. Update `tools/build-install-selftest.py`: remove the legacy-alias test and the `FE_` strip prefix. Keep only Hermes-only assertions. Thread name in `src/ipc.c` becomes `hermes-ipc`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 tools/legacy-purge-selftest.py`
Run: `python3 tools/build-install-selftest.py`
Run: `python3 tools/branding-selftest.py`
Expected: all PASS. If host dependencies permit, also run `./build.sh package` and confirm no `-Werror`/warning failures; otherwise report the dependency limitation.

- [ ] **Step 5: Commit**

```bash
git add -A src build.sh packaging tools docs/building.md docs/settings.md
git commit -m "refactor(identity): use only HERMES_* overrides"
```

---

### Task 6: Rewrite documentation and record the purge history

**Files:**
- Modify: `docs/migration.md`, `README.md`, `docs/install.md`, `docs/integration.md`, `docs/immutable-distros.md`, `docs/building.md`, `docs/architecture.md`, `docs/settings.md`, `CHANGELOG.md`, `docs/checkpoint.md`
- Modify: historical `docs/superpowers/specs/2026-11-17-hermes-identity-design.md`, `docs/superpowers/plans/2026-11-17-hermes-identity.md`, `docs/superpowers/specs/2026-11-18-portal-removal-default-fm-and-webdav-icon-design.md`, `docs/superpowers/plans/2026-11-18-portal-removal-default-fm-and-webdav-icon.md`

**Interfaces:**
- Consumes: completed code purge from Tasks 1-5.
- Produces: current docs describe Hermes-only behavior; historical docs carry a superseded banner; changelog has an Unreleased purge entry.

- [ ] **Step 1: Write the failing doc checks**

Add to `tools/legacy-purge-selftest.py` assertions that:
- `docs/migration.md` contains "removed" and does not promise an import;
- `CHANGELOG.md` Unreleased section mentions that early Hermes had migration and it was fully purged;
- the four historical superpowers docs contain "later fully purged".

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 tools/legacy-purge-selftest.py`
Expected: FAIL on the missing doc statements.

- [ ] **Step 3: Update the docs**

Rewrite `docs/migration.md` as a short historical page: early Hermes had a one-time importer and legacy compatibility; it was fully purged because the startup gate could block launch; current behavior is Hermes-only and legacy data is left untouched. Remove migration/adoption claims and dead links from the current docs listed above, replacing them with a Hermes-only paths/overrides statement where needed. Add the Unreleased changelog bullet. Add a top banner to the historical superpowers docs: "Historical design: implemented in early Hermes, later fully purged; see CHANGELOG. Do not treat as current behavior." Update `docs/checkpoint.md` with the user decision.

- [ ] **Step 4: Run checks to verify they pass**

Run: `python3 tools/legacy-purge-selftest.py`
Run: `python3 tools/build-install-selftest.py`
Expected: both PASS. `git diff --check` must be clean.

- [ ] **Step 5: Commit**

```bash
git add -A README.md CHANGELOG.md docs
git commit -m "docs: record removal of legacy File Explorer support"
```

---

### Task 7: Final verification, push, and installed-binary smoke check

**Files:**
- No new source files; verification and release handoff only.

**Interfaces:**
- Consumes: all previous tasks.
- Produces: verified branch state, pushed `main`.

- [ ] **Step 1: Run the full available verification set**

Run, from the repo root:
```sh
python3 tools/legacy-purge-selftest.py
python3 tools/build-install-selftest.py
python3 tools/branding-selftest.py
sh tools/check-release-version.sh
git diff --check
```
For the C standalone suites, run the compile commands from their first lines for `tools/credentials-selftest.c` and `tools/cloud-identity-selftest.c`. If the host has the build dependencies, run `./build.sh package` and then, with a fresh `XDG_STATE_HOME` and a pre-made `startup.lock` fixture, launch `./build/package/hermes --screenshot /tmp/opencode/hermes-purge.bmp /usr` and confirm no migration error. If dependencies are missing, report that limitation explicitly and do not install packages without asking.

- [ ] **Step 2: Confirm no legacy identifiers remain in active files**

Run: `grep -R -n --exclude=legacy-purge-selftest.py -E 'FE_|HERMES_LEGACY|migration_startup|ig_identity_adopt|cloud_legacy_owned' src tools build.sh packaging`
Expected: no matches. The checker itself is excluded because it must contain the patterns it forbids.

- [ ] **Step 3: Push**

Only after Steps 1-2 pass:
```bash
git push origin main
```
Expected: success. Do not tag or create a release.

---

## Self-Review Notes

- Spec goals all map to Tasks 1-6; history/documentation is Task 6.
- Review Focus inputs are pinned by Tasks 1-5; no task is left without a test.
- No function names or signatures are referenced inconsistently across tasks.
- The plan is task-oriented rather than a line-by-line transcript; bodies are given only for the new static checker and the moved private-dir helper.
