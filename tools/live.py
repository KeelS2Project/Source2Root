#!/usr/bin/env python3
import argparse
import datetime
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading

WINDOWS = os.name == "nt"
PLATFORM = "win64" if WINDOWS else "linuxsteamrt64"


def digest(path):
    if not path.is_file():
        return None
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def stopped():
    if WINDOWS:
        result = subprocess.check_output(["powershell", "-NoProfile", "-Command",
            "Get-CimInstance Win32_Process -Filter \"Name = 'cs2.exe'\" | Select-Object -ExpandProperty ProcessId"], text=True)
        active = result.split()
    else:
        active = []
        for entry in Path("/proc").iterdir():
            if entry.name.isdigit():
                try:
                    if (entry / "comm").read_text().strip() in ("cs2", "cs2.exe"):
                        active.append(entry.name)
                except (FileNotFoundError, PermissionError, ProcessLookupError):
                    pass
    if active:
        raise RuntimeError("CS2 is active; no installation/restore/launch performed. PIDs: " + ", ".join(active))


def safe_file(root, relative):
    relative = Path(relative)
    if relative.is_absolute() or ".." in relative.parts:
        raise RuntimeError("Unsafe manifest path")
    path = root / relative
    if path.resolve().is_relative_to(root.resolve()) is False:
        raise RuntimeError(f"Path escapes server root: {relative}")
    for parent in (path, *path.parents):
        if parent == root:
            break
        if parent.is_symlink():
            raise RuntimeError(f"Refusing symbolic link in managed path: {relative}")
    return path


def audit(root):
    stopped()
    game = root / "game/csgo"
    if not (game / "gameinfo.gi").is_file():
        raise RuntimeError("Expected a CS2 installation with game/csgo/gameinfo.gi")
    plugin_dir = game / "addons/keels2/plugins" / PLATFORM
    print("Stopped server:", root)
    print("KeelS2 native files:", ", ".join(sorted(p.name for p in plugin_dir.glob("*.*") if p.is_file())) or "none")
    return game


def save(path, data):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def install(args):
    root = args.server.resolve()
    game = audit(root)
    package = args.package.resolve()
    inventory = json.loads((package / "files.sha256.json").read_text())
    provenance = json.loads((package / "provenance.json").read_text())
    if provenance["os"] != ("windows" if WINDOWS else "linux"):
        raise RuntimeError("Package operating system does not match this host")
    for name, expected in inventory.items():
        if digest(safe_file(package, name)) != expected:
            raise RuntimeError(f"Package checksum mismatch: {name}")
    legacy_admins = (
        "addons/source2root/configs/permissions.json",
        "addons/source2root/configs/admins.json",
        "addons/keels2/configs/source2root/admins.json",
        "addons/keels2/configs/sourceroot/admins.json",
    )
    if any(safe_file(game, name).exists() for name in legacy_admins) and not all(
            safe_file(game, "addons/source2root/configs/" + name).is_file()
            for name in ("admins.cfg", "admin_groups.cfg")):
        raise RuntimeError("Legacy administrator data is present. Stage the reviewed admins.cfg and admin_groups.cfg migration before updating Source2Root.")
    legacy_settings = legacy_admins[1:]
    if any(safe_file(game, name).exists() for name in legacy_settings) and not all(
            safe_file(game, name).is_file() for name in (
                "addons/source2root/configs/allowed_maps.txt", "cfg/source2root/source2root.cfg")):
        raise RuntimeError("Legacy configuration is present. Stage the reviewed allowed_maps.txt and source2root.cfg migration before updating Source2Root.")
    legacy_bans = ("addons/keels2/data/source2root/bans.json", "addons/keels2/data/sourceroot/bans.json")
    if any(safe_file(game, name).exists() for name in legacy_bans) and not safe_file(
            game, "addons/source2root/data/bans.json").is_file():
        raise RuntimeError("Legacy ban data is present. Stage the reviewed data/bans.json migration before updating Source2Root.")
    backup = args.evidence.resolve()
    if backup.exists():
        raise RuntimeError("Choose a new evidence directory; existing backups are never overwritten")
    if backup.is_relative_to(root):
        raise RuntimeError("Keep evidence outside the server installation")
    backup.mkdir(parents=True)
    state = {"schema": 1, "server": str(root), "provenance": provenance, "status": "installing", "files": [], "directories": []}
    journal = backup / "installation.json"
    save(journal, state)

    def change(relative, content, mode=None):
        target = safe_file(root, relative)
        if target.exists() and not target.is_file():
            raise RuntimeError(f"Not a regular file: {relative}")
        old = digest(target)
        if old is not None:
            copy = backup / "originals" / relative
            copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, copy)
        record = {"path": relative, "original": old, "installed": hashlib.sha256(content).hexdigest() if content is not None else None}
        state["files"].append(record)
        parents = []
        parent = target.parent
        while not parent.exists():
            parents.append(parent)
            parent = parent.parent
        state["directories"].extend(str(p.relative_to(root)) for p in reversed(parents))
        save(journal, state)
        target.parent.mkdir(parents=True, exist_ok=True)
        if content is None:
            target.unlink(missing_ok=True)
        else:
            target.write_bytes(content)
            if mode is not None:
                target.chmod(mode)

    old_runner = "game/csgo/addons/keels2/plugins/" + PLATFORM + ("/sourceroot.dll" if WINDOWS else "/sourceroot.so")
    if safe_file(root, old_runner).exists():
        change(old_runner, None)
        print("Backed up and disabled only:", old_runner)
    for name in sorted(inventory):
        if name.startswith("addons/") or name.startswith("cfg/source2root/"):
            protected = name.startswith(("cfg/", "addons/source2root/configs/", "addons/source2root/data/"))
            if protected and safe_file(root, "game/csgo/" + name).exists():
                print("Preserved existing configuration:", name)
                continue
            source = safe_file(package, name)
            change("game/csgo/" + name, source.read_bytes(), source.stat().st_mode & 0o777)
    gameinfo = game / "gameinfo.gi"
    text = gameinfo.read_text()
    if not re.search(r"(?im)^\s*Game\s+csgo/addons/keels2\s*$", text):
        updated, count = re.subn(r"(SearchPaths\s*\{)", r"\1\n\t\t\tGame\tcsgo/addons/keels2", text, count=1)
        if count != 1:
            raise RuntimeError("Could not locate SearchPaths; restore journal and inspect gameinfo.gi")
        change("game/csgo/gameinfo.gi", updated.encode())
    state["status"] = "installed"
    save(journal, state)
    print("Installed Source2Root. Backup record:", journal)
    print("Next: launch using this tool. Existing admins and bans were not read or modified.")


def restore(args):
    stopped()
    backup = args.evidence.resolve()
    journal = backup / "installation.json"
    state = json.loads(journal.read_text())
    root = Path(state["server"])
    if state["status"] == "restored":
        print("Already restored:", journal)
        return
    for record in state["files"]:
        target = safe_file(root, record["path"])
        current = digest(target)
        if current not in (record["original"], record["installed"]):
            raise RuntimeError(f"File changed after installation; retained for review: {record['path']}")
        if record["original"] is not None and digest(backup / "originals" / record["path"]) != record["original"]:
            raise RuntimeError("Original backup checksum mismatch")
    for record in reversed(state["files"]):
        target = safe_file(root, record["path"])
        if record["original"] is None:
            target.unlink(missing_ok=True)
        else:
            shutil.copy2(backup / "originals" / record["path"], target)
        if digest(target) != record["original"]:
            raise RuntimeError("Restore verification failed: " + record["path"])
    for name in reversed(state["directories"]):
        try:
            safe_file(root, name).rmdir()
        except OSError:
            pass
    state["status"] = "restored"
    save(journal, state)
    print("Original files restored and checksums verified. Generated logs/data retained:", journal)


def launch_command(root, port):
    exe = root / "game/bin/win64/cs2.exe" if WINDOWS else root / "game/cs2.sh"
    return [str(exe), "-dedicated", "-console", "-usercon", "-port", str(port),
            "+game_type", "0", "+game_mode", "1", "+map", "de_dust2", "+log", "on"]


def smoke_missing(text):
    missing = []
    for plugin in ("admin", "communications", "moderation", "player_actions", "server"):
        if not re.search(r"(?m)^[ \t]*(?:\[Source2Root\][ \t]*)?" + plugin + r"[ \t]+running(?:[ \t]|$)", text):
            missing.append(plugin + " running in the script list")
    for value in ("Source2Root Core Settings:", "sr_reloadadmins - Reload administrators and groups",
                  "SOURCE2ROOT_SMOKE_COMPLETE", "[KeelS2] host stopped"):
        if value not in text:
            missing.append(value)
    return missing


def launch(args):
    root = args.server.resolve()
    audit(root)
    evidence = args.evidence.resolve()
    if evidence.is_relative_to(root):
        raise RuntimeError("Keep evidence outside the server installation")
    evidence.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    log_path = evidence / ("server-" + timestamp + ".log")
    config_snapshot = evidence / ("game-config-before-" + timestamp)
    configs = root / "game/csgo/cfg"
    before = {}
    if configs.is_dir():
        for path in configs.rglob("*"):
            if path.is_file() and not path.is_symlink():
                relative = path.relative_to(configs)
                target = config_snapshot / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
                before[relative.as_posix()] = digest(path)
    save(evidence / ("game-config-before-" + timestamp + ".json"), before)
    command = launch_command(root, args.port)
    env = os.environ.copy()
    print("ACTION: This terminal is the SERVER console. Wait for map startup, then type:", flush=True)
    print("  keel version\n  sr version\n  sr plugins list\n  sr_help\n  sr config", flush=True)
    print("Expected: the five bundled administration plugins are running; sr_help lists their commands. Type quit to stop this server.", flush=True)
    print("Full server output is archived at", log_path, flush=True)
    ready = threading.Event()
    commands_completed = threading.Event()
    with log_path.open("w", buffering=1) as log:
        master = slave = None
        if not WINDOWS:
            import pty
            master, slave = pty.openpty()
        process = subprocess.Popen(command, cwd=root / "game", env=env, stdin=subprocess.PIPE,
                                   stdout=slave if slave is not None else subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1)
        if slave is not None:
            os.close(slave)
            stream = os.fdopen(master, "r", errors="replace")
        else:
            stream = process.stdout

        def output():
            while True:
                try:
                    line = stream.readline()
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not line:
                    break
                log.write(line)
                if not args.smoke:
                    sys.stdout.write(line)
                    sys.stdout.flush()
                if "Host activate: Loading (" in line:
                    ready.set()
                if "SOURCE2ROOT_SMOKE_COMPLETE" in line:
                    commands_completed.set()
            stream.close()

        reader = threading.Thread(target=output, daemon=True)
        reader.start()

        def send(text):
            if process.poll() is None:
                log.write("[operator] " + text + "\n")
                process.stdin.write(text + "\n")
                process.stdin.flush()

        if args.smoke:
            print("SMOKE: waiting at most 90 seconds for startup; no client input is awaited.", flush=True)
            if not ready.wait(90):
                print("SMOKE: startup signal absent; sending quit to the process this tool started.", flush=True)
            else:
                for text in ("keel version", "sr version", "sr plugins list", "sr_help", "sr config",
                             "echo SOURCE2ROOT_SMOKE_COMPLETE"):
                    send(text)
                if not commands_completed.wait(15):
                    print("SMOKE: command completion was not observed within 15 seconds.", flush=True)
            send("quit")
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                print("SMOKE: quit did not complete; terminating only the process started by this tool.", flush=True)
                process.terminate()
                process.wait(timeout=15)
        else:
            def input_loop():
                for line in sys.stdin:
                    send(line.rstrip("\n"))
                if process.poll() is None:
                    print("Input closed; sending quit to this test server.", flush=True)
                    send("quit")
            threading.Thread(target=input_loop, daemon=True).start()
            try:
                process.wait()
            except KeyboardInterrupt:
                send("quit")
                process.wait(timeout=30)
        reader.join(timeout=10)
    sr_log = root / "game/csgo/addons/source2root/logs/source2root.log"
    if sr_log.exists():
        shutil.copy2(sr_log, evidence / ("source2root-" + timestamp + ".log"))
    print("Server exited", process.returncode, "Evidence:", log_path, flush=True)
    after = {p.relative_to(configs).as_posix(): digest(p) for p in configs.rglob("*") if p.is_file()}
    changed = {name: {"before": before.get(name), "after": after.get(name)}
               for name in before.keys() | after.keys() if before.get(name) != after.get(name)}
    save(evidence / ("game-config-changes-" + timestamp + ".json"), changed)
    if changed:
        print("Game configuration changed during the run; originals and hash differences are archived for review.", flush=True)
    if args.smoke:
        text = log_path.read_text()
        missing = smoke_missing(text)
        if not commands_completed.is_set():
            missing.append("server command completion response")
        save(evidence / ("smoke-" + timestamp + ".json"), {"exit_code": process.returncode, "missing": missing,
             "scope": "startup, bundled plugin inventory, help, settings and shutdown",
             "steam_identity": "not exercised", "client_rendering": "not exercised", "log": log_path.name})
        if missing or process.returncode != 0:
            raise RuntimeError("Smoke did not pass. Inspect archived log; missing: " + ", ".join(missing))


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="action", required=True)
    for name in ("audit", "install", "launch"):
        command = commands.add_parser(name)
        command.add_argument("--server", type=Path, required=True)
        if name != "audit":
            command.add_argument("--evidence", type=Path, required=True)
        if name == "install":
            command.add_argument("--package", type=Path, required=True)
        if name == "launch":
            command.add_argument("--port", type=int, default=27025)
            command.add_argument("--smoke", action="store_true")
    commands.add_parser("restore").add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    if args.action == "audit":
        audit(args.server.resolve())
    else:
        globals()[args.action](args)


if __name__ == "__main__":
    main()
