"""Keep both firmware families on the server's mDNS contract."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "firmware/esp8266/TrainMeetTambox8266"


class NetworkSetupTest(unittest.TestCase):
    def test_same_discovery_service_as_esp32(self):
        common = (ROOT / "firmware/common/server_discovery.h").read_text()
        self.assertRegex(common, r'DISCOVERY_SERVICE\[\]\s*=\s*"tmbox"')
        for path in (ROOT / "firmware/esp32/TrainMeetTMBox.ino", SKETCH / "TrainMeetTambox8266.ino"):
            source = path.read_text()
            self.assertIn("TrainMeetNetwork::discoverServers()", source)
            self.assertIn("TrainMeetNetwork::selectServer(servers, rememberedServerId.c_str())", source)
            self.assertNotIn("resetSettings()", source)
            self.assertNotIn("configuredGatewayHost", source)
            self.assertIn('terminal.frame["station_code"]', source)

    def test_automatic_server_uses_tested_query_without_manual_override(self):
        source = (SKETCH / "TrainMeetTambox8266.ino").read_text()
        self.assertEqual(source.count("TrainMeetNetwork::discoverServers()"), 1)
        self.assertNotIn("MDNS.queryService(", source)
        self.assertNotIn("settings.host", source)
        self.assertNotIn("gatewayParameter", source)
        self.assertIn("TrainMeetNetwork::selectServer(servers, rememberedServerId.c_str())", source)
        # Since 0.7.4 the 16x2 terminal is the only wire protocol.
        self.assertNotIn("tambox/v1", source)
        self.assertNotIn("tmbox/v1/", source)
        self.assertNotIn("wifiManager.stopConfigPortal()", source)
        self.assertIn("TrainMeetNetwork::finishSavedPortal(", source)

    def test_esp32_resolves_identity_before_every_new_connection(self):
        source = (ROOT / "firmware/esp32/TrainMeetTMBox.ino").read_text()
        gateway = source.split("void processGateway() {", 1)[1].split("bool discoverGateway()", 1)[0]
        self.assertNotIn("gatewayHost.isEmpty()", gateway)
        self.assertLess(gateway.index("discoverGateway()"), gateway.index("connectMqtt()"))
        self.assertIn("|| portalActive", gateway)
        self.assertIn("MDNS.end()", source.split("void processSavedParameters() {", 1)[1])
        esp8266 = (SKETCH / "TrainMeetTambox8266.ino").read_text()
        save = esp8266.split("void savePortalSettings() {", 1)[1].split("bool publish", 1)[0]
        self.assertIn("MDNS.close()", save)

    def test_discovery_documentation_matches_server(self):
        readme = (ROOT / "firmware/esp8266/README.md").read_text()
        self.assertIn("_tmbox._tcp", readme)
        self.assertNotIn("_tambox._tcp", readme)

    def test_only_the_terminal_speaks(self):
        """0.7.4: Server 2.0.0 answers only tmbox/terminal/... The v1 (ESP8266)
        and v2 (ESP32) paths, their last will and their offline message are gone,
        so nothing a box sends can land on a topic nobody reads."""
        sources = {path: path.read_text() for path in (
            SKETCH / "TrainMeetTambox8266.ino", SKETCH / "web_test.h", ROOT / "firmware/esp32/TrainMeetTMBox.ino")}
        for path, source in sources.items():
            with self.subTest(path=path.name):
                for old in ("tambox/v1", "tmbox/v2", "beginWill", "publishPresence", "protocol_version"):
                    self.assertNotIn(old, source)
        esp8266 = sources[SKETCH / "TrainMeetTambox8266.ino"]
        connect = esp8266.split("void connectServer() {", 1)[1].split("\n}\n", 1)[0]
        self.assertTrue(connect.rstrip().endswith("String(++commandSequence));"), "nothing after terminal.begin")
        # Connected without a session is not a state to stay in.
        self.assertIn("Start over.\n          disconnectServer(); nextConnection = current + 1000;", esp8266)
        esp32 = sources[ROOT / "firmware/esp32/TrainMeetTMBox.ino"]
        connect = esp32.split("bool connectMqtt() {", 1)[1].split("\n}\n", 1)[0]
        self.assertTrue(connect.rstrip().endswith("return true;"))
        self.assertEqual(1, connect.count("return true;"), "nothing after terminal.begin")
        disconnect = esp32.split("void disconnectMqtt() {", 1)[1].split("\n}\n", 1)[0]
        self.assertNotIn("publish", disconnect)

    def test_the_box_writes_swedish_letters_itself(self):
        """0.7.5: the box's own text has Å, Ä and Ö, drawn with the server's
        glyphs (common/lcd_text.h). Nothing folds them to A and O, nothing
        writes UTF-8 bytes straight to the display, and the cached catalog is
        still found under its folded keys. Benny asked for this in #32."""
        esp8266 = (SKETCH / "TrainMeetTambox8266.ino").read_text()
        esp32 = (ROOT / "firmware/esp32/TrainMeetTMBox.ino").read_text()
        terminal = (ROOT / "firmware/common/server_terminal.h").read_text()
        for word in ("SÖKER SERVER", "BE ADMIN HJÄLPA", "VÄNTAR PÅ SVAR", "HÅRDVARUTEST", "NÄT SAKNAS", "FÖRSÖKER IGEN"):
            self.assertIn(f'"{word}"', esp8266)
        for folded in ("SOKER SERVER", "HJALPA", "VANTAR PA SVAR", "HARDVARUTEST", "NAT SAKNAS", "FORSOKER IGEN"):
            self.assertNotIn(f'"{folded}', esp8266)
        self.assertNotIn('value.replace("', esp8266)
        show = esp8266.split("void showFrame(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("TrainMeetLcd::Screen", show)
        self.assertNotIn("lcd.print", show)
        ui = esp8266.split("String uiText(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("foldText", ui)
        # ESP32: its own screens go through the same drawing, and its renderer keeps the dots.
        self.assertNotIn("lcd.print(", esp32)
        self.assertIn("displayGeometry(TMBOX_LCD_ROWS, TMBOX_LCD_COLUMNS, true)", esp32)
        self.assertIn('text("VÄNTAR PÅ SVAR")', esp32)
        self.assertIn('"BE ADMIN HJÄLPA"', esp32)
        # After a session the server's glyphs are in CGRAM: the next local
        # text is written even if it is the one shown before.
        self.assertIn('shownLine1 = ""', esp8266.split("void disconnectServer() {", 1)[1].split("\n}\n", 1)[0])
        self.assertIn("drawnValid = false;", esp32.split("void drawScreen() {", 1)[1].split("\n}\n", 1)[0])
        # The waiting overlay is encoded, not copied byte by byte.
        self.assertIn('#include "lcd_text.h"', terminal)
        self.assertNotIn("uint8_t(overlay[c])", terminal)

    def test_the_language_is_chosen_on_the_server(self):
        """0.7.4: neither box nor its phone page has a language menu. The
        language comes with every frame; the administrator picks it."""
        page = (SKETCH / "web_test_page.h").read_text()
        self.assertNotIn('id="language"', page)
        self.assertNotIn("languageAvailable", (SKETCH / "web_test.h").read_text())
        for path in (SKETCH / "TrainMeetTambox8266.ino", ROOT / "firmware/esp32/TrainMeetTMBox.ino"):
            with self.subTest(path=path.name):
                source = path.read_text()
                self.assertNotIn("language_menu.h", source)
                self.assertNotIn("languageMenu", source)
        self.assertFalse((SKETCH / "language_menu.h").exists())

    def test_web_keys_share_server_command_safety(self):
        backend = (SKETCH / "web_test.h").read_text()
        firmware = (SKETCH / "TrainMeetTambox8266.ino").read_text()
        self.assertIn("sendKey(key[0], true)", backend)
        self.assertIn('String(data["session"] | "") != terminal.token()', backend)
        self.assertIn("webSession.permits(true, false, millis())", backend)
        self.assertIn("if (!terminal.started || !webSession.permits(virtualKey, keypadOK && lcdFound", firmware)
        self.assertIn("terminal.press(key)", firmware)
        self.assertIn("sendKey(event.pressed, false)", firmware)
        self.assertIn("SameSite=Strict", backend)
        self.assertIn('origin != "http://" + host', backend)
        self.assertNotIn('data["pin"] =', backend)

    def test_web_javascript_parses_and_has_all_keys(self):
        page = (SKETCH / "web_test_page.h").read_text()
        self.assertIn("123A456B789C*0#D", page)
        self.assertIn("state.allowedKeys.includes", page)
        self.assertNotIn("https://", page)  # Works offline; no CDN.
        node = shutil.which("node")
        if not node:
            self.skipTest("node missing")
        script = page.split("<script>", 1)[1].split("</script>", 1)[0]
        subprocess.run([node, "--check"], input=script, text=True,
                       capture_output=True, check=True, timeout=30)

    def test_digits_are_buffered_before_mqtt_publish(self):
        """Digits stay in the box until #; the terminal buffers them, and the
        phone page cannot send one on its own."""
        terminal = (ROOT / "firmware/common/server_terminal.h").read_text()
        press = terminal.split("bool press(char key) {", 1)[1].split("\n  }\n", 1)[0]
        self.assertIn("digits += key; dirty = true; return true;", press)
        backend = (SKETCH / "web_test.h").read_text()
        self.assertIn("Siffror stannar i telefonen tills #.", backend)

    def test_native_network_and_input_regressions(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ missing")
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / "esp8266-test")
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/esp8266_input_test.cpp"), "-o", binary],
                           check=True, capture_output=True, text=True, timeout=60)
            subprocess.run([binary], check=True, capture_output=True, text=True, timeout=30)
