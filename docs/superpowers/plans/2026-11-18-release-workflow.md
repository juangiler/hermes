# Release Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a pushed `v<version>` tag publish a GitHub Release with the Hermes source archive and checksum.

**Architecture:** Rewrite the existing `.github/workflows/release.yml` from manual preparation into a tag-triggered publishing workflow. No new scripts or third-party actions: reuse `tools/check-release-version.sh`, `tools/source-archive.sh`, `tools/branding-selftest.py`, `tools/build-install-selftest.py`, `awk`, `sha256sum` and the runner's preinstalled `gh` CLI.

**Tech Stack:** GitHub Actions, `gh release create`, POSIX shell, `GITHUB_TOKEN`.

**Spec:** `docs/superpowers/specs/2026-11-18-release-workflow-design.md`

## Global Constraints

- Trigger is `push` on tags matching `v*` only; no `workflow_dispatch`.
- The workflow never creates or pushes tags, and never deploys packages.
- `permissions: contents: read` at workflow level; `contents: write` only on the publishing job.
- `GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}`; no new secrets, external credentials, or third-party actions.
- Validate with `sh tools/check-release-version.sh "${GITHUB_REF_NAME}"` before publishing.
- Publish immediately (not draft) using `--verify-tag`.
- Artifacts: `hermes-<version>.tar.gz` and `hermes-<version>.tar.gz.sha256` only.
- `APP_VERSION` in `src/app.c` remains the single source of truth; no version bump.
- Tag push and release publication are not exercised in this session (separate authorization required).

## Review Focus

- Wrong tag accepted: the workflow must reject a tag that does not match `APP_VERSION`. Test: exact `check-release-version.sh "${GITHUB_REF_NAME}"` step in Task 1.
- Over-broad token permissions: `contents: write` must be job-scoped only. Test: diff review in Task 1 Step 2.
- Release notes from the wrong version: `awk` must select only the matching `## <version>` section. Test: local extraction check in Task 1 Step 2.
- Checksum mismatch or missing artifact: both files must be created before the publish step. Test: local archive/checksum dry run in Task 1 Step 2.
- Docs claiming capabilities the workflow lacks (tag creation, package deployment, external credentials). Test: exact MAINTENANCE wording review in Task 2 Step 2.

---

### Task 1: Rewrite the release workflow

**Files:**
- Rewrite: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: `tools/check-release-version.sh`, `tools/source-archive.sh`, `tools/branding-selftest.py`, `tools/build-install-selftest.py`, `CHANGELOG.md`, `src/app.c`.
- Produces: a workflow named `Release` triggered by `v*` tags, publishing `hermes-<version>.tar.gz` + `.sha256` with release notes.

- [ ] **Step 1: Replace the workflow file**

Write `.github/workflows/release.yml` with this structure and these exact commands:

```yaml
name: Release

on:
  push:
    tags:
      - 'v*'

permissions:
  contents: read

jobs:
  release:
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - uses: actions/checkout@v4
      - name: Validate release metadata
        run: |
          sh tools/check-release-version.sh "${GITHUB_REF_NAME}"
          python3 tools/branding-selftest.py
          python3 tools/build-install-selftest.py
      - name: Build source archive and checksum
        run: |
          version=$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' src/app.c)
          sh tools/source-archive.sh
          (cd dist && sha256sum "hermes-$version.tar.gz" > "hermes-$version.tar.gz.sha256")
          awk -v heading="## $version" '
            /^## / { active = ($0 == heading); next }
            active { print }
          ' CHANGELOG.md > dist/release-notes.md
          [ -s dist/release-notes.md ]
      - name: Publish release
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          version=$(sed -n 's/^#define APP_VERSION "\([^"]*\)".*/\1/p' src/app.c)
          gh release create "$GITHUB_REF_NAME" \
            "dist/hermes-$version.tar.gz" \
            "dist/hermes-$version.tar.gz.sha256" \
            --title "Hermes $version" \
            --notes-file dist/release-notes.md \
            --verify-tag
```

Keep the file pure ASCII and 2-space YAML indentation.

- [ ] **Step 2: Verify the workflow locally without publishing**

Run: `sh tools/check-release-version.sh v0.0.1`

Expected: `Release versions match: 0.0.1`.

Run: `sh tools/source-archive.sh /tmp/opencode/hermes-release-test.tar.gz` followed by `sha256sum /tmp/opencode/hermes-release-test.tar.gz`

Expected: archive created, checksum computed, no error.

Run: `awk -v heading="## 0.0.1" '/^## / { active = ($0 == heading); next } active { print }' CHANGELOG.md | head -3`

Expected: first three lines of the `## 0.0.1` section (starts with `**New**`), not the Unreleased section.

Run: `git diff --check`

Expected: no whitespace errors.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci(release): publish GitHub Release on version tag"
```

---

### Task 2: Document the release pipeline

**Files:**
- Modify: `MAINTENANCE.md:57-67` (Release infrastructure)
- Modify: `CHANGELOG.md:7-15` (Unreleased section)

**Interfaces:**
- Consumes: the workflow implemented in Task 1.
- Produces: documentation matching the implemented behavior.

- [ ] **Step 1: Rewrite the release infrastructure paragraph**

Replace the current "Release infrastructure" text with wording that states:

- Pushing a `v<version>` tag triggers the `Release` workflow.
- The workflow validates metadata against the tag, publishes a GitHub Release with
  `hermes-<version>.tar.gz` and its SHA256, and uses release notes from that
  version's CHANGELOG section.
- The workflow uses only `GITHUB_TOKEN` with job-scoped `contents: write`.
- It does not create tags, build/publish packages, deploy anywhere, or contact OBS/upstream.
- Creating the tag and pushing the release remain separately authorized human actions.

- [ ] **Step 2: Add the changelog entry**

Add a `**New**` subsection under `## Unreleased`, before the existing `**Changed**` subsection, with one bullet describing the tag-triggered release workflow and its artifacts. Do not add a new version heading.

- [ ] **Step 3: Verify documentation**

Run: `git diff -- MAINTENANCE.md CHANGELOG.md`

Expected: both files changed only in the described sections; no new version heading; no claims of tag creation, package publishing, or external deployment.

Run: `git diff --check`

Expected: no whitespace errors.

- [ ] **Step 4: Commit**

```bash
git add MAINTENANCE.md CHANGELOG.md
git commit -m "docs(release): document tag-triggered publication"
```
