"""Both boxes play the same sounds the same way (issue #44, Casper 2026-10-13).

The rules and the tones live in firmware/common/box_sound.h and are tested in
server_terminal_test.cpp. What those tests cannot see is whether a sketch plays
them. Up to 0.8.0 the ESP32 did not: it never took the server's sound out of
the shared terminal and kept a tone table of its own, so a request here, an
answer or a train on its way were silent on the ESP32 while the ESP8266 beeped.
The sketches are not compiled on the host, so this reads them.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKETCHES = {
    "ESP32": ROOT / "firmware/esp32/TrainMeetTMBox.ino",
    "ESP8266": ROOT / "firmware/esp8266/TrainMeetTMBox8266/TrainMeetTMBox8266.ino",
}


def body(source: str, signature: str) -> str:
    """The braces of the function defined as `signature` (not its declaration)."""
    found = re.search(re.escape(signature) + r"\s*\{", source)
    if not found:
        raise AssertionError(f"no definition of {signature}")
    open_brace = found.end() - 1
    depth = 0
    for index in range(open_brace, len(source)):
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        if depth == 0:
            return source[open_brace:index + 1]
    raise AssertionError(f"no end for {signature}")


class BothBoxesPlayTheSharedSoundsTest(unittest.TestCase):
    def sketches(self):
        for name, path in SKETCHES.items():
            yield name, path.read_text(encoding="utf-8")

    def test_the_servers_news_is_played_after_each_message(self):
        for name, source in self.sketches():
            with self.subTest(name):
                self.assertRegex(source, r"terminal\.receive\([^;]*\);\s*playSound\(terminal\.takeSound\(\)\);")

    def test_losing_and_getting_back_the_server_is_followed_every_loop(self):
        for name, source in self.sketches():
            with self.subTest(name):
                self.assertIn("TrainMeetSound::LinkWatch serverLink;", source)
                self.assertIn("playSound(serverLink.observe(terminal.started && terminal.fresh));", body(source, "void loop()"))

    def test_the_setup_portal_is_not_a_lost_server(self):
        for name, source in self.sketches():
            with self.subTest(name):
                portal = "void startSetupPortal()" if name == "ESP32" else "void startPortal()"
                self.assertIn("serverLink.forget()", body(source, portal))

    def test_the_tones_come_from_the_shared_table(self):
        for name, source in self.sketches():
            with self.subTest(name):
                play = body(source, "void playSound(TrainMeetSound::Kind kind)")
                self.assertIn("TrainMeetSound::toneFor(kind)", play)
                self.assertRegex(play, r"tone\([A-Z_]+, note\.hz, note\.ms\)")
                # No table of its own: no tone() with numbers anywhere in the sketch.
                self.assertNotRegex(source, r"tone\([^)]*\b\d{3,4}\b")
                self.assertNotRegex(source, r"frequency\s*=\s*\d")

    def test_the_esp32_no_longer_decides_sounds_with_the_old_local_core(self):
        source = SKETCHES["ESP32"].read_text(encoding="utf-8")
        self.assertNotIn('#include "attention.h"', source)
        self.assertNotIn("signalAttention", source)


if __name__ == "__main__":
    unittest.main()
