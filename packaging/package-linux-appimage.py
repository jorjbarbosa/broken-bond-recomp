#!/usr/bin/env python3
"""Builds the Linux AppImage of the native game from build/linux. No game data.

Usage: python3 packaging/package-linux-appimage.py [--version V] [--output-root DIR]
                                                    [--tools-dir DIR] [--jobs N]

The payload is the game, the SDK runtime, the POSIX wait check, the defaults
a new data folder starts from (on Vulkan), the version, a README and the
licenses. The game runs from the image and prepares its data folder itself;
the AppRun removes the program copy that earlier images ran from the data
folder (packaging/appimage/AppRun). No system
library is bundled: the binaries need the host's glibc, X11 or Wayland
client libraries and Vulkan loader. appimagetool and the type2 runtime are
pinned by SHA-256 and kept in --tools-dir once downloaded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parent.parent
NAME = 'broken-bond-recomp-linux-x86_64'
TOOLS = {
    'appimagetool': ('https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage',
                     'ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0'),
    # The continuous build of 2026-10-19. Upstream republishes this URL; the
    # download then fails until the pin is checked and moved.
    'runtime': ('https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64',
                '617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46'),
}
# The payload's binaries and their modes; Crashpad's handler joins them when the
# build has the crash reports (sentry-notices.txt beside the game).
BINARIES = {'broken_bond': 0o755, 'librexruntime.so': 0o644, 'broken_bond_posix_wait_test': 0o755}
SENTRY_BINARIES = {'crashpad_handler': 0o755}
# What a Linux desktop provides, and libcurl for the crash reports' uploads (D-85):
# the binaries may need nothing else directly.
HOST_LIBRARIES = {'linux-vdso.so.1', 'ld-linux-x86-64.so.2', 'libc.so.6', 'libm.so.6', 'libstdc++.so.6',
                  'libgcc_s.so.1', 'libX11.so.6', 'libX11-xcb.so.1', 'libxcb.so.1', 'libXau.so.6',
                  'libXdmcp.so.6', 'libwayland-client.so.0', 'libffi.so.8', 'libcurl.so.4'}


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def git(path, *arguments):
    return subprocess.run(['git', '-C', str(ROOT / path), *arguments], capture_output=True, text=True,
                          check=True).stdout.strip()


def release_version(requested):
    if not requested:
        match = re.search(r'#define MyAppVersion "([^"]+)"', (ROOT / 'packaging/version.iss').read_text())
        if not match:
            raise SystemExit('Missing release version.')
        requested = match.group(1)
    # major.minor.patch, with an optional pre-release suffix, as the ZIP.
    if not re.fullmatch(r'\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?', requested):
        raise SystemExit('Use a major.minor.patch version, with an optional -suffix.')
    return requested


def check_build(build):
    cache = build / 'CMakeCache.txt'
    if not cache.is_file():
        raise SystemExit('Configure and build build/linux first (docs/build-and-run.md, "Linux").')
    values = dict(re.findall(r'^([A-Za-z0-9_]+):[A-Z]+=(.*)$', cache.read_text(), re.M))
    def on(key):
        return values.get(key, '').upper() in ('ON', 'TRUE', 'YES', '1')
    if values.get('CMAKE_BUILD_TYPE') != 'Release':
        raise SystemExit('Packaging requires a Release build.')
    if not on('BROKENBOND_NATIVE_RENDERER') or not on('BROKENBOND_NATIVE_VULKAN'):
        raise SystemExit('The AppImage requires the native game on Vulkan (the linux-release preset).')
    if not on('BROKENBOND_NATIVE_TESTS'):
        raise SystemExit('The AppImage carries the POSIX wait check: configure build/linux with '
                         '-DBROKENBOND_NATIVE_TESTS=ON.')
    return values


def fetch_tool(tools_dir, key):
    url, digest = TOOLS[key]
    target = tools_dir / digest / url.rsplit('/', 1)[1]
    if not target.is_file():
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_name(target.name + '.part')
        request = urllib.request.Request(url, headers={'User-Agent': 'broken-bond-packaging'})
        with urllib.request.urlopen(request, timeout=120) as response, partial.open('wb') as out:
            shutil.copyfileobj(response, out)
        actual = sha256(partial)
        if actual != digest:
            partial.unlink()
            raise SystemExit(f'{key}: {url} has SHA-256 {actual}, not the pinned {digest}. '
                             'Upstream published another file: check it, then move the pin in this script.')
        partial.rename(target)
    if sha256(target) != digest:
        raise SystemExit(f'{key}: the kept {target} does not match its pin; delete it.')
    return target


def components():
    text = (ROOT / 'packaging/third-party-licenses.psd1').read_text(encoding='utf-8-sig')
    # A component block, whose quoted values may hold braces ('{fmt}').
    blocks = re.findall(r"@\{\s*(Name = (?:'(?:[^']|'')*'|[^'}])*)\}", text)
    parsed = [{key: value.replace("''", "'") for key, value in re.findall(r"(\w+) = '((?:[^']|'')*)'", block)}
              for block in blocks]
    keys = {'Name', 'License', 'Url', 'File', 'Target', 'Usage'}
    if len(parsed) != text.count('Name = ') or any(not keys <= set(item) for item in parsed):
        raise SystemExit('packaging/third-party-licenses.psd1 did not parse.')
    # Linux builds the Vulkan backend only, without FidelityFX.
    return [item for item in parsed if item.get('Condition') in (None, 'NativeVulkan')]


def write_licenses(folder, version, build):
    folder.mkdir()
    if not (ROOT / 'LICENSE').is_file():
        raise SystemExit('Missing project LICENSE.')
    shutil.copyfile(ROOT / 'LICENSE', folder / 'broken-bond-recomp.txt')
    entries = []
    for item in components():
        source = ROOT / item['File']
        if not source.is_file():
            raise SystemExit(f"Missing license for {item['Name']}: {item['File']}")
        shutil.copyfile(source, folder / item['Target'])
        usage = item['Usage'].replace('broken_bond.exe', 'broken_bond').replace('rexruntime.dll', 'librexruntime.so')
        entries.append(f"{item['Name']}\n  License: {item['License']} ({item['Target']})\n"
                       f"  Source:  {item['Url']}\n  Used in: {usage}")
    if (build / 'sentry-notices.txt').is_file():
        # The notices of the fetched release, as the build wrote them.
        shutil.copyfile(build / 'sentry-notices.txt', folder / 'sentry-native.txt')
        entries.append('Sentry Native 0.17.4 with Crashpad and their bundled libraries\n'
                       "  License: MIT (Sentry Native), Apache-2.0 (Crashpad) and the bundled libraries' "
                       'licenses (sentry-native.txt)\n'
                       '  Source:  https://github.com/getsentry/sentry-native\n'
                       '  Used in: broken_bond, crashpad_handler (opt-in crash reports)')
    # The LGPL libraries are statically linked into librexruntime.so: name the
    # exact source revisions it is built from, so it can be rebuilt with modified copies.
    sources = []
    for name, url, path in (
            ('Naruto: The Broken Bond PC port (SDK patches: patches)', 'https://github.com/jorjbarbosa/broken-bond-recomp', '.'),
            ('ReXGlue SDK (librexruntime.so)', 'https://github.com/rexglue/rexglue-sdk', 'sdk'),
            ('FFmpeg', 'https://github.com/wmarti/FFmpeg', 'sdk/thirdparty/FFmpeg'),
            ('libmspack', 'https://github.com/kyz/libmspack', 'sdk/thirdparty/libmspack')):
        sources.append(f'  {name}\n    {url}/tree/{git(path, "rev-parse", "HEAD")}')
    newline = '\n'
    (folder / 'README.txt').write_text(f"""THIRD-PARTY NOTICES - NARUTO: THE BROKEN BOND PC PORT ({version}, LINUX)

This port's own code is licensed under GPL-3.0-only: broken-bond-recomp.txt.
It includes the components below, each under its own license; the license texts
are in this folder. Naruto: The Broken Bond, its content, artwork and trademarks
belong to their respective owners and are not covered by these licenses. Game
data is not distributed with this port.

{(newline + newline).join(entries)}

LGPL SOURCE CODE:
FFmpeg (LGPL-2.1-or-later) and libmspack (LGPL-2.1-only) are statically linked
into librexruntime.so, which is built from the ReXGlue SDK (BSD-3-Clause). The
complete source code of these libraries and of librexruntime.so, at the
revisions this release was built from:

{newline.join(sources)}

You may modify these libraries, rebuild librexruntime.so from these sources and
replace the librexruntime.so of the game's data folder with it.
""", encoding='utf-8', newline='\n')


def host_libraries(payload, binaries):
    found = {}
    env = {**os.environ, 'LD_LIBRARY_PATH': str(payload)}
    for name in binaries:
        # The libraries a binary names itself (DT_NEEDED); what those load in turn
        # comes with them on the host.
        dynamic = subprocess.run(['readelf', '-d', str(payload / name)], capture_output=True, text=True,
                                 check=True).stdout
        needed = set(re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', dynamic))
        output = subprocess.run(['ldd', str(payload / name)], env=env, capture_output=True, text=True,
                                check=True).stdout
        if 'not found' in output:
            raise SystemExit(f'{name}: missing libraries\n{output}')
        for line in output.splitlines():
            parts = line.split()
            if not parts:
                continue
            soname = Path(parts[0]).name
            if soname not in needed:
                continue
            resolved = parts[2] if len(parts) > 2 and parts[1] == '=>' else parts[0]
            if soname == 'librexruntime.so':
                if Path(resolved).resolve().parent != payload.resolve():
                    raise SystemExit(f'{name} loads librexruntime.so from {resolved}, not the payload.')
                continue
            if soname not in HOST_LIBRARIES:
                raise SystemExit(f'{name} needs {soname} ({resolved}), which is not a host library here. '
                                 'Bundle it deliberately or drop the dependency.')
            found[soname] = resolved
    return dict(sorted(found.items()))


def glibc_required(paths):
    versions = set()
    for path in paths:
        text = subprocess.run(['readelf', '--version-info', '--wide', str(path)], capture_output=True, text=True,
                              check=True).stdout
        versions |= set(re.findall(r'GLIBC_(\d+(?:\.\d+)+)', text))
    return max(versions, key=lambda value: tuple(map(int, value.split('.'))))


def build_appdir(appdir, build, version, values):
    payload = appdir / 'usr/lib/broken_bond'
    payload.mkdir(parents=True)
    hashes = {}
    binaries = dict(BINARIES)
    if (build / 'sentry-notices.txt').is_file():
        binaries.update(SENTRY_BINARIES)
    for name, mode in binaries.items():
        source = build / name
        if not source.is_file() or source.stat().st_size == 0:
            raise SystemExit(f'Missing or empty binary: {source}')
        hashes[name] = sha256(source)
        shutil.copyfile(source, payload / name)
        (payload / name).chmod(mode)
        if sha256(payload / name) != hashes[name]:
            raise SystemExit(f'Payload changed while copied: {name}')
    # The ZIP's defaults, on the only API of the Linux build.
    seed, count = re.subn(r'^native_backend = "d3d12"$', 'native_backend = "vulkan"',
                          (ROOT / 'packaging/broken_bond.toml').read_text(encoding='utf-8'), flags=re.M)
    if count != 1:
        raise SystemExit('packaging/broken_bond.toml: expected one native_backend = "d3d12" line.')
    if re.search(r'^(custom_game_root|user_data_root|cache_root|log_file)\s*=', seed, re.M):
        raise SystemExit('packaging/broken_bond.toml carries a personal path.')
    (payload / 'defaults').mkdir()
    (payload / 'defaults/broken_bond.toml').write_text(seed, encoding='utf-8', newline='\n')
    (payload / 'broken_bond.version').write_text(version + '\n', encoding='ascii', newline='\n')
    readme = (ROOT / 'packaging/appimage/README.txt').read_text(encoding='utf-8').replace('@VERSION@', version)
    (payload / 'README.txt').write_text(readme, encoding='utf-8', newline='\n')
    write_licenses(payload / 'licenses', version, build)

    shutil.copyfile(ROOT / 'packaging/appimage/AppRun', appdir / 'AppRun')
    (appdir / 'AppRun').chmod(0o755)
    subprocess.run(['sh', '-n', str(appdir / 'AppRun')], check=True)
    shutil.copyfile(ROOT / 'packaging/appimage/broken_bond.desktop', appdir / 'broken_bond.desktop')
    shutil.copyfile(ROOT / 'ui/broken_bond.png', appdir / 'broken_bond.png')
    (appdir / '.DirIcon').symlink_to('broken_bond.png')

    material = {'binaries': hashes, 'defaults': sha256(payload / 'defaults/broken_bond.toml'),
                'apprun': sha256(appdir / 'AppRun'), 'version': version, 'layout': 'image-1'}
    build_id = hashlib.sha256(json.dumps(material, sort_keys=True).encode()).hexdigest()[:24]
    (appdir / 'build-id').write_text(build_id + '\n', encoding='ascii', newline='\n')
    dirty = git('.', 'status', '--porcelain', '--untracked-files=no', '--', '.', ':!sdk')
    info = {**material, 'build_id': build_id,
            'commit': git('.', 'rev-parse', 'HEAD') + ('-dirty' if dirty else ''),
            'compiler': values.get('CMAKE_CXX_COMPILER', ''),
            'glibc_required': glibc_required([payload / 'broken_bond', payload / 'librexruntime.so']),
            'host_libraries': host_libraries(payload, binaries),
            'tools': {key: {'url': TOOLS[key][0], 'sha256': TOOLS[key][1]} for key in TOOLS}}
    (appdir / 'packaging-info.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8', newline='\n')
    return info, readme


def main():
    if not sys.platform.startswith('linux'):
        raise SystemExit('Build the AppImage on Linux, from build/linux.')
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--version')
    parser.add_argument('--output-root', type=Path, default=ROOT / 'dist')
    parser.add_argument('--tools-dir', type=Path, default=ROOT / 'out/appimage-tools')
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    version = release_version(args.version)
    build = ROOT / 'build/linux'
    values = check_build(build)
    tools = {key: fetch_tool(args.tools_dir.resolve(), key) for key in TOOLS}
    subprocess.run(['cmake', '--build', str(build), '--target', 'broken_bond', 'broken_bond_posix_wait_test',
                    '--parallel', str(args.jobs)], check=True)

    # Built in a fresh stage; only complete artifacts are published.
    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    stage = output_root / f'.packaging-{uuid.uuid4().hex}'
    stage.mkdir()
    try:
        info, readme = build_appdir(stage / 'BrokenBond.AppDir', build, version, values)
        tool = stage / 'appimagetool'
        tool.mkdir()
        shutil.copyfile(tools['appimagetool'], tool / 'appimagetool.AppImage')
        (tool / 'appimagetool.AppImage').chmod(0o755)
        # Extracted, so building needs no FUSE.
        subprocess.run([str(tool / 'appimagetool.AppImage'), '--appimage-extract'], cwd=tool,
                       stdout=subprocess.DEVNULL, check=True)
        image = stage / f'{NAME}.AppImage'
        log = stage / 'appimagetool.log'
        with log.open('w') as stream:
            result = subprocess.run([str(tool / 'squashfs-root/AppRun'), '--runtime-file', str(tools['runtime']),
                                     '--no-appstream', str(stage / 'BrokenBond.AppDir'), str(image)],
                                    env={**os.environ, 'ARCH': 'x86_64'}, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode != 0 or not image.is_file():
            raise SystemExit('appimagetool failed:\n' + log.read_text()[-4000:])
        image.chmod(0o755)
        (stage / 'README.txt').write_text(readme, encoding='utf-8', newline='\n')
        (stage / 'SHA256SUMS.txt').write_text(f'{sha256(image)}  {image.name}\n{sha256(stage / "README.txt")}  README.txt\n',
                                              encoding='ascii', newline='\n')
        artifacts = [image.name, 'README.txt', 'SHA256SUMS.txt']
        # Existing artifacts are kept as backups, never deleted.
        backup = output_root / f'previous-release-{uuid.uuid4().hex}'
        for name in artifacts:
            if (output_root / name).exists():
                backup.mkdir(exist_ok=True)
                (output_root / name).rename(backup / name)
        for name in artifacts:
            (stage / name).rename(output_root / name)
        result = {'appimage': str(output_root / image.name), 'bytes': (output_root / image.name).stat().st_size,
                  'sha256': sha256(output_root / image.name), 'build_id': info['build_id'], 'version': version,
                  'glibc_required': info['glibc_required'], 'host_libraries': sorted(info['host_libraries'])}
        if backup.exists():
            result['previous'] = str(backup)
        print(json.dumps(result, indent=2))
    finally:
        # The stage is a unique folder made by this run, inside the output root.
        if stage.parent == output_root and stage.name.startswith('.packaging-'):
            shutil.rmtree(stage, ignore_errors=True)


if __name__ == '__main__':
    main()
