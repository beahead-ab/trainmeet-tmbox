"""Behavioral browser regressions for the exact embedded ESP8266 web script."""
from pathlib import Path
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parent.parent


class ESP8266WebPageTests(unittest.TestCase):
    def test_actual_javascript_with_fake_dom_fetch_and_clock(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node missing")
        result = subprocess.run(
            [node, str(ROOT / "tests/esp8266_web_page_test.js")],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("PASS initialPollCannotOverwriteCompletedAction", result.stdout)
        self.assertIn("PASS transportTimeoutIsNotARetriedPost", result.stdout)


if __name__ == "__main__":
    unittest.main()
