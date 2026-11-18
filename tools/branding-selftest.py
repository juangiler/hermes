#!/usr/bin/env python3
"""Run: python3 tools/branding-selftest.py."""
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parent.parent


class BrandingTest(unittest.TestCase):
    def test_generated_art_is_current(self):
        subprocess.run(["python3", "tools/gen_logo.py", "--check"], cwd=ROOT, check=True)

    def test_check_tolerates_different_zlib_encoding(self):
        # CI uses a different zlib than the developer machine; equivalent PNG pixels
        # must not be reported as stale just because the IDAT stream differs.
        sys.path.insert(0, str(ROOT / "tools"))
        import gen_logo
        real_compress = gen_logo.zlib.compress
        argv = sys.argv
        try:
            gen_logo.zlib.compress = lambda data, level: real_compress(data, 6)
            sys.argv = ["gen_logo.py", "--check"]
            gen_logo.main()
        finally:
            gen_logo.zlib.compress = real_compress
            sys.argv = argv

    def test_check_detects_changed_pixels(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import gen_logo
        rows = gen_logo.sprite()
        rows[0] = ("." if rows[0][0] != "." else "O") + rows[0][1:]
        altered = gen_logo.png(rows, 16)
        self.assertFalse(gen_logo.matches(ROOT / "assets/branding/hermes-16.png", altered))

    def test_svg_pixel_geometry(self):
        svg = ET.parse(ROOT / "assets/branding/hermes.svg").getroot()
        self.assertEqual(svg.attrib["viewBox"], "0 0 32 32")
        pixels = list(svg)
        self.assertGreater(len(pixels), 100)
        for pixel in pixels:
            self.assertTrue(pixel.tag.endswith("rect"))
            for axis in ("x", "y"):
                self.assertGreaterEqual(int(pixel.attrib[axis]), 0)
                self.assertLess(int(pixel.attrib[axis]), 32)

    def test_png_sizes_and_alpha(self):
        for size in (16, 24, 32, 48, 64, 128, 256):
            data = (ROOT / f"assets/branding/hermes-{size}.png").read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack(">II", data[16:24]), (size, size))
            self.assertEqual(data[24:26], b"\x08\x06")  # 8-bit RGBA
            pos, packed = 8, bytearray()
            while pos < len(data):
                length = struct.unpack(">I", data[pos:pos + 4])[0]
                if data[pos + 4:pos + 8] == b"IDAT":
                    packed.extend(data[pos + 8:pos + 8 + length])
                pos += 12 + length
            raw = zlib.decompress(packed)
            self.assertEqual(len(raw), size * (1 + size * 4))
            self.assertEqual(raw[4], 0)  # top-left remains transparent
            alphas = [
                raw[y * (1 + size * 4) + 4 + 4 * x]
                for y in range(size) for x in range(size)
            ]
            self.assertIn(255, alphas)
            self.assertIn(0, alphas)


if __name__ == "__main__":
    unittest.main()
