#!/usr/bin/env python3
"""Starts two real AppImages over the data folders of an update, with scratch homes.

Usage: python3 packaging/tests/test_data_appimage.py <older.AppImage> <newer.AppImage> <new output folder>

Linux with a desktop session and a Vulkan driver (llvmpipe will do). Each start
opens the setup screen (--setup, no game), waits until the log shows the data
folder and the setup screen, and stops the process group it started. Cases:
- a data folder as an earlier AppImage left it (D-52: the program copied there,
  marked by .build-id): the copy goes, the player's files stay;
- the newer image, mounted with FUSE when the host has it and extracted;
- a new home: the data folder starts from the image's defaults;
- the portable folder beside the images, chosen with a pending move: the data
  is copied there, the pending marker goes, the earlier folder stays.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import tomllib

SAVE = Path('user/E000000000000001/55530825/00000001/save/SaveData')
PROGRAM = ('broken_bond', 'librexruntime.so', 'broken_bond.version', 'README.txt', '.appimage.lock', '.build-id')


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    if not sys.platform.startswith('linux'):
        raise SystemExit('Start AppImages on Linux.')
    import fcntl
    older, newer, work = (Path(argument).resolve() for argument in sys.argv[1:4])
    assert sha256(older) != sha256(newer), 'Use two different AppImages'
    work.mkdir(parents=True, exist_ok=False)
    distribution = work / 'distribuição com espaços'
    distribution.mkdir()
    images = []
    for source, name in ((older, 'Broken Bond A.AppImage'), (newer, 'Broken Bond B.AppImage')):
        shutil.copy2(source, distribution / name)
        (distribution / name).chmod(0o755)
        images.append(distribution / name)
    home, cwd = work / 'home', work / 'other folder'
    home.mkdir()
    cwd.mkdir()
    inherited = {key: value for key, value in os.environ.items()
                 if not key.startswith('REX_') and key not in ('APPDIR', 'APPIMAGE', 'ARGV0', 'OWD', 'LD_LIBRARY_PATH')}

    def environment(base):
        return {**inherited, 'HOME': str(base), 'XDG_DATA_HOME': str(base / 'data'), 'XDG_CONFIG_HOME': str(base / 'config'),
                'XDG_CACHE_HOME': str(base / 'cache'), 'XDG_STATE_HOME': str(base / 'state')}

    env = environment(home)
    fuse = subprocess.run([str(images[1]), '--test-posix-wait'], env=env, capture_output=True, timeout=60)
    fuse = fuse.returncode == 0 and b'PASS' in fuse.stdout
    boots = []

    def boot(image, root, label, extract=False, environ=None):
        earlier = set((root / 'logs').glob('broken_bond_*.log'))
        command = [str(image)] + (['--appimage-extract-and-run'] if extract or not fuse else [])
        command += ['--setup', '--fullscreen=false', '--check_for_updates=false']
        ready = False
        with (work / (label + '.out')).open('wb') as console:
            process = subprocess.Popen(command, cwd=cwd, env=environ or env, stdout=console, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            try:
                deadline = time.monotonic() + 120
                while time.monotonic() < deadline and process.poll() is None:
                    text = ''.join(path.read_text(encoding='utf-8', errors='replace')
                                   for path in set((root / 'logs').glob('broken_bond_*.log')) - earlier)
                    if f'Data folder: {root} (' in text and 'Setup screen:' in text and (root / 'broken_bond.toml').is_file():
                        ready = True
                        break
                    time.sleep(0.2)
                assert ready, f'{label}: the setup screen did not start; see {label}.out and the logs'
                time.sleep(2)
                assert process.poll() is None, f'{label}: the game did not stay open'
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=10)
                # An extracted run's shell can end before the game releases the
                # data folder's lock: the next start waits for it.
                lock = root / '.broken_bond.lock'
                if lock.exists():
                    with lock.open('rb') as stream:
                        deadline = time.monotonic() + 30
                        while True:
                            try:
                                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
                                fcntl.flock(stream, fcntl.LOCK_UN)
                                break
                            except BlockingIOError:
                                assert time.monotonic() < deadline, f'{label}: the data folder stayed locked'
                                time.sleep(0.2)
        boots.append({'label': label, 'command': command[1:], 'ready': ready, 'exit': process.returncode})

    # A data folder as the earlier AppRun left it: its program copy and the player's files.
    data = home / 'data/broken_bond'
    # Unknown keys survive the game's rewrites of the settings; comments do not.
    player = {'broken_bond.toml': b'earlier_marker = "kept"\nnative_backend = "vulkan"\nfullscreen = false\n',
              str(SAVE): b'save bytes\x00', 'user/E000000000000001/55530825/Headers/00000001/save.header': b'header',
              'logs/broken_bond_001.log': b'earlier log', 'mods/community/mod.toml': b"[mod]\ntype = 'translation'\n",
              'mods/community/textos.json': b'{"1992":"MINE"}', 'dlc/pending.bin': b'pending package'}
    program = {'broken_bond': b'earlier program', 'librexruntime.so': b'earlier runtime', 'broken_bond.version': b'0.1.1-s10\n',
               'README.txt': b'earlier readme', 'licenses/README.txt': b'earlier notices', '.appimage.lock': b'',
               '.build-id': b'0123456789abcdef01234567\n'}
    for name, content in {**player, **program}.items():
        (data / name).parent.mkdir(parents=True, exist_ok=True)
        (data / name).write_bytes(content)

    boot(images[0], data, 'A-over-earlier-copy')
    assert not any((data / name).exists() for name in PROGRAM) and not (data / 'licenses').exists(), 'program copy left'
    for name, content in player.items():
        if name != 'broken_bond.toml':
            assert (data / name).read_bytes() == content, name
    active = data / 'broken_bond.toml'
    assert b"earlier_marker = 'kept'" in active.read_bytes() and b'fullscreen = false' in active.read_bytes(), \
        'settings lost'
    assert (data / 'logs/broken_bond_002.log').is_file(), 'the new log follows the earlier one'
    assert (data / '.broken_bond-migration/state.json').is_file(), 'no preparation journal'
    active.write_bytes(b'update_marker = "kept"\n' + active.read_bytes())
    settings = active.read_bytes()

    boot(images[1], data, 'B-fuse' if fuse else 'B-extracted')
    boot(images[1], data, 'B-extracted-again', extract=True)
    assert active.read_bytes() == settings, 'the update changed the settings'
    for name, content in player.items():
        if name != 'broken_bond.toml':
            assert (data / name).read_bytes() == content, name
    assert not (home / 'data/broken_bond/broken_bond').exists()
    assert not [path for path in home.rglob('*') if path.is_file() and path.name in ('broken_bond', 'librexruntime.so')], \
        'program files outside the image'
    assert (home / 'cache/broken_bond').is_dir() and not (data / 'cache').exists(), 'the cache is the XDG one'

    # A new home: the data folder starts from the image's defaults.
    fresh = work / 'new home'
    fresh.mkdir()
    fresh_data = fresh / 'data/broken_bond'
    boot(images[1], fresh_data, 'B-new-home', environ=environment(fresh))
    # The game may rewrite the copy at once (the controls' first profiles), so
    # its values are what is checked, not its bytes.
    seeded = tomllib.loads((fresh_data / 'broken_bond.toml').read_text(encoding='utf-8'))
    assert seeded.get('native_backend') == 'vulkan' and not seeded.get('custom_game_root'), \
        'the new settings come from the defaults'
    assert (fresh_data / 'user').is_dir() and (fresh_data / 'logs').is_dir(), 'a new data folder is laid out'

    # The portable folder beside the images, with a move pending from the XDG folder.
    (distribution / 'data_location.json').write_text(
        json.dumps({'version': 1, 'mode': 'portable', 'pending_source': str(data)}), encoding='utf-8')
    portable = distribution / 'data'
    boot(images[0], portable, 'A-portable-extracted', extract=True)
    boot(images[1], portable, 'B-portable')
    assert (portable / 'broken_bond.toml').read_bytes() == settings, 'the portable settings differ'
    for name, content in player.items():
        if name != 'broken_bond.toml':
            assert (portable / name).read_bytes() == content, 'portable ' + name
    assert 'pending_source' not in json.loads((distribution / 'data_location.json').read_text(encoding='utf-8'))
    assert active.read_bytes() == settings and (data / SAVE).read_bytes() == player[str(SAVE)], 'the earlier folder changed'
    assert (portable / 'cache').is_dir(), 'the portable folder keeps its own cache'

    report = {'status': 'PASS', 'older': str(older), 'newer': str(newer), 'fuse': fuse,
              'roots': [str(data), str(fresh_data), str(portable)], 'boots': boots,
              'scope': 'setup screen starts over the data folders; no game data, no save loaded'}
    (work / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
