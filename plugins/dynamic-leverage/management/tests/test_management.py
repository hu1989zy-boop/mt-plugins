import copy
import http.client
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from schema import (MAX_VOLUME, ValidationError, decode_json, default_config,
                    format_lots, group_matches, lots_units, preview, validate_config)
from server import ConflictError, DirectoryLease, ManagementServer, Store, encode
import server as management_server


def document(platform="mt5"):
    result = default_config(platform)
    result["enabled"] = True
    result["rules"] = [rule("groups", "group", "!demo*,real*,vip*")]
    return result


def rule(rid, scope, selector, leverage=1000):
    return {"id": rid, "name": "测试规则 " + rid, "enabled": True,
            "scope": scope, "selector": selector,
            "tiers": [{"above_lots": "0", "leverage": leverage},
                      {"above_lots": "2", "leverage": 500},
                      {"above_lots": "5", "leverage": 200}]}


class SchemaTests(unittest.TestCase):
    def test_masks_and_exclusions(self):
        vectors = [("*", "ABC", True), ("ABC", "ABCD", False),
                   ("ABC*", "ABCD", True), ("*ABC*", "real-ABC-usd", True),
                   ("!ABC", "ABC", False), ("!ABC", "DEF", True),
                   ("!ABC,*", "ABC", False), ("*,!ABC", "ABC", False),
                   ("real*,vip*,!*test*", "real-test", False),
                   ("real*,vip*,!*test*", "vip-main", True),
                   ("real*", "Real", False), ("real\\*", "real\\VIP", True),
                   ("组别*", "组别\U0001f31f", True), ("*a*b", "aaab", True)]
        for mask, group, expected in vectors:
            with self.subTest(mask=mask, group=group):
                self.assertEqual(group_matches(mask, group), expected)

    def test_exact_lots_boundaries(self):
        for value in (0, 1, 100_000_000, 500_000_001, MAX_VOLUME):
            self.assertEqual(lots_units(format_lots(value)), value)
        for value, expected in (("0", 1000), ("2", 1000), ("2.00000001", 500),
                                ("5", 500), ("5.00000001", 200)):
            self.assertEqual(preview(document(), "mt5", "10001", "real-1", value)["leverage"], expected)
        for value in ("-1", "+2", ".5", "2.", "2e1", "NaN", " 2", "0.000000001", "184467440737.09551616"):
            with self.subTest(value=value), self.assertRaises(ValidationError):
                lots_units(value)

    def test_one_global_priority_and_reordering(self):
        config = document()
        account = rule("account", "account", "10001")
        account["tiers"] = [{"above_lots": "0", "leverage": 100}]
        config["rules"].append(account)
        self.assertEqual(preview(config, "mt5", "10001", "real-a", "3")["rule_id"], "groups")
        config["rules"].reverse()
        result = preview(config, "mt5", "10001", "real-a", "3")
        self.assertEqual((result["rule_id"], result["leverage"]), ("account", 100))
        config["rules"][0]["enabled"] = False
        self.assertEqual(preview(config, "mt5", "10001", "real-a", "3")["rule_id"], "groups")
        config["enabled"] = False
        self.assertFalse(preview(config, "mt5", "10001", "real-a", "3")["matched"])

    def test_account_precision_and_platform_identity(self):
        config = document()
        config["rules"] = [rule("account", "account", "18446744073709551615,9007199254740993")]
        self.assertTrue(preview(config, "mt5", "9007199254740993", "", "1")["matched"])
        self.assertFalse(preview(config, "mt5", "9007199254740992", "", "1")["matched"])
        with self.assertRaises(ValidationError):
            validate_config(config, "mt4")
        config["platform"] = "mt4"
        with self.assertRaises(ValidationError):
            validate_config(config, "mt4")
        config["rules"][0]["selector"] = "001,1"
        with self.assertRaises(ValidationError):
            validate_config(config, "mt4")

    def test_strict_schema_and_safe_default(self):
        self.assertEqual(validate_config(default_config("mt4"), "mt4")["rules"], [])
        mutations = [lambda d: d.update(unknown=True), lambda d: d.update(enabled=1),
                     lambda d: d.update(schema_version=True), lambda d: d.update(poll_interval_ms=99),
                     lambda d: d.update(poll_interval_ms=100.0), lambda d: d.update(rules=[]),
                     lambda d: d["rules"][0].update(scope="all"),
                     lambda d: d["rules"][0].update(name=" "),
                     lambda d: d["rules"][0].update(id="../config"),
                     lambda d: d["rules"][0]["tiers"][0].update(above_lots=0),
                     lambda d: d["rules"][0]["tiers"][1].update(leverage=1000),
                     lambda d: d["rules"][0]["tiers"][1].update(above_lots="0")]
        for mutate in mutations:
            config = document()
            mutate(config)
            with self.subTest(config=config), self.assertRaises(ValidationError):
                validate_config(config, "mt5")
        for selector in ("", "!", "!!ABC", "ABC?", "A,,B", "A,", "real\n", "A" * 256):
            config = document()
            config["rules"][0]["selector"] = selector
            with self.subTest(selector=selector), self.assertRaises(ValidationError):
                validate_config(config, "mt5")

    def test_duplicate_keys_and_non_json_numbers(self):
        for raw in (b'{"enabled":true,"enabled":false}', b'{"x":NaN}', b'{"x":Infinity}', b'{} garbage', b'"\xff"',
                    b'\xef\xbb\xbf{}', '{}'.encode('utf-16')):
            with self.subTest(raw=raw), self.assertRaises(ValidationError):
                decode_json(raw)

    def test_native_contract_unicode_and_unknown_group(self):
        config = document()
        config["rules"][0]["selector"] = "*"
        self.assertFalse(preview(config, "mt5", "1", "", "0")["matched"])
        for name in ("\ufeff", " \u3000\u2009", "a\x85b"):
            config["rules"][0]["name"] = name
            with self.subTest(name=name), self.assertRaises(ValidationError):
                validate_config(config, "mt5")
        config["rules"] = [rule("account", "account", "0" * 254 + "1")]
        self.assertTrue(preview(config, "mt5", "1", "", "0")["matched"])


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.store = Store(self.directory.name)

    def test_atomic_save_version_conflict_and_platform_isolation(self):
        initial = self.store.load("mt5")
        self.assertFalse(initial["exists"])
        self.assertEqual(initial["revision"], "missing")
        saved = self.store.save("mt5", document(), initial["revision"])
        self.assertEqual(saved, self.store.load("mt5"))
        with self.assertRaises(ConflictError):
            self.store.save("mt5", default_config("mt5"), "missing")
        self.assertEqual(saved, self.store.load("mt5"))
        self.assertFalse(self.store.load("mt4")["exists"])
        self.assertEqual(len(self.store.audit("mt5")), 1)
        self.assertEqual(self.store.audit("mt4"), [])
        self.assertEqual(list(Path(self.directory.name).glob("*.tmp")), [])

    def test_validation_failure_keeps_saved_file(self):
        self.store.save("mt5", document(), "missing")
        before = self.store.read_raw("mt5")
        bad = document()
        bad["rules"][0]["selector"] = "!"
        with self.assertRaises(ValidationError):
            self.store.save("mt5", bad, self.store.load("mt5")["revision"])
        self.assertEqual(before, self.store.read_raw("mt5"))

    def test_concurrent_writers_only_one_wins(self):
        outcomes = []
        def save():
            try:
                self.store.save("mt5", document(), "missing")
                outcomes.append("saved")
            except ConflictError:
                outcomes.append("conflict")
        workers = [threading.Thread(target=save) for _ in range(2)]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join()
        self.assertCountEqual(outcomes, ["saved", "conflict"])

    def test_directory_lease_prevents_duplicate_management_hosts(self):
        lease = DirectoryLease(self.directory.name)
        try:
            with self.assertRaises(RuntimeError):
                DirectoryLease(self.directory.name)
        finally:
            lease.close()

    def test_windows_reader_sharing_conflict_retries_atomic_replace(self):
        first = self.store.save("mt5", document(), "missing")
        before = self.store.read_raw("mt5")
        update = document()
        update["rules"][0]["name"] = "更新后的规则"
        native_replace = management_server.os.replace
        attempts = []

        def temporarily_locked(source, destination):
            attempts.append(destination)
            self.assertEqual(self.store.read_raw("mt5"), before)
            if len(attempts) < 3:
                error = PermissionError("configuration is open by DLL reader")
                error.winerror = 32
                raise error
            native_replace(source, destination)

        with patch.object(management_server.os, "replace", side_effect=temporarily_locked), \
             patch.object(management_server.time, "sleep") as sleep:
            saved = self.store.save("mt5", update, first["revision"])
        self.assertEqual(len(attempts), 3)
        self.assertEqual(sleep.call_count, 2)
        self.assertEqual(saved, self.store.load("mt5"))
        self.assertEqual(len(self.store.audit("mt5")), 2)
        self.assertEqual(list(Path(self.directory.name).glob("*.tmp")), [])

    def test_failed_replace_preserves_revision_and_removes_temporary_file(self):
        first = self.store.save("mt5", document(), "missing")
        before = self.store.read_raw("mt5")
        error = PermissionError("permanent Windows file lock")
        error.winerror = 5
        with patch.object(management_server.os, "replace", side_effect=error) as replace, \
             patch.object(management_server.time, "sleep"):
            with self.assertRaises(PermissionError):
                self.store.save("mt5", default_config("mt5"), first["revision"])
        self.assertEqual(replace.call_count, 6)
        self.assertEqual(self.store.read_raw("mt5"), before)
        self.assertEqual(self.store.load("mt5")["revision"], first["revision"])
        self.assertEqual(len(self.store.audit("mt5")), 1)
        self.assertEqual(list(Path(self.directory.name).glob("*.tmp")), [])

    def test_non_windows_permission_failure_is_not_retried(self):
        with patch.object(management_server.os, "replace", side_effect=PermissionError("directory is read only")) as replace, \
             patch.object(management_server.time, "sleep") as sleep:
            with self.assertRaises(PermissionError):
                self.store.save("mt5", document(), "missing")
        self.assertEqual(replace.call_count, 1)
        sleep.assert_not_called()
        self.assertFalse(self.store.load("mt5")["exists"])
        self.assertEqual(list(Path(self.directory.name).glob("*.tmp")), [])


class HttpTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.server = ManagementServer(("127.0.0.1", 0), self.directory.name)
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()
        self.port = self.server.server_address[1]

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.worker.join()
        self.directory.cleanup()

    def request(self, method, path, value=None, extra=None):
        client = http.client.HTTPConnection("127.0.0.1", self.port)
        headers = {"Content-Type": "application/json"}
        if extra:
            headers.update(extra)
        client.request(method, path, body=encode(value) if value is not None else None, headers=headers)
        response = client.getresponse()
        raw = response.read()
        status = response.status
        client.close()
        return status, json.loads(raw)

    def test_csrf_origin_and_host_protection(self):
        path = "/api/plugins/dynamic-leverage/mt5/config"
        status, session = self.request("GET", "/api/session")
        self.assertEqual(status, 200)
        self.assertEqual(self.request("PUT", path, document(), {"If-Match": "missing"})[0], 403)
        headers = {"X-CSRF-Token": session["csrf_token"], "If-Match": "missing", "Origin": "https://other.example"}
        self.assertEqual(self.request("PUT", path, document(), headers)[0], 403)
        self.assertEqual(self.request("GET", "/api/session", extra={"Host": "evil.example"})[0], 403)
        self.assertFalse(self.server.store.load("mt5")["exists"])

    def test_save_reload_conflict_and_preview_end_to_end(self):
        _, session = self.request("GET", "/api/session")
        headers = {"X-CSRF-Token": session["csrf_token"], "If-Match": "missing"}
        path = "/api/plugins/dynamic-leverage/mt5/config"
        self.assertEqual(self.request("PUT", path, document(), headers)[0], 200)
        self.assertEqual(self.request("PUT", path, document(), headers)[0], 409)
        _, loaded = self.request("GET", path)
        self.assertEqual(loaded["config"], validate_config(document(), "mt5"))
        status, result = self.request("POST", path.replace("config", "preview"),
            {"config": loaded["config"], "login": "10001", "group": "real-main", "lots": "5.00000001"}, headers)
        self.assertEqual(status, 200)
        self.assertEqual((result["rule_id"], result["leverage"]), ("groups", 200))
        self.assertEqual(self.request("GET", "/api/plugins/dynamic-leverage/mt4/config")[1]["config"]["rules"], [])

    def test_unsupported_routes_and_missing_precondition(self):
        _, session = self.request("GET", "/api/session")
        headers = {"X-CSRF-Token": session["csrf_token"]}
        self.assertEqual(self.request("PUT", "/api/plugins/dynamic-leverage/mt5/config", document(), headers)[0], 400)
        self.assertEqual(self.request("GET", "/api/plugins/dynamic-leverage/../../config")[0], 404)
        self.assertEqual(self.request("GET", "/../server.py")[0], 404)


if __name__ == "__main__":
    unittest.main()
