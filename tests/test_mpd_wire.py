"""Exercise the production MPD socket/framing layer against fragmented peers."""
import ctypes as C
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIELD = C.CFUNCTYPE(C.c_bool, C.c_char_p, C.c_char_p, C.c_void_p)
BINARY = C.CFUNCTYPE(C.c_bool, C.c_void_p, C.c_size_t, C.c_void_p)


class Wire(C.Structure):
    _fields_ = [('fd', C.c_int), ('pos', C.c_uint), ('used', C.c_uint),
                ('rx', C.c_ubyte * 4096), ('error', C.c_char * 192)]


class ProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        library = Path(cls.tmp.name) / 'mpd.so'
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', str(ROOT/'app/mpd/mpd_wire.c'),
                        '-o', str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.mpd_wire_init.argtypes = [C.POINTER(Wire)]
        cls.lib.mpd_wire_close.argtypes = [C.POINTER(Wire)]
        cls.lib.mpd_wire_attach.argtypes = [C.POINTER(Wire), C.c_int, C.c_uint]
        cls.lib.mpd_wire_attach.restype = C.c_bool
        cls.lib.mpd_wire_connect.argtypes = [C.POINTER(Wire), C.c_char_p, C.c_uint16, C.c_uint]
        cls.lib.mpd_wire_connect.restype = C.c_bool
        cls.lib.mpd_wire_command.argtypes = [C.POINTER(Wire), C.c_char_p, FIELD, BINARY, C.c_void_p]
        cls.lib.mpd_wire_command.restype = C.c_bool
        cls.lib.mpd_wire_send.argtypes=[C.POINTER(Wire),C.c_char_p]
        cls.lib.mpd_wire_send.restype=C.c_bool
        cls.lib.mpd_wire_response.argtypes=[C.POINTER(Wire),FIELD,BINARY,C.c_void_p]
        cls.lib.mpd_wire_response.restype=C.c_bool
        cls.lib.mpd_wire_ready.argtypes=[C.POINTER(Wire)]
        cls.lib.mpd_timestamp.argtypes=[C.c_char_p]
        cls.lib.mpd_timestamp.restype=C.c_uint32
        cls.lib.mpd_quote.argtypes = [C.c_char_p, C.c_size_t, C.c_char_p]
        cls.lib.mpd_quote.restype = C.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.w = Wire()
        self.lib.mpd_wire_init(C.byref(self.w))
        self.peer, client = socket.socketpair()
        self.peer.settimeout(2)
        self.peer.sendall(b'OK MPD 0.24.0\n')
        self.assertTrue(self.lib.mpd_wire_attach(C.byref(self.w), client.detach(), 1000))
        self.errors = []
        self.threads = []

    def tearDown(self):
        self.lib.mpd_wire_close(C.byref(self.w))
        self.peer.close()
        for t in self.threads:
            t.join(3)
            self.assertFalse(t.is_alive())
        if self.errors:
            raise self.errors[0]

    def serve(self, command, reply, fragment=0):
        def server():
            try:
                data = b''
                while not data.endswith(b'\n'):
                    data += self.peer.recv(4096)
                self.assertEqual(data, command+b'\n')
                if fragment:
                    for i in range(0, len(reply), fragment):
                        self.peer.sendall(reply[i:i+fragment])
                else:
                    self.peer.sendall(reply)
            except Exception as e:
                self.errors.append(e)
        t = threading.Thread(target=server)
        self.threads.append(t)
        t.start()

    def command(self, cmd=b'status', field=None, binary=None):
        self.callbacks = (FIELD(field) if field else FIELD(), BINARY(binary) if binary else BINARY())
        return self.lib.mpd_wire_command(C.byref(self.w), cmd, *self.callbacks, None)

    def test_idle_request_waits_without_blocking_other_work(self):
        self.assertTrue(self.lib.mpd_wire_send(C.byref(self.w),b'idle database stored_playlist update'))
        self.assertEqual(self.peer.recv(200),b'idle database stored_playlist update\n')
        self.assertEqual(self.lib.mpd_wire_ready(C.byref(self.w)),0)
        self.peer.sendall(b'changed: database\nchanged: stored_playlist\nOK\n')
        self.assertEqual(self.lib.mpd_wire_ready(C.byref(self.w)),1)
        rows=[]
        field=FIELD(lambda k,v,_: rows.append((k,v)) or True)
        self.assertTrue(self.lib.mpd_wire_response(C.byref(self.w),field,BINARY(),None))
        self.assertEqual(rows,[(b'changed',b'database'),(b'changed',b'stored_playlist')])
        self.assertEqual(self.lib.mpd_wire_ready(C.byref(self.w)),0)

    def test_iso8601_dates_use_utc_and_accept_offsets(self):
        for stamp in [b'2024-01-02T03:04:05Z',b'2024-01-02T11:04:05+08:00',b'2024-01-01T22:04:05-05:00',b'2024-01-02T03:04:05.456Z']:
            self.assertEqual(self.lib.mpd_timestamp(stamp),1704164645)
        for stamp in [b'-1',b'',b'2023-02-29T00:00:00Z',b'2024-01-02T25:00:00Z',b'2024-01-02T03:04:05',b'2024-01-02T03:04:05+99:00']:
            self.assertEqual(self.lib.mpd_timestamp(stamp),0)
        self.assertGreater(self.lib.mpd_timestamp(b'2024-02-29T00:00:00Z'),0)

    def test_fragmented_utf8_fields_and_empty_values(self):
        rows = []
        self.serve(b'status', 'Artist: Björk\nAlbum: \nstate: play\nOK\n'.encode(), 1)
        self.assertTrue(self.command(field=lambda k,v,_: rows.append((k.decode(),v.decode())) or True))
        self.assertEqual(rows, [('Artist','Björk'), ('Album',''), ('state','play')])

    def test_tracks_multiline_comments_and_blank_lines_do_not_disconnect(self):
        rows=[]
        self.serve(b'listallinfo',b'file: music.flac\nTitle: Song\nComment: first line\nsecond line\n\nnotes: metadata text\nlast comment line\nduration: 120.0\nOK\n',19)
        self.assertTrue(self.command(b'listallinfo',field=lambda k,v,_: rows.append((k,v)) or True))
        self.assertIn((b'duration',b'120.0'),rows)
        self.assertGreaterEqual(self.w.fd,0)

    def test_paused_and_empty_queue_responses_remain_usable(self):
        self.serve(b'status',b'volume: 100\nplaylistlength: 0\nstate: stop\nduration: 0.000\nOK\n\n')
        self.assertTrue(self.command())
        self.threads[-1].join()
        self.serve(b'currentsong',b'OK\n')
        self.assertTrue(self.command(b'currentsong'))
        self.threads[-1].join()
        self.serve(b'status',b'state: pause\nOK\n')
        self.assertTrue(self.command())

    def test_stream_large_library(self):
        count = 0
        def field(k,v,_):
            nonlocal count
            if k == b'file': count += 1
            return True
        self.serve(b'listallinfo', b''.join(f'file: artist/{i}.flac\nTitle: Track {i}\n'.encode() for i in range(5000))+b'OK\n', 997)
        self.assertTrue(self.command(b'listallinfo', field=field))
        self.assertEqual(count, 5000)

    def test_binary_with_newlines_nuls_and_fake_protocol_text(self):
        data = b'\x00\xff\nOK\nACK [5@0] {fake} error\n' * 100
        received = bytearray()
        self.serve(b'readpicture "song.flac" 0', f'size: {len(data)}\nbinary: {len(data)}\n'.encode()+data+b'\nOK\n', 73)
        self.assertTrue(self.command(b'readpicture "song.flac" 0', binary=lambda p,n,_: received.extend(C.string_at(p,n)) or True))
        self.assertEqual(received, data)

    def test_ack_keeps_connection_and_next_response_clean(self):
        self.serve(b'status', b'ACK [4@0] {status} permission denied\n')
        self.assertFalse(self.command())
        self.assertGreaterEqual(self.w.fd, 0)
        self.assertIn(b'permission denied', self.w.error)
        self.threads[-1].join()
        self.serve(b'status', b'state: pause\nOK\n')
        self.assertTrue(self.command())

    def test_long_line_closes_socket(self):
        self.serve(b'status', b'Title: '+b'x'*5000+b'\nOK\n')
        self.assertFalse(self.command())
        self.assertEqual(self.w.fd, -1)

    def test_binary_consumer_failure_closes_socket(self):
        self.serve(b'status', b'binary: 4\n1234\nOK\n')
        self.assertFalse(self.command(binary=lambda *_: False))
        self.assertEqual(self.w.fd, -1)

    def test_invalid_binary_lengths(self):
        for length in [b'-1', b'99999999', b'12x']:
            with self.subTest(length=length):
                if self.w.fd < 0:
                    self.peer.close()
                    self.peer, client=socket.socketpair()
                    self.peer.sendall(b'OK MPD 0.24.0\n')
                    self.assertTrue(self.lib.mpd_wire_attach(C.byref(self.w),client.detach(),1000))
                self.serve(b'status',b'binary: '+length+b'\n')
                self.assertFalse(self.command())
                self.assertEqual(self.w.fd,-1)
                self.threads[-1].join()

    def test_disconnect_mid_response(self):
        self.peer.sendall(b'state: play\n')
        self.peer.shutdown(socket.SHUT_WR)
        self.assertFalse(self.command())
        self.assertEqual(self.w.fd,-1)

    def test_consumer_capacity_failure_closes_socket(self):
        self.serve(b'status',b'file: over-capacity.flac\nOK\n')
        self.assertFalse(self.command(field=lambda *_: False))
        self.assertEqual(self.w.fd,-1)

    def test_quotes_escape_and_reject_command_injection(self):
        out=C.create_string_buffer(100)
        self.assertTrue(self.lib.mpd_quote(out,len(out),b'a "quote" \\ folder'))
        self.assertEqual(out.value,b'"a \\"quote\\" \\\\ folder"')
        for invalid in [b'secret\nclear',b'secret\rstop',b'\tcommand',b'\x7f']:
            self.assertFalse(self.lib.mpd_quote(out,len(out),invalid))
        self.assertFalse(self.lib.mpd_quote(out,3,b'x'))
        self.assertTrue(self.lib.mpd_quote(out,3,b''))

    def test_real_tcp_greeting_and_connect(self):
        listener=socket.socket()
        listener.bind(('127.0.0.1',0)); listener.listen()
        port=listener.getsockname()[1]
        def accept():
            connection,_=listener.accept()
            with connection:
                for part in [b'O',b'K MP',b'D 0.24.0\n']:
                    connection.sendall(part)
            listener.close()
        t=threading.Thread(target=accept); self.threads.append(t); t.start()
        self.assertTrue(self.lib.mpd_wire_connect(C.byref(self.w),b'127.0.0.1',port,1000))

    def test_greeting_timeout_and_wrong_service(self):
        self.lib.mpd_wire_close(C.byref(self.w))
        peer,client=socket.socketpair()
        with peer:
            self.assertFalse(self.lib.mpd_wire_attach(C.byref(self.w),client.detach(),50))
        peer,client=socket.socketpair()
        with peer:
            peer.sendall(b'HTTP/1.1 200 OK\n')
            self.assertFalse(self.lib.mpd_wire_attach(C.byref(self.w),client.detach(),50))
        self.assertEqual(self.w.fd,-1)

if __name__ == '__main__':
    unittest.main()
