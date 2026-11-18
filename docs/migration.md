# Legacy File Explorer support (removed)

[README](../README.md) · [Install](install.md) · [Settings](settings.md)

Early Hermes builds included a one-time importer for File Explorer settings and
data, legacy `FE_*` environment aliases, lazy keyring handover, deferred cloud
units, and generated-launcher adoption. That compatibility layer has been
**removed**. The startup importer could prevent the app from opening when a stale
lock file or another instance was present, so the gate and importer were deleted
rather than repaired.

Current behavior:

- Hermes reads and writes only its own `hermes` XDG directories and uses
  `HERMES_*` environment overrides. Legacy names have no effect.
- Existing `file-explorer` directories, keyring entries, cloud units and
  launchers are left untouched; Hermes does not import, adopt or defer to them.
- Cloud units with a legacy header are treated as external.
- Saved credentials live only under the Hermes keyring application id.
- No FileManager1 or file-chooser role is claimed.

There is nothing to run to "complete a migration". To reuse data from the old
application, copy it yourself while both applications are closed.

This page is kept for history. The change is recorded under Unreleased in
[CHANGELOG.md](../CHANGELOG.md).
