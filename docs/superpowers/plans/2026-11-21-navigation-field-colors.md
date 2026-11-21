# Navigation Field Colors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add independent brightness controls for the path/pill field and the folder search field, plus a path-only transparency option, without changing other surfaces.

**Architecture:** Keep `navigation_field_color()` as the existing neutral text-selection base. Add `pathbar_field_color()`/`searchbar_field_color()` in `src/theme.c`, wire them to settings and Options, then use them at the two draw sites in `src/render.c`. Transparency is applied only to the path fill at the draw site, with a faint outline retained.

**Tech Stack:** C11 SDL3 unity build, `src/settings.c`, `src/options.c`, `src/render.c`, `tools/appearance-selftest.c`, in-app `--selftest-m9`, `docs/settings.md`, `CHANGELOG.md`.

**Spec:** `docs/superpowers/specs/2026-11-21-navigation-field-colors-design.md`

## Global Constraints

- Source must be pure ASCII; `build.sh` aborts otherwise.
- Warnings fail CI; keep `-Wall -Wextra` clean.
- Unity build: new `static` names must be unique across all `src/*.c`; include order matters.
- New user-visible setting requires `settings.c` read/write/default, an Options row, `docs/settings.md`, and a `CHANGELOG.md` Unreleased bullet.
- Tests must not touch real user config or open a real session bus.
- PR-only `main`: topic branch, atomic conventional commits, PR with seven required checks green, then `gh pr merge --rebase --delete-branch`.
- Commit signing must be verified before the feature commit; do not bypass signing with `-c commit.gpgsign=false`.
- Do not enable GitHub's `required_signatures` ruleset rule; only set up local signing and prove GitHub reports Verified.

## Review Focus

- Header brightness must not move the new field colors, and path brightness must not recolor text selection — Task 1 test.
- Path brightness must affect the pill enclosure but not the search field; search brightness must affect only the search field — Task 1 test.
- Transparent path must leave pill/search colors opaque and keep text readable — Task 1 alpha/contrast test and Task 4 screenshot smoke.
- New settings must default to off, round-trip through `settings.conf`, and clamp out-of-range values — Task 2 test.
- Options rows must change the right globals and persist — Task 3 test.

---

### Task 0: Set up verified SSH commits and open the topic branch

**Files:**
- Modify: local git config only
- Modify: local `docs/verified-commits.md`

**Interfaces:**
- Produces: signed commits that GitHub reports as `Verified`; feature branch `feat/ui/navigation-field-colors`.

- [ ] **Step 1: Check prerequisites and existing dedicated key**

Run from the repo root:

```bash
git --version
git config --get user.email
gh api user/emails --jq '.[] | select(.verified) | .email'
ls -l "$HOME/.ssh/id_ed25519_hermes_signing" 2>/dev/null || true
```

Expected: git >= 2.34, email `juangiler@icloud.com` appears in the verified GitHub email list, and a clear result on whether the dedicated key already exists. Stop if the email is not verified; a signature over an unverified email still shows as Unverified on GitHub.

- [ ] **Step 2: Generate the dedicated signing key if missing**

If Step 1 showed no key, the user should run this command directly so the passphrase stays private:

```bash
ssh-keygen -t ed25519 -a 100 \
  -C "Hermes commit signing <juangiler@icloud.com>" \
  -f "$HOME/.ssh/id_ed25519_hermes_signing"
```

Use a passphrase and keep the private key private. If the key already exists, do not regenerate it. If the user prefers a non-interactive key with no passphrase, ask for explicit approval before adding `-N ""`; do not assume it.

- [ ] **Step 3: Register the public key with GitHub as a signing key**

If `gh ssh-key list` does not already show the Hermes signing key, run:

```bash
gh ssh-key add "$HOME/.ssh/id_ed25519_hermes_signing.pub" \
  --type signing \
  --title "Hermes commit signing"
```

Expected: the key appears as a signing key, not an authentication key.

- [ ] **Step 4: Configure local git and allowed signers**

```bash
git config --local gpg.format ssh
git config --local user.signingkey "$HOME/.ssh/id_ed25519_hermes_signing.pub"
git config --local commit.gpgsign true
git config --local tag.gpgsign true
mkdir -p "$HOME/.config/git"
grep -qF "$(cat "$HOME/.ssh/id_ed25519_hermes_signing.pub")" "$HOME/.config/git/allowed_signers" 2>/dev/null || \
  printf '%s %s\n' "juangiler@icloud.com" "$(cat "$HOME/.ssh/id_ed25519_hermes_signing.pub")" \
  >> "$HOME/.config/git/allowed_signers"
git config --local gpg.ssh.allowedSignersFile "$HOME/.config/git/allowed_signers"
```

Expected: no output; local config points only at the dedicated key.

- [ ] **Step 5: Prove a signed commit locally and on GitHub**

```bash
git switch main
git pull --ff-only
git switch -c test/signed-commits
git commit --allow-empty -m "test: verify signed commit"
git verify-commit HEAD
git push -u origin test/signed-commits
sha=$(git rev-parse HEAD)
gh api "repos/juangiler/hermes/commits/$sha" --jq .commit.verification.verified
```

Expected: `git verify-commit` says "Good git signature" and the GitHub API returns `true`. If either fails, stop and fix signing before continuing. Do not bypass it.

- [ ] **Step 6: Clean up the scratch branch and create the feature branch**

```bash
gh pr create --draft --title "test: signed commit check" --body "Temporary signing verification"
gh pr close --delete-branch
git switch main
git pull --ff-only
git switch -c feat/ui/navigation-field-colors
```

Expected: no scratch branch or PR remains; `git status --short --branch` shows `## feat/ui/navigation-field-colors`.

- [ ] **Step 7: Record the result in local notes**

Append the commit SHA, GitHub Verified result, and date to `docs/verified-commits.md`. Do not commit that local runbook.

---

### Task 1: Field color helpers and headless contrast tests

**Files:**
- Modify: `src/theme.c:103-110`, `src/theme.c:215-235`
- Modify: `tools/appearance-selftest.c:18-79`, `tools/appearance-selftest.c:222-259`, `tools/appearance-selftest.c:454-465`

**Interfaces:**
- Consumes: `surface_adjust()`, `rgb_mix()`, `rgb_luma()`, `T.field`, `SURFACE_HEADER`, `g_navigation_selection_opacity`.
- Produces:
  - `static int g_pathbar_brightness, g_searchbar_brightness;`
  - `static bool g_transparent_pathbar;`
  - `static u32 pathbar_field_color(void);`
  - `static u32 searchbar_field_color(void);`
  - `static f32 pathbar_field_fill_alpha(void);`
  - `navigation_field_color()` keeps its current header-based meaning.

- [ ] **Step 1: Write the failing appearance test**

Add `test_navigation_fields()` to `tools/appearance-selftest.c`:

```c
static void test_navigation_fields(void) {
    theme_select(theme_find("Dark"));
    g_unified_surfaces = true;
    memset(g_surface_brightness, 0, sizeof g_surface_brightness);
    g_pathbar_brightness = g_searchbar_brightness = 0;
    g_transparent_pathbar = false;
    theme_apply();

    u32 neutral = navigation_field_color();
    assert(pathbar_field_color() == neutral);
    assert(searchbar_field_color() == neutral);
    assert(tab_container_color() == pathbar_field_color());
    assert(pathbar_field_fill_alpha() == 1.f);

    g_surface_brightness[SURFACE_HEADER] = -30;
    theme_apply();
    assert(navigation_field_color() != neutral);
    assert(pathbar_field_color() == neutral && searchbar_field_color() == neutral);

    g_pathbar_brightness = -30;
    u32 path_dark = pathbar_field_color();
    assert(tab_container_color() == path_dark);
    assert(rgb_luma(path_dark) < rgb_luma(searchbar_field_color()));
    assert(fabsf(rgb_luma(surface_foreground_color(path_dark, false, false)) - rgb_luma(path_dark)) >= 0.4f);

    g_searchbar_brightness = 30;
    u32 search_light = searchbar_field_color();
    assert(rgb_luma(search_light) > rgb_luma(path_dark));
    assert(tab_container_color() == path_dark);
    assert(navigation_field_color() != neutral); // header still independent

    g_transparent_pathbar = true;
    assert(pathbar_field_fill_alpha() == 0.f);
    assert(tab_container_color() == path_dark && searchbar_field_color() == search_light);

    theme_apply();
    theme_apply();
    assert(pathbar_field_color() == path_dark && searchbar_field_color() == search_light);

    g_transparent_pathbar = false;
    g_pathbar_brightness = g_searchbar_brightness = 0;
    memset(g_surface_brightness, 0, sizeof g_surface_brightness);
    theme_apply();
    assert(pathbar_field_fill_alpha() == 1.f);
    assert(tab_container_color() == navigation_field_color());
}
```

Call it from `main()` before `test_surfaces()`. Update the existing assertions in `test_surfaces()` and `test_tab_highlight()` so the tab enclosure compares against `pathbar_field_color()` rather than `navigation_field_color()`, and change the "header tint/brightness changes the enclosure" block in `test_tab_highlight()` to set `g_pathbar_brightness = 15`, assert the enclosure changes, then assert `T.toolbar` is unchanged and reset to 0. Extend the final `puts()` text to mention independent field colors.

- [ ] **Step 2: Run the test to verify it fails**

Run:

```sh
cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-format-truncation \
   tools/appearance-selftest.c -o /tmp/fe-appearance-test -lm -ldl -pthread && /tmp/fe-appearance-test
```

Expected: compile failure for `g_pathbar_brightness`, `pathbar_field_color`, `searchbar_field_color`, or `pathbar_field_fill_alpha` being undeclared.

- [ ] **Step 3: Implement the helpers in `src/theme.c`**

Add the three globals beside `g_navigation_selection_opacity`. Keep `navigation_field_color()` exactly as it is today. Add:

```c
static u32 pathbar_field_color(void);
static u32 searchbar_field_color(void);
static f32 pathbar_field_fill_alpha(void);
```

Each color helper uses the existing `surface_adjust(T.field, <new brightness>)` then `rgb_mix(..., 0x000000, 0.05f)` formula. `pathbar_field_fill_alpha()` returns `0.f` when `g_transparent_pathbar` is true, otherwise `1.f`. Change `tab_container_color()` and `classic_tab_color()` to call `pathbar_field_color()`.

- [ ] **Step 4: Run the test to verify it passes**

Run the Step 2 command again.

Expected: `Appearance checks passed` including the new phrase; no assertions fail.

- [ ] **Step 5: Commit**

```bash
git add src/theme.c tools/appearance-selftest.c
git commit -m "test(theme): cover independent navigation field colors"
```

Note: this commit contains the helpers and their tests together because the helpers are not used by the app yet.

---

### Task 2: Persist the new settings

**Files:**
- Modify: `src/settings.c:17-97`, `src/settings.c:274-287`, `src/settings.c:490-545`
- Modify: `src/selftest.c:3174-3181`

**Interfaces:**
- Consumes: the globals from Task 1.
- Produces: settings keys `pathbar_brightness`, `searchbar_brightness`, `transparent_pathbar`.

- [ ] **Step 1: Write the failing settings test**

Add to the m9 settings block in `src/selftest.c`, immediately after the navigation-opacity round trip:

```c
// Navigation field colors have their own defaults, persistence and clamping.
settings_defaults();
ST_CHECK(g_pathbar_brightness == 0 && g_searchbar_brightness == 0 && !g_transparent_pathbar);
g_pathbar_brightness = -15;
g_searchbar_brightness = 20;
g_transparent_pathbar = true;
settings_save(); settings_load();
ST_CHECK(g_pathbar_brightness == -15 && g_searchbar_brightness == 20 && g_transparent_pathbar);
char cfg[PATH_MAX];
settings_file(cfg, sizeof cfg, false);
FILE *sf = fopen(cfg, "w");
ST_CHECK(sf != NULL);
if (sf) {
    fputs("pathbar_brightness=99\nsearchbar_brightness=-99\ntransparent_pathbar=1\n", sf);
    fclose(sf);
}
settings_load();
ST_CHECK(g_pathbar_brightness == 30 && g_searchbar_brightness == -30 && g_transparent_pathbar);
settings_defaults();
```

- [ ] **Step 2: Run the test to verify it fails**

Build and run inside `fedora-build`:

```sh
./build.sh debug
./build/debug/hermes --selftest-m9
```

Expected: FAIL because the new keys are ignored and the globals stay at 0/false.

- [ ] **Step 3: Implement defaults, load and save in `src/settings.c`**

In `settings_defaults()`, set both brightness globals to `0` and `g_transparent_pathbar = false`. In `settings_load()`, add three `else if` branches using `CLAMP(atoi(v), -30, 30)` for the brightness keys and `atoi(v) != 0` for transparency. In `settings_write()`, print the three keys next to `header_brightness`.

- [ ] **Step 4: Run the test to verify it passes**

Run the Step 2 commands again.

Expected: `--selftest-m9` exits 0.

- [ ] **Step 5: Commit**

```bash
git add src/settings.c src/selftest.c
git commit -m "feat(settings): persist navigation field colors"
```

---

### Task 3: Add Options rows

**Files:**
- Modify: `src/options.c:7-17`, `src/options.c:50-58`, `src/options.c:307-331`, `src/options.c:355-399`, `src/options.c:400-440`
- Modify: `src/selftest.c:3174-3190`

**Interfaces:**
- Consumes: settings keys from Task 2.
- Produces: option IDs `OPT_PATHBAR_BRIGHTNESS`, `OPT_SEARCHBAR_BRIGHTNESS`, `OPT_TRANSPARENT_PATHBAR`.

- [ ] **Step 1: Write the failing Options test**

Add after the Task 2 block in `src/selftest.c`:

```c
opt_int_step(OPT_PATHBAR_BRIGHTNESS, 1);
opt_int_step(OPT_SEARCHBAR_BRIGHTNESS, -1);
opt_bool_toggle(OPT_TRANSPARENT_PATHBAR);
ST_CHECK(g_pathbar_brightness == 5 && g_searchbar_brightness == -5 && g_transparent_pathbar);
settings_save(); settings_load();
ST_CHECK(g_pathbar_brightness == 5 && g_searchbar_brightness == -5 && g_transparent_pathbar);
opt_int_step(OPT_PATHBAR_BRIGHTNESS, -1);
opt_int_step(OPT_SEARCHBAR_BRIGHTNESS, 1);
opt_bool_toggle(OPT_TRANSPARENT_PATHBAR);
settings_defaults();
ST_CHECK(g_pathbar_brightness == 0 && g_searchbar_brightness == 0 && !g_transparent_pathbar);
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```sh
./build.sh debug && ./build/debug/hermes --selftest-m9
```

Expected: compile failure for the three new `OPT_*` identifiers.

- [ ] **Step 3: Implement the option IDs, rows and handlers**

In `src/options.c`:

1. Add `OPT_PATHBAR_BRIGHTNESS, OPT_SEARCHBAR_BRIGHTNESS, OPT_TRANSPARENT_PATHBAR` after `OPT_PREVIEW_BRIGHTNESS` in the enum. Keep this order so the existing contiguous brightness range still maps to `SURFACE_*`.
2. Add three `OC_APPEARANCE` rows after `OPT_HEADER_BRIGHTNESS`: two `OK_INT` rows labelled "Path bar and pills brightness" and "Folder search background brightness", and one `OK_BOOL` row labelled "Transparent path bar".
3. Add cases to `opt_bool_get()` and `opt_bool_toggle()` for `OPT_TRANSPARENT_PATHBAR`; the toggle edits `g_transparent_pathbar` and marks settings dirty through the existing function tail.
4. Add cases to `opt_int_get()` and `opt_int_step()` for the two brightness IDs; step by 5 within -30..30.
5. Extend `opt_int_fmt()` so the two new IDs format as `%+d%%`.

- [ ] **Step 4: Run the test to verify it passes**

Run the Step 2 commands again.

Expected: `--selftest-m9` exits 0.

- [ ] **Step 5: Commit**

```bash
git add src/options.c src/selftest.c
git commit -m "feat(options): expose navigation field controls"
```

---

### Task 4: Wire the renderer

**Files:**
- Modify: `src/render.c:587-594`, `src/render.c:2484-2489`

**Interfaces:**
- Consumes: `pathbar_field_color()`, `searchbar_field_color()`, `pathbar_field_fill_alpha()` from Task 1.

- [ ] **Step 1: Change the two draw sites**

In `draw_toolbar_content()`, replace the path fill with `CA(pathbar_field_color(), pathbar_field_fill_alpha())`. When `g_transparent_pathbar` is true and not editing, draw a faint `CA(COL_BORDER, 0.7f)` rounded outline using `S(NAV_CORNER_RADIUS)` so the field stays discoverable. Keep the existing accent focus outline while editing.

In `draw_searchbar()`, replace `C(navigation_field_color())` with `C(searchbar_field_color())`. Keep the existing focused accent outline.

- [ ] **Step 2: Verify build, headless suites and screenshot smoke**

Run inside `fedora-build`:

```sh
./build.sh debug
./build/debug/hermes --selftest-m9
./build.sh release
./build/release/hermes --screenshot /tmp/hermes-fields.bmp /tmp/fe-st
```

Expected: builds clean, `--selftest-m9` exits 0, and the screenshot command exits 0 without Qt warnings. Confirm the binary has no Qt linkage with `ldd ./build/release/hermes | grep -i qt || echo "SDL3 build: no Qt"`.

- [ ] **Step 3: Commit**

```bash
git add src/render.c
git commit -m "feat(ui): use independent field backgrounds"
```

---

### Task 5: Document the settings and changelog

**Files:**
- Modify: `docs/settings.md:93-148`
- Modify: `CHANGELOG.md` (Unreleased)
- Modify: `docs/superpowers/specs/2026-11-21-navigation-field-colors-design.md` (status to accepted)

**Interfaces:**
- Consumes: final setting names and defaults from Tasks 2-4.

- [ ] **Step 1: Update settings documentation**

Add a paragraph after the existing brightness paragraph explaining that path bar/pills and the folder search field have independent brightness controls, and that "Transparent path bar" removes only the path fill while keeping a faint outline and leaving pills/search filled. Add the three keys to the saved-keys block with default `0`, `0`, `0`.

- [ ] **Step 2: Add a changelog entry**

Under `## Unreleased`, add a Changed bullet: "Path bar/pill and folder search backgrounds now have independent brightness controls, plus a path-only transparency option."

- [ ] **Step 3: Mark the design accepted**

Change `Status: proposed` to `Status: accepted` in the design doc.

- [ ] **Step 4: Verify docs and commit**

```sh
git diff --check
git add docs/settings.md CHANGELOG.md docs/superpowers/specs/2026-11-21-navigation-field-colors-design.md
git commit -m "docs(settings): document navigation field controls"
```

Expected: no whitespace errors and a clean commit.

---

### Task 6: Full verification and PR

**Files:**
- No source changes.

**Interfaces:**
- Consumes: all previous task commits.

- [ ] **Step 1: Run the complete local verification matrix in `fedora-build`**

```sh
cc -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-format-truncation \
   tools/appearance-selftest.c -o /tmp/fe-appearance-test -lm -ldl -pthread && /tmp/fe-appearance-test
./build.sh debug
./build/debug/hermes --selftest-m9
./build.sh release
python3 tools/build-install-selftest.py
python3 tools/branding-selftest.py
python3 tools/legacy-purge-selftest.py
sh tools/check-release-version.sh
if LC_ALL=C grep -nP "[^\x00-\x7F]" src/*.c src/*.h; then exit 1; fi
git diff --check
git verify-commit HEAD
```

Expected: every command exits 0; the ASCII guard prints nothing; `git verify-commit` says "Good git signature".

- [ ] **Step 2: Push and open the PR**

```bash
git push -u origin feat/ui/navigation-field-colors
gh pr create --base main --head feat/ui/navigation-field-colors \
  --title "feat(ui): add independent navigation field colors" \
  --body "Adds independent brightness controls for the path/pill field and folder search field, plus an opt-in transparent path background. See docs/superpowers/specs/2026-11-21-navigation-field-colors-design.md. Verified with appearance-selftest, --selftest-m9, debug/release builds, Python suites, release-version check, ASCII guard and git diff --check."
```

- [ ] **Step 3: Wait for required checks, then merge**

```bash
gh pr checks <number> --watch --interval 30 --fail-fast
gh pr merge <number> --rebase --delete-branch
git switch main && git pull --ff-only
```

Expected: all seven required checks pass; merge completes; local `main` matches `origin/main`.

- [ ] **Step 4: Record the verified merge commit locally**

Append the merged commit SHA and GitHub Verified result to `docs/verified-commits.md`. Do not commit that local runbook.
