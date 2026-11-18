# Hermes maintenance

## Identity and layout

- `src/app.c`: `APP_VERSION` remains the version source of truth.
- `assets/branding/helmet.pixels`: editable logo source; run `python3 tools/gen_logo.py`.
- `assets/desktop/`, `assets/udev/`: shared installation assets.
- `packaging/debian/`: Debian/Ubuntu; `packaging/rpm/`: Fedora;
  `packaging/arch/`: Arch; `packaging/appimage/`: opt-in portable bundle.
- `build/<mode>/`: generated executables; `build/packaging/`: isolated native
  package workspaces; `dist/`: generated distributable artifacts.

Keep upstream copyright, third-party licences, and historical release notes.
Do not fetch/merge upstream or rewrite Git history as part of maintenance.

## Version preparation

Do not bump the version merely to rebrand a working checkout. When a new release
is intentionally prepared:

1. Set `APP_VERSION` in `src/app.c` without a `v` prefix.
2. Add the corresponding `## <version>` changelog section.
3. Run `sh tools/sync-package-versions.sh`.
4. Run `sh tools/check-release-version.sh v<version>`.
5. Review RPM/Debian metadata, build the intended packages, and run the tests.

The Debian packaging revision is preserved when already valid for that version.
The generated `.dsc` belongs to package tooling, not checked-in OBS metadata.

## Build validation

```sh
python3 tools/gen_logo.py --check
python3 tools/branding-selftest.py
python3 tools/build-install-selftest.py
./build.sh debug
# Run the in-app suites listed in docs/building.md.
./build.sh package
DESTDIR="$PWD/build/stage" PREFIX=/usr ./install.sh
git diff --check
```

Record exact distro/dependency versions and packages actually built. Never
claim Debian/Ubuntu/Arch validation based solely on a Fedora build.

## Commit-series history

The five Hermes rebrand commits (`a97f229`..`0e7e892`) were split by concern
without rewriting the series. Every commit builds, but the first two snapshots
(`a97f229`, `2af4d2d`) are not fully test-green on their own: a few self-test
expectations were committed together with the later integration/docs changes
that implement them. From `d834b03` onward — including current `main` — the
full headless battery passes. This only matters when checking out those two old
snapshots (for example during `git bisect`); there is no runtime defect.
Rewriting the series would require a force-push and is deliberately avoided.

## Release infrastructure

Pushing a `v<version>` tag triggers the `Release` workflow. It validates the
checked-in release metadata against that tag, then publishes a GitHub Release
containing `hermes-<version>.tar.gz` and its SHA256 checksum. Release notes are
taken from that version's section of `CHANGELOG.md`.

The workflow uses only the built-in `GITHUB_TOKEN`, with `contents: write`
granted to the publishing job alone. It does not create or push tags, build or
publish packages, deploy anywhere, or contact the original maintainer's OBS
project. There are no inherited release credentials or repository promises.

Creating and pushing the tag, and the resulting release publication, remain
separately authorized human actions. Configure and review a Hermes-owned release
channel before enabling automatic application updates. Upstream File Explorer
binaries must never be installed by Hermes's updater.
