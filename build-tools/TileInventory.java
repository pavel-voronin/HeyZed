import java.io.*;
import java.nio.file.*;
import java.util.*;
import zombie.iso.sprite.*;

                                                                                   
public final class TileInventory {
    static final String[] FILES={"newtiledefinitions.tiles","tiledefinitions_erosion.tiles","tiledefinitions_overlays.tiles","tiledefinitions_b42chunkcaching.tiles","tiledefinitions_noiseworks.patch.tiles","jumbo_trees_big.tiles","jumbo_trees.tiles"};
    static final int[] IDS={1,2,4,5,-1,6,8};
    static Object describe(Object value,int depth)throws Exception {
        if(value==null||value instanceof String||value instanceof Number||value instanceof Boolean)return value;
        if(depth>12)return "depth-limit";
        if(value instanceof se.krka.kahlua.vm.KahluaTable table){var result=new TreeMap<String,Object>();var it=table.iterator();while(it.advance())result.put(it.getKey().toString(),describe(it.getValue(),depth+1));return result;}
        if(value.getClass().isArray()){var list=new ArrayList<Object>();for(int i=0;i<java.lang.reflect.Array.getLength(value);i++)list.add(describe(java.lang.reflect.Array.get(value,i),depth+1));return list;}
        if(value instanceof Iterable<?> values){var list=new ArrayList<Object>();for(var v:values)list.add(describe(v,depth+1));return list;}
        if(value instanceof Map<?,?> values){var map=new TreeMap<String,Object>();for(var e:values.entrySet())map.put(e.getKey().toString(),describe(e.getValue(),depth+1));return map;}
        if(value instanceof IsoSprite sprite)return sprite.getName();
        if(value instanceof IsoSpriteInstance sprite)return sprite.getParentSprite().getName();
        if(!value.getClass().getName().startsWith("zombie.erosion.obj."))return value.getClass().getName();
        var result=new TreeMap<String,Object>();
        for(var field:value.getClass().getDeclaredFields())if(!java.lang.reflect.Modifier.isStatic(field.getModifiers())){field.setAccessible(true);result.put(field.getName(),describe(field.get(value),depth+1));}
        return result;
    }
    static Object block(zombie.scripting.ScriptParser.Block block){var row=new TreeMap<String,Object>();row.put("type",block.type);row.put("id",block.id);row.put("values",block.values.stream().map(v->List.of(v.getKey().trim(),v.getValue().trim())).toList());row.put("children",block.children.stream().map(TileInventory::block).toList());return row;}
    static void scriptData(Path game,Path output)throws Exception {
        try(var writer=Files.newBufferedWriter(output.resolveSibling("native-geometry.jsonl"))){
            var root=zombie.scripting.ScriptParser.parse(zombie.scripting.ScriptParser.stripComments(Files.readString(game.resolve("media/tileGeometry.txt"))));
            for(var file:root.children)for(var tileset:file.children)if("tileset".equals(tileset.type))for(var tile:tileset.children)if("tile".equals(tile.type)){
                var xy=tile.getValue("xy").getValue().trim().split("x");String name=tileset.getValue("name").getValue().trim()+"_"+(Integer.parseInt(xy[0])+8*Integer.parseInt(xy[1]));
                writer.write(json(Map.of("name",name,"source","media/tileGeometry.txt","coordinateEncoding","native VERSION=2 integer coordinates; see TileGeometryFile","definition",block(tile))));writer.newLine();
            }
        }
        try(var writer=Files.newBufferedWriter(output.resolveSibling("native-sprite-configs.jsonl"));var paths=Files.walk(game.resolve("media/scripts"))){
            for(var path:paths.filter(p->p.toString().endsWith(".txt")).sorted().toList()){
                String text=Files.readString(path);if(!text.contains("component SpriteConfig"))continue;
                var root=zombie.scripting.ScriptParser.parse(zombie.scripting.ScriptParser.stripComments(text));
                for(var module:root.children)for(var entity:module.children)for(var component:entity.children)if("component".equals(component.type)&&"SpriteConfig".equals(component.id)){
                    writer.write(json(Map.of("source",game.relativize(path).toString().replace('\\','/'),"entity",module.id+"."+entity.id,"config",block(component))));writer.newLine();
                }
            }
        }
        var platform=se.krka.kahlua.j2se.J2SEPlatform.getInstance();var env=platform.newEnvironment();var thread=new se.krka.kahlua.vm.KahluaThread(platform,env);thread.debugOwnerThread=Thread.currentThread();
        String setup="""
            TILEZED=true
            require=function() end
            captured={}
            local api={}
            function api:addFenceTiles(a,b) captured.fences=b or a end
            function api:addBrokenTiles(a) captured.broken=a end
            function api:addDebrisTiles(a) captured.debris=a end
            BentFences={getInstance=function() return api end}
            BrokenFences={getInstance=function() return api end}
            getBrokenFences=function() return api end
            function resetFarming() farming_vegetableconf=setmetatable({}, {__index=function(t,k) local v={}; rawset(t,k,v); return v end}) end
            resetFarming()
            """;
        thread.call(se.krka.kahlua.luaj.compiler.LuaCompiler.loadstring(setup,"inventory",env),null,null,null);
        try(var writer=Files.newBufferedWriter(output.resolveSibling("native-lua-tables.jsonl"));var paths=Files.walk(game.resolve("media/lua/server"))){
            for(var path:paths.filter(p->p.toString().endsWith(".lua")).sorted().toList()){
                String name=path.getFileName().toString();if(!(name.endsWith("Overlays.lua")||name.equals("TileOverlaysSmooth.lua")||name.equals("WorldFiller.lua")||name.equals("BrokenFences.lua")||name.equals("BentFences.lua")||name.endsWith("_sprites.lua")&&path.toString().contains("Farming")))continue;
                thread.call(se.krka.kahlua.luaj.compiler.LuaCompiler.loadstring("captured={}; resetFarming()", "reset",env),null,null,null);
                var result=thread.call(se.krka.kahlua.luaj.compiler.LuaCompiler.loadstring(Files.readString(path),name,env),null,null,null);
                var row=new TreeMap<String,Object>();row.put("source",game.relativize(path).toString().replace('\\','/'));row.put("returned",describe(result,0));row.put("captured",describe(env.rawget("captured"),0));row.put("farming",describe(env.rawget("farming_vegetableconf"),0));writer.write(json(row));writer.newLine();
            }
        }
    }
    static String json(Object value) {
        if(value==null)return "null";
        if(value instanceof Number||value instanceof Boolean)return value.toString();
        if(value instanceof Map<?,?> map){var parts=new ArrayList<String>();for(var e:map.entrySet())parts.add(json(e.getKey().toString())+":"+json(e.getValue()));return "{"+String.join(",",parts)+"}";}
        if(value instanceof Iterable<?> list){var parts=new ArrayList<String>();for(var e:list)parts.add(json(e));return "["+String.join(",",parts)+"]";}
        var out=new StringBuilder("\"");for(char c:value.toString().toCharArray())switch(c){case '"'->out.append("\\\"");case '\\'->out.append("\\\\");case '\n'->out.append("\\n");case '\r'->out.append("\\r");case '\t'->out.append("\\t");default->{if(c<32)out.append(String.format("\\u%04x",(int)c));else out.append(c);}}return out.append('"').toString();
    }
    public static void main(String[] args)throws Exception {
        if(args.length!=3)throw new IllegalArgumentException("game-directory output-jsonl isolated-cache-directory");
        Path game=Path.of(args[0]),output=Path.of(args[1]);
        Files.createDirectories(output.toAbsolutePath().getParent());
        zombie.network.GameServer.server=true;
        zombie.ZomboidFileSystem.instance.setCacheDir(args[2]);
        zombie.core.Core.tileScale=2;
        zombie.ZomboidFileSystem.instance.base.set(game.toFile());
        var workdir=zombie.ZomboidFileSystem.class.getDeclaredField("workdir");workdir.setAccessible(true);
        ((zombie.ZomboidFileSystem.PZFolder)workdir.get(zombie.ZomboidFileSystem.instance)).set(game.resolve("media").toFile());
        var world=zombie.iso.IsoWorld.instance;var manager=IsoSpriteManager.instance;
        for(int i=0;i<FILES.length;i++)world.LoadTileDefinitionsPropertyStrings(manager,game.resolve("media").resolve(FILES[i]).toString(),IDS[i]);
        var custom=world.getClass().getDeclaredMethod("SetCustomPropertyValues");custom.setAccessible(true);custom.invoke(world);
        zombie.core.TilePropertyAliasMap.instance.Generate(zombie.iso.IsoWorld.PropertyValueMap);
        for(int i=0;i<FILES.length;i++)world.LoadTileDefinitions(manager,game.resolve("media").resolve(FILES[i]).toString(),IDS[i]);
        var fakeTree=world.getClass().getDeclaredMethod("registerFakeJumboTree",IsoSpriteManager.class,int.class);fakeTree.setAccessible(true);fakeTree.invoke(world,manager,7);
        manager.AddSprite("media/ui/missing-tile.png");
                                                                                               
        var base=new zombie.scripting.objects.ScriptModule();base.name="Base";
        zombie.scripting.ScriptManager.instance.moduleMap.put("Base",base);
        zombie.scripting.ScriptManager.instance.moduleList.add(base);
        var buckets=zombie.scripting.ScriptManager.class.getDeclaredField("bucketCollectionList");buckets.setAccessible(true);
        for(var bucket:(Iterable<?>)buckets.get(zombie.scripting.ScriptManager.instance))((zombie.scripting.ScriptBucketCollection<?>)bucket).registerModule(base);
        var models=new zombie.iso.SpriteModelsFile();var read=models.getClass().getDeclaredMethod("parseFile",String.class);read.setAccessible(true);read.invoke(models,Files.readString(game.resolve("media/spriteModels.txt")));
        var sets=models.getClass().getDeclaredField("tilesets");sets.setAccessible(true);
        for(var tileset:(Iterable<?>)sets.get(models)){var init=tileset.getClass().getDeclaredMethod("initSprites");init.setAccessible(true);init.invoke(tileset);}
        var ice=new zombie.erosion.season.ErosionIceQueen(manager);
        zombie.erosion.ErosionRegions.init();ice.setSnow(true);
        try(var writer=Files.newBufferedWriter(output.resolveSibling("native-erosion.jsonl"))){
            for(var region:zombie.erosion.ErosionRegions.regions)for(var category:region.categories){
                var record=new TreeMap<String,Object>();record.put("category",category.getClass().getName());record.put("region",region.id);
                for(var field:category.getClass().getDeclaredFields())if(field.getName().equals("objs")||field.getName().equals("objsRef")){field.setAccessible(true);record.put(field.getName(),describe(field.get(category),0));}
                writer.write(json(record));writer.newLine();
            }
        }
        Files.createDirectories(output.toAbsolutePath().getParent());
        var spriteNames=new IdentityHashMap<IsoSprite,String>();for(var entry:new TreeMap<>(manager.namedMap).entrySet())spriteNames.putIfAbsent(entry.getValue(),entry.getKey());
        try(var writer=Files.newBufferedWriter(output)){
            for(var e:new TreeMap<>(manager.namedMap).entrySet()){
                var s=e.getValue();var p=s.getProperties();var row=new TreeMap<String,Object>();
                row.put("name",e.getKey());row.put("id",s.id);row.put("type",s.getTileType().toString());
                var props=new TreeMap<String,String>();for(var key:p.getPropertyNames())props.put(key,p.get(key));row.put("properties",props);
                row.put("flags",p.getFlagsList().stream().map(Object::toString).sorted().toList());
                row.put("burntTile",s.burntTile);row.put("invisible",s.invisible);
                if(s.getSnowSprite()!=null)row.put("snowSprite",spriteNames.get(s.getSnowSprite()));
                var grid=s.getSpriteGrid();if(grid!=null){var g=new TreeMap<String,Object>();g.put("anchor",grid.getAnchorSprite()==null?null:grid.getAnchorSprite().getName());g.put("size",List.of(grid.getWidth(),grid.getHeight(),grid.getLevels()));g.put("position",List.of(grid.getSpriteGridPosX(s),grid.getSpriteGridPosY(s),grid.getSpriteGridPosZ(s)));var cells=new ArrayList<Object>();for(int z=0;z<grid.getLevels();z++)for(int y=0;y<grid.getHeight();y++)for(int x=0;x<grid.getWidth();x++){var part=grid.getSprite(x,y,z);if(part!=null)cells.add(List.of(part.getName(),x,y,z));}g.put("cells",cells);row.put("grid",g);}
                var model=s.spriteModel;if(model!=null){var m=new TreeMap<String,Object>();m.put("model",model.modelScriptName);m.put("runtime",model.runtimeString);m.put("texture",model.textureName);m.put("translation",List.of(model.translate.x,model.translate.y,model.translate.z));m.put("rotation",List.of(model.rotate.x,model.rotate.y,model.rotate.z));m.put("scale",model.scale);m.put("animation",model.animationName);m.put("animationTime",model.animationTime);row.put("nativeModel",m);}
                writer.write(json(row));writer.newLine();
            }
        }
        System.out.println("Native inventory: "+manager.namedMap.size()+" sprites");
        scriptData(game,output);
        Files.writeString(output.resolveSibling("native-probe.json"),json(Map.of("gameVersion",zombie.core.Core.getInstance().getVersion(),"phases",List.of("vanilla tile definitions in IsoWorld order","native spriteModels.txt with Base script buckets","ErosionIceQueen and ErosionRegions registration","SpriteConfig blocks parsed by ScriptParser","17 declarative Lua tables in isolated Kahlua"),"notRun",List.of("save loading","mods","GL texture decoding","full ScriptManager.Load","full IsoWorld.loadedTileDefinitions","gameplay updates")))+"\n");
    }
}
