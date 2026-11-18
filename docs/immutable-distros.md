# Building Hermes with Distrobox

On immutable Fedora desktops, keep development dependencies inside a build
container rather than installing them on the host. Creating/provisioning a
container and exporting launchers are separate user-controlled actions.

## Existing Fedora builder

If `fedora-build` already has the dependencies from [installation](install.md):

```sh
distrobox enter fedora-build -- sh -c 'cd "$1" && ./build.sh debug' sh "$PWD"
distrobox enter fedora-build -- sh -c 'cd "$1" && ./build/debug/hermes --selftest-m10' sh "$PWD"
```

In containers that restrict ptrace, prefix the test command with
`ASAN_OPTIONS=detect_leaks=0`; see [test limitations](building.md#headless-self-tests).

## Stage first, install/export explicitly

Inside the builder:

```sh
./build.sh package
DESTDIR="$PWD/build/stage" PREFIX=/usr/local ./install.sh
```

Inspect the staged files. Only when you choose a live container installation:

```sh
sudo PREFIX=/usr/local ./install.sh
distrobox-export --app /usr/local/share/applications/io.github.juangiler.hermes.desktop
```

Distrobox gives exported launchers container-specific IDs. Hermes's application
icon handling recognizes the exported ID as well as the native desktop entry.
Exporting affects the host's user applications directory; it is not part of
ordinary compilation or self-tests.

Do not export a stale File Explorer binary expecting it to become Hermes.
For an existing exported launcher, preserve user customization and follow the
[migration guidance](migration.md) before removing/replacing it.
