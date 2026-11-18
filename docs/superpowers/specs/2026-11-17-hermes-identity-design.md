> **Superseded:** this design still describes FileManager1, xdg-desktop-portal
> and a picker app id; those roles and the `--pick` window were removed by
> `2026-11-18-portal-removal-default-fm-and-webdav-icon-design.md`. Only the
> launcher and default-folder-handler adoption remain.

# Hermes identity, migration, branding, and distribution layout

## Intent and user decisions

Hermes is the independent fork maintained at `juangiler/hermes`. The user
requested a README/naming/build-layout revamp for Ubuntu, Debian, Fedora, and
Arch, a pixel-art Hermes helmet logo, and attribution to the original File
Explorer project. The user explicitly chose to migrate everything, including
existing settings and data.

Work remains in this checkout on main, uncommitted, per the existing user
instruction. Preserve the already implemented Network/Cloud separation and all
other existing changes. No upstream fetch, publishing, package installation on
the host, default-handler changes, or live service changes are authorized by
this design.

## Approaches

1. **Cosmetic rebrand:** lowest risk, but leaves old executable, configuration,
   package, and integration identities. Rejected by the user's full-migration
   choice.
2. **Blind global rename and move:** simple superficially, but loses settings
   when destinations exist, breaks mounted paths and keyring lookups, and may
   retarget the updater incorrectly. Not acceptable.
3. **Complete identity change with a staged, recoverable compatibility
   migration:** recommended implementation approach. New writes and new
   packages use Hermes identities; old identifiers remain only where necessary
   for migration, historical attribution, third-party notices, and tests.

## Identity

Implementation choices proposed for approval:

| Surface | New identity |
| --- | --- |
| Display name | `Hermes` |
| Executable / stable distro package | `hermes` |
| Arch development package | `hermes-git`, providing `hermes` |
| Desktop/application/polkit ID | `io.github.juangiler.hermes` |
| Picker application ID | `io.github.juangiler.hermes.picker` |
| XDG config/data/cache/state subdirectory | `hermes` |
| Runtime namespace | `hermes`, with per-user private fallback |
| Generated cloud services | `hermes-cloud-<escaped-remote>.service` |
| Environment overrides | `HERMES_*`, with documented legacy `FE_*` aliases during transition |
| Repository/release identity | `juangiler/hermes` |

Centralize shared identity constants and path definitions instead of repeating
new strings across modules. Keep the existing C11 unity build and include
ordering, without introducing CMake or restructuring unrelated application code.
Do not reset APP_VERSION or invent a published Hermes release.

## Migration behavior

### Persistent files

- Run a narrowly scoped migration before reading normal settings. Cover config,
  themes/icons, layouts, folder views, saved servers, recent folders/apps,
  application icons, cached thumbnails/device data, and phone pairing records
  stored in the old app-specific XDG roots.
- Copy through private staging and commit atomically where possible. Preserve
  source data for recovery; do not delete original directories automatically.
  Cache may be discarded/rebuilt only when copying it is unsafe or impossible,
  with a clear report.
- Existing Hermes files take precedence. Import nonconflicting legacy files;
  report conflicts rather than overwriting. Migration is idempotent and
  resumable after failure, and must not mark incomplete work successful.
- Keep a durable per-item ledger of imported, conflict-skipped, failed, and
  deliberately excluded items. Do not later reimport a completed/skipped item
  simply because the user deleted its destination. Retry failures explicitly.
  Rollback may remove/restore only unchanged migration-owned outputs.
- Respect absolute XDG overrides and the existing documented fallback behavior.
  Use private permissions for sensitive data; never follow copied symlinks into
  external locations or write through unsafe destination symlinks. Preserve
  permitted symlink objects without recursively traversing them.
- Remap known application-owned path fields (for example theme paths and cloud
  mount bookmarks/session paths) with path-component boundaries. Do not rewrite
  arbitrary user content, external paths, shared rclone configuration, or shell
  configuration.
- Cloud path references remain at legacy locations while handover is deferred;
  remap each remote's references only after its new mount is verified.
- Development verification uses fixtures, never the user's real config,
  keyring, pairing records, mountpoints, or home migration.

### Runtime and active mounts

- Acquire an early startup/migration lock and probe legacy listeners before
  importing mutable files or loading settings. A socket pathname does not
  establish a live instance. Never unlink the legacy socket; validate type and
  ownership before cleaning a stale Hermes socket.

- New IPC sockets, shell helper directories, device mount roots, and temporary
  files use the Hermes namespace. Stale runtime files are not copied.
- Detect legacy running instances/owned mounts and explain any necessary
  shutdown/reconnection rather than silently running duplicate owners.
- Never copy/move the contents of a live FUSE mount as if it were persistent
  data. Treat legacy cloud mountpoints as a separate handover problem.
- Exclude the entire legacy cloud subtree from generic data copying, including
  apparently unmounted directories: a service can mount there during a copy.
- Inventory app-generated legacy systemd services and enabled state. Offer a
  controlled handover to new Hermes units, retaining connect-at-login and saved
  remotes. Stopping/unmounting live services requires explicit in-app consent
  and must wait for/reject active operations. Do not edit arbitrary user units.
- If the user defers handover, display existing legacy mounts as connected,
  suppress duplicates, and do not start a second mount for the same remote.
  Remove/disable old generated integration only after the replacement is
  verified and the handover was accepted.
- Track mount lifecycle ownership as legacy-generated, Hermes-generated, or
  external. A legacy service must be stopped through that service, not by direct
  FUSE unmount while its restart policy remains active. Do not apply background
  rewrite/orphan cleanup to legacy units before consent.

### Desktop integration and credentials

- Update launcher/icon/WMClass, Distrobox-exported IDs, autostart, FileManager1
  activation, picker wrapper, and polkit actions coherently.
- Migrate only identifiable app-generated user files. Preserve custom desktop
  actions and unrelated MIME/portal settings. Restore enabled integrations under
  Hermes only if they belonged to the legacy app; do not claim defaults the
  user had not enabled.
- Adopt existing portal backup provenance (`.fe-backup`) without running normal
  enable/disable routines that overwrite or restore shared settings. Recognize
  generated FileManager1 activation content, not just its standard filename.
- Keep standard interoperability names such as `org.freedesktop.FileManager1`
  unchanged: these are protocols, not application branding.
- Look up keyring entries under Hermes first. On a normal connection, allow a
  legacy-key lookup and verified write to the new namespace. Preserve the old
  credential until the new record is confirmed; do not enumerate/export secrets
  or log them. Handle credential deletion consistently across both namespaces
  during migration. Report locked/unavailable keyrings without data loss.
- Secret store/clear helpers must return actionable success status. Verify the
  new entry privately and preserve deletion intent when either namespace is
  unavailable, so a deleted credential cannot reappear via legacy fallback.
- Migrating source code is not permission to install launchers, reload host
  services, or migrate the current user's real environment during development.

## Build and package layout

Proposed layout:

```text
assets/
  branding/                 # editable pixel grid, generated logo SVG/PNGs
  desktop/                  # launcher and polkit templates
  udev/                     # device permissions rule
packaging/
  debian/                   # shared Debian/Ubuntu package recipe
  rpm/                      # Fedora RPM spec
  arch/                     # PKGBUILD
  appimage/                 # portable bundle recipe
build/
  debug/hermes
  release/hermes
  package/hermes
  demo/hermes-demo
dist/                       # generated distribution artifacts only
```

- Retain `./build.sh` and `./install.sh` as simple entry points; mode-specific
  outputs stop debug/release/package builds overwriting each other.
- Install/package scripts consume the chosen build explicitly. Preserve
  `DESTDIR`, `PREFIX`, compiler/linker flags, and output overrides through a
  documented compatibility transition.
- Keep Debian/Ubuntu on one recipe, rather than duplicating it per distro.
  Respect distro packaging directory conventions inside each package area.
- Adapt version-sync/check scripts and CI to the new layout. Remove active
  dependence on the upstream maintainer's OBS project and release credentials;
  do not pretend a Hermes apt/dnf repository exists.
- Validate builds on all four distributions with a documented version matrix.
  SDL3 availability and the existing SDL/libvterm/OpenSSL minimums limit native
  packages; unsupported distro versions must not be advertised as supported.
- Fedora verification can use the existing `fedora-build` Distrobox. Ask before
  creating/provisioning additional distro environments or installing packages.
  Source-only checks are not proof a package builds on an untested distro.

## Logo

- A recognizable pixel-art winged Hermes helmet, transparent background,
  crisp square pixel geometry, no lettering or unrelated folder symbol.
- Proposed palette: gold helmet, pale silver wings, dark navy outline/shading.
  This is the implementer's visual proposal, not a user-selected palette.
- One editable pixel-grid source and deterministic generator produce the
  launcher SVG/PNGs and embedded application artwork. Small-size treatment must
  stay readable at 16/24/32 px; larger assets remain crisp.
- Replace README, launcher, window, tray, and in-app default logo consistently.
  User-selected custom application icons and file/folder icon themes stay
  unchanged. Test custom icon reset and desktop export paths.
- Present the logo for visual approval before making it the final default.

## README, attribution, and releases

- Introduce Hermes clearly, with current features, build/run/test instructions,
  the four distro paths, development status, and links to maintained docs.
- Credit: “Hermes is forked from and inspired by
  [File Explorer](https://github.com/AlexMedela/file-explorer).”
- Preserve original MIT copyright and third-party licences. Historical release
  and contribution credits are not rewritten into new authorship.
- Replace misleading upstream CI/release/install badges and download links.
  Clearly label or replace screenshots/video that still show upstream branding;
  do not claim they represent a newly captured Hermes build.
- Update checks must never offer an upstream File Explorer binary as a Hermes
  update. Disable automatic update checks/downloads until a Hermes release
  channel is configured and validated. Public distribution is not implied for
  the currently private repository.
- Prepare coherent release/build automation but do not tag, publish, push, or
  contact upstream deployment infrastructure.

## Validation and completion criteria

- Regression tests first for migration conflicts, repeat runs, interrupted
  imports, permissions/symlinks, XDG paths, owned-integration detection, keyring
  transition, legacy service handover/defer, and prevention of duplicate mounts.
- Update and run standalone icon/desktop tests, version checks, staged installs,
  ASCII checks, shell syntax checks, and package metadata/file-list validation.
- Run the existing complete headless in-app suite after the rename, retaining
  the Cloud/Network regressions. Keep self-tests isolated from real settings.
- Check normal startup, --version/--help, custom icon behavior, and logo
  visibility at small sizes in a built Hermes binary.
- Review security-sensitive migration paths independently. Report packages
  actually built versus recipes only inspected, plus any missing platforms.
- No new dependencies/build framework unless justified and approved. No
  commits, deployments, live installs, or destructive cleanup during this task.

## Risks and rollback

Identity collisions, package ownership changes, live mount paths, keyring
availability, exported Distrobox launchers, and partial XDG imports need explicit
tests. Rollback uses retained original files and recorded integration handover
state; it must not overwrite subsequently changed user settings.
Old package removal and live host installation are separate approved actions.

## Builder check

A read-only architecture review found no additional product decision needed.
Verified source confirmed the important implementation constraints above:
settings currently load before IPC acquisition, cloud ownership currently
depends on the mount path, credential write/delete helpers discard status, and
portal integration has an existing backup chain. The sequence is identity and
build changes, fixture-tested startup/file migration, credential/desktop
compatibility, and separately consented cloud handover. No live data was read.
