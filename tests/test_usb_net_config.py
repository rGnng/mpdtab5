"""Address planning and isolation rules for the single-peer USB network."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class UsbNetworkConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work=tempfile.TemporaryDirectory();p=Path(cls.work.name)
        (p/'plan.c').write_text('''#include "usb_net_config.h"
int plan(const char *server,const char *client,uint32_t *values) {
    usb_net_plan_t p;if(!usb_net_address_plan(server,client,&p))return 0;
    values[0]=p.server;values[1]=p.client;values[2]=p.mask;return 1;
}
const char *server_default(void) {return USB_NET_DEFAULT_IP;}
const char *client_default(void) {return USB_NET_DEFAULT_CLIENT_IP;}
''')
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'firmware/main'),str(p/'plan.c'),'-o',str(p/'plan.so')],check=True)
        cls.lib=C.CDLL(str(p/'plan.so'));cls.lib.plan.argtypes=[C.c_char_p,C.c_char_p,C.POINTER(C.c_uint32)]
        cls.lib.server_default.restype=cls.lib.client_default.restype=C.c_char_p
    @classmethod
    def tearDownClass(cls):cls.work.cleanup()
    def plan(self,server,client):
        out=(C.c_uint32*3)();ok=self.lib.plan(server.encode(),client.encode(),out)
        return bool(ok),list(out)
    def test_exact_requested_defaults_and_local_subnet(self):
        self.assertEqual(self.lib.server_default(),b'172.32.30.1')
        self.assertEqual(self.lib.client_default(),b'172.32.30.2')
        self.assertEqual(self.plan('172.32.30.1','172.32.30.2'),(True,[0xac201e01,0xac201e02,0xffffff00]))
    def test_custom_addresses_and_reversed_order(self):
        self.assertTrue(self.plan('192.168.80.200','192.168.80.199')[0])
        self.assertTrue(self.plan('10.20.30.254','10.20.30.1')[0])
    def test_peer_is_distinct_and_on_same_subnet(self):
        for server,client in [('172.32.30.1','172.32.30.1'),('172.32.30.1','172.32.31.2'),
                              ('192.168.0.1','10.0.0.2')]:
            with self.subTest(server=server,client=client):self.assertFalse(self.plan(server,client)[0])
    def test_network_broadcast_loopback_multicast_and_unspecified_are_rejected(self):
        for server,client in [('172.32.30.0','172.32.30.2'),('172.32.30.1','172.32.30.255'),
                              ('127.0.0.1','127.0.0.2'),('224.1.1.1','224.1.1.2'),
                              ('0.1.1.1','0.1.1.2'),('255.1.1.1','255.1.1.2')]:
            with self.subTest(server=server):self.assertFalse(self.plan(server,client)[0])
    def test_malformed_and_injection_values_are_rejected(self):
        for bad in ['', '1.2.3','1.2.3.4.5','256.2.3.4','-1.2.3.4',' 1.2.3.4',
                    '1.2.3.4\n','1.2.3.4;reboot','1..3.4','1111.2.3.4','1.2.3.4/24','hostname']:
            with self.subTest(value=bad):self.assertFalse(self.plan(bad,'172.32.30.2')[0])
if __name__=='__main__':unittest.main()
