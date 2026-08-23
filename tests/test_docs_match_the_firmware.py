"""Documentation that describes keys the box does not have.

`docs/underlag/tmbox-flodesbild.html` showed `D=MER` in seventeen places. The
letter `'D'` does not appear once in `navigation.cpp` - the key is wired in
hardware and does nothing in software. Anyone reading that file would have
built against a control that cannot be pressed.

It was replaced rather than corrected, which left links pointing at a file
that no longer exists. Both failures are mechanical, so both are checked here
rather than by eye.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAVIGATION = ROOT / "firmware/esp32/lib/tmbox_core/navigation.cpp"

#: Files that deliberately preserve superseded decisions. They carry a banner
#: saying so, and they are allowed to describe a box that never got built.
HISTORY = {
    "docs/underlag/tmbox-monsterprompt-claude.md",
    "docs/underlag/tmbox-monsterprompt-v2.md",
    "docs/underlag/gap-analys.md",
}


def _documents() -> list[Path]:
    found = [ROOT / "README.md"]
    found.extend(sorted(ROOT.glob("docs/**/*.md")))
    return found


def _relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


class KeysTheFirmwareActuallyHandlesTest(unittest.TestCase):
    def _handled(self) -> set[str]:
        source = NAVIGATION.read_text(encoding="utf-8")
        return set(re.findall(r"key == '([A-D0-9*#])'", source))

    def test_the_d_key_is_still_unhandled(self) -> None:
        """The premise of the test below.

        If somebody implements D, this fails first and says so, instead of the
        documentation test quietly starting to forbid something legitimate.
        """

        handled = self._handled()
        self.assertTrue(handled, "hittade inga tangentfall alls i navigation.cpp")
        self.assertNotIn(
            "D",
            handled,
            "D hanteras nu i firmware - ta bort den här spärren och låt "
            "dokumentationen beskriva vad tangenten gör",
        )

    def test_a_document_showing_the_d_key_says_it_is_not_built(self) -> None:
        """The spec may specify D. It may not imply a box does it.

        `docs/tmbox.md` describes a departure flow with `D=MER` and an
        arrival action `D=NÄRMAR SIG`. That is intent, and intent belongs in a
        spec - but a reader seeing a rendered frame reasonably assumes the
        frame is rendered. So a document that shows the key must also say the
        key is dead, and this pins that pairing rather than the wording.
        """

        # Two phrasings are accepted rather than one, because the disclaimer
        # reads differently in a spec ("specified but not built") than in a
        # note explaining why a file was retired ("does not appear once in
        # navigation.cpp"). Both say the key is dead; neither is the wording
        # under test.
        markers = ("specificerad men inte byggd", "förekommer inte en enda gång")
        offenders = []
        for document in _documents():
            name = _relative(document)
            if name in HISTORY:
                continue
            body = document.read_text(encoding="utf-8")
            # `D=` followed by a word is a key presented as an action.
            # "D saknar funktion" and "`'D'` förekommer inte" both pass.
            if not re.search(r"\bD\s*=\s*[A-ZÅÄÖ]{2,}", body):
                continue
            if not any(marker in body for marker in markers):
                offenders.append(name)

        self.assertEqual(
            [],
            offenders,
            "dokument visar D-tangenten utan att säga att den inte är byggd",
        )

    def test_the_golden_frames_agree_that_d_draws_nothing(self) -> None:
        """The evidence the note in the spec rests on."""

        frames = (ROOT / "firmware/esp32/test_native/golden_frames.txt").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("MER", frames)


class BoardMismatchTest(unittest.TestCase):
    """The spec named a microcontroller the built boxes do not have.

    `docs/tmbox.md` said "ESP32-S3" while `platformio.ini` builds for
    `esp32dev`, and the five boxes that exist are ESP8266 nodeMCU V3. Three
    different answers to one question, and the one in the spec was the one
    nobody could check without opening a box.

    Until the firmware and the hardware agree, any document naming a board
    has to say that they do not.
    """

    FACTS = ROOT / "firmware/esp32/HARDWARE-FACTS.md"
    PLATFORMIO = ROOT / "firmware/esp32/platformio.ini"

    def test_the_firmware_still_targets_esp32(self) -> None:
        """The premise. When the port lands, this fails first and says so."""

        self.assertIn("platform = espressif32", self.PLATFORMIO.read_text(encoding="utf-8"))

    def test_the_built_boxes_are_recorded_as_esp8266(self) -> None:
        self.assertIn("ESP8266", self.FACTS.read_text(encoding="utf-8"))

    def test_the_spec_says_the_two_do_not_match(self) -> None:
        """A reader who only opens tmbox.md must not come away thinking the
        firmware fits the boxes."""

        spec = (ROOT / "docs/tmbox.md").read_text(encoding="utf-8")
        self.assertIn("HARDWARE-FACTS.md", spec)
        self.assertIn("ESP8266", spec)

    def test_no_current_document_names_a_board_without_the_caveat(self) -> None:
        """`ESP32-S3` was asserted flatly in the spec's platform section."""

        offenders = []
        for document in _documents():
            name = _relative(document)
            if name in HISTORY:
                continue
            body = document.read_text(encoding="utf-8")
            if not re.search(r"ESP32[\s-]?S3", body):
                continue
            if "HARDWARE-FACTS" not in body and "ESP8266" not in body:
                offenders.append(name)
        self.assertEqual([], offenders, "dokument namnger ESP32-S3 utan förbehåll")


class DocumentationLinksTest(unittest.TestCase):
    #: Skips protocols, anchors and bare fragments - only repo-relative paths
    #: are ours to keep working.
    LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")

    def test_every_relative_link_points_at_a_file_that_exists(self) -> None:
        broken = []
        for document in _documents():
            body = document.read_text(encoding="utf-8")
            for target in self.LINK.findall(body):
                target = target.split()[0].strip()
                if target.startswith(("http://", "https://", "#", "mailto:")):
                    continue
                path = target.split("#", 1)[0]
                if not path:
                    continue
                resolved = (document.parent / path).resolve()
                if not resolved.exists():
                    broken.append(f"{_relative(document)} → {target}")

        self.assertEqual([], broken, "länkar pekar på filer som inte finns")

    def test_the_retired_flow_diagram_is_not_linked_anywhere(self) -> None:
        """It was removed because it disagreed with the firmware.

        A link to it coming back means somebody restored the file, or wrote a
        link from memory.
        """

        linked = [
            _relative(document)
            for document in _documents()
            if "](tmbox-flodesbild.html)" in document.read_text(encoding="utf-8")
            or "](docs/underlag/tmbox-flodesbild.html)" in document.read_text(encoding="utf-8")
        ]
        self.assertEqual([], linked)

    def test_the_replacement_reference_is_present(self) -> None:
        self.assertTrue((ROOT / "docs/underlag/tmbox-scenarier.html").exists())


if __name__ == "__main__":
    unittest.main()
