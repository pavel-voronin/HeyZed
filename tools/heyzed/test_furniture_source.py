import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from furniture_source import decode, prepare
from prepare_furniture_probe import bindings, structural


class FurnitureSourceContract(unittest.TestCase):
    def test_relative_catalog_emits_absolute_editor_input(self):
        import os
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'mesh.mtl').write_text('newmtl wood\nKd 1 1 1\n')
            obj = root/'mesh.obj'
            obj.write_text('mtllib mesh.mtl\nusemtl wood\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n')
            model = dict(key='chair', file=str(obj), root=str(root), sha256=hashlib.sha256(obj.read_bytes()).hexdigest())
            (root/'catalog.json').write_text(json.dumps(dict(schema=3, models=[model])))
            previous = Path.cwd()
            try:
                os.chdir(root)
                output = prepare(Path('catalog.json'))
            finally:
                os.chdir(previous)
            mesh = Path(json.loads(output.read_text())['models'][0]['meshInput'])
            self.assertTrue(mesh.is_absolute())
            self.assertTrue(mesh.is_file())

    def test_native_axes_origin_units_uv_and_front_face(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'mesh.mtl').write_text('newmtl wood\nKd 0.2 0.3 0.4\nd 1\n')
            obj = root/'mesh.obj'
            obj.write_text('mtllib mesh.mtl\nusemtl wood\nv -2 3 4\nv -1 3 4\nv -2 4 4\nvt 0.2 0.7\nvn 0 0 1\nf 1/1/1 2/1/1 3/1/1\n')
            mesh, materials, stamp = decode(dict(file=str(obj), root=str(root), sha256=hashlib.sha256(obj.read_bytes()).hexdigest()))
            self.assertEqual(mesh['vertices'][0][:6], [-200, 300, 400, 0, 0, 1])
            self.assertAlmostEqual(mesh['vertices'][0][7], .3)
            a, b, c = mesh['vertices']
            self.assertGreater((c[0]-a[0])*(b[1]-a[1])-(c[1]-a[1])*(b[0]-a[0]), 0)
            self.assertEqual(materials[0]['color'], [.2, .3, .4])
            self.assertFalse(materials[0]['masked'])

    def test_structural_assembly_rejected_before_resource_io(self):
        with tempfile.TemporaryDirectory() as folder:
            manifest = Path(folder)/'assets.json'
            manifest.write_text(json.dumps(dict(schema='heyzed:assets/1', assets=[dict(
                directory='absent', id='example', author='author', allowRepack=True,
                variants=dict(main=dict(file='missing.obj', bindings=[dict(parts=[
                    dict(tile='chair', x=0, y=0, z=0), dict(tile='fence', x=1, y=0, z=0)])])))])))
            selected, rejected = bindings(manifest, dict(chair={}, fence=dict(HoppableW='')))
            self.assertFalse(selected)
            self.assertEqual(rejected['structural_or_unknown'], 1)

    def test_nonstandard_stair_parts_and_railings_stay_construction(self):
        self.assertTrue(structural('fixtures_escalators_01_6', {}))
        self.assertTrue(structural('fixtures_railings_01_15', {}))
        self.assertTrue(structural('custom_stair', dict(stairsTN='')))
        self.assertFalse(structural('furniture_seating_indoor_01_0', dict(solid='')))

    def test_model_dependencies_cannot_escape_card(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)/'card'; root.mkdir()
            (Path(folder)/'outside.mtl').write_text('newmtl x\n')
            obj = root/'mesh.obj'; obj.write_text('mtllib ../outside.mtl\n')
            with self.assertRaisesRegex(ValueError, 'escaping'):
                decode(dict(file=str(obj), root=str(root), sha256=hashlib.sha256(obj.read_bytes()).hexdigest()))


if __name__ == '__main__': unittest.main()
