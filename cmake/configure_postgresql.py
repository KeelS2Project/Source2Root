"""Configure the pinned private libpq build; never starts a database or tests."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("source")
parser.add_argument("build")
parser.add_argument("--staged-source", required=True)
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--windows", action="store_true")
parser.add_argument("--openssl-prefix")
parser.add_argument("--flex", required=True)
parser.add_argument("--bison", required=True)
parser.add_argument("--perl", required=True)
args = parser.parse_args()
original = Path(args.source).resolve(strict=True)
staged = Path(args.staged_source).resolve()

if staged == original or staged.is_relative_to(original):
    raise RuntimeError("Staged PostgreSQL sources must be outside the input tree")

addition = (Path(__file__).parent / "libpq_tls_cleanup.c").read_bytes()
state_name = ".source2root-patch-state.json"
state = {"addition_sha256": hashlib.sha256(addition).hexdigest(), "source_files": {
    p.relative_to(original).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted(original.rglob("*")) if p.is_file() and p.name != state_name
    and ".git" not in p.relative_to(original).parts}}
previous = json.loads((staged / state_name).read_text()) if (staged / state_name).is_file() else None

if previous != state:
    if staged.exists():
        if previous is None:
            raise RuntimeError("Refusing to replace an unrecognized staged PostgreSQL source tree")

        shutil.rmtree(staged)

    shutil.copytree(original, staged, ignore=shutil.ignore_patterns(".git", "__pycache__", "*.pyc", state_name))
    tls = staged / "src/interfaces/libpq/fe-secure-openssl.c"
    content = tls.read_bytes()

    if b"Source2RootPQReleaseTLSMethod" not in content:
        if b"static BIO_METHOD *pgconn_bio_method_ptr;" not in content:
            raise RuntimeError("Pinned libpq TLS layout changed; review unload cleanup before building")

        tls.write_bytes(content + addition)
    elif not content.endswith(addition):
        raise RuntimeError("Unrecognized libpq unload cleanup in supplied sources")

    (staged / state_name).write_text(json.dumps(state, indent=2) + "\n")

command = [sys.executable, "-m", "mesonbuild.mesonmain", "setup", args.build, str(staged),
           "--backend=ninja", "--buildtype=debugoptimized", "--auto-features=disabled",
           "-Dssl=openssl", "-Db_staticpic=true", "-Ddefault_library=static", "-Dreadline=disabled",
           "-Dicu=disabled", "-Dlibcurl=disabled", "-Dzlib=disabled", "-Dzstd=disabled", "-Dgssapi=disabled",
           "-Dldap=disabled", "-Dtap_tests=disabled"]
command.extend(["-DFLEX=" + args.flex, "-DBISON=" + args.bison, "-DPERL=" + args.perl])

if (Path(args.build) / "meson-private/coredata.dat").exists():
    marker = Path(args.build) / "source2root-source-root.txt"
    command.append("--reconfigure" if marker.is_file() and marker.read_text() == str(staged) else "--wipe")

if args.sanitize:
    command.append("-Db_sanitize=address,undefined")

if args.windows:
    command.extend(["-Db_vscrt=md", "-Dc_link_args=['ws2_32.lib','crypt32.lib','bcrypt.lib']"])

if args.openssl_prefix:
    command.append("-Dcmake_prefix_path=" + str(Path(args.openssl_prefix).resolve(strict=True)))

result = subprocess.run(command).returncode

if result == 0:
    (Path(args.build) / "source2root-source-root.txt").write_text(str(staged))

raise SystemExit(result)
