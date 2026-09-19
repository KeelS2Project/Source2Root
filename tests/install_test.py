import hashlib
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile


def main():
    spec = importlib.util.spec_from_file_location("source2root_live", sys.argv[1])
    live = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(live)
    live.stopped = lambda: None

    with tempfile.TemporaryDirectory(prefix="source2root-install-") as directory:
        root = Path(directory)
        package = root / "package"
        extension = ".dll" if live.WINDOWS else ".so"
        payload = f"addons/keels2/plugins/{live.PLATFORM}/source2root{extension}"
        cfg = "cfg/source2root/source2root.cfg"
        files = {payload: b"native fixture", cfg: b'sr_show_activity 13\nsr_chat_public_trigger "!"\n'}
        files["addons/source2root/configs/admins.cfg"] = b'"Admins" {}'
        files["addons/source2root/configs/admin_groups.cfg"] = b'"Groups" { "root" { "immunity" "1000" } }'
        files["addons/source2root/configs/allowed_maps.txt"] = b"// unrestricted\n"
        files["addons/source2root/configs/map_menu.txt"] = b"de_dust2\n"
        files["addons/source2root/data/bans.json"] = b'{"schema":1,"bans":[]}'

        for name, contents in files.items():
            target = package / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents)

        (package / "files.sha256.json").write_text(json.dumps({name: hashlib.sha256(data).hexdigest() for name, data in files.items()}))
        (package / "provenance.json").write_text(json.dumps({"os": "windows" if live.WINDOWS else "linux"}))

        for existing in (False, True):
            server = root / ("existing" if existing else "fresh")
            game = server / "game/csgo"
            game.mkdir(parents=True)
            (game / "gameinfo.gi").write_text("GameInfo\n{\n FileSystem\n {\n SearchPaths\n {\n Game csgo/addons/keels2\n }\n }\n}\n")
            saved = {"addons/source2root/configs/admins.cfg": b"existing administrator identity",
                     "addons/source2root/configs/admin_groups.cfg": b"existing administrator group",
                     "addons/source2root/data/bans.json": b"existing ban data"} if existing else {}

            if existing:
                saved["addons/source2root/configs/allowed_maps.txt"] = b"de_dust2\n"
                saved["addons/source2root/configs/map_menu.txt"] = b"de_mirage\n"
                saved[cfg] = b'sr_show_activity 5\nsr_chat_public_trigger "!!"\n'

            for name, data in saved.items():
                target = game / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)

            args = SimpleNamespace(server=server, package=package, evidence=root / ("backup-existing" if existing else "backup-fresh"))
            live.install(args)
            assert (game / payload).read_bytes() == files[payload]
            assert (game / cfg).read_bytes() == (saved[cfg] if existing else files[cfg])

            for name, contents in saved.items():
                assert (game / name).read_bytes() == contents, name

            for name, contents in files.items():
                assert (game / name).read_bytes() == saved.get(name, contents), name

            live.restore(args)
            assert not (game / payload).exists()
            assert (game / cfg).exists() == existing

            for name, contents in saved.items():
                assert (game / name).read_bytes() == contents, name

            for name in files:
                assert (game / name).exists() == (name in saved), name

        for index, legacy in enumerate(("addons/source2root/configs/permissions.json", "addons/source2root/configs/admins.json",
                                       "addons/keels2/configs/source2root/admins.json", "addons/keels2/configs/sourceroot/admins.json")):
            server = root / ("legacy-" + str(index))
            game = server / "game/csgo"
            game.mkdir(parents=True)
            (game / "gameinfo.gi").write_text("untouched game configuration")
            legacy_file = game / legacy
            legacy_file.parent.mkdir(parents=True, exist_ok=True)
            legacy_file.write_text('{"schema":1,"admins":[{"identity":"[U:1:123]","permissions":["demo.hello"]}]}')
            old_runner = game / "addons/keels2/plugins" / live.PLATFORM / ("sourceroot" + extension)
            old_runner.parent.mkdir(parents=True, exist_ok=True)
            old_runner.write_bytes(b"legacy module must remain installed")
            before = {p.relative_to(game): p.read_bytes() for p in game.rglob("*") if p.is_file()}
            args = SimpleNamespace(server=server, package=package, evidence=root / ("blocked-backup-" + str(index)))

            try:
                live.install(args)
                raise AssertionError("unmigrated administrator defaults were installed")
            except RuntimeError as error:
                assert "Legacy administrator data" in str(error)

            assert not args.evidence.exists()
            assert {p.relative_to(game): p.read_bytes() for p in game.rglob("*") if p.is_file()} == before

            if index > 0:
                for name in ("admins.cfg", "admin_groups.cfg"):
                    target = game / "addons/source2root/configs" / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(files["addons/source2root/configs/" + name])

                for name in ("addons/source2root/configs/allowed_maps.txt", cfg):
                    before = {p.relative_to(game): p.read_bytes() for p in game.rglob("*") if p.is_file()}

                    try:
                        live.install(args)
                        raise AssertionError("unmigrated legacy settings were overwritten")
                    except RuntimeError as error:
                        assert "Legacy configuration" in str(error)

                    assert not args.evidence.exists()
                    assert {p.relative_to(game): p.read_bytes() for p in game.rglob("*") if p.is_file()} == before
                    target = game / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(files[name])

        for index, old_path in enumerate(("addons/keels2/data/source2root/bans.json", "addons/keels2/data/sourceroot/bans.json")):
            server = root / ("ban-migration-" + str(index))
            game = server / "game/csgo"
            game.mkdir(parents=True)
            (game / "gameinfo.gi").write_text("GameInfo { FileSystem { SearchPaths { Game csgo/addons/keels2 } } }")
            old = game / old_path
            old.parent.mkdir(parents=True, exist_ok=True)
            original = b'{"schema":1,"bans":[{"steamid":"76561197960265851","reason":"preserved"}]}'
            old.write_bytes(original)
            args = SimpleNamespace(server=server, package=package, evidence=root / ("ban-backup-" + str(index)))
            before = {str(f.relative_to(server)): f.read_bytes() for f in server.rglob("*") if f.is_file()}

            try:
                live.install(args)
                raise AssertionError("legacy bans were left behind an empty new store")
            except RuntimeError as error:
                assert "Legacy ban data" in str(error)

            assert not args.evidence.exists()
            assert before == {str(f.relative_to(server)): f.read_bytes() for f in server.rglob("*") if f.is_file()}
            migrated = game / "addons/source2root/data/bans.json"
            migrated.parent.mkdir(parents=True, exist_ok=True)
            migrated.write_bytes(original)
            live.install(args)
            assert old.read_bytes() == original and migrated.read_bytes() == original
            live.restore(args)
            assert old.read_bytes() == original and migrated.read_bytes() == original

    print("temporary installation and restore preserve existing configuration, admins and bans")


if __name__ == "__main__":
    main()
