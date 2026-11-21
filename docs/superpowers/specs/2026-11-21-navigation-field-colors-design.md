# Design: independent navigation field colors

Date: 2026-11-21
Status: accepted

## Intent

In dark themes the navigation path bar and the pill-tab enclosure are not dark
enough. Today the path field, pill enclosure, inactive classic tabs and folder
search field all use one color helper in `src/theme.c`, adjusted only by the
existing Header brightness control. That cannot darken path/pills without also
changing the whole header strip, and it cannot make only the path background
transparent.

## Goals

- Give the path field and pill enclosure their own brightness control.
- Give the folder search field its own brightness control.
- Add an option that makes only the path field background transparent.
- Keep text, focus outlines, accent/selection colors and other surfaces intact.

## Non-goals

- No change to Header, Sidebar, File area, Status or Preview brightness.
- No change to accent, navigation selection opacity, tab style or tab position.
- No Qt/Kirigami work and no portal behavior changes.
- No change to GitHub branch protection or required-signatures rules.

## Constraints

- C11 SDL3 unity build: include order and unique `static` names matter.
- Source files must stay pure ASCII; warnings fail CI.
- New settings need `src/settings.c` load/save/reset, `src/options.c` rows,
  `docs/settings.md`, `CHANGELOG.md` under Unreleased, and headless tests.
- PR-only `main`; work lands on a topic branch through a PR.

## Approaches

1. **Independent controls (chosen).** Add `pathbar_brightness`,
   `searchbar_brightness` and `transparent_pathbar`. Direct match for the
   request; small renderer/settings change; no existing setting changes meaning.
2. **Minimal controls.** Keep Header brightness for path/pills and add only
   search brightness plus transparency. Fewer settings, but darkening the path
   also darkens the title strip and nav buttons, which misses the request.
3. **Opacity slider.** Replace the transparency toggle with a 0-100% path opacity
   control. More flexible, but more UI and test surface than the asked-for option.

## Decisions

- Settings keys: `pathbar_brightness`, `searchbar_brightness`,
  `transparent_pathbar`; defaults `0`, `0`, `0` for new installs. Legacy files
  without the two brightness keys inherit the saved header brightness once during
  `settings_migrate()` so existing installs keep their appearance; explicit new
  keys in a legacy file win.
- Brightness range: -30 to +30 in 5% steps, matching the existing surface sliders.
- Add `pathbar_field_color()` and `searchbar_field_color()` beside the existing
  `navigation_field_color()`, which stays the neutral text-selection base so
  changing the path bar does not recolor text selection everywhere.
  `tab_container_color()` and inactive classic tabs follow the pathbar helper;
  the folder search field uses the searchbar helper.
- Transparency applies only to the path field fill at the draw site. Pills and
  search stay filled. A faint `COL_BORDER` outline stays when transparent so the
  field remains discoverable; the accent focus outline while editing stays.
- Options rows go under Appearance > Surfaces, after Header brightness:
  "Path bar and pills brightness", "Folder search background brightness",
  "Transparent path bar".
- Tests live in `tools/appearance-selftest.c`: each new brightness must affect
  only its own surface, reapplying the theme must not accumulate changes, and the
  transparency flag must not alter pill or search colors.

## Verified commits

Before the feature commit, set up dedicated SSH commit signing: generate a
separate signing key, add its public half to GitHub as a signing key, configure
`allowed_signers`, and prove on a scratch commit that GitHub reports Verified.
Do not enable the ruleset's `required_signatures` rule unless separately asked.
Record the result in the local `docs/verified-commits.md` runbook.

## Builder calls

- Keep the existing 5% black mix in the field color helpers; only the new
  brightness inputs differ.
- Use a boolean setting for transparency rather than an opacity percentage.
- No visual change to custom JSON theme keys; the new settings sit above themes.
