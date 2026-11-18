# Moving from File Explorer to Hermes

[README](../README.md) · [Install](install.md) · [Settings](settings.md)

Hermes is a fork, not an update downloaded from the original File Explorer
release channel. Back up your settings before switching, and close the old
application so it cannot change source settings while Hermes imports them.
Do not run old and new instances concurrently during migration.

## New identity

| Item | Hermes |
| --- | --- |
| Executable | `hermes` |
| Desktop ID | `io.github.juangiler.hermes.desktop` |
| Configuration | `$XDG_CONFIG_HOME/hermes` or `~/.config/hermes` |
| Data | `$XDG_DATA_HOME/hermes` or `~/.local/share/hermes` |
| State | `$XDG_STATE_HOME/hermes` or `~/.local/state/hermes` |
| Cache | `$XDG_CACHE_HOME/hermes` or `~/.cache/hermes` |
| Cloud units | `hermes-cloud-<escaped-remote>.service` |

Shared rclone configuration does not change. Hermes does not provide a
FileManager1 service or a system file chooser, and migration never claims
those roles. Use `HERMES_*` for new environment overrides;
legacy `FE_*` overrides remain accepted for compatibility, with the new name
taking precedence.

## Import and conflicts

The migration imports app-owned legacy settings and data without deleting the
original `file-explorer` directories. Existing Hermes files win: a newer Hermes
configuration must not be overwritten by an old one. Failed imports and
conflicts need attention rather than silent replacement.

Migration must be repeat-safe. Once an item has been handled, deleting its
Hermes destination should not cause the old item to reappear on every startup.
Keep original directories until you have checked saved layouts, themes, icon
overrides, folder views, recent items, server definitions and phone access.

## Cloud connections are different from ordinary data

**Never recursively copy the old `file-explorer/cloud` directory yourself
while mounts might exist there.** It may contain live remote files, not local
configuration. Hermes excludes that entire subtree from normal data import.

Legacy cloud mounts can remain at their existing paths while handover is
deferred. Their original generated services must continue owning their lifecycle;
Hermes must not start duplicate mounts or merely unmount a service that will
restart itself. Move to the new service/path only through the controlled
handover, after active operations have finished and you accept reconnection.
Bookmarks/session paths referring to a remote must not change until the new
mount has been verified.

External mounts and services you wrote yourself are not renamed automatically.
Shared rclone configuration and uploaded files are not modified by the rebrand.

## Passwords and desktop integration

Saved keyring passwords are migrated lazily on a normal connection, not exported
into plaintext files. Locked/unavailable keyrings may require unlocking or a new
login. A failed write must not delete the only working old credential.

Only the generated folder launcher and an app-generated default folder handler
are adopted; autostart and other roles are left as they are. Legacy
FileManager1 services, chooser wrappers and portal configs are never rewritten.
User-customized launchers need conservative handling; existing `.fe-backup` files
represent original settings and must not be discarded.
Installation alone does not claim desktop defaults.

Distrobox exports may include a container prefix in their desktop ID. Review
the actual exported launcher; an old `dev.fileexplorer` export is not a new
Hermes installation. Re-export only after installing the chosen Hermes binary.

## Recovery

- Do not delete legacy directories or old generated services until the new
  setup works.
- Preserve both sides of any conflict and compare them while applications are
  closed. Do not copy an entire old root over a working Hermes configuration.
- Retained old data lets you return to the old application, but changes made
  after migration are not automatically synchronized backwards.
- Do not restore stale chooser/portal backups over unrelated newer desktop settings.
- Removing old packages/repositories or changing live integrations is an
  explicit separate action, not a build step.

During development, migration tests use temporary fixtures. Building Hermes
does not migrate your actual home directory, keyring or active connections.
