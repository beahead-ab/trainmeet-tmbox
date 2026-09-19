"""Exercise the ESP8266 manual server parser without hardware or Arduino."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tests/esp8266_manual_server_test.cpp"


class ESP8266ManualServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("g++") or shutil.which("clang++")
        if not cls.compiler:
            raise unittest.SkipTest("A native C++ compiler is required")

    def compile_and_run(self, standard, extra_flags=()):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "esp8266-manual-server-test"
            command = [self.compiler, f"-std={standard}", "-Wall", "-Wextra", "-Werror",
                       "-pedantic", *extra_flags, str(SOURCE), "-o", str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("ESP8266 manual server:", result.stdout)

    def test_native_parser_cpp11(self):
        self.compile_and_run("c++11")

    def test_native_parser_cpp17(self):
        self.compile_and_run("c++17")


if __name__ == "__main__":
    unittest.main()
