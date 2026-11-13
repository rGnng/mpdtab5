"""Browser checks for the embedded MPD setup form (pip install playwright)."""
import json
import mimetypes
from pathlib import Path
from urllib.parse import urlparse
import unittest
try:
    from playwright.sync_api import sync_playwright
except ImportError:
    sync_playwright=None

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipIf(sync_playwright is None,'Install playwright and Chromium to run browser tests')
class SetupFormTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.playwright=sync_playwright().start()
        cls.browser=cls.playwright.chromium.launch(headless=True)
    @classmethod
    def tearDownClass(cls):
        cls.browser.close(); cls.playwright.stop()
    def setUp(self):
        self.page=self.browser.new_page(viewport={'width':1280,'height':1000})
        self.errors=[]; self.posts=[]
        self.page.on('pageerror',lambda error:self.errors.append(str(error)))
        self.page.route('http://tab5.test/**',self.route)
        self.page.goto('http://tab5.test/')
        self.page.wait_for_function("document.getElementById('mpd_host').value === 'music.local'")
    def tearDown(self):
        self.page.close(); self.assertEqual(self.errors,[])
    def route(self,route):
        path=urlparse(route.request.url).path
        if route.request.method=='POST':
            self.posts.append((path,route.request.post_data_json))
            route.fulfill(status=200,content_type='application/json',body=json.dumps({'host':'music.local','port':6601}))
            return
        fixtures={
            '/api/mpd':{'host':'music.local','port':6601,'password_set':True},
            '/api/status':{'mpd_status':'MPD connected','mpd_host':'music.local','mpd_port':6601,'mpd_connected':True,'sd_ready':True,'mpd_volume':45,'catalog':{'albums':2,'tracks':4},'storage_root':'sdcard://mpd/'},
            '/api/config':{'volume':65,'brightness':70},'/api/power':{},
        }
        if path in fixtures:
            route.fulfill(status=200,content_type='application/json',body=json.dumps(fixtures[path])); return
        file=ROOT/'assets/web'/('index.html' if path=='/' else path.lstrip('/'))
        route.fulfill(status=200,content_type=mimetypes.guess_type(str(file))[0] or 'text/plain',body=file.read_bytes())
    def submit(self):
        self.page.locator('#mpd_save').click()
        self.page.wait_for_function("document.getElementById('toast').textContent.includes('settings saved')")
        return self.posts[-1][1]
    def test_loads_saved_address_and_port_without_password(self):
        self.assertEqual(self.page.locator('#mpd_port').input_value(),'6601')
        self.assertEqual(self.page.locator('#mpd_password').input_value(),'')
    def test_save_preserves_password_unless_changed(self):
        self.page.locator('#mpd_host').fill('other.local')
        self.assertEqual(self.submit(),{'host':'other.local','port':6601})
    def test_clear_password_is_explicit(self):
        self.page.locator('#mpd_clear_password').check()
        self.assertEqual(self.submit()['password'],'')
    def test_new_password_is_submitted_then_removed_from_form(self):
        self.page.locator('#mpd_password').fill('test " password')
        self.assertEqual(self.submit()['password'],'test " password')
        self.assertEqual(self.page.locator('#mpd_password').input_value(),'')
    def test_empty_server_is_allowed_and_port_is_validated(self):
        self.page.locator('#mpd_host').fill('')
        self.page.locator('#mpd_port').fill('70000')
        self.page.locator('#mpd_save').click()
        self.assertEqual(self.posts,[])
        self.page.locator('#mpd_port').fill('6600')
        self.assertEqual(self.submit()['host'],'')

if __name__=='__main__': unittest.main()
