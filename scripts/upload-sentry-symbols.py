"""Upload only the selected Broken Bond build's native debug artifacts to Sentry
(naruto-rise/brokenbond), usually an archive of scripts/archive-native-symbols.py.

Requires sentry-cli and SENTRY_AUTH_TOKEN in the local/CI environment. Does not
upload source files. Never searches other builds or uploads game data.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='The exact build that will be distributed')
    parser.add_argument('--org', default='naruto-rise')
    parser.add_argument('--project', default='brokenbond')
    parser.add_argument('--cli', default='sentry-cli')
    parser.add_argument('--dry-run', action='store_true', help='List selected files; do not upload')
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    if not build.is_dir():
        parser.error('build must be a directory')
    binaries = [build / name for name in ('broken_bond.exe', 'broken_bond', 'rexruntime.dll',
                                          'librexruntime.so', 'sentry.dll',
                                          'crashpad_handler.exe', 'crashpad_handler', 'crashpad_wer.dll')]
    binaries = sorted({path for path in binaries if path.is_file()})
    if not any(path.name in ('broken_bond.exe', 'broken_bond') for path in binaries):
        parser.error('No Broken Bond application in the selected build')
    files = set(binaries)
    for binary in binaries:
        # Upload matching sidecars and the actual binaries (unwind information).
        files.update(path for path in (binary.with_suffix('.pdb'), Path(str(binary) + '.debug'),
                                       binary.with_suffix('.debug')) if path.is_file())
    if (build / 'broken_bond.exe').is_file() and not (build / 'broken_bond.pdb').is_file():
        parser.error('Missing broken_bond.pdb; link this Windows build with debug information first')
    files = sorted(files)
    print(f'Target: {args.org}/{args.project}')
    for path in files:
        print(path.name)
    if args.dry_run:
        return
    if not os.environ.get('SENTRY_AUTH_TOKEN'):
        parser.error('Set SENTRY_AUTH_TOKEN in the local/CI environment before uploading')
    cli = shutil.which(args.cli)
    if not cli:
        parser.error('sentry-cli is not installed; use --cli to select its executable')
    subprocess.run([cli, 'debug-files', 'upload', '--org', args.org, '--project', args.project,
                    '--no-sources', '--wait', *map(str, files)], check=True)


if __name__ == '__main__':
    main()
