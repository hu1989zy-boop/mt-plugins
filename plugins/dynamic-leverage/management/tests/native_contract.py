"""Exercise the exact management-file -> native-DLL rule-engine contract."""
from pathlib import Path
import argparse
import copy
import json
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from schema import default_config, preview
from server import Store

CHECKER = None


def rule(rid, scope, selector, first=1000):
    return {"id": rid, "name": "规则 " + rid, "enabled": True, "scope": scope, "selector": selector,
            "tiers": [{"above_lots": "0", "leverage": first},
                      {"above_lots": "2", "leverage": 400 if first == 800 else 500},
                      {"above_lots": "5", "leverage": 100 if first == 800 else 200}]}


class NativeContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="dynamic-leverage-contract-")
        self.addCleanup(self.temp.cleanup)
        self.store = Store(self.temp.name)
        self.config = default_config("mt5")
        self.config.update(enabled=True, rules=[rule("groups", "group", "!ABC,*"),
                                               rule("account", "account", "10001,10002", 800)])

    def native(self, config, login=None, group="", lots="0"):
        platform = config["platform"]
        saved = self.store.save(platform, config, self.store.load(platform)["revision"])
        args = [CHECKER, "--platform", platform, "--file", saved["path"]]
        if login is not None:
            args += ["--login", login, "--group", group, "--lots", lots]
        result = subprocess.run(args, capture_output=True, encoding="utf-8", timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = json.loads(result.stdout)
        self.assertTrue(output["ok"])
        self.assertEqual(output["config"], saved["config"])
        if login is not None:
            self.assertEqual(output["preview"], preview(saved["config"], platform, login, group, lots))
        return output

    def test_first_match_reorder_disable_and_restore(self):
        for platform in ("mt4", "mt5"):
            config = copy.deepcopy(self.config)
            config["platform"] = platform
            self.assertEqual(self.native(config, "10001", "real-main", "3")["preview"]["leverage"], 500)
            config["rules"].reverse()
            self.assertEqual(self.native(config, "10001", "real-main", "3")["preview"]["leverage"], 400)
            config["rules"][0]["enabled"] = False
            self.assertEqual(self.native(config, "10001", "real-main", "3")["preview"]["rule_id"], "groups")
            config["enabled"] = False
            self.assertFalse(self.native(config, "10001", "real-main", "3")["preview"]["matched"])
            config["enabled"] = True
            self.assertTrue(self.native(config, "10001", "real-main", "0")["preview"]["matched"])

    def test_exact_thresholds_and_large_account(self):
        self.config["rules"] = [rule("account", "account", "18446744073709551615,9007199254740993")]
        for lots, expected in (("0",1000),("2",1000),("2.00000001",500),("5",500),("5.00000001",200),
                               ("184467440737.09551615",200)):
            self.assertEqual(self.native(self.config, "18446744073709551615", "", lots)["preview"]["leverage"], expected)
        self.assertFalse(self.native(self.config, "9007199254740992", "", "0")["preview"]["matched"])

    def test_groups_unicode_exclusions_and_migration(self):
        self.config["rules"] = [rule("groups", "group", "!ABC,*")]
        for mask, group, matched in (("!ABC,*","ABC",False),("*,!ABC","ABC",False),("!ABC","ABCD",True),
                                      ("real*","Real",False),("real\\*","real\\VIP",True),
                                      ("组别*","组别🌟",True),("*","",False)):
            self.config["rules"][0]["selector"] = mask
            self.assertEqual(self.native(self.config,"10001",group,"2.5")["preview"]["matched"],matched)
        self.config["rules"] = [rule("vip","group","vip*",800),rule("standard","group","real*")]
        self.assertEqual(self.native(self.config,"10001","vip-main","3")["preview"]["leverage"],400)
        self.assertEqual(self.native(self.config,"10001","real-main","3")["preview"]["leverage"],500)

    def test_file_failure_does_not_use_previous_good_document(self):
        self.native(self.config, "10001", "real", "0")
        path = self.store.path("mt5")
        original = path.read_bytes()
        for raw in (b'{"schema_version":1,', original.replace(b'"mt5"', b'"mt4"'),
                    b'\xef\xbb\xbf' + original, original.replace(b'"schema_version": 1', b'"schema_version": 1, "schema_version": 1')):
            path.write_bytes(raw)
            result = subprocess.run([CHECKER,"--platform","mt5","--file",str(path)], capture_output=True, encoding="utf-8", timeout=10)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(json.loads(result.stdout)["ok"])
        path.unlink()
        result = subprocess.run([CHECKER,"--platform","mt5","--file",str(path)], capture_output=True, encoding="utf-8", timeout=10)
        self.assertNotEqual(result.returncode,0)
        self.native(self.config, "10001", "real", "0")

    def test_shipped_example_files_use_safe_default(self):
        examples = Path(__file__).resolve().parents[2] / "config"
        for platform in ("mt4", "mt5"):
            config = json.loads((examples / f"{platform}.rules.example.json").read_text(encoding="utf-8"))
            self.assertFalse(config["enabled"])
            self.assertFalse(self.native(config,"10001","real-main","6")["preview"]["matched"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--checker", type=Path, required=True)
    args = parser.parse_args()
    CHECKER = str(args.checker.resolve(strict=True))
    unittest.main(argv=[sys.argv[0]], verbosity=2)
