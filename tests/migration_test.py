import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    spec = importlib.util.spec_from_file_location("migration", sys.argv[1])
    migration = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(migration)
    validator = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="source2root-migration-") as directory:
        root = Path(directory)
        source, bans = root / "admins.json", root / "bans.json"
        original = {
            "schema": 1, "announce_actions": False, "allowed_maps": ["de_dust2"],
            "groups": {"Root": {"immunity": 1000, "permissions": ["*"]},
                       "Senior Moderator": {"immunity": 10, "permissions": ["kick", "map"]}},
            "admins": [{"steamid": "STEAM_0:1:61", "group": "Root"},
                       {"steamid": "[U:1:124]", "group": "Senior Moderator", "immunity": 25}]}
        source.write_text(json.dumps(original, indent=2) + "\n")
        ban_bytes = b'{"schema":1,"bans":[{"steamid":"76561197960265860","expires":"2025-01-01T00:00:00Z","reason":"retained old ban"}]}\n'
        bans.write_bytes(ban_bytes)
        before = source.read_bytes()
        output = root / "native-review"
        report = migration.migrate(source, output, bans)
        assert source.read_bytes() == before and bans.read_bytes() == ban_bytes
        assert (output / "originals/admins.json").read_bytes() == before
        assert (output / "data/bans.json").read_bytes() == ban_bytes == (output / "originals/bans.json").read_bytes()
        assert report["admin_count"] == 2 and report["group_count"] == 2 and report["ban_count"] == 1 and not report["installed"]
        assert report["unconverted_settings"] == {}
        assert report["converted_settings"] == {"sr_show_activity": 0, "allowed_maps": ["de_dust2"]}
        assert (output / "configs/allowed_maps.txt").read_text() == "de_dust2\n"
        assert (output / "cfg/source2root/source2root.cfg").read_text().startswith("sr_show_activity 0\n")
        assert report["legacy_permission_mapping"]["map"] == "admin.changemap"
        assert subprocess.check_output([validator, "--validate", output / "configs", "76561197960265851"], text=True).splitlines() == ["Root", "1000", "1", "1"]
        assert subprocess.check_output([validator, "--validate", output / "configs", "[U:1:124]"], text=True).splitlines() == ["Senior Moderator", "25", "1", "0"]
        try:
            migration.migrate(source, output, bans)
            raise AssertionError("existing destination was overwritten")
        except FileExistsError:
            pass
        assert (output / "originals/admins.json").read_bytes() == before
        foundation = {"schema": 1, "admins": [{"identity": "[U:1:123]", "permissions": ["admin.kick", "demo.status"]},
                                             {"identity": "[U:1:124]", "permissions": ["demo.status", "admin.kick"]}]}
        source.write_text(json.dumps(foundation))
        converted = root / "foundation-review"
        report = migration.migrate(source, converted)
        assert report["group_count"] == 1 and report["administrators"][0]["immunity"] == 0
        assert subprocess.check_output([validator, "--validate", converted / "configs", "[U:1:123]"], text=True).splitlines()[1:] == ["0", "1", "0"]
        for suffix, settings in (("true", {"announce_actions": True, "allowed_maps": []}), ("default", {})):
            native = {k: v for k, v in original.items() if k not in ("announce_actions", "allowed_maps")}
            native.update(settings)
            source.write_text(json.dumps(native))
            files, report = migration.prepare(source)
            assert report["converted_settings"] == {"sr_show_activity": 13, "allowed_maps": []}
            assert files["configs/allowed_maps.txt"] == b"\n"
        for invalid in ({"announce_actions": 1}, {"allowed_maps": ["de_dust2;quit"]}, {"allowed_maps": ["x"] * 1025}):
            source.write_text(json.dumps(dict(original, **invalid)))
            try:
                migration.prepare(source)
                raise AssertionError("invalid legacy setting accepted")
            except ValueError:
                pass
        full_maps = ["m" * 60 + str(i) for i in range(1024)]
        source.write_text(json.dumps(dict(original, allowed_maps=full_maps)))
        files, report = migration.prepare(source)
        assert files["configs/allowed_maps.txt"].decode().splitlines() == full_maps
        assert report["converted_settings"]["allowed_maps"] == full_maps
        duplicate = dict(original)
        duplicate["admins"] = [original["admins"][0], {"steamid": "[U:1:123]", "group": "Root"}]
        for index, value in enumerate([duplicate, {"schema": True, "admins": []}, {"schema": 1, "admins": [{}]},
                                       {"schema": 1, "admins": [{"identity": "[U:1:0]", "permissions": []}]}]):
            source.write_text(json.dumps(value))
            dest = root / f"invalid-{index}"
            try:
                migration.migrate(source, dest)
                raise AssertionError("invalid source was accepted")
            except ValueError:
                assert not dest.exists()
        source.write_text('{"schema":1,"schema":1,"admins":[]}')
        try:
            migration.prepare(source)
            raise AssertionError("duplicate JSON keys were accepted")
        except ValueError:
            pass
    print("Migration preserves identities, permissions, immunity, raw originals and bans; native parser accepts converted files.")


if __name__ == "__main__":
    main()
