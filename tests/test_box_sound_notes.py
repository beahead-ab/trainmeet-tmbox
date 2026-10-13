"""The note table in box_sound.h is equal temperament (PR #47).

The first version had NOTE_GS3 308 and NOTE_B3 245; a typo in a note nobody
plays yet would only be heard the day someone picks it. This recomputes every
NOTE_* from A4 = 440 Hz.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "firmware/common/box_sound.h"
NAMES = ["C", "CS", "D", "DS", "E", "F", "FS", "G", "GS", "A", "AS", "B"]


class NoteTableTest(unittest.TestCase):
    def notes(self):
        found = re.findall(r"^#define NOTE_([A-Z]+)(\d)\s+(\d+)", HEADER.read_text(encoding="utf-8"), re.M)
        self.assertEqual(9 * 12, len(found), "octaves 0-8, twelve notes each")
        return [(name, int(octave), int(hz)) for name, octave, hz in found]

    def test_every_note_is_a440_equal_temperament_rounded(self):
        for name, octave, hz in self.notes():
            semitones = (octave - 4) * 12 + NAMES.index(name) - NAMES.index("A")
            with self.subTest(f"NOTE_{name}{octave}"):
                self.assertEqual(round(440 * 2 ** (semitones / 12)), hz)

    def test_the_notes_rise(self):
        values = [hz for _, _, hz in self.notes()]
        self.assertEqual(sorted(values), values)
        self.assertEqual(len(set(values)), len(values))


if __name__ == "__main__":
    unittest.main()
