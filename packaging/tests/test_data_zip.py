"""Extracts a newer release ZIP over a copy of an older one with player data.

Usage: python packaging/tests/test_data_zip.py <older.zip> <newer.zip> <new output folder>

The copy holds a data folder, a portable data_location.json, the game and what
a 0.1.0-beta copy keeps beside the executable. Extracting the newer ZIP over it,
twice, must replace the package files and leave every one of those untouched.
Nothing is started: this checks the archives, not the game's import.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import sys
import zipfile

SAVE = 'E000000000000001/55530825/00000001/save/SaveData'


def extract(archive, destination):
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        for name in names:
            path = PurePosixPath(name)
            assert not path.is_absolute() and not PureWindowsPath(name).is_absolute() and '..' not in path.parts, name
            assert not name.startswith(('data/', 'user/', 'logs/', 'dlc/', 'mods/', 'cache/', 'game/')), name
        assert 'defaults/broken_bond.toml' in names
        assert 'broken_bond.toml' not in names and 'data_location.json' not in names
        package.extractall(destination)


def main():
    older, newer, output = (Path(argument).resolve() for argument in sys.argv[1:4])
    output.mkdir(parents=True, exist_ok=False)
    copy = output / 'copy with spaces'
    extract(older, copy)
    old_version = (copy / 'broken_bond.version').read_text().strip()
    fixtures = {
        # The data folder.
        'data/broken_bond.toml': b'launcher_language = "ru"\nunknown_option = "kept"\n',
        'data/user/' + SAVE: b'save bytes\x00',
        'data/user/E000000000000002/55530825/00000001/second/SaveData': b'second profile',
        'data/user/E000000000000001/55530825/Headers/00000001/save.header': b'save header',
        'data/user/0000000000000000/55530825/00000002/addon/payload': b'installed DLC',
        'data/dlc/pending.bin': b'pending package',
        'data/logs/broken_bond_001.log': b'a log',
        'data/logs/legacy/install/broken_bond_001.log': b'an imported log',
        'data/mods/community/mod.toml': b"[mod]\ntype = 'translation'\n",
        'data/mods/community/textos.json': b'{"1992":"MINE"}',
        'data/cache/shaders.bin': b'cache bytes',
        'data/launch.marker': b'',
        'data/menu-updates.json': b'{"checked":1}',
        'data_location.json': b'{"version":1,"mode":"portable"}',
        # The game beside the executable.
        'game/default.xex': b'original game XEX',
        # What a 0.1.0-beta copy keeps beside the executable, already imported.
        'broken_bond.toml': b'# 0.1.0-beta settings\nfullscreen = false\n',
        'user/' + SAVE: b'0.1.0-beta save',
        'logs/broken_bond_001.log': b'0.1.0-beta log',
        'mods/community/mod.toml': b"[mod]\ntype = 'translation'\n",
        'dlc/old.bin': b'0.1.0-beta staging',
    }
    state = {'version': 1, 'destination': str(copy / 'data'), 'completed': [], 'config_base': str(copy)}
    fixtures['data/.broken_bond-migration/state.json'] = json.dumps(state).encode()
    for name, content in fixtures.items():
        target = copy / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
    # Extracted twice: no extraction may restore a default over the files in use.
    for _ in range(2):
        extract(newer, copy)
        for name, content in fixtures.items():
            assert (copy / name).read_bytes() == content, name
    new_version = (copy / 'broken_bond.version').read_text().strip()
    assert old_version != new_version, 'Use two packages of different versions'
    with zipfile.ZipFile(newer) as package:
        for name in package.namelist():
            if not name.endswith('/'):
                assert (copy / name).read_bytes() == package.read(name), name
    report = {'status': 'PASS', 'old_version': old_version, 'new_version': new_version,
              'kept_files': len(fixtures),
              'sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (older, newer)},
              'scope': 'archive extraction over a copy; the import itself is the game tests and boots'}
    (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
