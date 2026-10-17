import json
import math
from pathlib import Path
import unittest
from prepare_ground import rectangles, unproject


class CoverageTests(unittest.TestCase):
    def test_rectangle_merging_preserves_holes(self):
        mask = [[True,True,False,True], [True,False,False,True], [True,True,True,True], [False,True,True,True]]
        result = rectangles(mask,4)
        reconstructed = [[0]*4 for _ in range(4)]
        for x,y,w,h in result:
            for iy in range(round(y*4),round((y+h)*4)):
                for ix in range(round(x*4),round((x+w)*4)):
                    reconstructed[iy][ix] += 1
        self.assertEqual(reconstructed, [[int(v) for v in line] for line in mask])

    def test_road_paint_and_flat_curb_registrations(self):
        source = json.loads(Path(__file__).with_name('ground-shapes.json').read_text())
        shapes = source['overlays']
        # Native slot 39 has neither a raster nor a FloorOverlay definition.
        for index in (i for i in range(50) if i != 39):
            name = 'street_trafficlines_01_'+str(index)
            self.assertTrue(name in shapes, name)
            expected = 'PaintYellow' if 16 <= index < 32 else 'PaintWhite'
            self.assertEqual(shapes[name]['material'],expected)
        self.assertTrue(any(name.startswith('street_curbs_01_') for name in shapes))
        for index in (26,27):
            self.assertTrue(shapes[f'street_curbs_01_{index}']['patches'])
        for shape in shapes.values():
            for patch in shape['patches']:
                self.assertEqual(len(patch),4)
                for vertex in patch:
                    self.assertEqual(len(vertex),2)
                    self.assertTrue(all(math.isfinite(v) and abs(v) < 8 for v in vertex))

    def test_native_floor_corners(self):
        for pixel, world in [((32,96),(0,0)),((64,112),(1,0)),((32,128),(1,1)),((0,112),(0,1))]:
            self.assertEqual(unproject(*pixel),list(world))



if __name__ == '__main__': unittest.main()
