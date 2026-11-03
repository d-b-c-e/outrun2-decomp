"""Exercise the real Windows snapshot reader against the real outer State publisher; no UI/input/game."""
import ast, collections, ctypes, ctypes.wintypes as wt, json, msvcrt, os, pathlib, sys, time
runner = pathlib.Path(__file__).parents[1] / 'shim_first_loop' / 'run.py'
node = next(n for n in ast.parse(runner.read_text()).body if isinstance(n, ast.FunctionDef) and n.name == 'read_supervisor_state')
scope = dict(ctypes=ctypes, wt=wt, json=json, msvcrt=msvcrt, os=os, kernel32=ctypes.WinDLL('kernel32', use_last_error=True))
exec(compile(ast.Module(body=[node], type_ignores=[]), str(runner), 'exec'), scope)
read = scope['read_supervisor_state']
path, result = map(pathlib.Path, sys.argv[1:3])
errors=collections.Counter(); first=None; reads=0; end=time.monotonic()+3
while time.monotonic()<end:
    try: read(path); reads+=1
    except (OSError, ValueError) as e:
        errors[str((type(e).__name__,getattr(e,'errno',None),getattr(e,'winerror',None)))]+=1
        first=first or str(e)
result.write_text(json.dumps(dict(reads=reads,errors=errors,first=first),indent=1))
