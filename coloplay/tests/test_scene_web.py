"""Optional Chromium checks for Global using JSON from the real scene service.

Install playwright and its Chromium browser to run. No hardware is contacted.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import unittest
from urllib.parse import parse_qs, urlsplit

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    sync_playwright = None

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sync_playwright, "Optional Playwright dependency unavailable")
class GlobalWebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="coloplay-scenes-")
        cls.addClassCleanup(cls.temp.cleanup)
        binary = str(Path(cls.temp.name) / "scene-service")
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra",
                        "-Werror", "-Wno-misleading-indentation", "-Itests/scene_fakes",
                        "-Ifirmware/include", "firmware/src/scene_service.cpp",
                        "tests/scene_service_test.cpp", "-o", binary], cwd=ROOT, check=True)
        cls.configs = {case: json.loads(subprocess.check_output([binary, case], text=True))
                       for case in ("legacy", "api")}
        html = re.search(r'R"WEB\((.*?)\)WEB"',
                         (ROOT / "firmware/include/web_ui.h").read_text(), re.S)[1].encode()

        class Assets(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                path = urlsplit(self.path).path
                if path == "/":
                    body, mime = html, "text/html"
                elif path.startswith("/assets/"):
                    name = re.sub(r"-[0-9a-f]{12}(?=\.)", "", path.rsplit("/", 1)[-1])
                    asset = ROOT / "firmware/web" / name
                    body = asset.read_bytes()
                    mime = {".js": "application/javascript", ".css": "text/css",
                            ".svg": "image/svg+xml"}.get(asset.suffix, "text/plain")
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", mime)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Assets)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.addClassCleanup(cls.server.server_close)
        cls.addClassCleanup(cls.server.shutdown)
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"
        cls.pw = sync_playwright().start()
        cls.addClassCleanup(cls.pw.stop)
        cls.browser = cls.pw.chromium.launch()
        cls.addClassCleanup(cls.browser.close)

    def open(self, case="legacy"):
        self.cfg = json.loads(json.dumps(self.configs[case]))
        self.posts, self.errors, self.external = [], [], []
        self.page = self.browser.new_page(viewport={"width": 1280, "height": 1000})
        self.addCleanup(self.page.close)
        self.page.on("pageerror", lambda error: self.errors.append(str(error)))
        self.page.on("request", lambda req: self.external.append(req.url)
                     if not req.url.startswith(self.url) else None)
        self.page.route("**/api/**", self.api)
        self.page.goto(self.url + "/#scenes", wait_until="networkidle")
        self.page.locator("#scene-tab-0").wait_for()

    def api(self, route):
        path = urlsplit(route.request.url).path.rsplit("/", 1)[-1]
        if route.request.method == "POST":
            values = {k: v[0] for k, v in parse_qs(route.request.post_data).items()}
            self.posts.append(values)
            self.assertEqual(path, "scenes")
        if path == "scenes":
            data = self.cfg
        elif path == "status":
            data = dict(firmware="test", wifi={"ip": "192.0.2.1"},
                        scene=self.cfg["state"], controller={"transport": "usb_cdc_acm"},
                        hardware=dict(ready=True, display=dict(brightness=70, effective_brightness=70),
                                      leds=dict(ring_brightness=70, keys_brightness=70,
                                                ring_effective=70, keys_effective=70,
                                                mode="rainbow", color="#ffffff", errors=0),
                                      psram={"test_passed": True}, audio={"io_errors": 0}))
        elif path == "clock":
            data = dict(ntp_servers=["pool.ntp.org", "", ""], timezone="Etc/UTC", zones=[])
        else:
            data = {}
        route.fulfill(json=data)

    def test_global_has_only_bindings_and_never_activates(self):
        self.open()
        active = self.page.locator("#active-scene").inner_text()
        self.page.locator("#scene-tab-0").click()
        self.assertEqual(self.page.locator("#scene-settings-title").inner_text(), "Global bindings")
        self.assertEqual(self.page.locator("#active-scene").inner_text(), active)
        for name in ("scene-config", "scene-icon-control", "scene-activate", "scene-rename",
                     "scene-delete", "scene-move-left", "scene-move-right"):
            self.assertFalse(self.page.locator("#" + name).is_visible(), name)
        self.assertTrue(self.page.locator("#binding-add").is_visible())
        self.assertTrue(self.page.locator("#scene-reload").is_visible())
        self.assertEqual(self.page.locator("#scene-tab-0 .active-badge").count(), 0)
        self.assertEqual(self.page.locator("#binding-global").count(), 0)
        for width in (375, 768, 1280):
            self.page.set_viewport_size({"width": width, "height": 1000})
            self.assertTrue(self.page.evaluate("document.documentElement.scrollWidth<=innerWidth+1"))
            self.assertFalse(self.page.locator("#scene-config").is_visible())
        self.page.locator("#scene-tab-1").click()
        self.assertTrue(self.page.locator("#scene-config").is_visible())
        self.assertTrue(self.page.locator("#scene-icon-control").is_visible())
        self.assertEqual(self.posts, [])
        self.assertEqual(self.errors, [])
        self.assertEqual(self.external, [])

    def test_global_run_and_switch_targets(self):
        self.open()
        self.page.locator("#scene-tab-0").click()
        self.page.locator(".binding-edit").click()
        self.assertTrue(self.page.locator("#binding-run").is_enabled())
        self.page.locator("#binding-run").click()
        self.page.wait_for_function("document.getElementById('binding-message').textContent==='Binding executed'")
        self.assertEqual(self.posts[-1]["scene_id"], "0")
        self.assertEqual(self.posts[-1]["op"], "run")
        self.page.locator("#action-list select[data-field=type]").evaluate(
            "el=>{el.value='switch_scene';el.dispatchEvent(new Event('change'))}")
        target = self.page.locator("#action-list select[data-field=target]")
        targets = target.locator("option").evaluate_all("els=>els.map(el=>el.value)")
        self.assertNotIn("0", targets)
        self.assertIn(target.input_value(), targets)
        self.assertNotEqual(target.input_value(), "")
        self.assertEqual(self.errors, [])

    def test_local_override_warning_and_last_scene_protection(self):
        self.open("api")
        self.assertTrue(self.page.locator("#scene-delete").is_disabled())
        self.page.locator("tr[data-input=key3][data-gesture=single] .binding-edit").click()
        warning = self.page.locator("#binding-global-warning")
        self.assertTrue(warning.is_visible())
        self.assertIn("exists in Global", warning.inner_text())
        self.assertIn("overrides", warning.inner_text())
        self.assertEqual(self.posts, [])
        self.assertEqual(self.errors, [])

    def test_global_save_does_not_need_a_scope_checkbox(self):
        self.open()
        self.page.locator("#scene-tab-0").click()
        self.page.locator("#binding-add").click()
        self.page.locator("#binding-save").click()
        self.page.wait_for_function("!document.getElementById('binding-dialog').open")
        self.assertEqual(self.posts[-1]["op"], "binding")
        self.assertEqual(self.posts[-1]["scene_id"], "0")
        self.assertNotIn("global", self.posts[-1])
        self.assertEqual(self.errors, [])


if __name__ == "__main__":
    unittest.main()
