import sys
from pathlib import Path
import unittest
sys.path.append(str(Path(__file__).resolve().parents[1]))
from geometry.profile import resolve, descriptor, FLOOR_HEIGHT
from geometry.openings import preview, card, wall_boxes, insert_boxes, section, section_for_tile


class SharedOpeningTests(unittest.TestCase):
    def test_wall_cut_and_insert_share_custom_bounds_in_both_native_orientations(self):
        values=dict(windowLeft=.2,windowRight=.8,windowBottom=1,windowTop=2,wallThickness=.3)
        for side,tangent,normal in (('N',0,1),('W',1,0)):
            data=preview('window.standard',side,values)
            self.assertEqual(data['card']['bounds'],[.2,.8,1,2])
            for b in data['wall']:
                self.assertFalse(b[tangent]<.8 and b[tangent+3]>.2 and b[2]<2 and b[5]>1)
                self.assertEqual((b[normal],b[normal+3]),(-.15,.15))
            self.assertAlmostEqual(data['frame'][0][tangent],.2-resolve(values)['windowFrameOverlap'])
            self.assertEqual(len(data['glass']),2)

    def test_legacy_imports_are_exact_shared_functions(self):
        sys.path.append(str(Path(__file__).resolve().parents[2]/'roof-lab'))
        import geometry_profile
        self.assertIs(geometry_profile.resolve,resolve)
        self.assertEqual(geometry_profile.descriptor(),descriptor())

    def test_full_cell_glazing_stops_at_the_slab_underside(self):
        d=preview('window.full-cell')
        boxes=d['frame']+d['glass']
        self.assertEqual(min(b[0] for b in boxes),0)
        self.assertEqual(max(b[3] for b in boxes),1)
        self.assertEqual(min(b[2] for b in boxes),0)
        self.assertEqual(max(b[5] for b in boxes),FLOOR_HEIGHT-resolve()['slabDepth'])
        self.assertEqual(d['card']['status'],'baseline-awaiting-review')

    def test_all_wall_cuts_reserve_the_next_slab_depth(self):
        for depth in (.04,.18,.3):
            p=resolve(dict(slabDepth=depth))
            for axis in ('north','west'):
                for opening in (0,1,2):
                    with self.subTest(depth=depth,axis=axis,opening=opening):
                        boxes=wall_boxes(axis,opening,p)
                        self.assertEqual(min(b[2] for b in boxes),0)
                        self.assertEqual(max(b[5] for b in boxes),FLOOR_HEIGHT-depth)
                self.assertEqual(max(b[5] for b in wall_boxes(axis,0,p,low=True)),p['lowWallHeight'])

    def test_legacy_meshes_rebuild_window_bounds_from_shared_values(self):
        from geometry.kernel import meshes
        data=meshes(dict(windowLeft=.2,windowRight=.8,windowBottom=1,windowTop=2))
        # The bottom panel reaches the shared sill; the wall excludes the cut.
        pts=list(zip(data['north2'][0::3],data['north2'][1::3],data['north2'][2::3]))
        from geometry.kernel import f32
        self.assertIn((f32(.2),f32(.075),f32(1)),pts)
        self.assertIn((f32(.8),f32(.075),f32(2)),pts)

    def test_reviewed_section_membership_does_not_fabricate_metric_geometry(self):
        entries=[e for e in descriptor()['openings']['sections'] if e['id'].startswith('opening.window.')]
        self.assertEqual({e['id'] for e in entries},{'opening.window.'+str(i).zfill(2) for i in range(1,22)})
        for e in entries:
            self.assertEqual(e['membershipStatus'],'candidate' if e['nativeShape'] is None else 'approved')
            self.assertEqual(e['geometryStatus'],'pending')
            self.assertIsNone(e['geometry'])
            self.assertEqual(e['scope'],'single-tile section')
            self.assertEqual(len(e['bindings']['walls']),{None:22,'9':474,'10':229,'11':0,'12':0,'17':175,'18':176,'19':57}.get(e['nativeShape'],226))
            self.assertEqual(len(e['bindings']['objects']),{None:0,'9':44,'10':29,'17':13,'18':13,'19':6}.get(e['nativeShape'],24))
            self.assertEqual(section_for_tile((e['bindings']['walls'] or e['bindings']['objects'])[0])['id'],e['id'])
            with self.assertRaises(ValueError):preview(e['id'])
        entry=section('opening.window.01');entry['bindings']['walls'].clear()
        self.assertEqual(len(section('opening.window.01')['bindings']['walls']),226)
        self.assertIsNone(section_for_tile('unknown'))

    def test_unknown_profiles_and_invalid_metric_overrides_are_rejected(self):
        for args in (('unknown',),('window.standard','NW'),('door.standard','N',{'tileSize':2})):
            with self.assertRaises(ValueError):preview(*args)


if __name__=='__main__':unittest.main()
