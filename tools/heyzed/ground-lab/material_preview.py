"""World-space base-color preview from the Unreal surface recipes and source maps."""
import ast
from functools import lru_cache
import io
import json
from pathlib import Path

import numpy as np
from PIL import Image


class Materials:
    def __init__(self, bridge):
        self.root = bridge / 'materials'
        tree = ast.parse((bridge / 'prepare_assets.py').read_text('utf-8'))
        styles = next(ast.literal_eval(n.value) for n in ast.walk(tree) if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'styles' for t in n.targets))
        sources = {a['id']: a for a in json.loads((self.root / 'sources.json').read_text('utf-8'))['assets']}
        self.recipes = {}
        for name, (source, tint) in styles.items():
            asset = sources[source]
            file = self.root / asset['maps']['diff']['file']
            if name != 'Unknown' and file.exists():
                self.recipes[name] = dict(file=file, width=asset['widthMeters'], tint=tint)

    @lru_cache(maxsize=12)
    def texture(self, file):
        image = np.asarray(Image.open(file).convert('RGB'), dtype=np.float32) / 255
        return np.where(image <= .04045, image/12.92, ((image+.055)/1.055)**2.4)

    @lru_cache(maxsize=24)
    def image(self, name, x, y):
        recipe = self.recipes[name]
        size = 512
        xx, yy = np.meshgrid((np.arange(size, dtype=np.float32)+.5)*3/size+x, (np.arange(size, dtype=np.float32)+.5)*3/size+y)
        xx, yy = xx*100, yy*100
        def frac(v):
            return v-np.floor(v)
        def noise(px, py):
            ix, iy = np.floor(px), np.floor(py)
            fx, fy = frac(px), frac(py)
            fx, fy = fx*fx*(3-2*fx), fy*fy*(3-2*fy)
            def h(a,b):
                return frac(np.sin(a*127.1+b*311.7)*43758.5453)
            return (h(ix,iy)*(1-fx)+h(ix+1,iy)*fx)*(1-fy)+(h(ix,iy+1)*(1-fx)+h(ix+1,iy+1)*fx)*fy
        tex = self.texture(recipe['file'])
        def sample(u,v):
            h,w = tex.shape[:2]
            px,py = frac(u)*w-.5,frac(v)*h-.5
            ax,ay = np.floor(px).astype(int),np.floor(py).astype(int)
            fx,fy = (px-ax)[...,None],(py-ay)[...,None]
            return (tex[ay%h,ax%w]*(1-fx)+tex[ay%h,(ax+1)%w]*fx)*(1-fy)+(tex[(ay+1)%h,ax%w]*(1-fx)+tex[(ay+1)%h,(ax+1)%w]*fx)*fy
        u,v = xx/(recipe['width']*100),yy/(recipe['width']*100)
        variation = noise(xx*.0009, yy*.0009)[...,None]
        grain = sample(u,v)*(1-variation)+sample(u*1.37+7.31,v*1.37+3.17)*variation
        luminance = np.sum(grain*np.array([.2126,.7152,.0722]), axis=2)[...,None]
        tint = np.array(recipe['tint'])
        base = luminance if name in ('Road_04','Gravel') else grain
        color = base*tint*(.82+.30*noise(xx*.00025,yy*.00025)[...,None])
        if name.startswith('Paint'):
            wear = np.clip((noise(xx*.18,yy*.18)*.68+noise(xx*.037,yy*.037)*.32-.23)/.24,0,1)
            wear = (wear*wear*(3-2*wear))[...,None]
            color = grain*.55*(1-wear)+tint*(.9+.15*np.clip(luminance,0,1))*wear
        color = np.clip(color,0,1)
        srgb = np.where(color <= .0031308,color*12.92,1.055*color**(1/2.4)-.055)
        output = io.BytesIO()
        Image.fromarray(np.uint8(np.clip(srgb*255,0,255))).save(output,format='PNG')
        return output.getvalue()
