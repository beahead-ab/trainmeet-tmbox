"""0.8.0: the ESP8266 box has a 16x2 or a 20x4 display, and a buzzer.

The record and the parsing are compiled and run on the host. The sketch itself
cannot be, so what it must do is pinned in its source: the same approach as
test_esp8266_network_setup.py.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "firmware/esp8266/TrainMeetTambox8266"


class DisplaySettingTest(unittest.TestCase):
    def test_record_and_portal_field_on_the_host(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ missing")
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / "display-test")
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/esp8266_display_test.cpp"), "-o", binary],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([binary], check=True, capture_output=True, text=True, timeout=30)
            self.assertIn("ESP8266 display setting: 3 scenarios passed", result.stdout)


class SketchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (SKETCH / "TrainMeetTambox8266.ino").read_text()

    def function(self, signature):
        return self.source.split(signature, 1)[1].split("\n}\n", 1)[0]

    def test_the_display_is_announced_in_every_hello(self):
        connect = self.function("void connectServer() {")
        self.assertIn("terminal.rows = lcdSize.rows; terminal.cols = lcdSize.cols;", connect)
        self.assertLess(connect.index("terminal.rows = lcdSize.rows"), connect.index("terminal.begin("))
        self.assertIn('"NodeMCU ESP8266 20x4"', connect)
        terminal = (ROOT / "firmware/common/server_terminal.h").read_text()
        self.assertIn('hello["display"]["rows"] = rows; hello["display"]["cols"] = cols;', terminal)

    def test_every_start_of_the_display_uses_the_chosen_size(self):
        self.assertNotIn("lcd.begin(16, 2, Wire)", self.source)
        # Startup, the LCD coming back, and a new size from the portal.
        self.assertEqual(3, self.source.count("lcd.begin(lcdSize.cols, lcdSize.rows, Wire);"))
        for call in re.findall(r"terminal\.draw\(lcd[^)]*\)", self.source):
            self.assertEqual("terminal.draw(lcd, lcdSize.cols, lcdSize.rows)", call)
        show = self.function("void showFrame(")
        self.assertIn("lcdSize.cols", show)
        self.assertIn("TrainMeetLcd::Screen screen(lcdSize.rows, lcdSize.cols);", show)

    def test_the_size_is_chosen_in_the_portal_and_kept_apart_from_the_other_records(self):
        self.assertIn("wifiManager.addParameter(&displaySize);", self.source)
        self.assertIn('WiFiManagerParameter displaySize("display"', self.source)
        # Shown with what is set now, so saving Wi-Fi again never changes it.
        start = self.function("void startPortal() {")
        self.assertIn("displaySize.setValue(TrainMeetDisplay::name(lcdSize), 4);", start)
        self.assertIn("applyDisplaySetting();", self.source.split("if (displayRequested)", 1)[1][:80])
        apply = self.function("void applyDisplaySetting() {")
        for step in ("TrainMeetDisplay::parse(displaySize.getValue()", "saveDisplaySetting();", "disconnectServer();"):
            self.assertIn(step, apply)
        self.assertIn("loadCachedDeviceUI(); loadDisplaySetting();", self.source)
        self.assertIn("constexpr int EEPROM_OFFSET = 1536;", (SKETCH / "display_setting.h").read_text())

    def test_the_buzzer_plays_the_esp32s_tones_for_news_only(self):
        play = self.function("void playSound(")
        self.assertIn("tone(TAMBOX_BUZZER_PIN, note.hz, note.ms);", play)
        self.assertIn("playSound(terminal.takeSound());", self.function("void receiveMessage("))
        # Losing the server is the box's own news; the operator's own choices are silent.
        self.assertIn("playSound(serverLink.observe(terminal.started && terminal.fresh));", self.source)
        self.assertIn("serverLink.forget()", self.function("void startPortal() {"))
        self.assertIn("serverLink.forget()", self.function("void savePortalSettings() {"))
        self.assertIn("serverLink.forget()", self.function("void applyDisplaySetting() {"))
        profile = (SKETCH / "hardware_profile.h").read_text()
        self.assertIn("#define TAMBOX_BUZZER_PIN 14 // D5", profile)

    def test_the_phone_page_gets_all_rows(self):
        status = (SKETCH / "web_test.h").read_text()
        self.assertIn('data["rows"] = lcdSize.rows; data["cols"] = lcdSize.cols;', status)
        self.assertIn('data["line4"]', status)


if __name__ == "__main__":
    unittest.main()
