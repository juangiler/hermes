# Branch Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `main` changeable only through PRs whose seven real CI checks pass, then record the new working procedure locally.

**Architecture:** Save the design/plan docs and commit them while direct push is still allowed, apply classic branch protection with one authenticated REST call using the exact check names observed from GitHub, verify by reading the setting back, then update the gitignored local workflow docs. No repository source files change.

**Tech Stack:** GitHub REST API via `gh api`, `jq`, Python 3 scratch assertion script, git.

**Spec:** `docs/superpowers/specs/2026-11-18-branch-protection-design.md`

## Global Constraints

- Owner decisions: 0 required approvals; all seven CI checks required; `enforce_admins: true`; force pushes and deletions blocked.
- Exact required contexts: `source-checks`, `package-versions`, `selftest`, `build (archlinux:latest)`, `build (debian:trixie)`, `build (fedora:latest)`, `build (ubuntu:26.04)`.
- `required_status_checks.strict` must be `true`.
- Existing `gh` authentication only; never print or persist the token.
- No repository source, CI, release, badge, version or package-identity changes.
- No Rulesets, CODEOWNERS, required reviewers, branch lock, or local git hooks.
- Document the working procedure only in gitignored `AGENTS.md` and `docs/checkpoint.md`.
- No tag, commit or PR created by the protection call itself.

## Review Focus

- A bare `build` required check would never match the four matrix legs → the read-back test asserts all seven exact context strings and that `build` alone is absent.
- `enforce_admins: false` would leave a direct-push bypass for the owner → the read-back test asserts `enforce_admins == true`.
- A nonzero approval count would block the sole collaborator from merging → the read-back test asserts `required_approving_review_count == 0`.
- Force push or deletion would still be allowed → the read-back test asserts `allow_force_pushes == false` and `allow_deletions == false`.
- Local working docs still say "commit directly to main" → Task 3's test asserts the new PR procedure is present in both local docs.

---

### Task 1: Save and commit the planning docs

**Files:**
- Create: `docs/superpowers/plans/2026-11-18-branch-protection.md`
- Existing: `docs/superpowers/specs/2026-11-18-branch-protection-design.md`

**Interfaces:**
- Consumes: the approved design doc.
- Produces: both docs tracked on `main` before protection blocks direct push.

- [ ] **Step 1: Commit both docs while direct push is still available**

Run:

```bash
git add docs/superpowers/specs/2026-11-18-branch-protection-design.md docs/superpowers/plans/2026-11-18-branch-protection.md
git commit -m "docs(repo): design PR-only branch protection"
git status --short
```

Expected: one commit; `git status --short` shows no tracked changes.

---

### Task 2: Apply and verify branch protection

**Files:**
- Create (scratch, outside repo): `/tmp/opencode/hermes-branch-protection.json`
- Create (scratch, outside repo): `/tmp/opencode/branch-protection-selftest.py`

**Interfaces:**
- Consumes: the seven context names from the design doc.
- Produces: a verified `main` protection setting on GitHub.

- [ ] **Step 1: Write the failing read-back test**

Create `/tmp/opencode/branch-protection-selftest.py` that calls
`gh api repos/juangiler/hermes/branches/main/protection` with `subprocess.run`
(`capture_output=True`, `text=True`) and asserts, printing one labelled PASS/FAIL
line each and exiting 1 if any fail:

- the command exit code is 0;
- `enforce_admins is True`;
- `required_pull_request_reviews.required_approving_review_count == 0`;
- `required_status_checks.strict is True`;
- `set(required_status_checks.contexts)` equals the seven exact names from the design doc, and no context is the bare string `build`;
- `allow_force_pushes is False`;
- `allow_deletions is False`;
- `restrictions is None`.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 /tmp/opencode/branch-protection-selftest.py`
Expected: RED — the API exits 404 ("Branch not protected").

- [ ] **Step 3: Apply the setting**

Write `/tmp/opencode/hermes-branch-protection.json` with the exact JSON payload from the design doc, then run:

```bash
gh api -X PUT repos/juangiler/hermes/branches/main/protection \
  --input /tmp/opencode/hermes-branch-protection.json
```

Expected: HTTP 200 JSON response showing the protection object.

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 /tmp/opencode/branch-protection-selftest.py`
Expected: GREEN — every assertion passes.

Also run `git status --short --branch` and confirm the local branch/HEAD did not change.

- [ ] **Step 5: No commit**

This task changes a remote GitHub setting only. Do not create a local commit.

---

### Task 3: Record the new working procedure locally

**Files:**
- Modify: `AGENTS.md` (gitignored local instructions)
- Modify: `docs/checkpoint.md` (gitignored local journal)

**Interfaces:**
- Consumes: the applied protection setting from Task 2.
- Produces: local docs describing the PR-only workflow so future sessions follow it.

- [ ] **Step 1: Write the failing local-docs test**

Create `/tmp/opencode/local-docs-selftest.py` reading both files and asserting, one labelled PASS/FAIL line each, exit 1 on failure:

- `AGENTS.md` contains `PR-only` and `git switch -c`;
- `AGENTS.md` contains `gh pr create` and `gh pr merge --rebase --delete-branch`;
- `AGENTS.md` contains `main` and `direct push` near the workflow section;
- `docs/checkpoint.md` contains `branch protection` and `PR-only`;
- neither file claims that direct pushes to `main` are still allowed.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 /tmp/opencode/local-docs-selftest.py`
Expected: RED — the new sections do not exist yet.

- [ ] **Step 3: Update the local docs**

Add a short "Workflow — PR-only main" section to `AGENTS.md` covering: branch from updated `main`, atomic commits, push branch, `gh pr create`, all seven CI checks must pass, `gh pr merge --rebase --delete-branch`, update `main` before the next branch, no direct pushes, CI-red means frozen until fixed, and tag pushes (`v*`) still work because protection targets branches. Append a dated chronological entry to `docs/checkpoint.md` recording the applied setting and the seven checks. Keep both changes ASCII.

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 /tmp/opencode/local-docs-selftest.py`
Expected: GREEN.

- [ ] **Step 5: No commit**

Both files are gitignored local records; do not commit them.
