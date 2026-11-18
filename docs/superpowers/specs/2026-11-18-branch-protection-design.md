# PR-only main: branch protection design

**Date:** 2026-11-18
**Status:** proposed

## Intent

Change how we work: `main` should only change through a pull request whose required
CI checks pass. No direct pushes, including by the repository owner. The rule is a
GitHub repository setting, so it applies to everyone regardless of local git
configuration.

## Current state (read-only recon, 2026-11-18)

- `juangiler/hermes`, default branch `main`, visibility `public`.
- One collaborator (the owner), and the authenticated token has `admin: true`.
- `main` protection: HTTP 404 — currently unprotected.
- `ci.yml` runs on both `push` and `pull_request`, with jobs `source-checks`,
  `package-versions`, `build` (4-way matrix), and `selftest`. No job has an `if:`
  condition that would skip it on PRs.
- **Actual check-run names on recent commits** are seven, not four:
  `source-checks`, `package-versions`, `selftest`, `build (archlinux:latest)`,
  `build (debian:trixie)`, `build (fedora:latest)`, `build (ubuntu:26.04)`.
  Matrix legs report as distinct checks, so requiring a bare `build` would never
  be satisfied and would make every PR unmergable.
- Merge methods: merge, squash and rebase all allowed; auto-delete-branch is off.

## Owner decisions

- Require a pull request before merging, with **0 required approvals**
  (1 would block the sole collaborator from merging their own PR).
- Require **all seven** CI checks, plus branches-up-to-date before merge.
- Enforce the rules on administrators: **no bypass**.
- Block force pushes and branch deletion.
- No push-restriction allowlist; the PR requirement already blocks direct pushes.
- Record the new working procedure in local, gitignored `AGENTS.md` and
  `docs/checkpoint.md`. No repository commit is needed for the setting itself.

## Exact change

One REST call, using the existing `gh` authentication (no new token, no secret):

```
PUT /repos/juangiler/hermes/branches/main/protection
```

```json
{
  "required_status_checks": {
    "strict": true,
    "contexts": [
      "source-checks",
      "package-versions",
      "selftest",
      "build (archlinux:latest)",
      "build (debian:trixie)",
      "build (fedora:latest)",
      "build (ubuntu:26.04)"
    ]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": {
    "required_approving_review_count": 0,
    "dismiss_stale_reviews": false,
    "require_code_owner_reviews": false,
    "require_last_push_approval": false
  },
  "restrictions": null,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "required_linear_history": false,
  "required_conversation_resolution": false,
  "lock_branch": false,
  "allow_fork_syncing": false
}
```

Classic branch protection is used rather than a Ruleset: it covers this need, is
simpler to inspect and reverse, and needs no repository file.

## How the working procedure changes

Old: commit on local `main`, then push `main`.

New:

1. Start from an up-to-date `main`, create a topic branch (`git switch -c <topic>`).
2. Make atomic commits there.
3. Push the branch and open a PR (`gh pr create`).
4. Wait for all seven checks to pass; fix failures in the branch.
5. Merge with `gh pr merge --rebase --delete-branch` — rebase keeps the existing
   atomic-commit convention and history linear.
6. Return to `main`, update it, then start the next branch.

Consequences worth accepting:

- If CI is red or a required check fails to report, `main` is frozen. There is no
  direct-push escape hatch. The only recovery is to temporarily disable the
  protection via an explicitly authorized API call and restore it afterwards.
- Tag pushes (`v*`) still work: branch protection applies to branches, not tags.
  The `Release` and `Publish release` workflows are unaffected.
- Merges need an up-to-date branch, so a stale PR must be rebased before merging.

## Non-goals

- No repository files are changed by this task; no commit or tag is created.
- No GitHub Ruleset, CODEOWNERS, required reviewers, conversation-resolution
  requirement, linear-history requirement, or branch lock.
- No changes to CI, release workflows, README badges, version or package identity.
- No local git hooks: they are bypassable and cannot replace the server-side rule.

## Verification

- Read back `GET /repos/juangiler/hermes/branches/main/protection` and assert every
  field, including all seven contexts and `enforce_admins: true`.
- Confirm local branch/HEAD state is unchanged (`git status --short --branch`).
- The direct-push rejection itself is **not** exercised here: that needs a real
  rejected push. The first genuine PR is the end-to-end proof.
- Do not print or persist the token; rely on the authenticated `gh` session.
