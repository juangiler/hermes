# Hermes Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for coordinated native execution; an isolated runtime implementation helper may work on disjoint files. No commits.

**Goal:** Rebrand and reorganize Hermes with recoverable migration and maintainable Linux packaging.

**Architecture:** Central identity definitions and a narrowly scoped migration
module support the existing unity build. Packaging/assets are disjoint from
runtime migration so they can be coordinated without concurrent file edits.

**Tech Stack:** C11, SDL3, shell build/install entry points, Python standard-library asset tools.

**Spec:** `docs/superpowers/specs/2026-11-17-hermes-identity-design.md` (approved; user requested automatic execution)

## Global Constraints

- Work in the current main checkout, preserve prior uncommitted changes, no commits/pushes/PRs.
- Display `Hermes`, binary/package `hermes`, application ID `io.github.juangiler.hermes`.
- XDG namespace `hermes`; legacy data retained, no overwrite of existing Hermes files.
- Keep the unity build, ASCII C sources, clean compiler warnings, no new runtime dependencies.
- No real home/keyring/mount migrations or live installs while developing.
- Never fetch upstream or contact upstream deployment/update infrastructure.
- Use existing fedora-build Distrobox for builds; ask before other provisioning.
- Keep third-party licences and historical upstream attribution.

## Review Focus

- Partial/repeated migration cannot resurrect deleted settings or overwrite conflicts.
- Active legacy mounts must remain service-owned and usable when handover is deferred.
- Keyring deletion must not resurrect a credential from the old namespace.
- Shared desktop configuration and original portal backups must survive migration.
- Mode-separated builds, package manifests, and installed launchers must agree.

### Task 1: Runtime identity and safe migration

**Ownership/files:** `src/` except generated `src/logo_data.h`; `tools/*selftest.c`;
new focused migration/identity test utilities. Do not edit build/packaging/docs/logo generator.

**Interfaces:**
- Introduce `src/identity.h` for `HERMES_NAME`, `HERMES_SLUG`, `HERMES_APP_ID`,
  legacy identifiers, and new/legacy environment override precedence.
- Introduce `src/migration.c` with startup ownership, fixture-testable XDG import,
  persistent import ledger, and migration status. Include it at the correct
  unity-build point before its first use.
- Runtime logo consumer uses generated `src/logo_data.h` exposing
  `g_logo_svg` and `g_logo_small_svg`; both SVGs are square.
- New executable defaults are `build/{debug,release,package}/hermes`.

- [ ] Write fixture-based regressions for rename/startup paths, XDG import,
  existing destination, idempotence/deletion, failed/partial import, unsafe
  symlinks, and exclusion of the whole cloud subtree.
- [ ] Run regressions before implementation and record the actual failure.
- [ ] Implement identity constants, new namespace writes and environment aliases.
  Rename UI/help/version/runtime/pairing/device/cache/desktop IDs coherently,
  retaining legacy strings only in compatibility/history/tests.
- [ ] Implement the approved early-lock/probe migration with per-file outcomes,
  safe permissions, retained sources, and no arbitrary user-content rewriting.
- [ ] Add keyring success/deletion tests and migrate lazily with verified writes.
  Add owned-desktop and portal-backup adoption tests; do not reset user defaults.
- [ ] Add explicit legacy/Hermes/external cloud ownership and deferred handover
  tests. Never rewrite mount references or stop/rewrite legacy services before
  accepted, operation-safe handover; preserve login state and prevent duplicates.
- [ ] Disable upstream update polling/downloads until Hermes channel is configured.
  Test that upstream binaries cannot be offered as Hermes updates.
- [ ] Update affected standalone/in-app tests without weakening prior behavior.
  Run standalone tests in isolated fixtures; do not run in-app suites concurrently.
- [ ] Write a report with commands, results, unimplemented requirements, and risks.

### Task 2: Branding assets and build/package layout

**Ownership/files:** `build.sh`, `install.sh`, `assets/`, `packaging/`,
`tools/gen_logo.py`, `src/logo_data.h`, package-version tools, `.github/`,
new build/install/branding tests. Do not edit other `src/` files.

**Interfaces:**
- Consumes Task 1 identity and square `g_logo_svg`/`g_logo_small_svg` strings.
- `./build.sh MODE` outputs `build/MODE/hermes`; demo outputs
  `build/demo/hermes-demo`. `HERMES_OUT` wins over legacy `FE_OUT`.
- `./install.sh` stages portable `build/package/hermes` by default; explicit
  `HERMES_BUILD_MODE` or `HERMES_BINARY` chooses another binary.
- `assets/branding/` is generated from one editable pixel grid using Python
  stdlib; `assets/desktop/` and `assets/udev/` hold shared install assets.
- Debian/Ubuntu recipe under `packaging/debian/`, Fedora under `packaging/rpm/`,
  Arch under `packaging/arch/`, portable bundle under `packaging/appimage/`.

- [ ] Add failing executable tests for mode-separated build selection and
  DESTDIR/PREFIX install contents using harmless fake compiler/pkg-config.
- [ ] Create deterministic gold/silver/navy pixel-art winged helmet source,
  SVG/PNG outputs, and embedded header. Verify regeneration reproducibility,
  PNG dimensions/transparency, and recognizable small-size artwork.
- [ ] Move shared assets and adapt build/install scripts with validated mode/
  output overrides; ensure no install step uses stale binaries.
- [ ] Adapt Debian/Ubuntu, RPM, Arch and AppImage recipes and package file lists.
  Retain portable flags and architecture support, remove upstream OBS deployment.
- [ ] Update version sync/check and CI/release paths and names. Disable automatic
  publication until Hermes release infrastructure is explicitly configured.
- [ ] Run syntax, reproducibility, staged install, metadata/version checks;
  build portable/release/debug in existing fedora-build. Report other distro
  build limitations instead of claiming tested support.

### Task 3: README, documentation, integration validation

**Files:** `README.md`, `MAINTENANCE.md`, `docs/*.md`, `CHANGELOG.md`,
demo/documentation tools; focused integration corrections if tests expose gaps.

**Interfaces:** Consumes Tasks 1–2 naming, migration behavior, assets, outputs.

- [ ] Rewrite README for Hermes, honest development/source installation status,
  supported/tested distro distinction and full fork attribution. Preserve MIT.
- [ ] Update all current build/install/settings/integration references, explain
  legacy migration, recovery and deferred mount handover, and show new logo.
  Label historical screenshots rather than falsely presenting new captures.
- [ ] Add architecture ownership entries and Unreleased notes; update demo and
  maintenance tooling to the actual outputs and nonpublishing release policy.
- [ ] Build debug and run all CI-listed in-app suites as non-root with isolated
  fixtures plus standalone tests; run staged install, --help/--version, ASCII,
  reproducible assets, version consistency and git diff --check.
- [ ] Request independent correctness and security review of the uncommitted
  migration/integration diff. Address important issues with regression tests.
- [ ] Produce a normal Hermes release build and report verified versus untested
  targets. Keep all work and verification evidence uncommitted.
