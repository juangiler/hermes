# Purge File Explorer legacy compatibility

**Date:** 2026-11-18
**Status:** proposed
**Owner decision:** full purge, with the history recorded in docs

## Intent and why

Early Hermes builds carried a one-time importer for File Explorer settings/data,
legacy `FE_*` environment aliases, lazy keyring migration, deferred cloud-unit
handover, and generated-launcher adoption. The importer runs before settings and
before SDL creates a window; any permissions problem, symlink, or stale lock in
`~/.local/state/hermes` makes Hermes exit with status 1 and never open. The user
has decided this compatibility layer is unnecessary maintenance and the direct
cause of startup failures, and wants all of it removed.

## Goals

- Hermes never reads, imports, adopts, or defers to File Explorer config, data,
  cache, state, keyring entries, cloud units, or desktop launchers.
- There is no startup migration gate: the app always reaches `settings_load()`
  and window creation.
- All runtime overrides are `HERMES_*` only. No `FE_*` aliases remain in code,
  tests, build scripts, packaging recipes, or current docs.
- Current docs describe only Hermes-native behavior. Historical design/plan docs
  and the changelog explicitly record that early Hermes had legacy support and
  that it was later purged.
- Tests and packaging checks are updated for the Hermes-only world.

## Non-goals / anti-goals

- Do not delete, rewrite, or migrate anything in a user's existing
  `file-explorer` directories, keyring, or systemd units. Legacy data is left
  untouched and is simply ignored.
- Do not change `APP_VERSION`, package names/IDs, or release tags.
- Do not rewrite Git history or the upstream attribution in `LICENSE` / README.
- Do not remove `settings_migrate()` in `src/settings.c`; it upgrades Hermes'
  own settings files and is unrelated to File Explorer.
- Do not claim full build/self-test success unless the host dependencies allow
  it; report exactly what was run.

## Constraints

- Unity build: `src/main.c` compiles everything; a `static` name must be unique
  across all `src/*.c`, and include order matters.
- Source must stay pure ASCII; warnings fail CI.
- Self-tests must not touch the user's real config (`XDG_CONFIG_HOME`,
  `XDG_STATE_HOME`, IPC socket overrides).
- Host build dependencies are currently incomplete; ask before installing
  packages or provisioning a container.

## Approaches considered

1. **Targeted removal (rejected):** delete only the startup gate and importer.
   Smallest fix, but leaves `FE_*` aliases, keyring/cloud/launcher handover, and
   their maintenance cost in place.
2. **Full purge (chosen):** remove every File Explorer compatibility path,
   rename all overrides to `HERMES_*`, and rewrite docs/tests. Larger diff, but
   matches the user's decision and leaves one naming scheme to maintain.

## Design

### Code removal

- Delete `src/migration.c` and its `#include` from `src/main.c`.
- In `src/app.c`, remove the `migration_startup()` / `migration_finish()` gate
  and the error message; keep `settings_load()` and the rest of startup.
- Move the symlink-safe private-directory helper (currently
  `migration_private_dir()`) into `src/credentials.c` as `secret_private_dir()`;
  keep the `O_NOFOLLOW`, uid and mode checks so deletion markers stay safe.
- Delete `src/integration_identity.c`, its `#include` from `src/integration.c`,
  `ig_identity_adopt()`, and its call in `src/app.c`. No launcher or default-FM
  role is adopted at startup.
- In `src/cloud_identity.c`, drop `CLOUD_LEGACY`, the `file-explorer-cloud-…`
  unit name and the legacy header check. Keep `CLOUD_HERMES` /
  `CLOUD_EXTERNAL` provenance based on the Hermes header only.
- In `src/rclone.c`, remove `cloud_legacy_owned()`, all three "legacy cloud
  service retained" early returns, the legacy cloud base path in
  `path_in_cloud()`, and set ownership solely from our own units.
- In `src/identity.h`, remove `HERMES_LEGACY_SLUG` and
  `HERMES_LEGACY_APP_ID`. `identity_env()` becomes a plain `getenv()` wrapper;
  every call site is renamed from `FE_*` to `HERMES_*`. `identity_xdg()` stays
  as the generic XDG path helper used by credentials.
- Rename compile-time `FE_ADDON` / `FE_DEMO` macros to `HERMES_ADDON` /
  `HERMES_DEMO` in `build.sh`, `src/addons.c`, `src/demo.c`, and `src/app.c`.
- `src/ipc.c`: `HERMES_IPC_SOCKET` only; thread name `hermes-ipc`.
- `build.sh`: keep only `HERMES_ADDON_SRC` and `HERMES_OUT`.
- `packaging/arch/PKGBUILD`: keep only `HERMES_SRC`.

### Tests

- Delete `tools/identity-selftest.c` and `tools/integration-identity-selftest.c`
  (they test only removed behavior).
- Rewrite `tools/credentials-selftest.c` for the Hermes-only namespace and the
  local private-dir helper; no legacy secret fixture.
- Rewrite `tools/cloud-identity-selftest.c` for Hermes unit naming/provenance.
- Update `src/selftest.c`: remove legacy cloud ownership assertions and legacy
  launcher/adoption assertions; rename `FE_*` overrides to `HERMES_*`.
- Update `tools/build-install-selftest.py`: drop the `FE_OUT` alias test and
  `FE_` from the environment strip list; keep Hermes-only assertions.
- Update `docs/building.md` standalone-check list to match the surviving tools.
- Add a regression assertion that a legacy `file-explorer` directory is ignored
  (no import). Do not set or name any `FE_*` variable in tests; if an override
  test is needed, assert that `HERMES_*` works and that unrelated variables have
  no effect. No active code, test, build script, or packaging recipe may contain
  an `FE_*` identifier.

### Documentation

- Replace `docs/migration.md` with a short historical page titled "Legacy File
  Explorer support (removed)": early Hermes had a one-time importer and legacy
  compatibility; it was fully purged because the startup gate could prevent the
  app from opening; current behavior is Hermes-only and legacy files are left
  untouched. Keep the filename so installed docs retain the history page.
- Remove migration/adoption claims and dead `docs/migration.md` links from
  `README.md`, `docs/install.md`, `docs/integration.md`,
  `docs/immutable-distros.md`, `docs/building.md`, `docs/architecture.md`, and
  `docs/settings.md`; replace with a short "Hermes uses only `hermes` paths and
  `HERMES_*` overrides" statement where needed.
- Add an `## Unreleased` changelog entry stating that early Hermes builds had
  File Explorer migration/compatibility and that it was fully removed; list the
  behavioral consequence (legacy data ignored, no startup gate, `HERMES_*`
  only).
- Add a top banner to historical `docs/superpowers/specs/2026-11-17-hermes-identity-design.md`,
  `docs/superpowers/plans/2026-11-17-hermes-identity.md`, the 2026-11-18 portal
  design/plan, and any other superpowers doc that describes migration:
  "Historical design: implemented in early Hermes, later fully purged; see
  CHANGELOG. Do not treat as current behavior." Do not rewrite their bodies.
- Update the gitignored `docs/checkpoint.md` user-decision note to record the
  purge.

## Verification

- Run the standalone Python checks (`tools/build-install-selftest.py`,
  `tools/branding-selftest.py`) and the surviving C standalone suites.
- Grep for `FE_` and `HERMES_LEGACY`: zero matches in active `src/`, `tools/`,
  `build.sh`, and packaging recipes. Historical design docs, the changelog, and
  `docs/migration.md` may mention them only as removed names.
- Grep for `file-explorer` and `migration`: only history/attribution notes,
  "old name is absent" assertions, and asset-name fixtures may remain.
- If host dependencies permit, rebuild with `./build.sh package` and run the
  binary with a fresh `XDG_STATE_HOME` to confirm it gets past startup with no
  migration error. Do not install system-wide without explicit permission.
- Report verified vs target-only (headless runs do not prove live Plasma).

## Decision log

- 2026-11-18: user chose full purge over targeted removal.
- 2026-11-18: no `FE_*` identifiers in active code, tests, build scripts, or
  packaging recipes; keep only a legacy-directory negative regression and
  Hermes-only override tests.
- 2026-11-18: historical design docs and the changelog record that early Hermes
  had File Explorer compatibility and that it was later purged.

## Left to the builder

Mechanical renaming and exact line-level edits are implementation details. Use
existing style: 2-space indent, snake_case, short static helpers, ASCII-only
source, no unrelated refactors.
