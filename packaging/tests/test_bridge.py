"""Real Inno installs of the bridge, with an isolated registration and shortcuts.

BROKENBOND_TEST_PACKAGE is the new payload (the folder package-release.ps1
publishes), BROKENBOND_TEST_OLD_PACKAGE the extracted v0.1.0-beta ZIP, and
BROKENBOND_TEST_OUTPUT keeps the logs. The 0.1.0-beta installer is compiled
from the v0.1.0-beta tag with that payload; only its AppId and shortcut
folders are changed. Neither the game nor the launcher is started.
"""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import tomllib
import unittest
import uuid
import winreg

ROOT = Path(__file__).resolve().parents[2]
NAME = 'Naruto - The Broken Bond.lnk'
OLD_TAG = 'v0.1.0-beta'
# What a 0.1.0-beta copy has and the bridge removes, and the installer's tools,
# which an installed copy never had and the bridge must not bring.
RETIRED = ('broken_bond_launcher.exe', 'assets/banner-broken-bond.png', 'assets/favicon.ico',
           'assets/fonts/Lato-Bold.ttf', 'assets/fonts/Lato-Regular.ttf', 'assets/fonts/OFL.txt',
           'licenses/lato.txt', 'licenses/extract-xiso.txt')
NEVER = ('broken_bond_setup_helper.exe', 'extract-xiso.exe')


class BridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(os.environ.get('BROKENBOND_TEST_OUTPUT') or tempfile.mkdtemp(prefix='broken-bond-bridge-')).resolve()
        cls.root.mkdir(parents=True, exist_ok=True)
        cls.package = Path(os.environ.get('BROKENBOND_TEST_PACKAGE', ROOT / 'dist/broken-bond-recomp-win-amd64')).resolve()
        cls.version = (cls.package / 'broken_bond.version').read_text().strip()
        cls.compiler = Path(os.environ.get('BROKENBOND_TEST_ISCC', Path(os.environ['LOCALAPPDATA']) / 'Programs/Inno Setup 6/ISCC.exe'))
        cls.app_id = '{' + str(uuid.uuid4()).upper() + '}'
        cls.key = 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\' + cls.app_id + '_is1'
        cls.links = cls.root / 'shortcuts'
        cls.links.mkdir(exist_ok=True)
        cls.setup = cls.compile(ROOT / 'packaging/installer.iss', cls.package, cls.version, cls.root / 'bridge')
        cls.previous = None
        old_package = Path(os.environ.get('BROKENBOND_TEST_OLD_PACKAGE',
                                          ROOT / 'dist/v0.1.0-beta/package/broken-bond-recomp-win-amd64')).resolve()
        if old_package.is_dir():
            fixture = cls.root / 'old-source'
            fixture.mkdir()
            source = subprocess.check_output(['git', 'show', f'{OLD_TAG}:packaging/installer.iss'], cwd=ROOT).decode('utf-8')
            source = source.replace('..\\launcher\\assets', str(old_package / 'assets'))
            source = source.replace('{autodesktop}', str(cls.links / 'desktop')).replace('{group}', str(cls.links / 'start'))
            (fixture / 'installer.iss').write_text(source, encoding='utf-8')
            (fixture / 'locales').mkdir()
            for language in ('fr', 'de', 'es', 'it', 'ru'):
                (fixture / 'locales' / (language + '.iss')).write_bytes(subprocess.check_output(
                    ['git', 'show', f'{OLD_TAG}:packaging/locales/{language}.iss'], cwd=ROOT))
            # The ISO import is never entered: its two embedded tools only need a file.
            placeholder = fixture / 'placeholder.exe'
            shutil.copy2(old_package / 'broken_bond_launcher.exe', placeholder)
            cls.previous = cls.compile(fixture / 'installer.iss', old_package, '0.1.0-beta', cls.root / 'old',
                                       [f'/DHelperPath={placeholder}', f'/DExtractorPath={placeholder}'])
        print('Bridge test artifacts: ' + str(cls.root), flush=True)

    @classmethod
    def compile(cls, source, package, version, output, extra=()):
        result = subprocess.run([str(cls.compiler), '/Q', f'/O{output}', f'/DPackageDir={package}', f'/DMyAppVersion={version}',
            f'/DSetupAppId={cls.app_id.replace("{", "{{")}', f'/DDefaultInstallDir={cls.root / "missing"}',
            f'/DDesktopRoot={cls.links / "desktop"}', f'/DStartMenuRoot={cls.links / "start"}',
            *extra, str(source)], capture_output=True, timeout=300)
        output.mkdir(parents=True, exist_ok=True)
        (output / 'compile.log').write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError((result.stdout + result.stderr).decode(errors='replace'))
        return output / 'BrokenBondInstaller.exe'

    def setUp(self):
        self.case = self.root / self._testMethodName
        self.case.mkdir()
        self.dest = self.case / 'instalação ação'
        self.dest.mkdir()
        self.count = 0
        self.uninstalled = False

    def tearDown(self):
        if not self.uninstalled and (self.dest / 'unins000.exe').exists():
            self.uninstall()
        try:
            winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, self.key, winreg.KEY_WOW64_64KEY)
        except FileNotFoundError:
            pass

    def install(self, setup=None, mode='update', language='english', use_dir=True, icons=False):
        self.count += 1
        args = [str(setup or self.setup), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOFORCECLOSEAPPLICATIONS',
                f'/MODE={mode}', f'/LANG={language}', '/TASKS=', f'/LOG={self.case / (str(self.count) + "-install.log")}']
        if not icons: args += ['/NOICONS']
        if use_dir: args += [f'/DIR={self.dest}']
        return subprocess.run(args, capture_output=True, timeout=120).returncode

    def uninstall(self):
        result = subprocess.run([str(self.dest / 'unins000.exe'), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                                 f'/LOG={self.case / "uninstall.log"}'], capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0)
        self.uninstalled = True

    def old_portable(self, version='0.1.0-beta'):
        """A 0.1.0-beta ZIP copy: the port files, the launcher and its assets."""
        for name in RETIRED + ('broken_bond.exe', 'rexruntime.dll'):
            path = self.dest / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'old portable file')
        (self.dest / 'broken_bond.version').write_text(version)

    def user_files(self):
        files = {
            'broken_bond.toml': b'# personal settings\r\nfullscreen = false\r\nlauncher_language = "ru"\r\n',
            'game/default.xex': b'original game XEX', 'game/data.bin': b'original assets',
            'user/0000000000000000/55530825/00000001/save.bin': b'personal save',
            'user/0000000000000000/55530825/00000002/content.bin': b'installed DLC',
            'dlc/pending.bin': b'pending STFS',
            'mods/community/mod.toml': b'name = "mine"\r\n', 'mods/community/texts.json': b'{"1":"My translation"}',
            'assets/personal.png': b'personal picture', 'personal.txt': b'personal file',
            # A data folder a newer game made, and its portable location.
            'data/broken_bond.toml': b'launcher_language = "ru"\nunknown_option = "kept"\n',
            'data/user/E000000000000001/55530825/00000001/save.bin': b'data folder save',
            'data/logs/broken_bond_001.log': b'data folder log', 'data/dlc/pending.bin': b'data folder staging',
            'data/mods/community/texts.json': b'{"1":"Active translation"}',
            'data/.broken_bond-migration/state.json': b'{"version":1,"completed":[]}',
            'data_location.json': b'{"version":1,"mode":"portable"}',
        }
        for name, data in files.items():
            path = self.dest / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        return files

    def preserved(self, files):
        for name, data in files.items():
            self.assertEqual((self.dest / name).read_bytes(), data, name)

    def current(self, factory_defaults=True):
        # An update keeps the defaults as packaged; a new copy writes its language there.
        names = ['broken_bond.exe', 'rexruntime.dll', 'README.txt', 'licenses/README.txt']
        if factory_defaults:
            names.append('defaults/broken_bond.toml')
        for name in names:
            self.assertEqual(hashlib.sha256((self.dest / name).read_bytes()).digest(),
                             hashlib.sha256((self.package / name).read_bytes()).digest(), name)
        self.assertEqual((self.dest / 'broken_bond.version').read_text().strip(), self.version)
        for name in RETIRED + NEVER:
            self.assertFalse((self.dest / name).exists(), name)

    def shortcut(self, path, target=None):
        command = '''$p = $env:BROKENBOND_LINK_FIXTURE | ConvertFrom-Json
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($p.path)
if ($p.target) { $s.TargetPath=$p.target; $s.WorkingDirectory=(Split-Path $p.target -Parent); $s.Save() }
@{target=$s.TargetPath; cwd=$s.WorkingDirectory; args=$s.Arguments} | ConvertTo-Json -Compress'''
        path.parent.mkdir(parents=True, exist_ok=True)
        value = json.dumps({'path': str(path), 'target': str(target) if target else ''})
        return json.loads(subprocess.check_output(['pwsh', '-NoProfile', '-Command', command],
                          env=dict(os.environ, BROKENBOND_LINK_FIXTURE=value), encoding='utf-8'))

    def test_portable_update_and_repeat(self):
        self.old_portable()
        files = self.user_files()
        for _ in range(2):
            self.assertEqual(self.install(), 0)
            self.current()
            self.preserved(files)
        self.uninstall()
        self.preserved(files)

    def test_locked_launcher_rejects_before_replacing_files(self):
        self.old_portable()
        files = self.user_files()
        before = {p.relative_to(self.dest): p.read_bytes() for p in self.dest.rglob('*') if p.is_file()}
        create = ctypes.windll.kernel32.CreateFileW
        create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                           wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        create.restype = wintypes.HANDLE
        close = ctypes.windll.kernel32.CloseHandle
        close.argtypes = [wintypes.HANDLE]
        # Read access without write sharing, as a running launcher holds its image.
        handle = create(str(self.dest / 'broken_bond_launcher.exe'), 0x80000000, 1, None, 3, 0, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        try:
            self.assertNotEqual(self.install(), 0, 'a blocked update must not report success')
            after = {p.relative_to(self.dest): p.read_bytes() for p in self.dest.rglob('*') if p.is_file()}
            self.assertEqual(after, before, 'a refused update must not replace any installed file')
            with self.assertRaises(FileNotFoundError):
                winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.key)
        finally:
            close(handle)
        self.assertEqual(self.install(), 0)
        self.current()
        self.preserved(files)

    def test_registered_010_and_shortcuts(self):
        if not self.previous:
            self.skipTest('Supply BROKENBOND_TEST_OLD_PACKAGE (the extracted v0.1.0-beta ZIP)')
        self.assertEqual(self.install(self.previous, mode='install'), 0)
        for name in ('broken_bond_launcher.exe', 'assets/favicon.ico', 'licenses/lato.txt'):
            self.assertTrue((self.dest / name).exists(), name)
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.key, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            self.assertEqual(winreg.QueryValueEx(key, 'DisplayVersion')[0], '0.1.0-beta')
        files = self.user_files()
        for folder in ('desktop', 'start'):
            self.shortcut(self.links / folder / NAME, self.dest / 'broken_bond_launcher.exe')
        self.assertEqual(self.install(language='brazilianportuguese', use_dir=False, icons=True), 0)
        self.current()
        self.preserved(files)
        for folder in ('desktop', 'start'):
            link = self.shortcut(self.links / folder / NAME)
            self.assertEqual(Path(link['target']), self.dest / 'broken_bond.exe')
            self.assertEqual(Path(link['cwd']), self.dest)
            self.assertEqual(link['args'], '')
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.key, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            self.assertEqual(winreg.QueryValueEx(key, 'DisplayVersion')[0], self.version)
        self.uninstall()
        self.preserved(files)

    def test_shortcuts_to_other_copy(self):
        self.old_portable()
        other = self.case / 'another-copy/broken_bond_launcher.exe'
        self.shortcut(self.links / 'desktop' / NAME, other)
        self.assertEqual(self.install(icons=True), 0)
        self.assertEqual(Path(self.shortcut(self.links / 'desktop' / NAME)['target']), other)

    def test_reject_missing_invalid_mode_and_downgrade(self):
        self.assertNotEqual(self.install(), 0)
        self.assertEqual(list(self.dest.iterdir()), [])
        self.old_portable('9.0.0')
        for mode in ('update', 'install', 'invalid'):
            self.assertNotEqual(self.install(mode=mode), 0)
            self.assertEqual((self.dest / 'broken_bond.exe').read_bytes(), b'old portable file')
        self.assertTrue((self.dest / 'broken_bond_launcher.exe').exists())

    def test_new_install_language(self):
        for language, code in (('english', 'en'), ('brazilianportuguese', 'pt_BR'), ('french', 'fr'),
                               ('german', 'de'), ('spanish', 'es'), ('italian', 'it'), ('russian', 'ru')):
            with self.subTest(language=language):
                self.dest = self.case / language
                self.dest.mkdir()
                self.uninstalled = False
                self.assertEqual(self.install(mode='install', language=language), 0)
                self.current(factory_defaults=False)
                config = tomllib.loads((self.dest / 'defaults/broken_bond.toml').read_text(encoding='utf-8-sig'))
                self.assertEqual(config['launcher_language'], code)
                self.assertEqual(config['translation_mod'], '')
                # The game makes the data folder and its settings from the defaults.
                self.assertFalse((self.dest / 'broken_bond.toml').exists())
                self.assertFalse((self.dest / 'data').exists())
                self.uninstall()


if __name__ == '__main__':
    unittest.main()
