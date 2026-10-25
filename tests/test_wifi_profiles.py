from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class WifiProfilesTest(unittest.TestCase):
    def test_saved_networks_and_connection_sequence(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ missing")
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / "wifi-profiles")
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/wifi_profiles_test.cpp"), "-o", binary],
                           check=True, capture_output=True, text=True, timeout=60)
            subprocess.run([binary], check=True, capture_output=True, text=True, timeout=30)

    def test_production_adapter_with_fake_io(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ missing")
        with tempfile.TemporaryDirectory() as tmp:
            for family in ("ESP32", "ESP8266"):
                with self.subTest(family=family):
                    binary = str(Path(tmp) / family)
                    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                    "-D" + family, "-I" + str(ROOT / "tests/wifi_host"),
                                    str(ROOT / "tests/wifi_profiles_adapter_test.cpp"), "-o", binary],
                                   check=True, capture_output=True, text=True, timeout=60)
                    subprocess.run([binary], check=True, capture_output=True, text=True, timeout=30)

    def test_both_boxes_use_shared_profiles(self):
        for path in ("firmware/esp32/TrainMeetTMBox.ino",
                     "firmware/esp8266/TrainMeetTMBox8266/TrainMeetTMBox8266.ino"):
            source = (ROOT / path).read_text()
            with self.subTest(path=path):
                for hook in ("wifiNetworks.begin()", "wifiNetworks.rememberCurrent()",
                             "wifiNetworks.process(portalActive)", "wifiNetworks.takeSaved()"):
                    self.assertIn(hook, source)
                self.assertNotIn("WiFi.reconnect()", source)
                self.assertNotIn("WiFi.setAutoReconnect(true)", source)

    def test_source_downloads_include_profiles(self):
        import sys
        sys.path.insert(0, str(ROOT / "scripts"))
        import package_firmware
        for profile, (family, sketch, *_) in package_firmware.PROFILES.items():
            with self.subTest(profile=profile):
                files = package_firmware.arduino_files(ROOT, profile)
                for header in ("wifi_profiles.h", "wifi_profiles_arduino.h"):
                    self.assertIn(f"{sketch}/{header}", files)
        for family in ("esp32", "esp8266"):
            files = package_firmware.platformio_files(ROOT, family)
            self.assertTrue(any(name.endswith("/wifi_profiles_arduino.h") or name == "wifi_profiles_arduino.h"
                                for name in files))
