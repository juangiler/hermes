<div align="center">

<img src="assets/branding/hermes.svg" width="112" alt="Hermes pixel-art winged helmet">

# Hermes

**A fast, keyboard-first file manager for Linux.**

Split panels, cloud folders, an integrated terminal, and a GPU-drawn interface.

[![CI](https://github.com/juangiler/hermes/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/juangiler/hermes/actions/workflows/ci.yml)
[![Build](https://github.com/juangiler/hermes/actions/workflows/ci.yml/badge.svg?branch=main&job=build)](https://github.com/juangiler/hermes/actions/workflows/ci.yml?query=branch%3Amain)
[![Built with OpenCode](https://img.shields.io/badge/Built%20with-OpenCode-7aa2f7)](https://opencode.ai)

[![License: MIT](https://img.shields.io/badge/license-MIT-9ece6a)](LICENSE)
![Linux](https://img.shields.io/badge/Linux-Wayland%20%7C%20X11-bb9af7)
![C11 + SDL3](https://img.shields.io/badge/C11-SDL3-e0af68)

</div>

Hermes is **forked from and inspired by
[File Explorer](https://github.com/AlexMedela/file-explorer)** by AlexMedela
(amedela). It preserves the original project's MIT licence and attribution,
while developing its own identity and desktop experience.

## Features

- **Keyboard-first navigation:** jump with `Ctrl+P`, filter with `Ctrl+F`, and rebind shortcuts.
- **Panels and tabs:** split the workspace, move tabs, and save layouts.
- **Integrated terminal:** a folder-following panel (`F4`) or terminal tab (`Shift+F4`).
- **Files and previews:** list/grid views, thumbnails, archives as folders, and batch rename.
- **Search and analysis:** recursive/content search, disk usage maps, folder compare and sync.
- **Git integration:** repository badges, branches, and changed-file indicators.
- **Devices and connections:** removable drives, phones, cameras, SMB, SFTP, NFS, and WebDAV.
- **Separate Network and Cloud:** native connections stay in Network; rclone remotes, including WebDAV, live in Cloud.
- **Appearance:** themes, installed/custom file icons, folder badges, and configurable headers.

## Built for KDE Plasma

Hermes is developed **for KDE Plasma 6.x first** — the 6.6, 6.7 and 6.8
series. It fits Plasma's keyboard-driven workflow and speaks the desktop's
integration points directly: freedesktop icon themes, the XDG Settings
portal for the system accent colour, autostart and systemd user services for
cloud mounts. It does not claim the FileManager1 role or provide a system
file chooser. It runs natively on Wayland and X11. Other desktops work through
the same open standards, but they are not a development focus.

### Compatibility status

"Verified" means it actually ran here; "target" means it is intended but not
yet run. Headless self-tests render offscreen and do **not** by themselves
prove live desktop behavior.

| Environment | Status |
| --- | --- |
| Fedora 45, Plasma 6.8.3 (Wayland) | **Verified (build + headless tests):** debug/release/package builds and all 15 self-test suites pass on this development host. Live Plasma session checks are still pending. |
| Plasma 6.6 / 6.7 / 6.8 | **Target.** The primary desktop; manual session verification is planned, not yet recorded. |
| Ubuntu / Debian and Arch Linux | **Target.** Recipes exist under `packaging/`; only the Fedora spec is parse-checked here, .deb and `makepkg` builds are untested. |
| AppImage | **Target.** Opt-in container recipe (`packaging/appimage/`), not built by default. |
| GNOME and other desktops | **Not a focus.** Standards-based integration only, no active testing. |

## Build and run

This fork is in development. **There is no configured public Hermes package
repository or automatic-update channel.** Upstream File Explorer packages are
not Hermes packages.

From a checkout with the [build dependencies](docs/building.md) installed:

```sh
./build.sh
./build/release/hermes

./build.sh debug       # build/debug/hermes, ASan + UBSan
./build.sh package     # build/package/hermes, portable CPU flags
```

| Distribution | Packaging path |
| --- | --- |
| Ubuntu / Debian | Shared debhelper recipe: `packaging/debian/` |
| Fedora | RPM spec: `packaging/rpm/hermes.spec` |
| Arch Linux | Development package: `packaging/arch/PKGBUILD` |
| Portable bundle | Opt-in AppImage recipe: `packaging/appimage/` |

See [installation](docs/install.md) for local builds, package commands, and
distribution/version limits. Package recipes are not a claim of official
distribution inclusion. Installation does not make Hermes your default manager
or file chooser.

## Moving from File Explorer

Hermes uses its own `hermes` configuration/data namespace and
`io.github.juangiler.hermes` desktop identity. There is no automatic migration:
existing File Explorer settings, data and desktop roles are left untouched.
See [legacy support (removed)](docs/migration.md) for the history.

## First steps

| Shortcut | Action |
| --- | --- |
| `Ctrl+P` | Jump to a folder or file |
| `Ctrl+T` / `Ctrl+W` | New tab / close tab |
| `Ctrl+\` | Split the panel |
| `Space` | Preview panel |
| `F4` / `Shift+F4` | Terminal panel / terminal tab |
| `Ctrl+Shift+U` | Disk usage map |
| `Ctrl+,` | Options |

## Documentation

[Install](docs/install.md) · [Build and test](docs/building.md) ·
[Legacy support (removed)](docs/migration.md) · [Keys](docs/keys.md) ·
[Settings](docs/settings.md) · [Integration](docs/integration.md) ·
[Custom icons](docs/custom-icons.md) · [Architecture](docs/architecture.md) ·
[Maintenance](MAINTENANCE.md)

Historical screenshots and demo recordings inherited from File Explorer may
still show its former name; they are not evidence of a newly captured Hermes release.

## Licence and credits

[MIT](LICENSE). Original copyright notices are retained. Bundled libraries and
file icon themes keep their respective licences; see
[third-party credits](docs/building.md#third-party-credits).
The pixel-art winged helmet is Hermes's application logo, separate from file
and folder icon themes.
