#!/usr/bin/env python3
"""Run: python3 tools/branding-selftest.py."""
from pathlib import Path
import struct
import subprocess
import unittest
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parent.parent


class BrandingTest(unittest.TestCase):
    def test_generated_art_is_current(self):
        subprocess.run(["python3", "tools/gen_logo.py", "--check"], cwd=ROOT, check=True)

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
