"""Exercise the actual ESP8266 logging macros without hardware or Arduino."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tests/esp8266_debug_test.cpp"


class ESP8266DebugTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("g++") or shutil.which("clang++")
        if not cls.compiler:
            raise unittest.SkipTest("A native C++ compiler is required")

    def compile_and_run(self, flag, expected):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "esp8266-debug-test"
            command = [self.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                       f"-DEXPECTED_DEBUG={expected}"]
            if flag is not None:
                command.append(f"-DTAMBOX_DEBUG_ENABLED={flag}")
            command.extend([str(SOURCE), "-o", str(binary)])
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn(f"ESP8266 debug={expected}:", result.stdout)

    def test_default_is_quiet_without_debug_argument_evaluation(self):
        self.compile_and_run(None, 0)

    def test_explicit_disabled_is_quiet_without_debug_argument_evaluation(self):
        self.compile_and_run(0, 0)

    def test_enabled_locations_formatting_limiter_and_clock_wrap(self):
        self.compile_and_run(1, 1)

    def test_invalid_numeric_flags_fail_compilation(self):
        with tempfile.TemporaryDirectory() as temporary:
            for flag in (-1, 2):
                with self.subTest(flag=flag):
                    command = [self.compiler, "-std=c++17", "-DEXPECTED_DEBUG=0",
                               f"-DTAMBOX_DEBUG_ENABLED={flag}", str(SOURCE),
                               "-o", str(Path(temporary) / "invalid-debug-test")]
                    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                    self.assertNotEqual(0, result.returncode)
                    self.assertIn("TAMBOX_DEBUG_ENABLED must be 0 or 1", result.stderr)


if __name__ == "__main__":
    unittest.main()
