#!/usr/bin/env python3
"""Compile the extracted customer ZIP, never the repository source directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
import zipfile

from package_firmware import CORES, PROFILES, ROOT, dependencies


def run(*args, env=None):
    subprocess.run(args, check=True, env=env)


def check(archive_path, kind, target):
    with tempfile.TemporaryDirectory(prefix='trainmeet-download-test-') as temporary:
        with zipfile.ZipFile(archive_path) as archive:
            for entry in archive.infolist():
                path = PurePosixPath(entry.filename)
                if path.is_absolute() or '..' in path.parts or path.parts[0] != archive_path.stem:
                    raise ValueError('Unsafe archive path')
            archive.extractall(temporary)
        project = Path(temporary) / archive_path.stem
        manifest = json.loads((project / 'PACKAGE.json').read_text())
        actual = {p.relative_to(project).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in project.rglob('*') if p.is_file() and p.name != 'PACKAGE.json'}
        assert actual == manifest['files'], 'Package checksum mismatch'
        if kind == 'platformio':
            for profile, definition in PROFILES.items():
                if definition[0] == target:
                    run('pio', 'run', '--project-dir', str(project), '-e', profile)
                    if target == 'esp8266':
                        # Test the IDE-derived setting AND the supported legacy
                        # override. Keep the hardware-check profile's flags.
                        for flag in ('-DDEBUG_ESP_PORT=Serial', '-DTAMBOX_DEBUG_ENABLED=1'):
                            debug_env = os.environ.copy()
                            debug_env['PLATFORMIO_BUILD_SRC_FLAGS'] = ' '.join(filter(None, (
                                debug_env.get('PLATFORMIO_BUILD_SRC_FLAGS', ''), flag,
                            )))
                            run('pio', 'run', '--project-dir', str(project), '-e', profile, env=debug_env)
            return
        family, sketch, _board, fqbn, _number = PROFILES[target]
        core, version, index = CORES[family]
        # Ordinary mode uses the same source discovery/build process as Arduino
        # IDE. Then test the isolated CLI profile too, including library locking.
        run('arduino-cli', 'core', 'update-index', '--additional-urls', index)
        run('arduino-cli', 'core', 'install', f'{core}@{version}', '--additional-urls', index)
        for name, library_version in dependencies(ROOT, family):
            run('arduino-cli', 'lib', 'install', f'{name}@{library_version}')
        run('arduino-cli', 'compile', '--fqbn', fqbn, str(project / sketch))
        run('arduino-cli', 'compile', '--profile', 'build', str(project / sketch))
        if family == 'esp8266':
            # Exercise the actual Arduino Debug port menu option, not just a
            # compiler flag. This must work in ordinary and isolated builds.
            run('arduino-cli', 'compile', '--fqbn', fqbn, '--board-options', 'dbg=Serial', str(project / sketch))
            run('arduino-cli', 'compile', '--profile', 'build', '--board-options', 'dbg=Serial', str(project / sketch))
            debug_property = 'compiler.cpp.extra_flags=-DTAMBOX_DEBUG_ENABLED=1'
            run('arduino-cli', 'compile', '--fqbn', fqbn, '--build-property', debug_property, str(project / sketch))
            run('arduino-cli', 'compile', '--profile', 'build', '--build-property', debug_property, str(project / sketch))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=['arduino', 'platformio'], required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    check(args.archive, args.kind, args.target)
