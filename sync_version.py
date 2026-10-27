"""Generate runtime version metadata from VERSION; --check detects drift."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def synchronize(check=False):
    version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('VERSION must contain a semantic release version')
    updates = {}
    for filename in ('app/appinfo.json', 'package/packageinfo.json'):
        path = ROOT / filename
        data = json.loads(path.read_text(encoding='utf-8'))
        if data['version'] != version:
            data['version'] = version
            updates[path] = json.dumps(data, indent=2, ensure_ascii=False) + '\n'
    header = '#ifndef HYPERDRM_VERSION_H\n#define HYPERDRM_VERSION_H\n#define HYPERDRM_VERSION "' + version + '"\n#endif\n'
    path = ROOT / 'native/version.h'
    if not path.exists() or path.read_text(encoding='utf-8') != header:
        updates[path] = header
    path = ROOT / 'app/version.js'
    script = "'use strict';\nvar HYPERDRM_APP_VERSION = '" + version + "';\n"
    if not path.exists() or path.read_text(encoding='utf-8') != script:
        updates[path] = script
    if check and updates:
        raise ValueError('Version metadata is stale; run sync_version.py')
    for path, content in updates.items():
        path.write_text(content, encoding='utf-8')
    return version

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    synchronize(parser.parse_args().check)
