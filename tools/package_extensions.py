"""Extension package assembly. Does not load modules, run tests or activate plugins."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def relative(value):
    path = PurePosixPath(value)

    if not value or path.is_absolute() or '\\' in value or ':' in value or '..' in path.parts:
        raise RuntimeError(f'Invalid extension package path: {value!r}')

    return Path(*path.parts)


def inventory(build, configuration):
    data = json.loads((build / f'extension-package-{configuration}.json').read_text())

    if data.get('schema') != 1 or data.get('configuration') != configuration:
        raise RuntimeError('Extension package inventory does not match this build configuration')

    if data.get('platform') not in ('linuxsteamrt64', 'win64'):
        raise RuntimeError('Extension package inventory has an unsupported platform')

    for module in data['modules']:
        relative(module['installed'])

        if relative(module['filename']).name != module['filename']:
            raise RuntimeError('Extension module filename must be a basename')

    for library in data['libraries']:
        relative(library['installed'])

    missing = set(data.get('required_external_sources', [])) - {d['name'] for d in data['dependencies']}

    if missing:
        raise RuntimeError('Missing corresponding Windows dependency sources: ' + ', '.join(sorted(missing)))

    return data


def source_inputs(data, source, lock):
    """Ship the actual source trees used by CMake, including explicit overrides.

    Upstream archive pins describe the intended dependency. Per-file snapshots
    describe what was actually built; the generated preload file selects those
    copies without fetching another revision during a corresponding-source build.
    """
    manifest = {}
    preload = ['# Actual extension sources accompanying this build.']
    licenses = source / 'extension-licenses'

    for dependency in data['dependencies']:
        name = dependency['name']

        if name not in lock or relative(name).name != name:
            raise RuntimeError(f'Unknown extension dependency: {name}')

        original = Path(dependency['source']).resolve(strict=True)
        target = source / 'dependencies' / name

        if target.exists():
            raise RuntimeError(f'Duplicate dependency source destination: {name}')

        if dependency.get('git_snapshot'):
            actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=original, text=True).strip()

            if actual != lock[name]['revision'] or subprocess.check_output(
                    ['git', 'diff', 'HEAD', '--name-only'], cwd=original, text=True).strip():
                raise RuntimeError(f'Dependency recipe differs from the lock: {name}')

            content = subprocess.check_output(['git', 'archive', 'HEAD'], cwd=original)
            target.mkdir(parents=True)

            with tarfile.open(fileobj=io.BytesIO(content)) as archive:
                archive.extractall(target, filter='data')
        else:
            # Internal links become regular files so ZIPs relocate on Windows.
            for entry in original.rglob('*'):
                if entry.is_symlink() and not entry.resolve(strict=True).is_relative_to(original):
                    raise RuntimeError(f'Extension source link escapes its tree: {entry}')

            shutil.copytree(original, target,
                ignore=shutil.ignore_patterns('.git', '__pycache__', '*.pyc'))

        hashes = {p.relative_to(target).as_posix(): digest(p)
                  for p in sorted(target.rglob('*')) if p.is_file()}
        manifest[name] = {'upstream': lock[name], 'files': hashes,
            'snapshot_sha256': hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()}

        if dependency.get('fetchcontent'):
            key = dependency['fetchcontent'].upper()

            if not key.startswith('SR_') or not key.replace('_', '').isalnum():
                raise RuntimeError('Invalid FetchContent source key')

            preload.append(f'set(FETCHCONTENT_SOURCE_DIR_{key} "${{CMAKE_CURRENT_LIST_DIR}}/{name}" CACHE PATH "Packaged source" FORCE)')
        else:
            preload.append(f'# {name}: external dependency source and build recipe; build before configuring Source2Root.')

        for entry in target.rglob('*'):
            if not entry.is_file() or not entry.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'COPYRIGHT', 'NOTICE')):
                continue

            destination = licenses / name / entry.relative_to(target)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(entry, destination)

    (source / 'extension-source-inputs.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (source / 'dependencies/extension-sources.cmake').write_text('\n'.join(preload) + '\n')
    return {name: {'upstream': entry['upstream'], 'snapshot_sha256': entry['snapshot_sha256'],
                   'file_count': len(entry['files'])} for name, entry in manifest.items()}


def assemble(data, build, configuration, package, developer):
    subprocess.run(['cmake', '--install', str(build), '--config', configuration,
        '--component', 'Source2RootExtensions', '--prefix', str(package)], check=True)
    native = package / 'addons/keels2/plugins' / data['platform']

    for module in data['modules']:
        installed = package / relative(module['installed'])

        if not installed.is_file():
            raise RuntimeError(f'Enabled extension was not installed: {module["name"]}')

        desired = native / module['filename']

        if installed != desired:
            if desired.exists():
                raise RuntimeError(f'Extension destination collision: {desired.name}')

            installed.rename(desired)

    for library in data['libraries']:
        if not (package / relative(library['installed'])).is_file():
            raise RuntimeError(f'Extension dependency was not installed: {library["target"]}')

    expected = {module['filename'] for module in data['modules']}
    actual = {path.name for path in native.iterdir() if path.is_file()}

    if actual != expected:
        raise RuntimeError('Unexpected files in native extension discovery directory')

    shutil.copytree(native, developer / 'extensions' / data['platform'])
    catalog = {
        'schema': 1, 'version': '1.0.0', 'platform': data['platform'],
        'requires': 'Matching Source2Root core and KeelS2 runtime package',
        'activation': 'Installing modules into addons/keels2/plugins activates them on the next host load. Select only needed modules.',
        'shared_libraries': 'Keep the lib subdirectory beside the installed modules; do not move its files into plugin discovery.',
        'configuration': 'Examples and operator templates are in the developer package. No credentials, production GeoIP database or reviewed game target catalog is bundled.',
        'modules': [{'name': module['name'], 'file': module['filename']} for module in data['modules']],
        'dependencies': [library['installed'] for library in data['libraries']],
        'runtime_validation': 'Not established by package assembly; see the separate verification report.'}

    for destination in (package / 'extensions.json', developer / 'extensions/extensions.json'):
        destination.write_text(json.dumps(catalog, indent=2) + '\n')

    return catalog
