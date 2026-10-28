"""Audit the release ZIP against its published folder; no game data is required.

Usage: python packaging/tests/test_package.py dist/<out>/broken-bond-recomp-win-amd64.zip
"""
import hashlib
from pathlib import Path
import sys
import tomllib
import zipfile


def main():
    archive = Path(sys.argv[1]).resolve()
    staged = archive.with_suffix('')
    forbidden = {'broken_bond_launcher.exe', 'broken_bond_setup_helper.exe', 'extract-xiso.exe',
                 'BrokenBondInstaller.exe', 'install.ps1', 'default.xex'}
    required = {'broken_bond.exe', 'rexruntime.dll', 'broken_bond.toml', 'broken_bond.version',
                'README.txt', 'licenses/README.txt', 'licenses/broken-bond-recomp.txt',
                'licenses/rmlui.txt', 'licenses/freetype.txt', 'licenses/roboto-condensed.txt',
                'licenses/volk.txt', 'licenses/vulkan-headers.txt'}
    with zipfile.ZipFile(archive) as package:
        assert package.testzip() is None, 'ZIP integrity failure'
        names = {item.filename for item in package.infolist() if not item.is_dir()}
        assert required <= names, sorted(required - names)
        assert not {Path(name).name for name in names} & forbidden, sorted({Path(n).name for n in names} & forbidden)
        assert not any(name.startswith(('game/', 'user/', 'assets/', 'mods/')) for name in names)
        assert not any(name.lower().endswith(('.iso', '.xex', '.sdb', '.bf', '.sra')) for name in names)
        for name in names:
            assert '\\' not in name and not Path(name).is_absolute() and '..' not in Path(name).parts, name
            assert hashlib.sha256(package.read(name)).digest() == hashlib.sha256((staged / name).read_bytes()).digest(), name
        assert names == {path.relative_to(staged).as_posix() for path in staged.rglob('*') if path.is_file()}
        config = tomllib.loads(package.read('broken_bond.toml').decode('utf-8-sig'))
        assert not config.get('custom_game_root')
        assert not config.get('user_data_root')
        assert not config.get('dlc_source_path', '').startswith('C:')
        version = package.read('broken_bond.version').decode('ascii').strip()
        assert version, 'empty version'
        assert version in package.read('README.txt').decode('utf-8-sig')
    sums = (archive.parent / 'SHA256SUMS.txt').read_text(encoding='ascii').splitlines()
    line = f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}'
    assert line in sums, sums
    print(f'PASS: {len(names)} ZIP files match the published folder; required files and notices, '
          f'clean defaults, version {version}, no launcher, installer or game data, SHA256SUMS.txt matches')


if __name__ == '__main__':
    main()
