# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""scripts/build-glyphs.py: the Latin letters it keeps from a Unifont .hex, the check of what it kept, and the C."""

import gzip
import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/build-glyphs.py'
spec = importlib.util.spec_from_file_location('build_glyphs', SCRIPT)
bg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bg)


def glyph(cp, wide=False):
    """a .hex line, its rows numbered after the code point"""
    return f'{cp:04X}:' + f'{cp & 0xFF:02X}' * (32 if wide else 16) + '\n'


class Glyphs(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix='snx-glyphs-')
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        (self.root / 'src/gui').mkdir(parents=True)
        (self.root / 'src/common/utilities').mkdir(parents=True)
        shutil.copy(ROOT / bg.FONT, self.root / bg.FONT)
        self.unifont = self.root / 'unifont.hex.gz'

    def run_script(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), '--root', str(self.root), *args], capture_output=True)

    def write_unifont(self, lines):
        with gzip.open(self.unifont, 'wt', encoding='ascii') as f:
            f.writelines(lines)

    def test_the_letters_kept(self):
        points = bg.wanted(ROOT)
        self.assertEqual(points, sorted(points))
        self.assertTrue({0xE3, 0xF5, 0xD5, 0x152, 0x219, 0x24F} <= set(points))  # the tilde's a and o, OE, s comma
        self.assertFalse(set(points) & bg.codepage(ROOT))  # CP437's stay its own
        self.assertNotIn(0xAD, points)  # the soft hyphen, 16 wide in Unifont
        self.assertEqual((ROOT / bg.KEPT).read_text().count('\n'), len(points))

    def test_from_check_and_the_c(self):
        points = bg.wanted(self.root)
        self.write_unifont([glyph(0x41)] + [glyph(cp) for cp in points] + [glyph(0xAD, True), glyph(0x4E00, True)])
        self.assertEqual(self.run_script('--from', str(self.unifont)).returncode, 0)
        kept = (self.root / bg.KEPT).read_text()
        self.assertEqual(kept, ''.join(glyph(cp) for cp in points))
        self.assertEqual(self.run_script('--check').returncode, 0)
        out = self.root / 'gui_latin.c'
        self.assertEqual(self.run_script('--out', str(out)).returncode, 0)
        text = out.read_text()
        self.assertIn(f'const int gui_latin_count = {len(points)};', text)
        self.assertIn('0x00e3,', text)
        self.assertIn(' '.join(f'0x{b:02x},' for b in bg.fit(bytes([0xE3] * 16))) + ' /* U+00E3 */', text)
        self.assertTrue(text.isascii())

    def test_fit(self):
        def rows(*pattern):  # (row, bits) pairs, the rest blank
            out = [0] * 16
            for r, bits in pattern:
                out[r] = bits
            return bytes(out)

        # a small letter: up the two blank rows to the VGA's baseline (row 11), its stems doubled leftwards
        o = [(6, 0x3C), (7, 0x42), (8, 0x42), (9, 0x42), (10, 0x42), (11, 0x42), (12, 0x42), (13, 0x3C)]
        self.assertEqual(bg.fit(rows(*o)), rows(*[(r - 2, b | b << 1) for r, b in o]))
        # a capital with its mark in the top rows: one of the two blank rows under the mark, then a repeated row of
        # its body (the lowest), as the VGA font's accented capitals
        mark = [(0, 0x32), (1, 0x4C)]
        body = [(4, 0x18), (5, 0x24), (6, 0x24), (7, 0x42), (8, 0x42), (9, 0x7E), (10, 0x42), (11, 0x42), (12, 0x42)]
        body.append((13, 0x42))
        fitted = bg.fit(rows(*mark, *body))
        doubled = [(r, (b | b << 1) & 0xFF) for r, b in mark + body]
        self.assertEqual(fitted[:3], bytes([doubled[0][1], doubled[1][1], 0]))  # the mark, one blank row
        self.assertEqual(fitted[3:12], bytes(b for _, b in doubled[2:10] + doubled[11:]))  # a 0x42 row fewer
        self.assertEqual(fitted[12:], bytes(4))
        # no blank row and no repeated one: it stays where it is
        full = bytes(range(1, 17))
        self.assertEqual(bg.fit(full), bytes((b | b << 1) & 0xFF for b in full))
        self.assertEqual(len(bg.fit(rows())), 16)

    def test_check_refuses(self):
        points = bg.wanted(self.root)
        kept = self.root / bg.KEPT
        lines = [glyph(cp) for cp in points]
        for broken in (
            lines[1:],  # one missing
            lines + [glyph(0x4E00)],  # one more
            lines[1:2] + lines[:1] + lines[2:],  # out of order
            [glyph(points[0], True)] + lines[1:],  # one 16 wide
        ):
            kept.write_text(''.join(broken))
            self.assertNotEqual(self.run_script('--check').returncode, 0)

    def test_from_refuses(self):
        points = bg.wanted(self.root)
        self.write_unifont([glyph(cp) for cp in points[1:]])  # one the font lacks
        self.assertNotEqual(self.run_script('--from', str(self.unifont)).returncode, 0)
        self.write_unifont([glyph(cp, cp == points[0]) for cp in points])  # one it draws 16 wide
        self.assertNotEqual(self.run_script('--from', str(self.unifont)).returncode, 0)
        self.write_unifont([glyph(cp) for cp in points] + ['not a glyph\n'])
        self.assertNotEqual(self.run_script('--from', str(self.unifont)).returncode, 0)


if __name__ == '__main__':
    unittest.main()
