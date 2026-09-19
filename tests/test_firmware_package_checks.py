"""Exercise the release compile matrix against real generated customer ZIPs."""
import hashlib
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parent.parent


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package = load_module('package_for_compile_checks', 'package_firmware.py')
with patch.dict(sys.modules, {'package_firmware': package}):
    checker = load_module('firmware_package_checker', 'check_firmware_package.py')


class FirmwarePackageCompileChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='trainmeet-compile-matrix-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.archives = {path.stem: path for path in package.build(
            ROOT, Path(cls.temporary.name) / 'packages', 'compile-matrix-test')}

    def commands_for(self, kind, target):
        archive = self.archives[f'trainmeet-tmbox-{kind}-{target}']
        original_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
        commands = []

        def capture(*args, env=None):
            commands.append((args, env))
            if args[:2] == ('arduino-cli', 'compile'):
                sketch = Path(args[-1])
                self.assertTrue((sketch.parent / 'PACKAGE.json').is_file())
                self.assertTrue((sketch / 'TrainMeetFirmware.cpp').is_file())
                self.assertNotEqual(ROOT, sketch.parent)
                if target.startswith('nodemcu-'):
                    hardware = (sketch / 'hardware_profile.h').read_text()
                    self.assertIn('#define TAMBOX_DEBUG_ENABLED 0', hardware)
                    build = (sketch / 'TrainMeetBuild.h').read_text()
                    self.assertEqual(target == 'nodemcu-hardware-check',
                                     '#define TAMBOX_HARDWARE_CHECK 1' in build)
            elif args[:2] == ('pio', 'run'):
                project = Path(args[args.index('--project-dir') + 1])
                self.assertTrue((project / 'PACKAGE.json').is_file())
                self.assertTrue((project / 'src/main.cpp').is_file())
                self.assertNotEqual(ROOT / 'firmware' / target, project)
                if target == 'esp8266':
                    self.assertIn('build_flags = -D TAMBOX_HARDWARE_CHECK=1',
                                  (project / 'platformio.ini').read_text())
                    self.assertIn('#define TAMBOX_DEBUG_ENABLED 0',
                                  (project / 'TrainMeetTambox8266/hardware_profile.h').read_text())

        with patch.object(checker, 'run', side_effect=capture):
            checker.check(archive, kind, target)
        self.assertEqual(original_hash, hashlib.sha256(archive.read_bytes()).hexdigest())
        return commands

    def test_both_arduino_esp8266_profiles_compile_default_and_debug_in_both_modes(self):
        for target in ('nodemcu-i2c', 'nodemcu-hardware-check'):
            with self.subTest(target=target):
                commands = self.commands_for('arduino', target)
                compiles = [(args, env) for args, env in commands if args[:2] == ('arduino-cli', 'compile')]
                self.assertEqual(4, len(compiles))
                fqbn = package.PROFILES[target][3]
                sketch = compiles[0][0][-1]
                expected = [
                    ('arduino-cli', 'compile', '--fqbn', fqbn, sketch),
                    ('arduino-cli', 'compile', '--profile', 'build', sketch),
                    ('arduino-cli', 'compile', '--fqbn', fqbn, '--build-property',
                     'compiler.cpp.extra_flags=-DTAMBOX_DEBUG_ENABLED=1', sketch),
                    ('arduino-cli', 'compile', '--profile', 'build', '--build-property',
                     'compiler.cpp.extra_flags=-DTAMBOX_DEBUG_ENABLED=1', sketch),
                ]
                self.assertEqual(expected, [args for args, _ in compiles])
                self.assertTrue(all(env is None for _, env in commands))
                libraries = [args[-1] for args, _ in commands if args[:3] == ('arduino-cli', 'lib', 'install')]
                self.assertEqual([f'{name}@{version}' for name, version in package.dependencies(ROOT, 'esp8266')], libraries)

    def test_platformio_both_esp8266_profiles_keep_build_flags_and_add_debug_source_flag(self):
        ambient = {
            'PLATFORMIO_BUILD_SRC_FLAGS': '-DEXISTING_SOURCE_FLAG=7',
            'PLATFORMIO_BUILD_FLAGS': '-DEXISTING_BUILD_FLAG=3',
            'TRAINMEET_COMPILE_TEST': 'inherited',
        }
        with patch.dict(os.environ, ambient):
            commands = self.commands_for('platformio', 'esp8266')
            self.assertEqual(4, len(commands))
            for offset, profile in ((0, 'nodemcu-i2c'), (2, 'nodemcu-hardware-check')):
                default, debug = commands[offset:offset + 2]
                self.assertEqual(default[0], debug[0])
                self.assertEqual(('pio', 'run'), default[0][:2])
                self.assertEqual(('-e', profile), default[0][-2:])
                self.assertIsNone(default[1])
                self.assertIsNot(os.environ, debug[1])
                self.assertEqual('-DEXISTING_SOURCE_FLAG=7 -DTAMBOX_DEBUG_ENABLED=1',
                                 debug[1]['PLATFORMIO_BUILD_SRC_FLAGS'])
                self.assertEqual('-DEXISTING_BUILD_FLAG=3', debug[1]['PLATFORMIO_BUILD_FLAGS'])
                self.assertEqual('inherited', debug[1]['TRAINMEET_COMPILE_TEST'])
            self.assertEqual(ambient, {key: os.environ[key] for key in ambient})

    def test_esp32_build_matrix_has_no_debug_builds_or_environment_changes(self):
        for target, definition in package.PROFILES.items():
            if definition[0] != 'esp32':
                continue
            with self.subTest(target=target):
                commands = self.commands_for('arduino', target)
                compiles = [args for args, _ in commands if args[:2] == ('arduino-cli', 'compile')]
                self.assertEqual(2, len(compiles))
                self.assertEqual(('arduino-cli', 'compile', '--fqbn', definition[3]), compiles[0][:-1])
                self.assertEqual(('arduino-cli', 'compile', '--profile', 'build'), compiles[1][:-1])
                self.assertTrue(all(env is None for _, env in commands))
        commands = self.commands_for('platformio', 'esp32')
        self.assertEqual([profile for profile, definition in package.PROFILES.items() if definition[0] == 'esp32'],
                         [args[-1] for args, _ in commands])
        self.assertTrue(all(env is None for _, env in commands))

    def test_run_passes_environment_and_fails_on_compiler_errors(self):
        environment = {'PLATFORMIO_BUILD_SRC_FLAGS': '-DTAMBOX_DEBUG_ENABLED=1'}
        with patch.object(checker.subprocess, 'run') as execute:
            checker.run('pio', 'run', env=environment)
        execute.assert_called_once_with(('pio', 'run'), check=True, env=environment)
        failure = checker.subprocess.CalledProcessError(1, ('pio', 'run'))
        with patch.object(checker.subprocess, 'run', side_effect=failure):
            with self.assertRaises(checker.subprocess.CalledProcessError):
                checker.run('pio', 'run', env=environment)


if __name__ == '__main__':
    unittest.main()
