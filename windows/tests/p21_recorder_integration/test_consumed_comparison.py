"""Cross-check the shipped managed comparer with the reviewed Python synthetic corpus.
No game, devices, install or owner files. Usage: py -3 test_consumed_comparison.py --dll <P21Finalizer.dll>
"""
import argparse
import importlib.util
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--dll', type=Path, required=True)
parser.add_argument('--pair', nargs=2, type=Path, action='append', default=[],
                    help='also compare a pair of existing sources, read only; unsealed analysis')
args = parser.parse_args()
source = Path(__file__).resolve().parent.parent / 'shim_first_loop'
spec = importlib.util.spec_from_file_location('compare_consumed', source / 'compare_consumed.py')
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)
python_compare = baseline.compare
calls = 0

def managed_compare(a, b):
    global calls
    expected = json.loads(json.dumps(python_compare(a, b)))
    with tempfile.TemporaryDirectory() as d:
        output = Path(d) / 'comparison.json'
        completed = subprocess.run(['dotnet', str(args.dll), '--compare-consumed-sources', str(a), str(b), str(output)],
                                   text=True, capture_output=True, timeout=30)
        if not output.exists():
            raise AssertionError(completed.stdout + completed.stderr)
        actual = json.loads(output.read_text())
    expected_exit = 1 if not expected['valid'] else 0 if expected['followed'] and expected['fedEqualRows'] == expected['referenceRows'] else 2
    assert completed.returncode == expected_exit, completed.stdout + completed.stderr
    assert actual['valid'] == expected['valid'] and actual['followed'] == expected['followed']
    assert actual['sealed'] is False and actual['referenceSealSha256'] is None and actual['replaySealSha256'] is None
    calls += 1
    if not expected['valid']:
        # System.Text.Json rejects bare NaN at parse time; Python accepts it and rejects
        # the row during comparison. Invalid evidence must be refused in both, but the
        # number of rows inspected before refusal is deliberately not a wire contract.
        assert actual['problems'], actual
        actual['problems'] = expected['problems']
        return actual
    fields = ('referenceRows', 'replayRows', 'playedRows', 'fedChannels', 'valid', 'fedEqualRows', 'followed',
              'referenceModes', 'replayModes', 'referenceRaceRow', 'replayRaceRow')
    for field in fields:
        assert actual[field] == expected[field], (field, actual[field], expected[field])
    for field in ('fedFirstDifference', 'firstDivergence'):
        for key, value in (expected[field] or {}).items():
            assert actual[field][key] == value, (field, actual[field], expected[field])
        if expected[field] is None:
            assert actual[field] is None
    for key, value in expected['pose'].items():
        assert actual['pose'][key] == value, ('pose.' + key, actual['pose'][key], value)
    # The original corpus's human error-message checks belong to the Python implementation;
    # validity/difference/classification were independently compared above, keep those messages for its assertions.
    actual['problems'] = expected['problems']
    return actual

baseline.compare = managed_compare
sys.modules['compare_consumed'] = baseline
try:
    runpy.run_path(str(source / 'test_compare_consumed.py'), run_name='__main__')
except SystemExit as e:
    if e.code:
        raise
for a, b in args.pair:
    result = managed_compare(a, b)
    assert result['valid'] and result['followed'] and result['fedEqualRows'] == result['referenceRows'], result
    print('recorded sources: %d rows, %d poses, %s' % (result['referenceRows'], result['pose']['pairedRows'], a))
print('managed comparison cross-check: %d comparisons passed' % calls)
