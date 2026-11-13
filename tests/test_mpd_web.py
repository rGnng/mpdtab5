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
            '/api/status':{'mpd_status':'MPD connected','mpd_host':'music.local','mpd_port':6601,'mpd_connected':True,'sd_ready':True,'mpd_volume':45,'catalog':{'albums':2,'tracks':4},'storage_root':'/sdcard/mpd/'},
            '/api/config':{'pagination_size':64,'volume':65,'brightness':70,'usb_mode':0,'usb_net_ip':'172.32.30.1','usb_net_client_ip':'172.32.30.2'},'/api/power':{},
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

    def test_pagination_options_submit_sizes_as_numbers(self):
        options=self.page.locator('#pagination_size option')
        self.assertEqual(options.all_text_contents(),['Off','On touch 64','On touch 96'])
        self.assertEqual(self.page.locator('#pagination_size').input_value(),'64')
        for size in [96,0,64]:
            before=len(self.posts)
            self.page.locator('#pagination_size').select_option(str(size))
            self.page.evaluate('saveConfig()')
            self.page.wait_for_function("dirty.pagination_size === undefined")
            self.assertGreater(len(self.posts),before)
            self.assertEqual(self.posts[-1],('/api/config',{'pagination_size':size}))

    def test_usb_network_addresses_load_and_save_as_strings(self):
        self.assertEqual(self.page.locator('#usb_net_ip').input_value(),'172.32.30.1')
        self.assertEqual(self.page.locator('#usb_net_client_ip').input_value(),'172.32.30.2')
        self.page.locator('#usb_net_ip').fill('192.168.80.10')
        self.page.locator('#usb_net_client_ip').fill('192.168.80.11')
        self.page.evaluate('saveConfig()')
        self.page.wait_for_function("document.getElementById('toast').textContent.length > 0")
        self.assertEqual(self.posts[-1],('/api/config',{
            'usb_net_ip':'192.168.80.10','usb_net_client_ip':'192.168.80.11'}))
        self.assertIn('no gateway',self.page.locator('#system').inner_text())
        self.assertIn('One DHCP client',self.page.locator('#system').inner_text())
        self.assertIn('Reboot',self.page.locator('#system').inner_text())
    def test_usb_network_status_does_not_replace_wifi_address(self):
        self.page.evaluate("render({...S,ip:'10.0.0.181',usb_network:{ready:true,connected:true,ip:'172.32.30.1'}},C,{})")
        rows=self.page.locator('#device_rows').inner_text()
        self.assertIn('172.32.30.1',rows)
        self.assertIn('10.0.0.181',rows)

    def test_cache_progress_and_activity_states(self):
        for state,label in [('caching','Caching artwork'),('paused','Paused while using Tab5'),
                            ('waiting','Waiting for idle'),('retrying','Waiting to retry'),
                            ('offline','MPD offline'),('library','Loading library'),('no_sd','Insert SD card')]:
            self.page.evaluate("""state => renderArtwork({artwork_cached:12,
                artwork:{total:100,checked:15,missing:2,failed:1,state:state}}, {})""",state)
            self.assertEqual(self.page.locator('#artwork_state').inner_text(),label)
            self.assertEqual(self.page.locator('#artwork_progress').get_attribute('value'),'15')
            self.assertIn('12 / 100 albums cached',self.page.locator('#artwork_counts').inner_text())
            self.assertIn('15% checked',self.page.locator('#artwork_counts').inner_text())
    def test_completed_cache_pass_counts_missing_images(self):
        self.page.evaluate("""renderArtwork({artwork_cached:8,
            artwork:{total:10,checked:10,missing:2,state:'complete'}}, {})""")
        self.assertEqual(self.page.locator('#artwork_state').inner_text(),'Cache pass complete')
        self.assertEqual(self.page.locator('#artwork_progress').get_attribute('value'),'10')
        self.assertIn('8 / 10 albums cached · 100% checked · 2 without supported artwork',
                      self.page.locator('#artwork_counts').inner_text())
    def test_storage_uses_firmware_path_and_empty_library_has_finite_progress(self):
        self.assertIn('/sdcard/mpd/',self.page.locator('#library').inner_text())
        self.assertNotIn('sdcard://',self.page.locator('#library').inner_text())
        self.page.evaluate("renderArtwork({artwork_cached:0,artwork:{total:0,state:'library'}}, {})")
        self.assertEqual(self.page.locator('#artwork_progress').get_attribute('max'),'1')
        self.assertEqual(self.page.locator('#artwork_progress').get_attribute('value'),'0')
        self.assertIn('0%',self.page.locator('#artwork_counts').inner_text())

if __name__=='__main__': unittest.main()
