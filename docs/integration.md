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

### Desktop portals

Hermes reads the XDG Settings portal for the system accent colour and light/dark
preference (see [Settings](settings.md)); that is appearance only. It claims no
file-manager or file-chooser desktop role, and installers never change a portal
preference. Files dragged into other applications keep the normal
clipboard/DND targets.

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
