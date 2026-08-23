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
import shutil
import subprocess
import tempfile
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


class ProfileTwoIsTheProductTest(unittest.TestCase):
    """One pin number, one place.

    The v2 hardware specification carries a pin table, `hardware_profile.h`
    carries the same numbers as constants, and the enclosure gets drilled from
    the first while the firmware is built from the second. A table and a header
    that disagree is a box that is soldered wrong.

    So the numbers are read out of the compiled header and diffed against the
    document, rather than trusted to stay in step by hand.
    """

    SPEC = ROOT / "docs/TMBOX-V2-HARDWARE.md"
    LEGACY = ROOT / "docs/TMBOX-V1-LEGACY.md"
    HEADER = ROOT / "firmware/esp32/hardware_profile.h"
    PLATFORMIO = ROOT / "firmware/esp32/platformio.ini"

    #: Radrubrik i specens pinntabell → konstant i profilen.
    PINS = {
        "Knappsats rad R1": "TMBOX_ROW_PINS[0]",
        "Knappsats rad R2": "TMBOX_ROW_PINS[1]",
        "Knappsats rad R3": "TMBOX_ROW_PINS[2]",
        "Knappsats rad R4": "TMBOX_ROW_PINS[3]",
        "Knappsats kolumn C1": "TMBOX_COL_PINS[0]",
        "Knappsats kolumn C2": "TMBOX_COL_PINS[1]",
        "Knappsats kolumn C3": "TMBOX_COL_PINS[2]",
        "Knappsats kolumn C4": "TMBOX_COL_PINS[3]",
        "I2C SDA": "TMBOX_LCD_SDA",
        "I2C SCL": "TMBOX_LCD_SCL",
        "Summer": "TMBOX_BUZZER_PIN",
        "Status röd": "TMBOX_STATUS_LED_RED",
        "Status grön": "TMBOX_STATUS_LED_GREEN",
        "Status blå": "TMBOX_STATUS_LED_BLUE",
        "Provisioneringsknapp": "TMBOX_PROVISION_BUTTON",
    }

    def _compiled(self) -> dict[str, int]:
        """Read the numbers the compiler sees, not the ones the file says.

        Arduino.h is not available here, so it is swapped for <cstdint>. The
        profile uses nothing else from it.
        """

        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ saknas")
        with tempfile.TemporaryDirectory() as work:
            work_path = Path(work)
            header = self.HEADER.read_text(encoding="utf-8").replace(
                "#include <Arduino.h>", "#include <cstdint>"
            )
            (work_path / "profile.h").write_text(header, encoding="utf-8")
            fields = "\n".join(
                f'  printf("{name}=%d\\n", (int)({name}));' for name in self.PINS.values()
            )
            (work_path / "probe.cpp").write_text(
                "#include <cstdio>\n"
                "#define TMBOX_HARDWARE_PROFILE 2\n"
                '#include "profile.h"\n'
                "int main() {\n"
                f"{fields}\n"
                '  printf("cols=%d\\n", (int)TMBOX_LCD_COLUMNS);\n'
                '  printf("rows=%d\\n", (int)TMBOX_LCD_ROWS);\n'
                '  printf("addr=%d\\n", (int)TMBOX_LCD_ADDRESS);\n'
                '  printf("buzzer=%d\\n", (int)TMBOX_HAS_BUZZER);\n'
                "  return 0;\n}\n",
                encoding="utf-8",
            )
            build = subprocess.run(
                [compiler, "-std=c++17", str(work_path / "probe.cpp"), "-o", str(work_path / "probe")],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(0, build.returncode, build.stderr)
            run = subprocess.run([str(work_path / "probe")], capture_output=True, text=True, timeout=60)
            self.assertEqual(0, run.returncode, run.stderr)

        values = {}
        for line in run.stdout.splitlines():
            key, _, value = line.partition("=")
            values[key] = int(value)
        return values

    def _spec_pins(self) -> dict[str, int]:
        found = {}
        for line in self.SPEC.read_text(encoding="utf-8").splitlines():
            cells = [cell.strip() for cell in line.split("|")]
            if len(cells) < 4:
                continue
            if cells[1] in self.PINS and cells[2].isdigit():
                found[cells[1]] = int(cells[2])
        return found

    def test_the_spec_and_the_profile_agree_on_every_pin(self) -> None:
        compiled = self._compiled()
        documented = self._spec_pins()

        missing = sorted(set(self.PINS) - set(documented))
        self.assertEqual([], missing, "pinnar saknas i specens tabell")

        wrong = [
            f"{name}: specen säger {documented[name]}, profilen {compiled[self.PINS[name]]}"
            for name in self.PINS
            if documented[name] != compiled[self.PINS[name]]
        ]
        self.assertEqual([], wrong)

    def test_profile_two_is_the_default(self) -> None:
        """A box built from the spec must be what a plain build produces."""

        header = self.HEADER.read_text(encoding="utf-8")
        self.assertIn("#define TMBOX_HARDWARE_PROFILE 2", header)
        self.assertIn("default_envs = esp32-s3", self.PLATFORMIO.read_text(encoding="utf-8"))

    def test_the_display_is_twenty_by_four(self) -> None:
        compiled = self._compiled()
        self.assertEqual(20, compiled["cols"])
        self.assertEqual(4, compiled["rows"])
        self.assertEqual(0x27, compiled["addr"])

    def test_the_buzzer_is_wired_in_v2(self) -> None:
        """The attention policy was built and tested long before a pin existed."""

        self.assertEqual(1, self._compiled()["buzzer"])

    def test_the_legacy_boxes_are_documented_and_out_of_scope(self) -> None:
        legacy = self.LEGACY.read_text(encoding="utf-8")
        self.assertIn("ESP8266", legacy)
        self.assertIn("v1 Legacy", legacy)

    def test_the_build_does_not_target_esp8266(self) -> None:
        """The decision, pinned where it can actually be checked.

        An earlier version of this test tried to police prose - it looked for
        the words "porta till ESP8266" and duly flagged the sentence that
        states we will not. Intent in a paragraph is not mechanically
        checkable. A platform in a build file is.

        Somebody starting the port has to add an espressif8266 environment,
        and that is where this stops them long enough to ask why.
        """

        platformio = self.PLATFORMIO.read_text(encoding="utf-8")
        self.assertNotIn("espressif8266", platformio)
        self.assertIn("platform = espressif32", platformio)

    def test_the_legacy_document_records_the_decision(self) -> None:
        """So the next reader does not re-open a settled question."""

        legacy = self.LEGACY.read_text(encoding="utf-8")
        self.assertIn("portas", legacy)
        self.assertIn("mqttTamBox", legacy)


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
