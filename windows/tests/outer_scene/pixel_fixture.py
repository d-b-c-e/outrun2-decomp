"""Characterize the retained start gaps. A passing fixture reproduces the gaps; it is not a renderer pass."""
import argparse
import hashlib
import json
import math
import pathlib
import unittest
import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ROIS = {'left': (0, 500, 420, 645), 'right': (3350, 390, 3840, 495)}

def region_black(rgb, box):
    x0, y0, x1, y1 = box
    return np.all(rgb[y0:y1, x0:x1] == 0, axis=2)

def panel_rays(panel, fov_y=64.4, pixel_shift=0.):
    # Independent analytic cross-check of rig_panels' matched-FOV geometry.
    # FOV is rounded in the runtime log; callers test the full rounding interval.
    width = 32 * 25.4 * 16 / math.sqrt(16*16 + 9*9)
    height = width * 9/16
    eye = height / 2 / math.tan(math.radians(fov_y)/2)
    angle = math.radians(70)
    u = (np.arange(1280) + .5 + pixel_shift) / 1280
    distance = 8 + width * ((1-u) if panel == 0 else u)
    x = width/2 + distance * math.cos(angle)
    if panel == 0: x = -x
    z = -eye + distance * math.sin(angle)
    return x, z, eye

def analyze(folder):
    case = json.loads((HERE / 'attempt-02.json').read_text())
    rows = []
    for frame in case['frames']:
        path = folder / frame['file']
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != frame['sha256']: raise ValueError(f'Unqualified frame identity: {path}')
        with Image.open(path) as picture: rgb = np.asarray(picture)
        if rgb.shape != (720,3840,3): raise ValueError(f'Unexpected shape: {path}')
        counts = {name: int(region_black(rgb, roi).sum()) for name,roi in ROIS.items()}
        directions = {}
        for panel,name in ((0,'left'),(2,'right')):
            local_columns = np.nonzero(region_black(rgb, ROIS[name]))[1] + ROIS[name][0] - panel*1280
            ranges = []
            behind = True
            for fov in (64.35,64.45):
                for shift in (-2.,2.):  # conservative allowance for native half-pixel/capture sampling
                    x,z,eye=panel_rays(panel,fov,shift)
                    if len(local_columns):
                        angles = np.abs(np.degrees(np.arctan2(x[local_columns],-z[local_columns])))
                        ranges.extend((float(angles.min()),float(angles.max())))
                        behind = behind and bool(np.all(z[local_columns] > 0))
            directions[name] = dict(behindCenterPlane=behind if len(local_columns) else None,
                                    absoluteYawDegrees=[min(ranges),max(ranges)] if ranges else None)
        rows.append(dict(file=frame['file'],sha256=digest,blackPixels=counts,rays=directions,
                         centerControlNonblack=bool(np.any(rgb[680,2000] != 0)),
                         adjacentControlsNonblack=bool(np.any(rgb[620,500] != 0) and np.any(rgb[450,3300] != 0))))
    return dict(case=case['case'],candidate=case['candidate'],scope='Pinned-frame gap characterization, not cause attribution or rendered acceptance',
                rois=ROIS,matchedEyeMm=panel_rays(0)[2],rows=rows)

class PixelAssertions(unittest.TestCase):
    report = None
    def test_all_twelve_qualified_frames(self): self.assertEqual(len(self.report['rows']),12)
    def test_initial_gap_areas(self):
        self.assertGreater(self.report['rows'][0]['blackPixels']['left'],40000)
        self.assertGreater(self.report['rows'][0]['blackPixels']['right'],20000)
    def test_left_region_clears_by_frame_eight(self):
        self.assertTrue(all(r['blackPixels']['left']==0 for r in self.report['rows'][8:]))
    def test_right_region_shrinks_by_at_least_89_percent(self):
        rows=self.report['rows'];self.assertLess(rows[-1]['blackPixels']['right'],rows[0]['blackPixels']['right']*.11)
    def test_no_gap_area_reappearance_in_this_burst(self):
        for name in ROIS:
            counts=[r['blackPixels'][name] for r in self.report['rows']]
            self.assertTrue(all(a>=b for a,b in zip(counts,counts[1:])))
    def test_visible_center_and_adjacent_controls(self):
        self.assertTrue(all(r['centerControlNonblack'] and r['adjacentControlsNonblack'] for r in self.report['rows']))
    def test_gap_rays_behind_center_for_entire_fov_rounding_band(self):
        for row in self.report['rows']:
            for name in ROIS:
                if row['blackPixels'][name]: self.assertTrue(row['rays'][name]['behindCenterPlane'])

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frames',type=pathlib.Path,required=True)
    parser.add_argument('--out',type=pathlib.Path,required=True)
    args=parser.parse_args()
    report=analyze(args.frames)
    PixelAssertions.report=report
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PixelAssertions))
    report['characterizationPass']=result.wasSuccessful()
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=1))
    raise SystemExit(0 if result.wasSuccessful() else 1)
