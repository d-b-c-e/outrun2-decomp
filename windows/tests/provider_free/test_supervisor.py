"""Inert rehearsal of the real publication/close gates; no game, hooks, native APIs or OS input."""
import ast, json, pathlib, tempfile, threading, time, unittest

RUNNER = pathlib.Path(__file__).parents[1] / 'shim_first_loop' / 'run.py'
tree = ast.parse(RUNNER.read_text())
functions = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in ('supervisor_gate', 'supervisor_closing')]
scope = dict(time=time, json=json)
exec(compile(ast.Module(body=functions, type_ignores=[]), str(RUNNER), 'exec'), scope)
gate, closing = scope['supervisor_gate'], scope['supervisor_closing']

class Rehearsal(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = pathlib.Path(self.temp.name) / 'supervisor.json'
        self.state()
    def tearDown(self): self.temp.cleanup()
    def state(self, **kw):
        s = dict(unix=time.time(), ok=True, pending=False, fenceReady=True, cleanup=False)
        s.update(kw); self.path.write_text(json.dumps(s))
    def test_optional_original_route(self): gate(None, True); closing(None)
    def test_launch_heartbeat(self): self.state(fenceReady=False); gate(self.path)
    def test_admitted_stimulus(self): gate(self.path, True)
    def test_stop_refuses_publication(self):
        self.state(ok=False)
        with self.assertRaises(RuntimeError): gate(self.path)
    def test_stale_refuses_publication(self):
        self.state(unix=time.time()-3)
        with self.assertRaises(RuntimeError): gate(self.path)
    def test_future_refuses_publication(self):
        self.state(unix=time.time()+3)
        with self.assertRaises(RuntimeError): gate(self.path)
    def test_pending_refuses_stimulus(self):
        self.state(pending=True)
        with self.assertRaises(RuntimeError): gate(self.path, True)
    def test_unfenced_refuses_stimulus(self):
        self.state(fenceReady=False)
        with self.assertRaises(RuntimeError): gate(self.path, True)
    def test_missing_state_refuses(self):
        self.path.unlink()
        with self.assertRaises(OSError): gate(self.path)
    def test_close_acknowledgement(self):
        self.state(cleanup=True); closing(self.path)
        self.assertTrue(self.path.with_suffix('.closing').exists())
    def test_close_waits_for_acknowledgement(self):
        def acknowledge():
            while not self.path.with_suffix('.closing').exists(): time.sleep(.01)
            self.state(cleanup=True)
        t=threading.Thread(target=acknowledge);t.start();closing(self.path);t.join()
    def test_stop_prevents_next_batch_in_same_lifecycle(self):
        gate(self.path);gate(self.path, True)
        self.state(ok=False)
        with self.assertRaises(RuntimeError): gate(self.path, True)
        self.state(ok=False, cleanup=True);closing(self.path)

if __name__ == '__main__': unittest.main(verbosity=2)
