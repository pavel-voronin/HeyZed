import json
import tempfile
import unittest
from pathlib import Path
from prepare_furniture_catalog import prepare, furniture_domain
from publish_furniture_catalog import publish


class FurnitureCatalogContract(unittest.TestCase):
    def test_catalog_needs_no_world_and_preserves_author_transform(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); (root/'chair.obj').write_text('resource')
            transform = dict(position=[1, 2, 3], rotation=[10, 20, 30], scale=[2, 3, 4])
            pack = root/'assets.json'
            pack.write_text(json.dumps(dict(schema='heyzed:assets/1', assets=[dict(
                id='card', directory='.', author='author', allowRepack=True, variants=dict(
                    main=dict(file='chair.obj', bindings=[dict(tile='chair', placement=transform)])))])))
            source = prepare(root/'prepared', pack, dict(chair={}))
            data = json.loads(source.read_text())
            self.assertEqual(data['bindings']['chair'][0]['placement'], transform)
            self.assertEqual(len(data['models']), 1)
            self.assertFalse(set(data) & {'objects', 'footprint', 'regions', 'bounds', 'identity'})
            target = publish(source, root/'published')
            runtime = json.loads(target.read_text())
            self.assertEqual(set(runtime), {'schema', 'packSha256', 'tiles'})
            self.assertEqual(runtime['tiles'], ['chair'])
            self.assertEqual(list((root/'published').iterdir()), [target])

    def test_ground_and_vegetation_stay_out(self):
        for tile, props in [('blends_natural_02_0', {}), ('vegetation_ornamental_01_0', {}),
                            ('industry_railroad_01_0', {}), ('custom_tree', {'tree': '2'})]:
            self.assertFalse(furniture_domain(tile, props))
        self.assertTrue(furniture_domain('furniture_seating_indoor_01_0', {}))
        self.assertTrue(furniture_domain('floors_rugs_01_0', {}))

    def test_baked_placement_catalog_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/'legacy.json'; source.write_text('{"schema":2,"regions":[]}')
            with self.assertRaisesRegex(ValueError, 'Resource-only'):
                publish(source, Path(folder)/'published')


if __name__ == '__main__': unittest.main()
