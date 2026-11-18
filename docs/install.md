# Installing Hermes

[README](../README.md) · [Building](building.md) · [Migration](migration.md)

Hermes currently ships source and local package recipes, not a public apt/dnf
repository, AUR submission, or published AppImage channel. Do not use the
original File Explorer repositories expecting Hermes.

## Distribution status

| Target | Requirements / status |
| --- | --- |
| Fedora 45 | Existing `fedora-build` environment available for local verification |
| Arch Linux | Rolling-release recipe; requires an Arch build environment |
| Debian 13+ | Native build requires packaged SDL3 3.2+, libvterm 0.3+, OpenSSL 3 |
| Ubuntu 26.04+ | Same native dependency requirements; validate on the exact release |
| Older releases | No native support claim; AppImage recipe requires separate validation |

This table is a target matrix, not a statement that every package has been built
or published. Build results are reported separately. Derivatives must supply
compatible versions of all dependencies.

## Debian / Ubuntu

Install build dependencies in a suitable build environment:

```sh
sudo apt install build-essential pkgconf debhelper dpkg-dev \
  libsdl3-dev libfreetype-dev libharfbuzz-dev libfontconfig-dev \
  libarchive-dev libvterm-dev libfuse3-dev libssl-dev
sh tools/build-package.sh deb
```

The helper stages the current source and `packaging/debian/` into an isolated
directory under `build/packaging/`, then invokes `dpkg-buildpackage`. Packages
are copied into `dist/`; nothing is installed automatically.
After reviewing the result, install the exact package with `sudo apt install ./dist/<package>.deb`.

## Fedora

```sh
sudo dnf install gcc rpm-build pkgconf-pkg-config SDL3-devel freetype-devel \
  harfbuzz-devel fontconfig-devel libarchive-devel libvterm-devel fuse3-devel openssl-devel
sh tools/build-package.sh rpm
```

The helper builds `packaging/rpm/hermes.spec` with a private RPM tree under
`build/packaging/`. Review `dist/` and install the chosen binary RPM with
`sudo dnf install ./dist/<package>.rpm`. Do not install the source RPM as the app.

On immutable Fedora desktops, build inside Distrobox rather than layering
development dependencies onto the host; see [Distrobox](immutable-distros.md).

## Arch Linux

```sh
cd packaging/arch
makepkg -s
```

Review the resulting `hermes-git-*.pkg.tar.zst`, then install with `pacman -U`.
The recipe uses the local Git repository when present, or the Hermes repository
otherwise. **Git sources include committed state, not uncommitted edits.**
Use `HERMES_SRC=/absolute/checkout` to select another local repository.
No AUR publication is implied.

## Direct staged installation

```sh
./build.sh package
DESTDIR="$PWD/build/stage" PREFIX=/usr ./install.sh
```

Inspect `build/stage` first. A live installation is an explicit separate step:
`sudo PREFIX=/usr/local ./install.sh`. The installer defaults to
`build/package/hermes`, never a leftover debug build. Select another build with
`HERMES_BUILD_MODE=release` or an explicit `HERMES_BINARY=/absolute/path`.
Use a package for clean upgrades/removal.

## AppImage

`packaging/appimage/build.sh` is an opt-in recipe for a **disposable Ubuntu 22.04
build container**, bundling SDL3 and libvterm. It provisions that container,
downloads third-party sources/tools, and requires
`HERMES_APPIMAGE_PROVISION=1` plus a separately verified `LINUXDEPLOY_SHA256`.
Do not run it on the host. No AppImage compatibility or download availability
is claimed until an artifact has actually been built and tested.

## Removing and migrating

Remove Hermes through the same package manager used to install it. User data
under the Hermes XDG directories is retained. The old File Explorer package,
its repositories, and retained migration source data are not automatically
uninstalled or deleted; see [migration](migration.md).
