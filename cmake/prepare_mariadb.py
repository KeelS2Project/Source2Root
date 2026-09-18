"""Stage the pinned connector with strict peer verification for every TLS host."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("staged", type=Path)
args = parser.parse_args()
source = args.source.resolve(strict=True)
staged = args.staged.resolve()
if source == staged or staged.is_relative_to(source):
    raise RuntimeError("Staged MariaDB sources must be outside the input tree")

name = "plugins/auth/my_auth.c"
content = (source / name).read_text()
marker = "/* Source2Root, 2026-09-18: verify every TLS peer, including local addresses. */"
original_flags = "    verify_flags= MARIADB_TLS_VERIFY_PERIOD;"
strict_flags = marker + "\n    verify_flags= MARIADB_TLS_VERIFY_PERIOD | MARIADB_TLS_VERIFY_HOST | MARIADB_TLS_VERIFY_TRUST;"
original_failure = """      if (mysql->net.tls_verify_status > MARIADB_TLS_VERIFY_AUTO ||
          (mysql->options.ssl_ca || mysql->options.ssl_capath))
        goto error;
      if (!password_and_hashing(mysql, mpvio->plugin))
        goto error;"""
strict_failure = """      /* Source2Root: password authentication cannot replace peer verification. */
      goto error;"""
if marker not in content:
    if content.count(original_flags) != 1 or content.count(original_failure) != 1:
        raise RuntimeError("Pinned MariaDB TLS policy changed; review before building")
    content = content.replace(original_flags, strict_flags).replace(original_failure, strict_failure)
elif strict_flags not in content or strict_failure not in content:
    raise RuntimeError("Unrecognized strict MariaDB TLS policy in supplied sources")

state_name = ".source2root-tls-state.json"
state = {"patched_auth_sha256": hashlib.sha256(content.encode()).hexdigest(), "source_files": {
    p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted(source.rglob("*")) if p.is_file() and p.name != state_name
    and ".git" not in p.relative_to(source).parts}}
previous = json.loads((staged / state_name).read_text()) if (staged / state_name).is_file() else None
if previous != state:
    if staged.exists():
        if previous is None:
            raise RuntimeError("Refusing to replace an unrecognized staged MariaDB source tree")
        shutil.rmtree(staged)
    shutil.copytree(source, staged, ignore=shutil.ignore_patterns(".git", "__pycache__", "*.pyc", state_name))
    (staged / name).write_bytes(content.encode())
    (staged / state_name).write_text(json.dumps(state, indent=2) + "\n")
