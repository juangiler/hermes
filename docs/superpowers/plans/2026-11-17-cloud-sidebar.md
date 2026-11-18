# Cloud Sidebar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Separate rclone remotes, including WebDAV, from native Network locations.

**Architecture:** Add `PK_CLOUDHDR` and `A.clouds_collapsed` using the existing
sidebar header patterns. Keep `PK_CLOUD` rows and their connection logic intact.

**Tech Stack:** Existing C11 unity build and in-app self-tests.

**Spec:** `docs/superpowers/specs/2026-11-17-cloud-sidebar-design.md`

## Global Constraints

- No dependencies or connection backend changes.
- Source remains ASCII; existing configurations default Cloud to expanded.
- No commits, installations, or container provisioning without permission.
- Rebuild after source changes before running application self-tests.

## Review Focus

- Discovered rclone mounts must not duplicate Cloud rows.
- Native WebDAV stays in Network.
- Collapsing either section must leave the other section unchanged.
- Cloud controls remain usable with no remotes and disappear without rclone.
- Settings reload must preserve both collapse states independently.

### Task 1: Separate Cloud section and controls

**Files:**
- Modify: `src/app.c` (place kind, app state, row construction, menus/actions)
- Modify: `src/input.c` (header click and plus handling)
- Modify: `src/render.c` (header chevron and plus drawing)
- Modify: `src/settings.c` (default/load/save `cloud_collapsed`)
- Test: `src/selftest.c` (existing m10 suite)
- Modify: `src/ui_tools.c`, `docs/settings.md`, `CHANGELOG.md`

**Interfaces:**
- Consumes: existing `init_places(void)`, `place_is_header(const Place *)`,
  `ACT_CLOUD_NEW`, `ACT_CLOUD_REFRESH`, and `PK_CLOUD`.
- Produces: `PK_CLOUDHDR`, `A.clouds_collapsed`, persisted `cloud_collapsed`.

- [x] Extend m10 fixtures with a WebDAV remote; assert its row follows
  `PK_CLOUDHDR` rather than Network. Preserve the native-network and duplicate
  mount assertions. Exercise independent header collapse by mouse/menu,
  Cloud plus configuration, empty remotes, unavailable rclone, and settings
  round-trip with opposite collapse states.
- [x] Run `./build.sh debug` and then
  `./build/file-explorer --selftest-m10` to observe the regression failures.
  If dependencies block compilation, report that limitation; do not install
  anything or use a stale executable.
- [x] Add the header and state; move rclone rows after the complete Network
  section. Route collapse, plus, menus, and settings through the new state.
  Leave all remote connection and duplicate-suppression logic unchanged.
- [x] Update connection-form WebDAV guidance and its exact-string tests to
  refer to Cloud. Document `cloud_collapsed` and the new section, and add an
  Unreleased changelog bullet.
- [x] Rebuild debug and run m10 plus relevant settings/sidebar regression
  suites, if build dependencies permit. Check `git diff --check`, ASCII
  source compliance, and review all header-kind call sites.
- [x] Review the focused diff and report verification evidence and limitations.
  Leave changes uncommitted.
