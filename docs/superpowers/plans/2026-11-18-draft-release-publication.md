# Draft Release Publication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make tag pushes create a reviewable draft release and add a dispatch-only workflow that publishes a chosen draft without disturbing its edited content.

**Architecture:** Add `--draft` to the existing `Release` workflow's `gh release create`. Add a second `Publish release` workflow triggered only by `workflow_dispatch` with a required `tag` input; it verifies the named release exists and is a draft, then runs `gh release edit "$TAG" --draft=false --verify-tag` with no notes/title/asset flags. Docs are updated so no file still claims immediate publication.

**Tech Stack:** GitHub Actions, `gh` CLI (preinstalled on runners), `GITHUB_TOKEN`, POSIX shell.

**Spec:** `docs/superpowers/specs/2026-11-18-draft-release-publication-design.md`

## File Structure

- `.github/workflows/release.yml` — tag-triggered draft creation; gains one flag.
- `.github/workflows/publish-release.yml` — new, dispatch-only draft publication.
- `MAINTENANCE.md`, `CHANGELOG.md` — user-facing two-step release procedure.
- `2026-11-18-release-workflow-design.md`, `2026-11-18-release-workflow.md` — earlier docs, corrected so they no longer contradict the new flow.

## Global Constraints

- Tag trigger for drafts stays `push` on `v*`; no `workflow_dispatch` on `release.yml`.
- `gh release create` gains `--draft` and keeps `--verify-tag`.
- Publish workflow is `workflow_dispatch` only, with a **required** `tag` string input.
- Publish workflow refuses a missing release or one that is not a draft.
- Publish command passes no `--notes`, `--notes-file`, `--title` or assets.
- `permissions: contents: read` at workflow level; `contents: write` only on the publishing job of each workflow.
- `GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}`; no new secrets or third-party actions.
- Neither workflow creates/pushes tags, builds packages, or deploys.
- No version bump, package identity change, or README badge change.
- Pure ASCII YAML, 2-space indentation; pass inputs via `env:` never interpolated into `run:`.
- No actual tag push or publication this session (separate authorization).

## Review Focus

- `--draft` missing from the tag workflow → a tag push publishes immediately; pinned by Task 1's structural test asserting `--draft` in `release.yml` and no `--draft=false` there.
- Publish step overwrites user-edited notes/title/assets → pinned by Task 2's structural test asserting no notes/title/asset flags on `gh release edit`.
- Wrong or nonexistent tag published → pinned by Task 2's structural test asserting the `isDraft` guard, `--verify-tag`, and required input.
- Input interpolated into shell → pinned by Task 2's test asserting `TAG: ${{ inputs.tag }}` in `env:` and no `${{` inside `run:`.
- Docs still say publication is immediate or one-step → pinned by Task 3's docs structural test.

---

### Task 1: Make the tag workflow create a draft

**Files:**
- Modify: `.github/workflows/release.yml:38-43`

**Interfaces:**
- Consumes: existing `Release` workflow from `8288ae8`.
- Produces: a `Release` workflow whose `gh release create` includes `--draft`.

- [ ] **Step 1: Write the failing structural test**

Create a scratch script in the git-ignored SDD workspace (or `/tmp/opencode`) that reads `.github/workflows/release.yml` and asserts: file contains `--draft`, file does **not** contain `--draft=false`, `--verify-tag` is still present, `push` tag trigger `- 'v*'` is still present, and no `workflow_dispatch`. Exit 1 with a labelled failure line per failed check.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 <scratch>/draft-selftest.py`
Expected: FAIL on the `--draft` check; all other checks pass.

- [ ] **Step 3: Implement the minimal change**

In `.github/workflows/release.yml`, change the final command from:

```yaml
            --notes-file dist/release-notes.md \
            --verify-tag
```

to:

```yaml
            --notes-file dist/release-notes.md \
            --verify-tag \
            --draft
```

Change nothing else in the file.

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 <scratch>/draft-selftest.py`
Expected: GREEN — all checks pass.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci(release): create draft release on version tag"
```

---

### Task 2: Add the dispatch-only publish workflow

**Files:**
- Create: `.github/workflows/publish-release.yml`

**Interfaces:**
- Consumes: the draft created by Task 1's `Release` workflow (tag name is the `tag` input).
- Produces: a `Publish release` workflow with a required `tag` input that publishes that draft.

- [ ] **Step 1: Write the failing structural test**

Create a scratch script asserting, on `.github/workflows/publish-release.yml`:

- `name: Publish release`
- `workflow_dispatch:` present and `push:` absent
- `tag:` input with `required: true`, `type: string`, and a `description:`
- workflow-level `permissions:\n  contents: read`
- job-level `permissions:\n      contents: write`
- `GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}` present
- `TAG: ${{ inputs.tag }}` in an `env:` block
- no `${{` inside any `run:` block
- `gh release view "$TAG" --repo "$GITHUB_REPOSITORY" --json isDraft --jq .isDraft`
- a guard that fails unless `is_draft = true`
- `gh release edit "$TAG" --repo "$GITHUB_REPOSITORY" --draft=false --verify-tag`
- no `--notes`, `--notes-file`, `--title`, or asset paths in the file
- pure ASCII, no tabs

Run it and expect FAIL because the file does not exist.

- [ ] **Step 2: Create the workflow**

Write `.github/workflows/publish-release.yml` with exactly this content:

```yaml
name: Publish release

on:
  workflow_dispatch:
    inputs:
      tag:
        description: 'Draft release tag to publish (for example v0.0.1)'
        required: true
        type: string

permissions:
  contents: read

jobs:
  publish:
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - name: Verify draft exists
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          TAG: ${{ inputs.tag }}
        run: |
          if ! is_draft=$(gh release view "$TAG" --repo "$GITHUB_REPOSITORY" --json isDraft --jq .isDraft); then
            echo "No release found for tag $TAG" >&2
            exit 1
          fi
          [ "$is_draft" = true ] || {
            echo "Release $TAG is not a draft; refusing to publish" >&2
            exit 1
          }
      - name: Publish draft
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          TAG: ${{ inputs.tag }}
        run: gh release edit "$TAG" --repo "$GITHUB_REPOSITORY" --draft=false --verify-tag
```

- [ ] **Step 3: Run the test to verify it passes**

Run: `python3 <scratch>/publish-selftest.py`
Expected: GREEN — all checks pass.

Also run: `gh release edit --help | grep -- '--draft'`
Expected: a line showing `--draft[=true|false]`, confirming the publish flag exists.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/publish-release.yml
git commit -m "ci(release): add draft publication workflow"
```

---

### Task 3: Update release documentation

**Files:**
- Modify: `MAINTENANCE.md` (Release infrastructure section)
- Modify: `CHANGELOG.md` (Unreleased `**New**` bullet)
- Modify: `docs/superpowers/specs/2026-11-18-release-workflow-design.md`
- Modify: `docs/superpowers/plans/2026-11-18-release-workflow.md`

**Interfaces:**
- Consumes: the two workflows from Tasks 1 and 2.
- Produces: docs that describe the two-step flow without contradictions.

- [ ] **Step 1: Write the failing docs test**

Create a scratch script asserting across these files:

- `MAINTENANCE.md` contains `draft`, `Publish release`, `workflow_dispatch`, and a statement that publication is a separate authorized human action.
- `MAINTENANCE.md` does not contain the exact phrase `publishes a GitHub Release` (the current one-step wording).
- `CHANGELOG.md` Unreleased section mentions both the draft and the publish workflow; no new version heading.
- The earlier spec's "publish immediately" decision is marked superseded and points to `2026-11-18-draft-release-publication-design.md`.
- The earlier plan's Global Constraints no longer say "Publish immediately (not draft)" and its `gh release create` example contains `--draft`.

Run it and expect FAIL.

- [ ] **Step 2: Rewrite the MAINTENANCE release section**

State: pushing `v<version>` creates a **draft** with the archive, SHA256 and CHANGELOG notes; review it in GitHub; then dispatch `Publish release` with the tag to make it public. Publishing is a separate authorized human action. The `Last Release` badge stays red until publication. Keep the existing denials: no tag creation by workflows, no package builds, no deployment, no OBS, no inherited credentials, no upstream binaries in the updater.

- [ ] **Step 3: Update the CHANGELOG bullet**

Rewrite the existing Unreleased `**New**` bullet to say a pushed tag creates a draft release with archive and checksum, and a `Publish release` dispatch publishes it after review. Add no heading.

- [ ] **Step 4: Correct the earlier spec and plan**

In `2026-11-18-release-workflow-design.md`, change the owner-decision line and the publication step to describe a draft plus the separate publish workflow, and add a pointer to the new spec. In `2026-11-18-release-workflow.md`, change the Global Constraints line "Publish immediately (not draft) using `--verify-tag`" to describe draft creation and dispatch publication, and change the last two lines of the `gh release create` example in Task 1 Step 1 from `--verify-tag` to `--verify-tag \` followed by a new line `--draft`.

- [ ] **Step 5: Verify docs**

Run: `python3 <scratch>/docs-selftest.py`
Expected: GREEN.
Run: `git diff --check`
Expected: no whitespace errors.

- [ ] **Step 6: Commit**

```bash
git add MAINTENANCE.md CHANGELOG.md docs/superpowers/specs/2026-11-18-release-workflow-design.md docs/superpowers/plans/2026-11-18-release-workflow.md
git commit -m "docs(release): document draft then publish flow"
```
