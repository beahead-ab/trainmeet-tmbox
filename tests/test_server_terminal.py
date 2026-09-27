"""Compile the actual adapter against pinned ArduinoJson, with fake I/O only.

No downloads and no physical device access. CI requires this after its firmware
build has installed ArduinoJson; source-only test runs may explicitly skip it.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class ServerTerminalTest(unittest.TestCase):
    def test_shared_production_adapter(self):
        compiler = shutil.which("g++")
        configured = os.environ.get("ARDUINOJSON_INCLUDE")
        candidates = ([Path(configured)] if configured else
                      sorted((ROOT / "firmware").glob("*/.pio/libdeps/*/ArduinoJson/src")))
        include = next((p for p in candidates if (p / "ArduinoJson.h").is_file()), None)
        if not compiler or include is None:
            reason = "g++ and cached ArduinoJson 7.4.2 are required; build a firmware profile first"
            if configured or os.environ.get("TMBOX_REQUIRE_TERMINAL_TESTS") == "1":
                self.fail(reason)
            self.skipTest(reason)
        version = (include / "ArduinoJson/version.hpp").read_text()
        self.assertIn('#define ARDUINOJSON_VERSION "7.4.2"', version)
        with tempfile.TemporaryDirectory(prefix="tmbox-terminal-") as directory:
            binary = str(Path(directory) / "terminal-test")
            build = subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0", "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0",
                "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0", "-I" + str(ROOT / "tests/terminal_host"),
                "-I" + str(include), str(ROOT / "tests/server_terminal_test.cpp"), "-o", binary,
            ], capture_output=True, text=True, timeout=60)
            self.assertEqual(0, build.returncode, build.stdout + build.stderr)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=30)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("PASS 18 shared terminal contract scenarios", result.stdout)


if __name__ == "__main__":
    unittest.main()
