import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location("live", Path(__file__).resolve().parents[1] / "tools/live.py")
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)


class LiveLaunchTest(unittest.TestCase):
    def test_normal_authentication_command_on_each_platform(self):
        for windows in (False, True):
            with self.subTest(windows=windows), patch.object(live, "WINDOWS", windows):
                command = live.launch_command(Path("test-server"), 27035)
                self.assertTrue(command[0].endswith("cs2.exe" if windows else "cs2.sh"))
                self.assertNotIn("-insecure", command)
                self.assertNotIn("+sv_lan", command)
                self.assertEqual(command[command.index("-port") + 1], "27035")

    def test_old_example_output_does_not_pass_current_runtime_smoke(self):
        old = "hello running\nSourcePawn and the C++ extension returned 42.\n[KeelS2] host stopped\n"
        self.assertIn("admin running in the script list", live.smoke_missing(old))
        self.assertIn("Source2Root Core Settings:", live.smoke_missing(old))
        self.assertIn("server running in the script list", live.smoke_missing("server\nrunning\n"))
        self.assertIn("admin running in the script list", live.smoke_missing("admin running_disabled\n"))

    def test_simulated_server_receives_current_commands_and_preserves_configuration(self):
        self.run_server(False)

    def test_missing_bundled_plugin_fails_smoke(self):
        self.run_server(True)

    def run_server(self, missing_plugin):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            server = root / "server"
            game = server / "game/csgo"
            game.mkdir(parents=True)
            (game / "gameinfo.gi").write_text('"GameInfo" {}')
            config = game / "cfg/server.cfg"
            config.parent.mkdir()
            config.write_bytes(b"operator configuration\n")
            evidence = root / "evidence"
            fixture = root / "server.py"
            fixture.write_text('''import sys
print("Host activate: Loading (de_dust2)", flush=True)
for line in sys.stdin:
    command = line.strip()
    if command == "sr plugins list":
        print("[Source2Root] Source2Root Script Plugins:")
        for plugin in ("admin", "communications", "moderation", "player_actions", "server"):
            if len(sys.argv) > 1 and plugin == "moderation":
                print("  moderation error main.smx")
            else:
                print("  " + plugin + "    running   main.smx")
    elif command == "sr_help":
        print("[Source2Root] sr_reloadadmins - Reload administrators and groups")
    elif command == "sr config":
        print("[Source2Root] Source2Root Core Settings:")
    elif command == "echo SOURCE2ROOT_SMOKE_COMPLETE":
        print("SOURCE2ROOT_SMOKE_COMPLETE")
    elif command == "quit":
        print("[KeelS2] host stopped", flush=True)
        break
    sys.stdout.flush()
''')
            original_popen = subprocess.Popen
            launched = []

            def start(command, **kwargs):
                launched.append(command)
                return original_popen([sys.executable, str(fixture), *(["missing"] if missing_plugin else [])], **kwargs)

            args = SimpleNamespace(server=server, evidence=evidence, port=27035, smoke=True)
            with patch.object(live, "stopped"), patch.object(live.subprocess, "Popen", side_effect=start), \
                    contextlib.redirect_stdout(io.StringIO()):
                if missing_plugin:
                    with self.assertRaisesRegex(RuntimeError, "moderation running"):
                        live.launch(args)
                else:
                    live.launch(args)
            self.assertEqual(len(launched), 1)
            self.assertNotIn("-insecure", launched[0])
            self.assertEqual(config.read_bytes(), b"operator configuration\n")
            report = json.loads(next(evidence.glob("smoke-*.json")).read_text())
            self.assertEqual(report["exit_code"], 0)
            self.assertEqual(report["steam_identity"], "not exercised")
            self.assertEqual(report["client_rendering"], "not exercised")
            self.assertEqual(bool(report["missing"]), missing_plugin)
            log = next(evidence.glob("server-*.log")).read_text()
            self.assertIn("[operator] sr_help", log)
            self.assertIn("[operator] sr config", log)
            self.assertIn("[operator] quit", log)
            self.assertNotIn("sr_hello", log)
            self.assertNotIn("mp_restartgame", log)
            changes = json.loads(next(evidence.glob("game-config-changes-*.json")).read_text())
            self.assertEqual(changes, {})


if __name__ == "__main__":
    unittest.main()
