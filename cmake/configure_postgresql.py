"""Configure the pinned private libpq build; never starts a database or tests."""
import argparse
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("source")
parser.add_argument("build")
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--windows", action="store_true")
parser.add_argument("--flex", required=True)
parser.add_argument("--bison", required=True)
args = parser.parse_args()
command = [sys.executable, "-m", "mesonbuild.mesonmain", "setup", args.build, args.source,
           "--backend=ninja", "--buildtype=debugoptimized", "--auto-features=disabled",
           "-Dssl=openssl", "-Db_staticpic=true", "-Ddefault_library=static", "-Dreadline=disabled",
           "-Dicu=disabled", "-Dlibcurl=disabled", "-Dzlib=disabled", "-Dzstd=disabled", "-Dgssapi=disabled",
           "-Dldap=disabled", "-Dtap_tests=disabled"]
command.extend(["-DFLEX=" + args.flex, "-DBISON=" + args.bison])
if (Path(args.build) / "meson-private/coredata.dat").exists():
    command.append("--reconfigure")
if args.sanitize:
    command.append("-Db_sanitize=address,undefined")
if args.windows:
    command.append("-Db_vscrt=md")
raise SystemExit(subprocess.run(command).returncode)
