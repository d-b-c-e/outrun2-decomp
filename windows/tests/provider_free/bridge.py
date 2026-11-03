"""Own-job root. Its game, publishers and finalizer all inherit the same job."""
import contextlib, pathlib, runpy, sys, traceback
runner, transcript, *args = sys.argv[1:]
with pathlib.Path(transcript).open('w', encoding='utf-8', buffering=1) as stream:
    with contextlib.redirect_stdout(stream), contextlib.redirect_stderr(stream):
        sys.argv = [runner] + args
        try:
            runpy.run_path(runner, run_name='__main__')
        except SystemExit:
            raise
        except BaseException:
            traceback.print_exc()
            raise
