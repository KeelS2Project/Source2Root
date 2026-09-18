"""Fetch and select compatibility-data snapshots; never modify a game installation."""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone, timedelta
import hashlib
import http.client
import json
import math
import os
from pathlib import Path
import re
import shutil
import ssl
import stat
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

MANIFEST_LIMIT = 1024 * 1024
DATA_LIMIT = 512 * 1024
FILE_LIMIT = 128 * 1024
RELEASE_LIMIT = 16


class UpdateError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise UpdateError(message)


def keys(value, required, optional=()):
    require(isinstance(value, dict) and set(required) <= value.keys()
            and value.keys() <= set(required) | set(optional), 'Unexpected or missing fields')


def integer(value, minimum=1, maximum=2**63 - 1):
    require(type(value) is int and minimum <= value <= maximum, 'Integer is out of range')
    return value


def decode(raw):
    require(len(raw) <= MANIFEST_LIMIT, 'JSON exceeds the size limit')
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, 'Duplicate JSON key')
            result[key] = value
        return result
    def constant(value):
        raise UpdateError('Nonfinite JSON number')
    try:
        result = json.loads(raw.decode('utf-8'), object_pairs_hook=pairs, parse_constant=constant)
        pending = [(result, 0)]
        while pending:
            value, depth = pending.pop()
            require(depth <= 32, 'JSON is too deeply nested')
            if isinstance(value, dict):
                pending.extend((item, depth + 1) for item in value.values())
            elif isinstance(value, list):
                pending.extend((item, depth + 1) for item in value)
            elif isinstance(value, float):
                require(math.isfinite(value), 'Nonfinite JSON number')
        return result
    except (ValueError, UnicodeError, RecursionError) as error:
        raise UpdateError('Invalid UTF-8 JSON') from error


def encoded(value):
    return (json.dumps(value, indent=2, ensure_ascii=True, sort_keys=True) + '\n').encode('utf-8')


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def digest(value):
    require(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value), 'Invalid SHA-256')
    return value


def filename(value):
    require(isinstance(value, str) and re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,62}\.(json|tsv)', value),
            'Data filenames must be lowercase .json or .tsv basenames')
    require(value.split('.')[0] not in {'con', 'prn', 'aux', 'nul'}
            and not re.fullmatch(r'(com|lpt)[0-9]', value.split('.')[0]), 'Reserved data filename')
    require(value != 'snapshot.json', 'snapshot.json is reserved for export metadata')
    return value


def no_link(path):
    require(not path.is_symlink() and not getattr(path, 'is_junction', lambda: False)(),
            'Links are not allowed in the snapshot store')
    try:
        attributes = getattr(path.lstat(), 'st_file_attributes', 0)
    except FileNotFoundError:
        return
    require(not attributes & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0), 'Reparse points are not allowed in the snapshot store')


def read_file(path, limit=MANIFEST_LIMIT):
    no_link(path)
    require(stat.S_ISREG(path.stat().st_mode), 'Expected a regular file')
    with path.open('rb') as stream:
        info = os.fstat(stream.fileno())
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, 'Expected a regular file without hard links')
        require(info.st_size <= limit, 'File exceeds the size limit')
        raw = stream.read(limit + 1)
        require(len(raw) <= limit, 'File exceeds the size limit')
        return raw


def timestamp(value):
    require(isinstance(value, str) and re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ', value),
            'Timestamp must use UTC YYYY-MM-DDTHH:MM:SSZ')
    try:
        return datetime.strptime(value, '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=timezone.utc)
    except ValueError as error:
        raise UpdateError('Invalid timestamp') from error


def configuration(path):
    data = decode(read_file(path, 32 * 1024))
    keys(data, ('schema', 'url', 'product', 'game', 'platform', 'files', 'minimum_sequence'), ('ca_file', 'timeout'))
    require(type(data['schema']) is int and data['schema'] == 1, 'Unsupported configuration schema')
    require(isinstance(data['url'], str) and len(data['url']) <= 2048
            and not any(ord(c) <= 32 or ord(c) >= 127 for c in data['url']), 'Invalid update URL')
    url = urllib.parse.urlsplit(data['url'])
    require(url.scheme == 'https' and url.hostname and not url.username and not url.password
            and not url.fragment and not url.query, 'Update source requires HTTPS without credentials, query or fragment')
    try:
        url.port
    except ValueError as error:
        raise UpdateError('Invalid update port') from error
    require(data['product'] in ('KeelS2', 'Source2Root') and data['game'] == 'cs2'
            and data['platform'] in ('linuxsteamrt64', 'win64'), 'Unsupported product, game or platform')
    require(isinstance(data['files'], list) and 1 <= len(data['files']) <= 32, 'Configure 1..32 data filenames')
    for name in data['files']:
        filename(name)
    require(len(set(data['files'])) == len(data['files']), 'Duplicate allowed filename')
    integer(data['minimum_sequence'])
    data['timeout'] = integer(data.get('timeout', 15), 1, 60)
    if 'ca_file' in data:
        require(isinstance(data['ca_file'], str) and data['ca_file'], 'Invalid CA file')
        data['ca_file'] = str((path.parent / data['ca_file']).resolve(strict=True))
    return data


def identity(config):
    result = {key: config[key] for key in ('url', 'product', 'game', 'platform')}
    result['files'] = sorted(config['files'])
    # A trust-store change must not silently reinterpret an existing stream.
    result['ca_sha256'] = sha256(read_file(Path(config['ca_file']))) if config.get('ca_file') else 'system'
    return result


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file, code, message, headers, new_url):
        file.close()
        raise UpdateError('The update source redirected; configure the intended HTTPS URL explicitly')


def fetch(config):
    context = ssl.create_default_context(cafile=config.get('ca_file'))
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect(),
                                        urllib.request.HTTPSHandler(context=context))
    request = urllib.request.Request(config['url'], headers={'Accept': 'application/json', 'Accept-Encoding': 'identity'})
    deadline = time.monotonic() + config['timeout']
    try:
        response = opener.open(request, timeout=config['timeout'])
    except urllib.error.HTTPError as error:
        error.close()
        raise UpdateError('Update source returned HTTP ' + str(error.code)) from error
    with response:
        require(response.status == 200 and response.url == config['url'], 'Unexpected update response')
        require(response.headers.get('Content-Encoding', 'identity').lower() == 'identity', 'Compressed manifests are not accepted')
        length = response.headers.get('Content-Length')
        if length is not None:
            require(length.isascii() and length.isdecimal() and int(length) <= MANIFEST_LIMIT, 'Invalid manifest length')
        result = bytearray()
        while True:
            require(time.monotonic() < deadline, 'Update response exceeded its deadline')
            chunk = response.read1(min(16384, MANIFEST_LIMIT + 1 - len(result)))
            if not chunk:
                break
            result.extend(chunk)
            require(len(result) <= MANIFEST_LIMIT, 'Manifest exceeds 1 MiB')
        require(length is None or len(result) == int(length), 'Incomplete manifest response')
        return bytes(result)


def manifest(raw, config, fresh=False):
    value = decode(raw)
    keys(value, ('schema', 'product', 'game', 'platform', 'sequence', 'version', 'issued_at', 'expires_at', 'files'))
    require(type(value['schema']) is int and value['schema'] == 1, 'Unsupported manifest schema')
    for key in ('product', 'game', 'platform'):
        require(value[key] == config[key], 'Manifest does not match the configured ' + key)
    integer(value['sequence'], config['minimum_sequence'] if fresh else 1)
    require(isinstance(value['version'], str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}', value['version']), 'Invalid version label')
    issued, expires = timestamp(value['issued_at']), timestamp(value['expires_at'])
    require(issued < expires <= issued + timedelta(days=30), 'Manifest validity must be at most 30 days')
    if fresh:
        now = datetime.now(timezone.utc)
        require(issued <= now + timedelta(minutes=5) and now < expires, 'Manifest has expired or is not yet valid')
    require(isinstance(value['files'], list) and 1 <= len(value['files']) <= 32, 'Manifest needs 1..32 files')
    names, total = set(), 0
    for entry in value['files']:
        keys(entry, ('name', 'sha256', 'content'))
        name = filename(entry['name'])
        require(name in config['files'] and name not in names, 'Unexpected or duplicate data filename')
        names.add(name)
        require(isinstance(entry['content'], str) and '\0' not in entry['content'], 'Data must be NUL-free UTF-8 text')
        try:
            content = entry['content'].encode('utf-8')
        except UnicodeError as error:
            raise UpdateError('Invalid data encoding') from error
        total += len(content)
        require(0 < len(content) <= FILE_LIMIT and total <= DATA_LIMIT, 'Data exceeds snapshot limits')
        require(sha256(content) == digest(entry['sha256']), 'Data hash mismatch')
        if name.endswith('.json'):
            require(isinstance(decode(content), dict), 'JSON data must contain an object')
        else:
            rows = entry['content'].splitlines()
            require(rows and rows[0] == 'keels2-compatibility-profile\t1', 'TSV data must be a Keel compatibility profile')
            for field, expected in (('status', 'candidate-untrusted'), ('review', 'required'),
                                    ('game', config['game']), ('platform', config['platform'])):
                matches = [row.split('\t') for row in rows[1:] if row.split('\t')[0] == field]
                require(matches == [[field, expected]], 'Profile trust or target does not match the candidate policy')
    require(names == set(config['files']), 'Manifest must contain the complete configured file set')
    return value


def sync_directory(path):
    if os.name != 'nt':
        descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def write_new(path, raw):
    with path.open('xb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())


class Store:
    def __init__(self, path, config):
        self.path, self.config = Path(path).absolute(), config
        for parent in [*reversed(self.path.parents), self.path]:
            no_link(parent)
        self.path.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.releases = self.path / 'releases'
        no_link(self.releases)
        self.releases.mkdir(exist_ok=True, mode=0o700)

    @contextmanager
    def locked(self):
        lock = self.path / 'update.lock'
        try:
            descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        except FileExistsError as error:
            raise UpdateError('Snapshot store is locked; finish the other updater or inspect a stale lock') from error
        try:
            with os.fdopen(descriptor, 'w') as stream:
                stream.write(str(os.getpid()) + '\n')
            yield
        finally:
            lock.unlink()

    def state(self):
        path = self.path / 'state.json'
        if not path.exists() and not path.is_symlink():
            require(not any(self.releases.iterdir()), 'Snapshot index is missing; restore the index before updating')
            return {'schema': 1, 'source': identity(self.config), 'highest': 0, 'selected': None, 'previous': None, 'releases': {}}
        state = decode(read_file(path, 32 * 1024))
        keys(state, ('schema', 'source', 'highest', 'selected', 'previous', 'releases'))
        require(type(state['schema']) is int and state['schema'] == 1 and state['source'] == identity(self.config), 'Store belongs to another source or trust policy')
        integer(state['highest'], 0)
        require(isinstance(state['releases'], dict) and len(state['releases']) <= RELEASE_LIMIT, 'Invalid snapshot index')
        for sequence, value in state['releases'].items():
            require(re.fullmatch('[1-9][0-9]{0,18}', sequence), 'Invalid stored sequence')
            integer(int(sequence))
            keys(value, ('sha256', 'version'))
            digest(value['sha256'])
        require(state['highest'] == max((int(n) for n in state['releases']), default=0), 'Invalid sequence history')
        for field in ('selected', 'previous'):
            require(state[field] is None or (type(state[field]) is int and str(state[field]) in state['releases']), 'Invalid snapshot selection')
        return state

    def save(self, state):
        target = self.path / 'state.json'
        no_link(target)
        fd, name = tempfile.mkstemp(prefix='.state-', dir=self.path)
        temporary = Path(name)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(encoded(state)); stream.flush(); os.fsync(stream.fileno())
            os.replace(temporary, target)
            sync_directory(self.path)
        finally:
            temporary.unlink(missing_ok=True)

    def verify(self, sequence, record):
        directory = self.releases / str(sequence)
        no_link(directory)
        raw = read_file(directory / 'manifest.json')
        require(sha256(raw) == record['sha256'], 'Stored manifest hash mismatch')
        value = manifest(raw, self.config)
        require(value['sequence'] == sequence and value['version'] == record['version'], 'Snapshot identity mismatch')
        data = directory / 'data'
        no_link(data)
        require({p.name for p in directory.iterdir()} == {'manifest.json', 'data'}, 'Unexpected snapshot files')
        require({p.name for p in data.iterdir()} == set(self.config['files']), 'Snapshot data set changed')
        for entry in value['files']:
            require(read_file(data / entry['name'], FILE_LIMIT) == entry['content'].encode('utf-8'), 'Stored data differs from its manifest')
        return value

    def stage(self, raw):
        value = manifest(raw, self.config, fresh=True)
        state = self.state()
        sequence, key = value['sequence'], str(value['sequence'])
        record = {'sha256': sha256(raw), 'version': value['version']}
        require(sequence >= state['highest'], 'Manifest sequence is older than the highest staged version')
        if key in state['releases']:
            require(record == state['releases'][key], 'The source changed an existing sequence')
            self.verify(sequence, record)
            return {'status': 'unchanged', 'sequence': sequence, **record}
        require(len(state['releases']) < RELEASE_LIMIT, 'Snapshot limit reached; prune old snapshots explicitly')
        if not (self.path / 'state.json').exists():
            self.save(state)
        directory = self.releases / key
        if directory.exists() or directory.is_symlink():
            # Recover only a complete matching snapshot from a prior state-write failure.
            self.verify(sequence, record)
        else:
            require(len(list(self.releases.iterdir())) < RELEASE_LIMIT, 'Snapshot storage limit reached; inspect incomplete staging directories')
            temporary = Path(tempfile.mkdtemp(prefix='.staging-', dir=self.releases))
            try:
                (temporary / 'data').mkdir()
                write_new(temporary / 'manifest.json', raw)
                for entry in value['files']:
                    write_new(temporary / 'data' / entry['name'], entry['content'].encode('utf-8'))
                sync_directory(temporary / 'data'); sync_directory(temporary)
                temporary.rename(directory)
                sync_directory(self.releases)
            finally:
                if temporary.exists():
                    shutil.rmtree(temporary)
        state['highest'] = sequence
        state['releases'][key] = record
        self.save(state)
        return {'status': 'staged', 'sequence': sequence, **record, 'review': 'required'}

    def select(self, sequence, reviewed_hash):
        integer(sequence, self.config['minimum_sequence'])
        state = self.state()
        require(str(sequence) in state['releases'], 'Snapshot is not staged')
        require(state['selected'] is None or sequence >= state['selected'], 'Use rollback to select the previous version')
        record = state['releases'][str(sequence)]
        require(digest(reviewed_hash) == record['sha256'], 'Review hash does not match the staged snapshot')
        self.verify(sequence, record)
        if state['selected'] != sequence:
            state['previous'], state['selected'] = state['selected'], sequence
            self.save(state)
        return {'selected': sequence, **record, 'activation': 'Snapshot selection only; no live installation or compiled profile change'}

    def rollback(self):
        state = self.state()
        require(state['previous'] is not None, 'No previous selection is available')
        previous = state['previous']
        integer(previous, self.config['minimum_sequence'])
        self.verify(previous, state['releases'][str(previous)])
        state['selected'], state['previous'] = previous, state['selected']
        self.save(state)
        return {'selected': previous, 'highest': state['highest'], 'activation': 'Snapshot selection only'}

    def status(self):
        state = self.state()
        for sequence, record in state['releases'].items():
            self.verify(int(sequence), record)
        return state

    def export(self, destination):
        state = self.state()
        require(state['selected'] is not None, 'Select a reviewed snapshot before export')
        sequence = state['selected']
        integer(sequence, self.config['minimum_sequence'])
        record = state['releases'][str(sequence)]
        value = self.verify(sequence, record)
        destination = Path(destination).absolute()
        for parent in [*destination.parents, destination]:
            no_link(parent)
        require(not destination.is_relative_to(self.path) and not self.path.is_relative_to(destination), 'Export must be outside the snapshot store')
        destination.mkdir(mode=0o700)
        try:
            for entry in value['files']:
                write_new(destination / entry['name'], entry['content'].encode('utf-8'))
            write_new(destination / 'snapshot.json', encoded({'sequence': sequence, **record, 'source': state['source'], 'review': 'required', 'activation': 'Not installed'}))
            sync_directory(destination)
        except BaseException:
            shutil.rmtree(destination)
            raise
        return {'directory': str(destination), 'sequence': sequence, 'activation': 'Export only; install through the product-specific review process'}

    def prune(self, sequence):
        state = self.state()
        require(sequence not in (state['selected'], state['previous'], state['highest']), 'Cannot prune the selected, rollback or highest snapshot')
        require(str(sequence) in state['releases'], 'Snapshot is not staged')
        self.verify(sequence, state['releases'][str(sequence)])
        # Preserve recoverable orphan data if a deletion fails after this atomic index update.
        del state['releases'][str(sequence)]
        self.save(state)
        shutil.rmtree(self.releases / str(sequence))
        sync_directory(self.releases)
        return {'pruned': sequence}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--store', required=True, type=Path, help='Private snapshot directory, outside the game installation')
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('check', help='Fetch and stage; suitable for an operator-configured scheduler')
    commands.add_parser('status', help='Verify and list stored snapshots')
    select = commands.add_parser('select', help='Select a reviewed snapshot for export; does not install it')
    select.add_argument('sequence', type=int)
    select.add_argument('--sha256', required=True, help='Manifest hash from the operator review')
    commands.add_parser('rollback', help='Restore the previous snapshot selection without lowering the sequence floor')
    export = commands.add_parser('export', help='Export the selection into a new directory for separate installation review')
    export.add_argument('directory', type=Path)
    prune = commands.add_parser('prune', help='Remove one unselected old snapshot')
    prune.add_argument('sequence', type=int)
    args = parser.parse_args(argv)
    try:
        config = configuration(args.config)
        store = Store(args.store, config)
        with store.locked():
            if args.command == 'check':
                store.state()  # Refuse a changed trust policy before making a request.
                result = store.stage(fetch(config))
            elif args.command == 'select':
                result = store.select(integer(args.sequence), args.sha256)
            elif args.command == 'export':
                result = store.export(args.directory)
            elif args.command == 'prune':
                result = store.prune(integer(args.sequence))
            else:
                result = getattr(store, args.command)()
        print(json.dumps(result, indent=2))
        return 0
    except (UpdateError, OSError, ValueError, http.client.HTTPException) as error:
        print('Compatibility update failed: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
