# Draft release publication design

**Date:** 2026-11-18
**Status:** proposed
**Supersedes:** the "publish immediately (not draft)" decision in
`2026-11-18-release-workflow-design.md` on this one point.

## Intent

The user wants a human review gate between a `v<version>` tag push and a public
GitHub Release, so the generated notes can be properly formatted and the assets
checked before the release goes out.

Today `Release` publishes immediately with notes copied verbatim from the
matching `CHANGELOG.md` section. There is no opportunity to correct formatting,
add highlights, or verify the archive before the release is public.

## Goals

- A pushed `v<version>` tag creates a **draft** release with the source archive,
  its SHA256 checksum, and notes from that version's CHANGELOG section.
- A separate dispatch-only workflow publishes a chosen draft after review.
- Publishing preserves whatever notes/title the user edited in the GitHub UI.
- The publish step refuses to act unless the named release exists and is still a
  draft; it never creates a release or a tag.
- Only the built-in `GITHUB_TOKEN` is used, with `contents: write` scoped to the
  publishing job. No third-party actions or new secrets.
- Docs describe the two-step flow and the badge consequence.

## Non-goals / anti-goals

- No automatic publication on tag push.
- Neither workflow creates, pushes, or retags anything.
- No deb/rpm/AppImage builds, package repository, OBS, updater, or deployment.
- No version bump, package identity change, or README badge change.
- The publish step must not overwrite notes/title/asset edits made to the draft.

## Constraint worth naming

The README `Last Release` badge reads **published** releases only. With drafts,
it stays red ("no releases or repo not found") until the publish workflow runs.
The user already accepts that red state as accurate; this makes it deliberate.

## Approaches considered for selecting the draft

| Approach | Tradeoff |
| --- | --- |
| Explicit `tag` input on dispatch (chosen) | No ambiguity with several drafts; a typo cannot publish the wrong one. Cost: type the tag when dispatching. |
| Auto-pick newest draft | One click, but publishes the wrong release if two drafts are in flight. |
| Second `publish-v<version>` tag | No UI, but adds a tag type and a mistag publishes immediately. |

The user chose the explicit tag input.

## Design

### 1. Modify `.github/workflows/release.yml`

Change only the final publish command: add `--draft` to `gh release create`.
Keep the `v*` tag trigger, metadata validation, archive/checksum generation,
notes extraction, `--verify-tag`, and job-scoped `contents: write` unchanged.

### 2. Add `.github/workflows/publish-release.yml`

- `name: Publish release`
- `on: workflow_dispatch:` with a **required** `tag` string input (for example
  `v0.0.1`) and a description telling the user to review the draft first.
- `permissions: contents: read` at workflow level; single job with
  `contents: write`.
- No checkout step is needed: `gh` uses `GITHUB_REPOSITORY` and the token.
- Steps, with `GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}`:
  1. Guard: `gh release view "$tag" --json isDraft` must succeed and `isDraft`
     must be `true`; fail with a clear message otherwise. Use `--verify-tag` on
     the publish command so a missing git tag also aborts.
  2. Publish: `gh release edit "$tag" --draft=false --verify-tag`. Do **not**
     pass `--notes`, `--notes-file`, `--title` or assets, so user edits survive.
- Pure ASCII, 2-space YAML indentation, no third-party actions.

### 3. Documentation

- `MAINTENANCE.md`: describe the two-step flow — tag push creates a draft;
  review it; dispatch `Publish release` with the tag to make it public. State
  that publishing is a separate authorized human action and that the badge
  stays red until then.
- `CHANGELOG.md`: update the Unreleased bullet to say the tag creates a draft
  and a dispatch publishes it. No new version heading.
- Update `2026-11-18-release-workflow-design.md` and its plan to reflect the
  draft flow, so the repo does not carry contradictory release docs.

## Verification

- Structural checks on both workflow files (trigger, `--draft`, no
  `--draft=false` in the draft workflow, guard and `--draft=false` in the
  publish workflow, job-scoped write permissions).
- Confirm with `gh release edit --help` that `--draft[=true|false]` exists and
  that publishing without notes flags preserves them.
- `sh tools/check-release-version.sh v0.0.1`, standalone self-tests,
  `git diff --check`.
- Not exercised: an actual tag push, draft creation, or dispatch publication —
  all require separate explicit authorization. Report the flow as configured
  but untested end-to-end.
