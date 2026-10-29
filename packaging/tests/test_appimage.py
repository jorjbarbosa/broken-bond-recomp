#!/usr/bin/env python3
"""Checks a built AppImage with scratch home folders; the game itself is not started.

Usage: python3 packaging/tests/test_appimage.py <broken-bond-recomp-linux-x86_64.AppImage> [<scratch root>]

The image runs its POSIX wait check (extracted, then mounted with FUSE when the
host has it) and is extracted to check its payload. The AppRun then runs over a
stub game that records its arguments, its working folder and where it ran from.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib

image = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[2]
work = Path(tempfile.mkdtemp(prefix='broken-bond-appimage-', dir=sys.argv[2] if len(sys.argv) > 2 else None))
home = work / 'home with spaces'
home.mkdir()
inherited = {key: value for key, value in os.environ.items() if key not in ('APPDIR', 'APPIMAGE', 'ARGV0', 'OWD')}
env = {**inherited, 'HOME': str(home), 'XDG_DATA_HOME': str(home / 'data'), 'XDG_CONFIG_HOME': str(home / 'config'),
       'XDG_CACHE_HOME': str(home / 'cache'), 'XDG_STATE_HOME': str(home / 'state')}
report = {'appimage': str(image), 'work': str(work), 'checks': []}


def run(command, label, **kwargs):
    result = subprocess.run([str(part) for part in command], env=kwargs.pop('env', env), capture_output=True,
                            timeout=60, **kwargs)
    (work / (label + '.log')).write_bytes(result.stdout + result.stderr)
    return result


def check(condition, label):
    if not condition:
        raise AssertionError(label)
    report['checks'].append(label)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# The image's runtime and the SDK's waits, with no copy and no game.
first = run([image, '--appimage-extract-and-run', '--test-posix-wait'], 'wait-extract-and-run')
check(first.returncode == 0 and b'PASS' in first.stdout, 'the wait check passes from the extracted image')
check(not (home / 'data').exists(), 'the wait check copies nothing')
mounted = run([image, '--test-posix-wait'], 'wait-fuse')
report['fuse'] = mounted.returncode == 0 and b'PASS' in mounted.stdout

# The payload.
extract = work / 'extracted'
extract.mkdir()
check(run([image, '--appimage-extract'], 'extract', cwd=extract).returncode == 0, 'the image extracts')
appdir = extract / 'squashfs-root'
payload = appdir / 'usr/lib/broken_bond'
info = json.loads((appdir / 'packaging-info.json').read_text())
check((appdir / 'build-id').read_text().strip() == info['build_id'], 'the build id is recorded')
for name, digest in info['binaries'].items():
    check(sha256(payload / name) == digest, 'unchanged binary ' + name)
check(all(os.access(path, os.X_OK) for path in (appdir / 'AppRun', payload / 'broken_bond',
                                                payload / 'broken_bond_posix_wait_test')), 'executables keep their mode')
check((appdir / 'AppRun').read_bytes() == (root / 'packaging/appimage/AppRun').read_bytes(), "the repository's AppRun")
check(sorted(path.name for path in payload.iterdir()) ==
      sorted(['broken_bond', 'librexruntime.so', 'broken_bond_posix_wait_test', 'broken_bond.version', 'README.txt',
              'defaults', 'licenses']), 'the payload holds the game files only')
check(not [path for path in appdir.rglob('*') if '.so' in path.name and path.name != 'librexruntime.so'],
      'no system library is bundled')
seed = tomllib.loads((payload / 'defaults/broken_bond.toml').read_text(encoding='utf-8'))
check(seed.get('native_backend') == 'vulkan', 'the settings seed runs on Vulkan')
check(not any(seed.get(key) for key in ('custom_game_root', 'user_data_root', 'cache_root', 'log_file')),
      'no personal path in the seed')
version = (payload / 'broken_bond.version').read_text().strip()
check(version and version in (payload / 'README.txt').read_text(encoding='utf-8'), 'the README names the version')
notices = (payload / 'licenses/README.txt').read_text(encoding='utf-8')
check('librexruntime.so' in notices and 'rexruntime.dll' not in notices and 'broken_bond.exe' not in notices,
      'the notices name the Linux files')
check((payload / 'licenses/volk.txt').is_file() and not (payload / 'licenses/d3d12memoryallocator.txt').exists(),
      'the Vulkan notices without Direct3D 12')
check((appdir / '.DirIcon').is_file() and (appdir / 'broken_bond.desktop').is_file() and
      (appdir / 'broken_bond.png').read_bytes() == (root / 'ui/broken_bond.png').read_bytes(), 'icon and desktop entry')
report['glibc_required'] = info['glibc_required']
report['host_libraries'] = sorted(info['host_libraries'])

# The AppRun over a stub game: the game runs from the image with the caller's
# arguments and folder, and the copy an earlier image made in the data folder
# goes while the player's files stay.
stub = work / 'stub.AppDir'
stub_payload = stub / 'usr/lib/broken_bond'
(stub_payload / 'defaults').mkdir(parents=True)
(stub_payload / 'defaults/broken_bond.toml').write_text('native_backend = "vulkan"\n')
(stub / 'build-id').write_text('build-new\n')
(stub / 'AppRun').write_bytes((root / 'packaging/appimage/AppRun').read_bytes())
(stub / 'AppRun').chmod(0o755)
(stub_payload / 'broken_bond').write_text(
    '#!/bin/sh\nprintf "%s\\0" "$@" > "$CAPTURE/argv"\npwd > "$CAPTURE/cwd"\nprintf "%s" "$0" > "$CAPTURE/exe"\n')
(stub_payload / 'broken_bond').chmod(0o755)

capture = work / 'capture'
capture.mkdir()
cwd = work / 'another folder'
cwd.mkdir()
stub_env = {**env, 'APPDIR': str(stub), 'APPIMAGE': str(work / 'apps/Broken Bond.AppImage'), 'CAPTURE': str(capture)}
arguments = ['--game_data_root=relative game', '--user_data_root=saves with spaces', 'literal;$value']
data = home / 'data/broken_bond'

check(run([stub / 'AppRun', *arguments], 'stub-first', env=stub_env, cwd=cwd).returncode == 0, 'the AppRun starts the game')
check((capture / 'argv').read_bytes().split(b'\0')[:-1] == [value.encode() for value in arguments],
      'the arguments pass unchanged')
check((capture / 'cwd').read_text().strip() == str(cwd), "the working folder stays the caller's")
check((capture / 'exe').read_text() == str(stub_payload / 'broken_bond'), 'the game runs from the image')
check(not data.exists(), 'the AppRun makes no data folder; the game prepares it')


def earlier_copy(folder, marked=True):
    """A data folder as the earlier AppRun left it (D-52), with the player's files."""
    program = {'broken_bond': b'old program', 'librexruntime.so': b'old runtime', 'broken_bond.version': b'0.1.1-s10\n',
               'README.txt': b'old readme', 'licenses/README.txt': b'old notices', 'licenses/volk.txt': b'volk',
               '.appimage.lock': b''}
    if marked:
        program['.build-id'] = b'0123456789abcdef01234567\n'
    player = {'broken_bond.toml': b'native_backend = "vulkan"\nfullscreen = false\n',
              'user/0000000000000000/55530825/00000001/save.bin': b'save', 'logs/broken_bond_001.log': b'log',
              'mods/community/mod.toml': b"[mod]\ntype = 'translation'\n", 'dlc/pending.bin': b'package',
              '.broken_bond-migration/state.json': b'{"version":1}', '.broken_bond.lock': b''}
    for name, content in {**program, **player}.items():
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(content)
    return program, player


program, player = earlier_copy(data)
check(run([stub / 'AppRun'], 'stub-earlier-copy', env=stub_env, cwd=cwd).returncode == 0, 'a start over an earlier copy')
check(not any((data / name).exists() for name in program) and not (data / 'licenses').exists(),
      "the earlier image's program files are removed")
check(all((data / name).read_bytes() == content for name, content in player.items()), "the player's files stay")
check((capture / 'exe').read_text() == str(stub_payload / 'broken_bond'), 'and the game runs from the image')

unmarked = work / 'unmarked'
(unmarked / 'broken_bond').mkdir(parents=True)
program, player = earlier_copy(unmarked / 'broken_bond', marked=False)
check(run([stub / 'AppRun'], 'stub-unmarked', env={**stub_env, 'XDG_DATA_HOME': str(unmarked)}, cwd=cwd).returncode == 0,
      'a start over a folder without .build-id')
check(all((unmarked / 'broken_bond' / name).read_bytes() == content for name, content in {**program, **player}.items()),
      'without .build-id nothing is removed')

default_home = home / '.local/share/broken_bond'
default_home.mkdir(parents=True)
program, player = earlier_copy(default_home)
default_env = {key: value for key, value in stub_env.items() if key != 'XDG_DATA_HOME'}
check(run([stub / 'AppRun'], 'stub-default-data', env=default_env, cwd=cwd).returncode == 0 and
      not (default_home / 'broken_bond').exists() and (default_home / 'broken_bond.toml').is_file(),
      'without XDG_DATA_HOME: ~/.local/share/broken_bond')

(work / 'result.json').write_text(json.dumps(report, indent=2))
print(json.dumps({'status': 'PASS', 'checks': len(report['checks']), 'fuse': report['fuse'],
                  'glibc_required': report['glibc_required'], 'report': str(work / 'result.json')}))
