#!/usr/bin/env python3
"""Static checks that legacy File Explorer compatibility stays purged."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
SELF = Path(__file__).resolve()

FORBIDDEN = (
    "migration_startup",
    "migration_finish",
    "migration_import",
    "migration_private_dir",
    "startup.lock",
    "HERMES_MIGRATION_RETRY",
    "HERMES_LEGACY_SLUG",
    "cloud_legacy_owned",
    "ig_identity_adopt",
    "identity_legacy_desktop",
    "HERMES_LEGACY_APP_ID",
    "FE_",
    "__fe_",
    "fe.bash",
    "fe.fish",
)



def active_files():
    files = sorted((ROOT / "src").glob("*.c"))
    files += sorted((ROOT / "src").glob("*.h"))
    files += sorted((ROOT / "tools").glob("*.c"))
    files += sorted((ROOT / "tools").glob("*.py"))
    files += [ROOT / "build.sh"]
    files += sorted(p for p in (ROOT / "packaging").rglob("*") if p.is_file())
    return [p for p in files if p.resolve() != SELF]


def test_no_migration_symbols_in_active_code(errors):
    for path in active_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix()
        for needle in FORBIDDEN:
            if needle in text:
                errors.append(f"{rel} contains {needle}")
    main = (ROOT / "src/main.c").read_text(encoding="utf-8")
    if 'include "migration.c"' in main:
        errors.append("src/main.c still includes migration.c")
    app = (ROOT / "src/app.c").read_text(encoding="utf-8")
    if "migration/startup unavailable" in app:
        errors.append("src/app.c still has the migration startup error message")


def test_legacy_directory_not_referenced(errors):
    if (ROOT / "src/migration.c").exists():
        errors.append("src/migration.c still exists")


def test_no_legacy_app_name_in_ui(errors):
    for rel in ("src/header_preferences.c", "src/options.c", "tools/appearance-selftest.c"):
        text = (ROOT / rel).read_text(encoding="utf-8")
        if "File-Explorer" in text:
            errors.append(f"{rel} still shows the legacy app name")


def test_checker_is_wired_into_ci_and_docs(errors):
    command = "python3 tools/legacy-purge-selftest.py"
    ci = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    if command not in ci:
        errors.append("ci.yml does not run the legacy purge checker")
    docs = (ROOT / "docs/building.md").read_text(encoding="utf-8")
    if command not in docs:
        errors.append("docs/building.md does not list the legacy purge checker")
    for suite in ("tools/credentials-selftest.c", "tools/cloud-identity-selftest.c"):
        if suite not in docs:
            errors.append(f"docs/building.md does not list {suite}")


HISTORICAL_DOCS = (
    "docs/superpowers/specs/2026-11-17-hermes-identity-design.md",
    "docs/superpowers/plans/2026-11-17-hermes-identity.md",
    "docs/superpowers/specs/2026-11-18-portal-removal-default-fm-and-webdav-icon-design.md",
    "docs/superpowers/plans/2026-11-18-portal-removal-default-fm-and-webdav-icon.md",
)


def test_migration_page_is_historical(errors):
    text = (ROOT / "docs/migration.md").read_text(encoding="utf-8")
    if "removed" not in text.lower():
        errors.append("docs/migration.md does not say legacy support was removed")
    for phrase in ("migration imports", "are migrated lazily",
                   "handover is deferred", "accepted for compatibility"):
        if phrase in text:
            errors.append(f"docs/migration.md still promises {phrase!r}")


def test_changelog_records_purge(errors):
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    if "## Unreleased" not in text:
        errors.append("CHANGELOG.md has no Unreleased section")
        return
    section = text.split("## Unreleased", 1)[1].split("\n## ", 1)[0].lower()
    if "early hermes" not in section or "fully purged" not in section:
        errors.append("CHANGELOG.md Unreleased does not record the early support and full purge")


def test_historical_docs_marked_superseded(errors):
    for rel in HISTORICAL_DOCS:
        path = ROOT / rel
        if not path.exists():
            errors.append(f"{rel} is missing")
        elif "later fully purged" not in path.read_text(encoding="utf-8"):
            errors.append(f"{rel} lacks the superseded banner")


def main():
    errors = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn(errors)
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        print(f"legacy-purge: {len(errors)} problem(s)")
        return 1
    print("legacy-purge: static checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
