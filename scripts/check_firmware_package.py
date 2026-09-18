#!/usr/bin/env python3
"""Compile the extracted customer ZIP, never the repository source directory."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
import zipfile

from package_firmware import CORES, PROFILES, ROOT, dependencies


def run(*args):
    subprocess.run(args, check=True)


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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=['arduino', 'platformio'], required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    check(args.archive, args.kind, args.target)
