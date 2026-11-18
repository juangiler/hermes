# Integration with the desktop

[README](../README.md) · [Install](install.md) · [Keys](keys.md) · [Settings](settings.md) · [Integration](integration.md) · [Building](building.md) · [Architecture](architecture.md)

Installing Hermes never takes over a role from your current file manager, and packages never touch your
settings or your default apps: no install script runs `xdg-mime` or writes to `~/.config`.
Each of these is your choice.

## Options > Integration
One switch, showing who has the role now. Turning it off restores the previous handler.

| Switch | What it does |
|---|---|
| **Default file manager** | Sets `inode/directory` in your `~/.config/mimeapps.list` (and in the desktop's own `<desktop>-mimeapps.list` when that one names folders), remembering the previous app to restore later. |

On the first start, Hermes asks once whether it should be the default file manager.
No existing desktop integration is adopted or rewritten; files created by other
applications are left as they are.

### Desktop portals (headless plumbing)

The portal work landed so far is plumbing, not a user-visible picker. The
`hermes_core` C ABI (`src/core_api.h`) accepts inbound `file://` URIs and batch
opens, and the `hermes-portals` C++ library has async clients for the XDG
Settings portal (`ReadOne`), OpenURI (`OpenURI`, outgoing `file://` URIs) and
the Documents portal (`Add`). Invoked with no flags, the long-lived
`hermes-portal-backend` registers two headless services:

| Service | Current behaviour |
|---|---|
| `org.freedesktop.FileManager1.ShowItems` | Decodes the URIs and hands the paths to the core host hook, emits `itemsRequested`, then returns `org.freedesktop.portal.Error.Cancelled` because no picker is attached. |
| `org.freedesktop.impl.portal.FileChooser.OpenFile` / `SaveFile` on `org.freedesktop.impl.portal.desktop.hermes` | Records the request for the headless tests, then returns `response=1` (Cancelled; `2` means the interaction ended in some other way) immediately; no UI is shown. |

Taking the file-chooser role is opt-in and happens only when you ask for it.
These are flags to `hermes-portal-backend`; each edits the config and exits
instead of starting the long-lived services:

| Command | What it does |
|---|---|
| `--enable-filechooser` | Writes only `org.freedesktop.impl.portal.FileChooser=hermes` under `[preferred]` in `$XDG_CONFIG_HOME/xdg-desktop-portal/portals.conf`, keeping unrelated bytes unchanged and backing up the original to `portals.conf.hermes-backup`. |
| `--disable-filechooser` | Restores that backup byte-for-byte when the config is unchanged since opt-in; if it has since been edited, removes only the Hermes key (restoring a previous value) so unrelated edits survive. Removes the backup in both cases. |
| `--status` | Prints whether the Hermes file-chooser role is enabled, without changing anything. |

Installers and the default configuration never take over a desktop role: Hermes
writes a portal preference only through these flags, and they touch only your
XDG config. The XDG Settings portal is still read for the system accent colour
(see [Settings](settings.md)); that is appearance only, not a file-management
integration.

The picker UI, a documents-portal drag target and live portal arbitration on
KDE Plasma 6.x arrive in a later step of the port. Until then headless tests
prove D-Bus plumbing only — live Plasma behaviour is target-only, not verified.
Files dragged into other applications keep the normal clipboard/DND targets.

The same by hand:
```sh
xdg-mime default io.github.juangiler.hermes.desktop inode/directory     # default file manager
```

## Phones and cameras
iPhones / iPads and Android phones / cameras on a cable use the built-in connector (no GVfs needed). The package
installs a udev rule (`70-hermes-phones.rules`) that gives the user at the seat access to them; built from
source, `install.sh` puts it in place. GVfs (`gvfs-mtp`, `gvfs-gphoto2`) is used as a fallback when installed.

## Administrator actions
"Retry as administrator" runs a small helper through `pkexec`, so it needs polkit and an authentication agent.
Full desktops start one; on tiling compositors start e.g. `hyprpolkitagent` or `polkit-gnome`.

### Local shared folders

Configured local share roots keep their base folder icon with protocol-specific badges in folder lists, grids,
path headers, sidebar bookmarks, and previews. SMB uses `folder-remote-smb`,
NFS uses `folder-remote-nfs`, and WebDAV/other shares use `folder-publicshare`,
as small overlays at the lower right. Custom themes can supply SVGs with these names (see
[Settings](settings.md)). Children keep their normal icons
unless they are also shared explicitly. Detection reads these local files without
running commands or requesting administrator access, and caches results for ten
seconds:

- SMB: `/etc/samba/smb.conf` (including absolute `include` files) and
  `/var/lib/samba/usershares`, plus a configured `usershare path`.
- NFS: `/etc/exports`, `/etc/exports.d/*.exports`, and `/var/lib/nfs/etab`
  when readable.
- WebDAV: explicit Apache `<Directory /absolute/path>` blocks containing
  `Dav On` in `/etc/httpd/conf/httpd.conf`, `/etc/httpd/conf.d/*.conf`,
  `/etc/apache2/sites-enabled/*.conf`, and `/etc/apache2/conf-enabled/*.conf`.

When running in Distrobox, detection also reads the host configuration under
`/run/host`, including host Samba include files.

These icons indicate configuration, not live server availability. Unreadable
files are skipped. Samba variable substitutions, registry-backed shares,
nonstandard include layouts, and WebDAV configurations for other servers are
not detected. Paths are matched directly, with a fallback for parent-directory
symbolic links when the folder name matches a configured share. No remote
network requests are made.

The parser regression checks run headlessly:

```sh
cc -std=gnu11 -Wno-unused-function tools/local-shares-selftest.c -o /tmp/fe-local-shares-test -pthread
/tmp/fe-local-shares-test
```

### SVG previews and host applications

SVG files render directly in grid thumbnails and the preview panel, without an
external thumbnailer. Rendering preserves transparency and aspect ratio, with an
8 MiB source limit and a bounded output size. The built-in renderer supports SVG
paths, shapes, and gradients; advanced features such as text, filters, embedded
images, and animation may not render fully. SVGZ still uses installed thumbnailers.

When running through Distrobox, Open with also scans native host applications and
host Flatpak exports under `/run/host`. Host-only handlers launch through
`distrobox-host-exec` or `host-spawn`; they are added only when a host launcher is
available. This allows SVG handlers such as Inkscape installed on the host to be
offered even when their executables are absent from the container.
