from collections import defaultdict
import hashlib
import io
import json
from pathlib import Path
import os
from PIL import Image


def visible_images(game, rows, local, catalog_id):
    cache = local / 'visible-images.json'
    revision = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    if cache.exists():
        data = json.loads(cache.read_text('utf-8'))
        if data.get('schema') == 1 and data.get('catalog') == catalog_id and data.get('revision') == revision:
            return data
    visible, frames, errors = set(), {}, []
    pages = defaultdict(list)
    for row in rows:
        if row['kind'] == 'sprite':
            pages[(row['source'], row['imageOffset'], row['imageLength'])].append(row)
        elif row['kind'] == 'file' and row.get('preview'):
            try:
                with Image.open(game / row['source']) as image:
                    for frame in range(getattr(image, 'n_frames', 1)):
                        image.seek(frame)
                        if image.convert('RGBA').getchannel('A').getbbox():
                            visible.add(row['id'])
                            frames[row['id']] = frame
                            break
            except Exception as error:
                errors.append(dict(source=row['source'], error=str(error)))
    for (source, offset, length), entries in pages.items():
        try:
            with (game / source).open('rb') as stream:
                stream.seek(offset)
                with Image.open(io.BytesIO(stream.read(length))) as image:
                    alpha = image.convert('RGBA').getchannel('A')
            for row in entries:
                x, y, width, height = row['rect']
                if min(x, y, width, height) < 0 or x + width > alpha.width or y + height > alpha.height:
                    errors.append(dict(source=source, resource=row['id'], error='Invalid raster rectangle'))
                    continue
                if width and height and alpha.crop((x, y, x + width, y + height)).getbbox():
                    visible.add(row['id'])
        except Exception as error:
            errors.append(dict(source=source, pageOffset=offset, error=str(error)))
    tile_images = {}
    for row in rows:
        if row['kind'] != 'tile':
            continue
        image = next((identity for identity in [row.get('image'), *row.get('variants', [])] if identity in visible), None)
        if image:
            visible.add(row['id'])
            tile_images[row['id']] = image
    data = dict(schema=1, catalog=catalog_id, revision=revision, ids=sorted(visible), frames=frames, tileImages=tile_images, errors=errors)
    temporary = cache.with_name(cache.name + '.' + os.urandom(6).hex() + '.tmp')
    temporary.write_text(json.dumps(data, separators=(',', ':')), encoding='utf-8')
    temporary.replace(cache)
    return data
