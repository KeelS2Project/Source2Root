import argparse
from copy import deepcopy
from datetime import datetime, timezone, timedelta
import http.server
import importlib.util
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/compatibility_update.py'
spec = importlib.util.spec_from_file_location('compatibility_update', TOOL)
update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update)
from http_fixture import certificate
OPENSSL = 'openssl'


def release(sequence=1):
    now = datetime.now(timezone.utc).replace(microsecond=0)
    files = [('targets.json', '{"schema":1,"targets":{}}\n'), ('profile.tsv',
        'keels2-compatibility-profile\t1\nstatus\tcandidate-untrusted\nreview\trequired\ngame\tcs2\n'
        'version\t25218825\nplatform\tlinuxsteamrt64\nmodule\tserver\tlibserver.so\t40575640\tb2ce91a0f330222a\n')]
    return {'schema': 1, 'product': 'Source2Root', 'game': 'cs2', 'platform': 'linuxsteamrt64',
        'sequence': sequence, 'version': 'cs2-' + str(sequence),
        'issued_at': (now - timedelta(minutes=1)).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'expires_at': (now + timedelta(days=1)).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'files': [{'name': name, 'content': content, 'sha256': update.sha256(content.encode())} for name, content in files]}


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sr-updater-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = {'schema': 1, 'url': 'https://updates.example.invalid/cs2.json', 'product': 'Source2Root',
            'game': 'cs2', 'platform': 'linuxsteamrt64', 'files': ['targets.json', 'profile.tsv'],
            'minimum_sequence': 1, 'timeout': 5}
        self.store = update.Store(self.root / 'store', self.config)

    def stage(self, sequence=1):
        return self.store.stage(update.encoded(release(sequence)))

    def selected(self, sequence):
        result = self.stage(sequence)
        self.store.select(sequence, result['sha256'])
        return result

    def test_staging_selection_export_and_rollback(self):
        first = self.stage()
        self.assertIsNone(self.store.status()['selected'])
        with self.assertRaises(update.UpdateError):
            self.store.export(self.root / 'unselected')
        self.store.select(1, first['sha256'])
        second = self.selected(2)
        self.assertEqual(self.store.status()['previous'], 1)
        self.store.export(self.root / 'export')
        self.assertEqual((self.root / 'export/targets.json').read_bytes(), b'{"schema":1,"targets":{}}\n')
        self.assertEqual(json.loads((self.root / 'export/snapshot.json').read_text())['sha256'], second['sha256'])
        self.assertIn('status\tcandidate-untrusted', (self.root / 'export/profile.tsv').read_text())
        self.assertEqual(self.store.rollback()['selected'], 1)
        self.assertEqual(self.store.status()['highest'], 2)
        with self.assertRaises(update.UpdateError):
            self.stage(1)
        self.assertEqual(self.store.status()['selected'], 1)
        self.assertFalse((self.root / 'game').exists())

    def test_same_sequence_cannot_change(self):
        raw = update.encoded(release())
        self.store.stage(raw)
        self.assertEqual(self.store.stage(raw)['status'], 'unchanged')
        value = update.decode(raw); value['version'] = 'changed'
        with self.assertRaises(update.UpdateError):
            self.store.stage(update.encoded(value))
        with self.assertRaises(update.UpdateError):
            self.store.select(1, '0' * 64)

    def test_corrupt_files_never_replace_selection(self):
        self.selected(1); self.selected(2)
        (self.store.releases / '1/data/targets.json').write_text('{}')
        before = (self.store.path / 'state.json').read_bytes()
        for action in (self.store.rollback, self.store.status):
            with self.assertRaises(update.UpdateError): action()
        self.assertEqual((self.store.path / 'state.json').read_bytes(), before)
        self.store.export(self.root / 'current')
        (self.store.releases / '2/manifest.json').write_bytes(b'{}')
        with self.assertRaises(update.UpdateError): self.store.export(self.root / 'broken')
        self.assertFalse((self.root / 'broken').exists())

    def test_source_and_ca_identity_are_bound(self):
        self.stage()
        for key, value in [('url', 'https://another.invalid/manifest.json'), ('platform', 'win64'), ('files', ['other.json'])]:
            config = deepcopy(self.config); config[key] = value
            with self.assertRaises(update.UpdateError): update.Store(self.store.path, config).state()
        ca = self.root / 'ca.pem'; ca.write_text('first trust file')
        config = deepcopy(self.config); config['ca_file'] = str(ca)
        store = update.Store(self.root / 'custom-ca', config)
        store.stage(update.encoded(release()))
        ca.write_text('different trust file')
        with self.assertRaises(update.UpdateError): store.state()

    def test_floor_can_rise_without_losing_history(self):
        self.selected(1); self.selected(2)
        self.config['minimum_sequence'] = 2
        self.assertEqual(self.store.status()['previous'], 1)
        with self.assertRaises(update.UpdateError): self.store.rollback()
        self.assertEqual(self.store.status()['selected'], 2)
        with self.assertRaises(update.UpdateError): self.stage(1)

    def test_failure_to_replace_index_retains_selection_and_recovers(self):
        self.selected(1)
        original = (self.store.path / 'state.json').read_bytes()
        raw = update.encoded(release(2))
        with mock.patch.object(update.os, 'replace', side_effect=OSError('simulated replacement failure')):
            with self.assertRaises(OSError): self.store.stage(raw)
        self.assertEqual((self.store.path / 'state.json').read_bytes(), original)
        self.assertEqual(self.store.status()['selected'], 1)
        self.assertEqual(self.store.stage(raw)['status'], 'staged')
        self.assertEqual(self.store.status()['highest'], 2)
        self.assertEqual(self.store.status()['selected'], 1)

    def test_missing_index_fails_closed(self):
        self.selected(4)
        (self.store.path / 'state.json').unlink()
        with self.assertRaises(update.UpdateError): self.stage(1)
        self.assertFalse((self.store.releases / '1').exists())

    def test_write_failure_removes_partial_snapshot(self):
        self.selected(1)
        original = update.write_new
        def fail_data(path, raw):
            if path.name == 'targets.json': raise OSError('disk write failure')
            original(path, raw)
        with mock.patch.object(update, 'write_new', side_effect=fail_data):
            with self.assertRaises(OSError): self.stage(2)
        self.assertEqual([p.name for p in self.store.releases.iterdir()], ['1'])
        self.assertEqual(self.store.status()['selected'], 1)

    def test_process_lock_prevents_another_writer(self):
        config = self.root / 'config.json'; config.write_bytes(update.encoded(self.config))
        with self.store.locked():
            result = subprocess.run([sys.executable, '-B', str(TOOL), '--config', str(config), '--store', str(self.store.path), 'status'], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertIn('locked', result.stderr)
        self.assertFalse((self.store.path / 'update.lock').exists())

    def test_does_not_overwrite_export_or_use_store(self):
        self.selected(1)
        existing = self.root / 'existing'; existing.mkdir(); (existing / 'keep').write_text('important')
        with self.assertRaises(OSError): self.store.export(existing)
        self.assertEqual((existing / 'keep').read_text(), 'important')
        with self.assertRaises(update.UpdateError): self.store.export(self.store.path / 'export')

    def test_limits_pruning_and_protected_snapshots(self):
        self.selected(1); self.selected(2)
        for sequence in range(3, 17): self.stage(sequence)
        with self.assertRaises(update.UpdateError): self.stage(17)
        for sequence in (1, 2, 16):
            with self.assertRaises(update.UpdateError): self.store.prune(sequence)
        self.store.prune(3)
        self.assertFalse((self.store.releases / '3').exists())
        self.stage(17)
        self.assertEqual(self.store.status()['highest'], 17)

    def test_symlink_and_hardlink_data_are_rejected(self):
        self.selected(1)
        path = self.store.releases / '1/data/targets.json'
        outside = self.root / 'outside'; outside.write_bytes(path.read_bytes()); path.unlink()
        os.link(outside, path)
        with self.assertRaises(update.UpdateError): self.store.status()
        path.unlink()
        try: path.symlink_to(outside)
        except OSError:
            path.write_bytes(outside.read_bytes())
            return  # Windows may require an elevated account for symbolic links.
        with self.assertRaises(update.UpdateError): self.store.status()
        self.assertEqual(outside.read_bytes(), b'{"schema":1,"targets":{}}\n')

    def test_manifest_policy_rejects_bad_inputs_without_mutation(self):
        self.selected(1)
        original = (self.store.path / 'state.json').read_bytes()
        base = release(2)
        variants = []
        for key, value in [('sequence', True), ('sequence', 0), ('sequence', 2**63), ('game', 'other'),
                           ('product', 'other'), ('platform', 'win64'), ('schema', True), ('version', '../bad'),
                           ('issued_at', '2099-01-01T00:00:00Z'), ('expires_at', '2000-01-01T00:00:00Z')]:
            item = deepcopy(base); item[key] = value; variants.append(update.encoded(item))
        for name in ('../outside.json', 'C:/bad.json', 'a\\b.json', '/bad.json', 'CON.json', 'nul.json', 'snapshot.json'):
            item = deepcopy(base); item['files'][0]['name'] = name; variants.append(update.encoded(item))
        for content in ('{"number":NaN}', '{"number":1e999}', '{"duplicate":1,"duplicate":2}', '[' * 40 + ']' * 40, '{}\0'):
            item = deepcopy(base); entry = item['files'][0]; entry['content'] = content; entry['sha256'] = update.sha256(content.encode()); variants.append(update.encoded(item))
        item = deepcopy(base); item['files'][0]['sha256'] = '0' * 64; variants.append(update.encoded(item))
        item = deepcopy(base); item['files'].pop(); variants.append(update.encoded(item))
        item = deepcopy(base); item['files'].append(item['files'][0]); variants.append(update.encoded(item))
        item = deepcopy(base); entry = item['files'][1]; entry['content'] = entry['content'].replace('candidate-untrusted', 'accepted').replace('required', 'complete'); entry['sha256'] = update.sha256(entry['content'].encode()); variants.append(update.encoded(item))
        item = deepcopy(base); entry = item['files'][0]; entry['content'] = '{"padding":"' + 'x' * update.FILE_LIMIT + '"}'; entry['sha256'] = update.sha256(entry['content'].encode()); variants.append(update.encoded(item))
        variants.extend([b'{"schema":1,"schema":1}', b'x' * (update.MANIFEST_LIMIT + 1), b'\xff'])
        for raw in variants:
            with self.subTest(raw=raw[:100]):
                with self.assertRaises(update.UpdateError): self.store.stage(raw)
                self.assertEqual((self.store.path / 'state.json').read_bytes(), original)
        self.assertEqual(self.store.status()['selected'], 1)

    def test_total_data_budget(self):
        config = deepcopy(self.config); config['files'] = [f'data{i}.json' for i in range(5)]
        store = update.Store(self.root / 'budget', config)
        value = release(); content = '{"padding":"' + 'x' * 110000 + '"}'
        value['files'] = [{'name': name, 'content': content, 'sha256': update.sha256(content.encode())} for name in config['files']]
        with self.assertRaises(update.UpdateError): store.stage(update.encoded(value))
        self.assertEqual(store.state()['highest'], 0)
        self.assertFalse(any(store.releases.iterdir()))

    def test_config_requires_exact_https_source(self):
        for url in ('http://example.invalid/x', 'https://user:password@example.invalid/x',
                    'https://example.invalid/x?token=secret', 'https://example.invalid/#x', 'file:///tmp/file',
                    'https://example.invalid:bad/x', 'https://example.invalid/x\nHeader:x'):
            config = deepcopy(self.config); config['url'] = url
            path = self.root / 'source.json'; path.write_bytes(update.encoded(config))
            with self.subTest(url=url), self.assertRaises((update.UpdateError, ValueError)):
                update.configuration(path)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        if self.path == '/delay': time.sleep(1.5)
        status = 200
        raw = self.server.body
        if self.path == '/redirect': status = 302
        elif self.path == '/notfound': status = 404
        self.send_response(status)
        if self.path == '/redirect': self.send_header('Location', '/manifest')
        if self.path == '/gzip': self.send_header('Content-Encoding', 'gzip')
        size = len(raw)
        if self.path == '/huge': size = update.MANIFEST_LIMIT + 1
        if self.path == '/truncated': size += 10
        if self.path != '/stream': self.send_header('Content-Length', str(size))
        self.end_headers()
        try: self.wfile.write(raw)
        except (OSError, ssl.SSLError): pass


class NetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='sr-updater-tls-')
        cls.root = Path(cls.temp.name)
        cls.ca, _ = certificate(cls.root, 'ca', openssl=OPENSSL)
        cls.bad_ca, _ = certificate(cls.root, 'bad-ca', openssl=OPENSSL)
        cls.servers, cls.threads = [], []
        for name, san in [('valid', 'DNS:localhost,IP:127.0.0.1'), ('wrong-name', 'DNS:wrong.invalid')]:
            cert, key = certificate(cls.root, name, cls.ca, san, OPENSSL)
            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            server.daemon_threads = True
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(cert, key)
            server.socket = context.wrap_socket(server.socket, server_side=True)
            server.body = update.encoded(release())
            thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.05}, daemon=True)
            thread.start(); cls.servers.append(server); cls.threads.append(thread)

    @classmethod
    def tearDownClass(cls):
        for server in cls.servers: server.shutdown(); server.server_close()
        for thread in cls.threads: thread.join()
        cls.temp.cleanup()

    def config(self, path='/manifest', server=0):
        return {'schema': 1, 'url': f'https://127.0.0.1:{self.servers[server].server_port}' + path,
            'product': 'Source2Root', 'game': 'cs2', 'platform': 'linuxsteamrt64',
            'files': ['targets.json', 'profile.tsv'], 'minimum_sequence': 1, 'timeout': 5, 'ca_file': str(self.ca)}

    def test_cli_authenticated_check_select_and_export(self):
        with tempfile.TemporaryDirectory(dir=self.root) as directory:
            root = Path(directory); config = root / 'source.json'; config.write_bytes(update.encoded(self.config()))
            command = [sys.executable, '-B', str(TOOL), '--config', str(config), '--store', str(root / 'store')]
            env = dict(os.environ, https_proxy='http://127.0.0.1:1', HTTPS_PROXY='http://127.0.0.1:1', ALL_PROXY='http://127.0.0.1:1', NO_PROXY='', no_proxy='')
            def run(*args):
                result = subprocess.run([*command, *args], env=env, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                return json.loads(result.stdout)
            staged = run('check')
            self.assertEqual(staged['status'], 'staged')
            self.assertIsNone(run('status')['selected'])
            run('select', '1', '--sha256', staged['sha256'])
            run('export', str(root / 'export'))
            self.assertTrue((root / 'export/snapshot.json').is_file())

    def test_tls_rejects_wrong_ca_hostname_and_implicit_private_ca(self):
        configs = [self.config(server=1), {**self.config(), 'ca_file': str(self.bad_ca)}, self.config()]
        del configs[-1]['ca_file']
        for config in configs:
            with self.subTest(config=config), self.assertRaises(OSError): update.fetch(config)

    def test_redirect_status_compression_size_and_short_body_reject(self):
        for path in ('/redirect', '/notfound', '/gzip', '/huge', '/truncated'):
            with self.subTest(path=path), self.assertRaises((update.UpdateError, OSError)):
                update.fetch(self.config(path))
        self.servers[0].body = b'x' * (update.MANIFEST_LIMIT + 1)
        try:
            with self.assertRaises(update.UpdateError): update.fetch(self.config('/stream'))
        finally:
            self.servers[0].body = update.encoded(release())

    def test_request_timeout(self):
        config = self.config('/delay'); config['timeout'] = 1
        with self.assertRaises(OSError): update.fetch(config)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--openssl', default='openssl')
    args, remaining = parser.parse_known_args()
    OPENSSL = args.openssl
    unittest.main(argv=[sys.argv[0], *remaining])
