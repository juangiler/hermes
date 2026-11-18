# README badges design

## Context

`README.md` currently has a three-badge row inside the centered header block:

- License: MIT (static Shields badge, links to `LICENSE`)
- Linux / Wayland | X11 (static)
- C11 + SDL3 (static)

The user asked for "more proper badges about the build, CI pass status" and a badge
showing the project was built with AI, specifically OpenCode. They chose the
**Hybrid** approach: live workflow status for the technical badges, a static badge
for OpenCode. They later asked that release status be dynamic too and chose a
badge for the manual `Release preparation` workflow, then also chose a dynamic
latest-release version badge despite its current empty-state limitations.

Constraint: `juangiler/hermes` is a private repository. GitHub documents that
workflow badges in private repos "are not accessible externally", so live badges
will render for people with repo access and show no status for outsiders. The
static OpenCode badge renders everywhere.

## Approaches considered

| Approach | Tradeoff |
| --- | --- |
| All static | Renders for everyone, but "build passing" is a manual claim that can go stale. |
| All live | Accurate, but an AI/tooling badge has no live source; also private-repo visibility caveat. |
| **Hybrid (chosen)** | Live CI + build status where it can be sourced, static OpenCode badge. Best fit for "proper" status while keeping the AI badge always visible. |

## Chosen design

Keep all badges in the existing centered header block, split into two rows:
dynamic status/tooling badges first, existing static identity badges second.

**Row 1 — new badges**

1. **CI** — the workflow status badge for `.github/workflows/ci.yml`, pinned to
   `main`, linked to the workflow page:

   ```markdown
   [![CI](https://github.com/juangiler/hermes/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/juangiler/hermes/actions/workflows/ci.yml)
   ```

2. **Build** — the `build` job status from the same workflow, pinned to `main`,
   linked to the workflow's branch run list:

   ```markdown
   [![Build](https://github.com/juangiler/hermes/actions/workflows/ci.yml/badge.svg?branch=main&job=build)](https://github.com/juangiler/hermes/actions/workflows/ci.yml?query=branch%3Amain)
   ```

   Caveat: GitHub officially documents `branch` and `event` query parameters but
   not `job`. The `job` parameter is widely used in practice and degrades
   gracefully — if GitHub ignores it, the badge shows the whole workflow status
   instead of just the `build` job. This is acceptable: it can never show a
   *better* status than the overall workflow. The `build` job is a fail-fast=false
   matrix across Arch, Fedora, Debian and Ubuntu, so its badge represents the
   aggregate portable-build result.

3. **Release preparation** — a live workflow badge for
   `.github/workflows/release.yml`, pinned to `main`, linked to the workflow page:

   ```markdown
   [![Release preparation](https://github.com/juangiler/hermes/actions/workflows/release.yml/badge.svg?branch=main)](https://github.com/juangiler/hermes/actions/workflows/release.yml)
   ```

   This workflow is `workflow_dispatch` only and Hermes has no published release
   or tag yet, so the badge will read "no status" until someone triggers it
   manually. That is intentional and honest: it tracks the preparation pipeline,
   not a published binary. A Shields "latest release" badge was initially rejected
   for the same reason (no release to report, private repo), but the user later
   explicitly chose to add it anyway; see item 4.

4. **Release version** — a dynamic Shields badge reading GitHub's latest release,
   linked to the releases page:

   ```markdown
   [![Release](https://img.shields.io/github/v/release/juangiler/hermes)](https://github.com/juangiler/hermes/releases)
   ```

   The user chose this after being told the limitations. It is not expected to
   show a useful value yet: there is no GitHub Release or `v0.0.1` tag, and the
   repository is private, so the unauthenticated Shields endpoint cannot read
   release data. It will typically render as `not found` / invalid until a real
   release exists and the repo is public (or Shields is given a token). This is
   recorded so the expected empty state is not mistaken for a broken URL.

5. **Built with OpenCode** — static Shields badge using the palette already used
   by the existing badges (Tokyo Night blue `7aa2f7`), linked to the project site:

   ```markdown
   [![Built with OpenCode](https://img.shields.io/badge/Built%20with-OpenCode-7aa2f7)](https://opencode.ai)
   ```

**Row 2 — existing badges (unchanged URLs/targets)**

License, Linux, C11 + SDL3 stay exactly as they are.

## Non-goals

- No GitHub Release, tag, or version bump is created by this badge work; the dynamic version badge simply reads whatever GitHub already publishes.
- No version bump, workflow change, or package identity change.
- No change to badge colors in the existing static row.
- No attempt to make private-repo badges visible externally (not possible).

## Verification

- `git diff --check` on `README.md`.
- Confirm the new badge URLs match the workflow filename (`ci.yml`), job name
  (`build`), branch (`main`) and repo (`juangiler/hermes`) by reading
  `.github/workflows/ci.yml`.
- No build is needed for a Markdown-only change; report that explicitly rather
  than claiming the application was rebuilt.
