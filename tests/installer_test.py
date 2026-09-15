import importlib.util
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location("live", Path(__file__).resolve().parents[1] / "tools/live.py")
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)


class InstallerTest(unittest.TestCase):
    def test_restore_preserves_private_data_and_refuses_concurrent_change(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            server = root / "server"
            game = server / "game/csgo"
            platform = live.PLATFORM
            suffix = ".dll" if live.WINDOWS else ".so"
            old = game / "addons/keels2/plugins" / platform / ("sourceroot" + suffix)
            old.parent.mkdir(parents=True)
            old.write_bytes(b"old runner")
            gameinfo = game / "gameinfo.gi"
            gameinfo.write_text('"GameInfo" { FileSystem { SearchPaths { Game csgo } } }')
            private = game / "addons/keels2/configs/sourceroot/admins.json"
            private.parent.mkdir(parents=True)
            private.write_bytes(b"private sentinel")
            package = root / "package"
            relative = "addons/keels2/plugins/" + platform + "/source2root" + suffix
            module = package / relative
            module.parent.mkdir(parents=True)
            module.write_bytes(b"candidate")
            (package / "provenance.json").write_text(json.dumps({"os": "windows" if live.WINDOWS else "linux"}))
            (package / "files.sha256.json").write_text(json.dumps({relative: live.digest(module)}))
            originals = {p.relative_to(server): p.read_bytes() for p in server.rglob("*") if p.is_file()}
            args = SimpleNamespace(server=server, package=package, evidence=root / "evidence")
            with self.assertRaisesRegex(RuntimeError, "Legacy administrator data is present"):
                live.install(args)
            self.assertFalse(args.evidence.exists())
            self.assertEqual(originals, {p.relative_to(server): p.read_bytes() for p in server.rglob("*") if p.is_file()})
            staged = {
                "addons/source2root/configs/admins.cfg": b'"Admins" {}\n',
                "addons/source2root/configs/admin_groups.cfg": b'"Groups" {}\n',
                "addons/source2root/configs/allowed_maps.txt": b"de_dust2\n",
                "cfg/source2root/source2root.cfg": b"sr_show_activity 5\n",
            }
            for staged_relative, content in staged.items():
                destination = game / staged_relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(content)
            originals = {p.relative_to(server): p.read_bytes() for p in server.rglob("*") if p.is_file()}
            live.install(args)
            self.assertFalse(old.exists())
            self.assertEqual(private.read_bytes(), b"private sentinel")
            installed = game / relative
            installed.write_bytes(b"someone else's edit")
            with self.assertRaisesRegex(RuntimeError, "changed after installation"):
                live.restore(args)
            self.assertFalse(old.exists())
            self.assertEqual(installed.read_bytes(), b"someone else's edit")
            installed.write_bytes(b"candidate")
            live.restore(args)
            self.assertFalse(installed.exists())
            for relative, content in originals.items():
                self.assertEqual((server / relative).read_bytes(), content)
            live.restore(args)


if __name__ == "__main__":
    unittest.main()
