# README Badges Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add live CI/build status badges and a static OpenCode badge to the README header.

**Architecture:** Markdown-only change inside the existing centered header `<div>` in `README.md`. Two badge rows: new dynamic/tooling badges first, the existing static identity badges unchanged below. No application code, workflow, or build-script changes.

**Tech Stack:** GitHub Actions workflow status badge URLs, Shields.io static badge URLs, Markdown.

**Spec:** `docs/superpowers/specs/2026-11-18-readme-badges-design.md`

## Global Constraints

- Repository is private: workflow badges are not visible externally; this is documented, not fixable.
- Source must remain ASCII (repo-wide rule); badge URLs contain only ASCII.
- No version bump, workflow change, or package identity change.
- Existing three static badges keep their exact URLs and link targets.
- Badge colors for new static badges use the existing Tokyo Night palette (`7aa2f7`).
- `?job=` is an undocumented GitHub badge parameter; keep the documented `?branch=main` too.
- Markdown-only change: do not claim an application rebuild occurred.

## Review Focus

- Workflow filename mismatch: badge links must use `ci.yml`, the actual file name. Test: static URL check in Step 2.
- Branch pin mismatch: badges must pin `main`, the repo's default and pushed branch. Test: static URL check in Step 2.
- Job name mismatch: `job=build` must match the `build` job in `.github/workflows/ci.yml`. Test: static URL check in Step 2.
- Link targets: each image must be wrapped in a link (workflow page / branch run list / opencode.ai). Test: visual diff review in Step 2.
- Accidental changes to the existing static badges or to the rest of the README. Test: diff scoped to the badge block in Step 2.

---

### Task 1: Add badges to README header

**Files:**
- Modify: `README.md:1-15` (centered header block only)
- Design doc (already committed separately, no change needed): `docs/superpowers/specs/2026-11-18-readme-badges-design.md`

**Interfaces:**
- Consumes: `.github/workflows/ci.yml` (`name: CI`, job `build`, triggers `push`/`pull_request`), repo `juangiler/hermes`, branch `main`.
- Produces: no code interfaces; output is the rendered README header.

- [ ] **Step 1: Replace the badge block**

Replace lines 11-13 of `README.md` (the three existing badge lines) with two rows separated by a blank line:

Row 1 (new badges, exactly as written in the spec):

```markdown
[![CI](https://github.com/juangiler/hermes/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/juangiler/hermes/actions/workflows/ci.yml)
[![Build](https://github.com/juangiler/hermes/actions/workflows/ci.yml/badge.svg?branch=main&job=build)](https://github.com/juangiler/hermes/actions/workflows/ci.yml?query=branch%3Amain)
[![Built with OpenCode](https://img.shields.io/badge/Built%20with-OpenCode-7aa2f7)](https://opencode.ai)
```

Row 2: the original three badge lines, byte-for-byte unchanged.

Keep both rows inside the existing `<div align="center">` … `</div>` block, with a blank line between them.

- [ ] **Step 2: Verify the change**

Run: `git diff -- README.md`

Expected: only the badge block inside the header div changed; the three original badge lines are present unchanged in row 2; each new badge has an image and a link; URLs match `ci.yml`, `branch=main`, `job=build`, `juangiler/hermes`, and `opencode.ai`.

Run: `git diff --check`

Expected: no whitespace errors.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): add CI, build and OpenCode badges"
```
