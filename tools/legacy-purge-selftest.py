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
)

# Task 2 replaces this helper with a credentials-local one; allow it here until then.
TEMP_ALLOWED = {"src/credentials.c": {"migration_private_dir"}}


def active_files():
    files = sorted((ROOT / "src").glob("*.c"))
    files += sorted((ROOT / "src").glob("*.h"))
    files += sorted((ROOT / "tools").glob("*.c"))
    files += [ROOT / "build.sh"]
    files += sorted(p for p in (ROOT / "packaging").rglob("*") if p.is_file())
    return [p for p in files if p.resolve() != SELF]


def test_no_migration_symbols_in_active_code(errors):
    for path in active_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix()
        for needle in FORBIDDEN:
            if needle in text and needle not in TEMP_ALLOWED.get(rel, set()):
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
