"""Test, compile and verify a release locally. Never contacts the TV.

Use --push to commit the reviewed tree and push main and its version tag.
Release attachment publication requires the separately authorized Gitea session.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from sync_version import synchronize

ROOT = Path(__file__).resolve().parent

GIT = ['git', '-c', 'safe.directory=' + ROOT.as_posix()]

def run(*args):
    if args[0] == 'git':
        args = (*GIT, *args[1:])
    subprocess.run(args, cwd=ROOT, check=True)

def linux(*args):
    if os.name == 'nt':
        path = '/mnt/' + str(ROOT)[0].lower() + str(ROOT)[2:].replace('\\', '/')
        run('wsl', '-d', 'Debian', '--cd', path, '--exec', *args)
    else:
        run(*args)

def verify_package(version):
    import io
    import tarfile
    package = ROOT / f'HyperDRM_{version}_arm.ipk'
    data = package.read_bytes()
    manifest = json.loads((ROOT / 'HyperDRM.manifest.json').read_text(encoding='utf-8'))
    assert manifest['version'] == version
    assert manifest['ipkSize'] == len(data)
    assert manifest['ipkHash']['sha256'] == hashlib.sha256(data).hexdigest()
    assert data.startswith(b'!<arch>\n')
    offset = 8
    members = {}
    while offset < len(data):
        header = data[offset:offset + 60]
        assert len(header) == 60 and header[58:60] == b'`\n'
        size = int(header[48:58])
        offset += 60
        members[header[:16].decode().strip().rstrip('/')] = data[offset:offset + size]
        offset += size + size % 2
    with tarfile.open(fileobj=io.BytesIO(members['data.tar.gz']), mode='r:gz') as archive:
        for directory, filename in [('applications', 'appinfo.json'), ('packages', 'packageinfo.json')]:
            entry = archive.extractfile(f'usr/palm/{directory}/org.webosbrew.histogramhyperion/{filename}')
            assert json.load(entry)['version'] == version
        assert archive.extractfile('usr/palm/applications/org.webosbrew.histogramhyperion/version.js').read() == (ROOT / 'app/version.js').read_bytes()
        binary = archive.extractfile('usr/palm/services/org.webosbrew.histogramhyperion.service/histogram-service').read()
        assert binary[:4] == b'\x7fELF' and binary[4] == 1 and binary[18:20] == b'\x28\x00'
        assert version.encode() in binary
    print(f'Verified {package.name}: {len(data)} bytes, SHA256 {manifest["ipkHash"]["sha256"]}')
    return package

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--push', action='store_true')
    parser.add_argument('--sanitizers', action='store_true', help='Run parser, atomic-save, output and service lifecycle tests with ASan/UBSan')
    options = parser.parse_args()
    version = synchronize()
    run(sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py')
    node = shutil.which('node')
    if not node:
        raise RuntimeError('Node.js is required for UI tests')
    for test in sorted((ROOT / 'tests').glob('*.test.js')):
        run(node, str(test))
    # Host tests exercise the same portable code without LG libraries or a TV.
    for test in sorted((ROOT / 'native').glob('test_*.c')):
        name = '/tmp/hyperdrm-' + test.stem
        linux('gcc', '-Wall', '-Wextra', '-Werror', '-O2', str(test.relative_to(ROOT)).replace('\\', '/'), '-ldl', '-lm', '-o', name)
        linux(name)
    if options.sanitizers:
        for name in ('json_protocol', 'atomic_file', 'receiver_output', 'service_lifecycle'):
            destination = '/tmp/hyperdrm-sanitized-' + name
            linux('gcc', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-fsanitize=address,undefined',
                  'native/test_' + name + '.c', '-ldl', '-lm', '-o', destination)
            linux(destination)
    linux('arm-linux-gnueabi-gcc', '-Wall', '-Wextra', '-Werror', '-O2', '-rdynamic',
          'native/service.c', '-ldl', '-lm', '-o', 'service/histogram-service')
    run(sys.executable, 'build_package.py')
    run(sys.executable, 'build_manifest.py')
    verify_package(version)
    run('git', 'diff', '--check')
    if options.push:
        branch = subprocess.check_output(GIT + ['branch', '--show-current'], cwd=ROOT, text=True).strip()
        if branch != 'main':
            raise RuntimeError('Release pushes require main')
        # Fail before committing when this version already exists.
        exists = subprocess.run(GIT + ['rev-parse', '--verify', 'refs/tags/v' + version], cwd=ROOT, capture_output=True)
        if exists.returncode == 0:
            raise RuntimeError('Version tag already exists; bump VERSION')
        run('git', 'add', '--all')
        run('git', 'commit', '-m', f'Release {version}')
        run('git', 'tag', 'v' + version)
        run('git', 'push', '--atomic', 'origin', 'main', 'v' + version)
    print('Attach the verified IPK to the matching Gitea release through an authorized session.')

if __name__ == '__main__':
    main()
