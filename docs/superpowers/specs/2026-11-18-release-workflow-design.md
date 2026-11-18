# Automated GitHub Release workflow design

**Date:** 2026-11-18
**Status:** proposed
**Owner decisions:** tag push `v<version>` triggers; publish a source archive plus SHA256; publish immediately (not draft).

## Context

`.github/workflows/release.yml` is currently named "Release preparation". It runs
only on `workflow_dispatch`, has `contents: read`, validates release metadata and
branding, creates `dist/hermes-<version>.tar.gz` with `tools/source-archive.sh`,
and uploads it as a workflow artifact. It never creates a GitHub Release, which is
why the README's `Last Release` badge renders
`Last Release: no releases or repo not found`.

`APP_VERSION` in `src/app.c` is the single source of truth. `tools/check-release-version.sh`
already verifies that `APP_VERSION`, the RPM spec, the Debian changelog and the
`## <version>` CHANGELOG section agree, and accepts an optional `v<version>` tag
argument. No external release credentials or package-repository promises exist.

## Goals

- Pushing a `v<version>` tag validates all checked-in release metadata against that tag.
- A GitHub Release is created and published for that exact tag with:
  - `hermes-<version>.tar.gz` from `tools/source-archive.sh`
  - `hermes-<version>.tar.gz.sha256`
- Release notes come from the matching `## <version>` section of `CHANGELOG.md`.
- The workflow uses only the built-in `GITHUB_TOKEN`; no new secrets or third-party actions.
- Documentation states exactly what triggers publication and what is published.

## Non-goals / anti-goals

- The workflow never creates or pushes tags. That remains a separate, explicitly authorized human action.
- No deb/rpm/AppImage builds. `tools/build-package.sh` needs distro tooling and
  `packaging/appimage/build.sh` needs a disposable Ubuntu container plus a verified
  `linuxdeploy` checksum; neither belongs in this first release pipeline.
- No version bump, package identity change, or out-of-repo deployment.
- No OBS, package repository, updater endpoint, or upstream File Explorer release channel.
- No manual-dispatch trigger in this change; the chosen trigger is tag push only.

## Design

### Workflow change

Rewrite `.github/workflows/release.yml`:

- `name: Release`
- `on: push: tags: ['v*']`
- `permissions: contents: read` at workflow level, with `contents: write` on the
  publishing job only.
- One job, `release`, on `ubuntu-latest`, with these steps:

1. `actions/checkout@v4` (checks out the pushed tag by default).
2. **Validate metadata and source tree:**
   `sh tools/check-release-version.sh "${GITHUB_REF_NAME}"`,
   `python3 tools/branding-selftest.py`,
   `python3 tools/build-install-selftest.py`.
   A mismatch stops the workflow before any release is created.
3. **Build source archive and checksum:**
   `sh tools/source-archive.sh` writes `dist/hermes-<version>.tar.gz`;
   `(cd dist && sha256sum "hermes-$version.tar.gz" > "hermes-$version.tar.gz.sha256")`
   produces the companion file.
4. **Extract release notes:**
   `awk` copies the `## <version>` section of `CHANGELOG.md` to `dist/release-notes.md`,
   failing if the section is empty.
5. **Publish release:** `gh release create "${GITHUB_REF_NAME}" ... --title "Hermes <version>" --notes-file dist/release-notes.md --verify-tag`
   with `GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}`. Files are the archive, checksum,
   and (implicitly) the notes file is not uploaded as an artifact.

`gh` is preinstalled on GitHub-hosted runners, so no third-party release action is
needed. `--verify-tag` prevents creating a release for a tag that is not present
in the checkout.

### Documentation and changelog

- Update `MAINTENANCE.md` "Release infrastructure" to describe tag-triggered
  publication, the artifacts, the required `contents: write` permission, and that
  the workflow does not create tags or deploy packages.
- Add an `Unreleased` CHANGELOG bullet describing the new automated release workflow.
- No README badge change: the existing `Last Release` badge starts showing the
  version after the first published release.

## Verification

- `git diff --check`.
- `sh tools/check-release-version.sh v0.0.1` locally (no tag creation).
- `python3 tools/legacy-purge-selftest.py`, `python3 tools/build-install-selftest.py`,
  `python3 tools/branding-selftest.py`.
- After merging, `gh workflow list --repo juangiler/hermes` should show `Release`.
- Not exercised here: an actual tag push/release publication, because creating a
  tag or a public release requires separate explicit authorization. Report that
  the workflow is configured but untested end-to-end.
