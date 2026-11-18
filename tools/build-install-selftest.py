#!/usr/bin/env python3
"""Run: python3 tools/build-install-selftest.py (no compiler/SDL required)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class BuildInstallTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="hermes-build-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for name in ("build.sh", "install.sh", "LICENSE", "README.md", "MAINTENANCE.md", "CHANGELOG.md"):
            shutil.copy2(ROOT / name, self.root / name)
        for name in ("assets", "packaging", "docs"):
            if (ROOT / name).exists():
                shutil.copytree(ROOT / name, self.root / name)
        (self.root / "src").mkdir()
        (self.root / "src/main.c").write_text("/* compiler fixture */\n")
        shutil.copy2(ROOT / "src/app.c", self.root / "src/app.c")
        (self.root / "tools").mkdir()
        for name in ("source-archive.sh", "check-release-version.sh", "sync-package-versions.sh"):
            shutil.copy2(ROOT / "tools" / name, self.root / "tools" / name)
        for source in ("stb_image_LICENSE.txt", "nanosvg_LICENSE.txt",
                       "material-icons/LICENSE", "icons-catppuccin/LICENSE",
                       "icons-symbols/LICENSE"):
            target = self.root / "third_party" / source
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / "third_party" / source, target)
        fakebin = self.root / "fakebin"
        fakebin.mkdir()
        self.env = dict(os.environ)
        for key in list(self.env):
            if key.startswith(("HERMES_", "CFLAGS_PKG", "LDFLAGS_PKG")):
                self.env.pop(key)
        self.env["PATH"] = str(fakebin) + os.pathsep + self.env["PATH"]
        self.env["CC"] = str(fakebin / "cc")
        (fakebin / "pkg-config").write_text("#!/bin/sh\nexit 0\n")
        (fakebin / "cc").write_text(
            "#!/bin/sh\n"
            "printf '%s\\n' \"$*\" >> compiler.log\n"
            "while [ \"$#\" -gt 0 ]; do\n"
            "  if [ \"$1\" = -o ]; then shift; out=$1; fi\n"
            "  shift\n"
            "done\n"
            "printf '#!/bin/sh\\necho hermes-fixture\\n' > \"$out\"\n"
            "chmod 755 \"$out\"\n"
        )
        for p in fakebin.iterdir():
            p.chmod(0o755)

    def run_script(self, script, *args, ok=True, **extra):
        env = dict(self.env, **extra)
        result = subprocess.run(
            ["sh", script, *args], cwd=self.root, env=env,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def test_build_modes_do_not_overwrite_each_other(self):
        for mode, binary in (("release", "hermes"), ("debug", "hermes"),
                             ("package", "hermes"), ("demo", "hermes-demo")):
            self.run_script("build.sh", mode)
            path = self.root / "build" / mode / binary
            self.assertTrue(path.is_file(), str(path))
        self.assertFalse((self.root / "build/file-explorer").exists())
        commands = (self.root / "compiler.log").read_text().splitlines()
        self.assertIn("-fsanitize=address,undefined", commands[1])
        self.assertNotIn("-march=native", commands[2])

    def test_build_rejects_missing_dependency_versions(self):
        pkgconfig = self.root / "fakebin/pkg-config"
        pkgconfig.write_text("#!/bin/sh\n[ \"$1\" != --atleast-version=3.2 ]\n")
        self.run_script("build.sh", "package", ok=False)
        self.assertFalse((self.root / "build/package/hermes").exists())

    def test_output_override(self):
        self.run_script("build.sh", "package", HERMES_OUT="chosen/hermes")
        self.assertTrue((self.root / "chosen/hermes").is_file())

    def test_staged_install_uses_package_build(self):
        self.run_script("build.sh", "package")
        stage = self.root / "stage"
        self.run_script("install.sh", DESTDIR=str(stage), PREFIX="/usr")
        installed = stage / "usr"
        self.assertTrue((installed / "bin/hermes").is_file())
        desktop = installed / "share/applications/io.github.juangiler.hermes.desktop"
        text = desktop.read_text()
        self.assertIn('Exec="/usr/bin/hermes" %F', text)
        self.assertIn("Name=Hermes\n", text)
        self.assertTrue((installed / "share/icons/hicolor/scalable/apps/io.github.juangiler.hermes.svg").is_file())
        self.assertTrue((installed / "share/polkit-1/actions/io.github.juangiler.hermes.policy").is_file())
        self.assertTrue((installed / "lib/udev/rules.d/70-hermes-phones.rules").is_file())
        self.assertFalse((installed / "bin/file-explorer").exists())
        # local gitignored notes are not shipped (as in the source archive)
        self.assertFalse((installed / "share/doc/hermes/docs/checkpoint.md").exists())
        self.assertFalse((installed / "share/doc/hermes/docs/PR-details.md").exists())
        policy = (installed / "share/polkit-1/actions/io.github.juangiler.hermes.policy").read_text()
        self.assertIn("/usr/bin/hermes", policy)
        self.assertNotIn("@BINDIR@", policy)

    def test_install_rejects_missing_build_and_invalid_mode(self):
        self.run_script("install.sh", ok=False, DESTDIR=str(self.root / "stage"))
        self.run_script("build.sh", "unknown", ok=False)
        self.run_script("install.sh", ok=False, HERMES_BUILD_MODE="../src",
                        DESTDIR=str(self.root / "stage"))

    def test_install_explicit_mode_and_prefix_with_spaces(self):
        self.run_script("build.sh", "debug")
        stage = self.root / "stage"
        self.run_script("install.sh", HERMES_BUILD_MODE="debug",
                        DESTDIR=str(stage), PREFIX="/opt/Hermes Tools")
        desktop = stage / "opt/Hermes Tools/share/applications/io.github.juangiler.hermes.desktop"
        self.assertIn('Exec="/opt/Hermes Tools/bin/hermes" %F', desktop.read_text())

    def test_install_rejects_unsafe_prefix(self):
        self.run_script("build.sh", "package")
        for prefix in ("relative", "/opt/a&b", '/opt/a"b', "/opt/a\nb", "/opt/a%b"):
            self.run_script("install.sh", ok=False, PREFIX=prefix,
                            DESTDIR=str(self.root / "stage"))

    def test_source_archive_excludes_local_and_generated_state(self):
        for directory in (".git", ".superpowers", "build", "dist"):
            (self.root / directory).mkdir(exist_ok=True)
            (self.root / directory / "private-marker").write_text("not source")
        (self.root / ".env").write_text("fixture only")
        (self.root / "src/.env").write_text("nested fixture only")
        (self.root / "src/.git").mkdir()
        (self.root / "src/.git/private-marker").write_text("not source")
        self.run_script("tools/source-archive.sh")
        import tarfile
        archive = next((self.root / "dist").glob("hermes-*.tar.gz"))
        with tarfile.open(archive) as tar:
            names = ["/".join(Path(name).parts[1:]) for name in tar.getnames()]
        self.assertIn("assets/branding/hermes.svg", names)
        self.assertIn("src/app.c", names)
        self.assertNotIn(".env", names)
        self.assertNotIn("src/.env", names)
        self.assertFalse(any("private-marker" in name for name in names))
        self.assertNotIn("docs/checkpoint.md", names)

    def test_package_version_check_and_sync(self):
        self.run_script("tools/check-release-version.sh")
        self.run_script("tools/check-release-version.sh", "v999.0.0", ok=False)
        spec = self.root / "packaging/rpm/hermes.spec"
        before = spec.read_text()
        spec.write_text(before.replace("Version:", "IgnoredVersion:", 1))
        self.run_script("tools/check-release-version.sh", ok=False)
        spec.write_text(before)
        self.run_script("tools/sync-package-versions.sh")
        self.run_script("tools/check-release-version.sh")

    def test_appimage_requires_explicit_provisioning_and_tool_checksum(self):
        result = self.run_script("packaging/appimage/build.sh", ok=False)
        self.assertIn("LINUXDEPLOY_SHA256", result.stdout)
        self.assertFalse((self.root / "build/appimage").exists())


if __name__ == "__main__":
    unittest.main()
