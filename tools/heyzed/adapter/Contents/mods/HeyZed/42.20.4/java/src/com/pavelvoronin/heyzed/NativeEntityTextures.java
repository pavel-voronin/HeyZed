package com.pavelvoronin.heyzed;
import java.nio.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import org.json.*;
import org.lwjgl.opengl.*;
import org.lwjgl.system.MemoryUtil;
import zombie.core.textures.Texture;
import zombie.core.textures.SmartTexture;
import zombie.core.skinnedmodel.model.VehicleModelInstance;

/** GL readback is once per changed appearance; image composition/encoding belongs to the worker. */
final class NativeEntityTextures {
    static final class Image {
        final int width,height;final byte[] rgba;Car car;String digest;Image finished;
        Image(int w,int h,byte[] rgba){width=w;height=h;this.rgba=rgba;}
    }
    private record Car(Image[] layers,float[] paint,float rust,float[][] zones,boolean random){}
    private static final LinkedHashMap<Texture,Image> readCache=new LinkedHashMap<>(32,.75f,true);
    private static long readCacheBytes;
    static boolean characterPlaceholder(Image image){
        if(image.width<=2||image.height<=2)return true;
        boolean opaque=false;
        for(int index=0;index<image.rgba.length;index+=4)if((image.rgba[index+3]&255)>=128){opaque=true;if((image.rgba[index]&255)<250||(image.rgba[index+1]&255)<250||(image.rgba[index+2]&255)<250)return false;}
        return opaque;
    }
    static boolean hasVisiblePixels(Image image){
        for(int index=3;index<image.rgba.length;index+=4)if((image.rgba[index]&255)>=128)return true;
        return false;
    }
    static Image read(Texture texture){
        boolean reusable=!(texture instanceof SmartTexture);Image cached=reusable?readCache.get(texture):null;if(cached!=null){if(!texture.isDestroyed())return cached;readCache.remove(texture);readCacheBytes-=cached.rgba.length;}
        int previousActive=GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);GL13.glActiveTexture(GL13.GL_TEXTURE0);
        int packBuffer=GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,0);
        int previousBinding=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D),alignment=GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT),rowLength=GL11.glGetInteger(GL11.GL_PACK_ROW_LENGTH),skipPixels=GL11.glGetInteger(GL11.GL_PACK_SKIP_PIXELS),skipRows=GL11.glGetInteger(GL11.GL_PACK_SKIP_ROWS);
        ByteBuffer pixels=null;
        try{
            int id=texture.getID();if(id<=0)throw new IllegalStateException("Native texture has no GPU image");
            Texture resolved=texture;
            while(resolved instanceof SmartTexture smart){resolved=smart.result;if(resolved==null)throw new IllegalStateException("Native composed entity texture not ready");}
            int w=resolved.getWidth(),h=resolved.getHeight(),hw=resolved.getWidthHW(),hh=resolved.getHeightHW();
            if(w<=0||h<=0||hw<w||hh<h||hw>8192||hh>8192)throw new IllegalArgumentException("Invalid native entity texture size");
            pixels=MemoryUtil.memAlloc(hw*hh*4);GL13.glActiveTexture(GL13.GL_TEXTURE0);GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,0);GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,0);GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,0);GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,1);GL11.glBindTexture(GL11.GL_TEXTURE_2D,id);GL11.glGetTexImage(GL11.GL_TEXTURE_2D,0,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,pixels);
            byte[] rgba=new byte[w*h*4];for(int y=0;y<h;y++){pixels.position(y*hw*4);pixels.get(rgba,y*w*4,w*4);}Image image=new Image(w,h,rgba);
            if(reusable&&rgba.length<=64*1024*1024){readCache.put(texture,image);readCacheBytes+=rgba.length;while(readCacheBytes>64L*1024*1024||readCache.size()>256){var first=readCache.entrySet().iterator();var oldest=first.next();readCacheBytes-=oldest.getValue().rgba.length;first.remove();}}
            return image;
        }finally{if(pixels!=null)MemoryUtil.memFree(pixels);GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,alignment);GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,rowLength);GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,skipPixels);GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,skipRows);GL11.glBindTexture(GL11.GL_TEXTURE_2D,previousBinding);GL13.glActiveTexture(previousActive);GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,packBuffer);}
    }
    static float[] zones(float[] a,float[] b){float[] r=new float[27];for(int i=0;i<27;i++){int n=i%16;r[i]=(i<16?a:b)[n/4+n%4*4];}return r;}
    static Image vehicle(VehicleModelInstance v,Image base){
        Texture[] textures={v.textureMask,v.textureRust,v.textureLights,v.textureDamage1Overlay,v.textureDamage1Shell,v.textureDamage2Overlay,v.textureDamage2Shell};Image[] images=new Image[7];
        for(int i=0;i<7;i++)if(textures[i]!=null){if(!textures[i].isReady())throw new IllegalStateException("Native vehicle appearance map not ready");images[i]=read(textures[i]);}
        float[][] z={zones(v.textureUninstall1,v.textureUninstall2),zones(v.textureDamage1Enables1,v.textureDamage1Enables2),zones(v.textureDamage2Enables1,v.textureDamage2Enables2),zones(v.matrixBlood1Enables1,v.matrixBlood1Enables2),zones(v.matrixBlood2Enables1,v.matrixBlood2Enables2)};
        base=new Image(base.width,base.height,base.rgba);base.car=new Car(images,new float[]{v.painColor.x,v.painColor.y,v.painColor.z},v.textureRustA,z,v.model.effect==null||!v.model.effect.getName().contains("norandom"));return base;
    }
    static final float[][] PALETTE={{1,0,0},{0,1,0},{0,1,1},{1,1,0},{1,0,1},{0,0,1},{0,.5f,.5f},{.5f,.5f,0},{.5f,0,.5f},{0,0,.5f},{.5f,0,0},{0,.5f,0},{0,.75f,.75f},{.75f,.75f,0},{.75f,0,.75f},{0,0,.75f},{0,0,0},{.25f,0,0},{.75f,0,0},{0,.75f,0},{0,.25f,0},{.5f,.25f,0},{.5f,.75f,0},{.75f,.75f,.75f},{.25f,.25f,.25f},{1,0,.5f},{0,1,.5f}};
    static float sample(Image image,int x,int y,int w,int h,int channel){if(image==null)return 0;int xx=Math.min(image.width-1,x*image.width/w),yy=Math.min(image.height-1,y*image.height/h);return (image.rgba[(yy*image.width+xx)*4+channel]&255)/255f;}
    static int zone(Image mask,int x,int y,int w,int h){if(mask==null)return -1;for(int n=0;n<PALETTE.length;n++){double d=0;for(int c=0;c<3;c++){float delta=sample(mask,x,y,w,h,c)-PALETTE[n][c];d+=delta*delta;}if(d<.018*.018)return n;}return -1;}
    static float clamp(float v){return Math.max(0,Math.min(1,v));}
    static void paint(float[] color,float[] modifier){float hi=Math.max(color[0],Math.max(color[1],color[2])),lo=Math.min(color[0],Math.min(color[1],color[2]));float saturation=clamp((hi>0?(hi-lo)/hi:0)+modifier[1]-.5f),value=clamp(hi+modifier[2]-.5f),h=modifier[0];for(int c=0;c<3;c++){float p=Math.abs(((h+(c==0?0:c==1?2f/3:1f/3))%1)*6-3);color[c]=value*((1-saturation)+saturation*clamp(p-1));}}
    static Image finish(Image image){
        Car car=image.car;if(car==null)return image;if(image.finished!=null)return image.finished;int w=image.width,h=image.height;byte[] out=image.rgba.clone();float[] color=new float[3],layer=new float[3],painted=new float[3];
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){
            int p=(y*w+x)*4;for(int c=0;c<3;c++)color[c]=(out[p+c]&255)/255f;
            if(car.random){System.arraycopy(color,0,painted,0,3);paint(painted,car.paint);float factor=1-(out[p+3]&255)/255f;for(int c=0;c<3;c++)color[c]+=factor*(painted[c]-color[c]);}
            Image mask=car.layers[0];int z=zone(mask,x,y,w,h);boolean glass=z>=6&&z<=11;
            float rust=sample(car.layers[1],x,y,w,h,3)*car.rust;for(int c=0;c<3;c++)color[c]+=rust*(sample(car.layers[1],x,y,w,h,c)-color[c]);
            if(z>=0){boolean unpainted=glass||z<2||(z>=17&&z<=22);
                for(int step=0;step<2;step++){Image damage=car.layers[step==0?4:6];for(int c=0;c<3;c++)layer[c]=sample(damage,x,y,w,h,c);if(car.random&&!unpainted)paint(layer,car.paint);float amount=sample(damage,x,y,w,h,3)*car.zones[step+1][z]*(unpainted?1:.75f);for(int c=0;c<3;c++)color[c]+=amount*(layer[c]-color[c]);}
                Image blood=car.layers[5],bloodMask=car.layers[3];float amount=clamp(car.zones[3][z]),ba=sample(blood,x,y,w,h,3),bm=sample(bloodMask,x,y,w,h,3);float coverage=(1-(float)Math.pow(1-ba,3))*(1-(float)Math.pow(1-bm,3));coverage=amount>.0001f?clamp((coverage-1+amount)/amount):0;float mix=coverage*ba*car.zones[4][z];for(int c=0;c<3;c++){color[c]+=mix*(sample(blood,x,y,w,h,c)*coverage-color[c]);color[c]+=car.zones[0][z]*(.2f-color[c]);}
            }
            for(int c=0;c<3;c++)out[p+c]=(byte)Math.round(clamp(color[c])*255);out[p+3]=(byte)(glass?Math.round(64*(1-clamp(car.zones[0][z]))):255);
        }
        image.finished=new Image(w,h,out);return image.finished;
    }
    static Image hue(Image image,float shift){
        if(shift==0)return image;byte[] rgba=image.rgba.clone();float[] hsv=new float[3];
        for(int p=0;p<rgba.length;p+=4){java.awt.Color.RGBtoHSB(rgba[p]&255,rgba[p+1]&255,rgba[p+2]&255,hsv);int rgb=java.awt.Color.HSBtoRGB(hsv[0]+shift,hsv[1],hsv[2]);rgba[p]=(byte)(rgb>>16);rgba[p+1]=(byte)(rgb>>8);rgba[p+2]=(byte)rgb;}
        return new Image(image.width,image.height,rgba);
    }
    static String hash(Image image)throws Exception{if(image.digest==null){MessageDigest md=MessageDigest.getInstance("SHA-256");md.update(ByteBuffer.allocate(8).putInt(image.width).putInt(image.height).array());image.digest=HexFormat.of().formatHex(md.digest(image.rgba));}return image.digest;}
    static void save(Image image,Path directory)throws Exception{
        String id=hash(image);Path path=directory.resolve(id+".png");if(Files.exists(path))return;BufferedImage png=new BufferedImage(image.width,image.height,BufferedImage.TYPE_INT_ARGB);int[] pixels=new int[image.width*image.height];
        for(int i=0;i<pixels.length;i++){int p=i*4;pixels[i]=((image.rgba[p+3]&255)<<24)|((image.rgba[p]&255)<<16)|((image.rgba[p+1]&255)<<8)|(image.rgba[p+2]&255);}png.setRGB(0,0,image.width,image.height,pixels,0,image.width);
        Path temporary=directory.resolve(id+".tmp");if(!ImageIO.write(png,"png",temporary.toFile()))throw new IllegalStateException("PNG encoder unavailable");Files.move(temporary,path,StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
    }
    static JSONObject nativeVehicle(Image image,Path directory)throws Exception{
        if(image.car==null)return null;Car car=image.car;JSONObject result=new JSONObject();
        save(image,directory);result.put("Paint",hash(image));
        String[] names={"Mask","Rust","Lamps","BloodMask","Damage1","Blood","Damage2"};
        for(int i=0;i<names.length;i++)if(car.layers[i]!=null){save(car.layers[i],directory);result.put(names[i],hash(car.layers[i]));}
        result.put("paint",new JSONArray(car.paint)).put("rust",car.rust).put("random",car.random);
        String[] zones={"Removed","Damage1Enabled","Damage2Enabled","BloodAmount","BloodEnabled"};
        for(int i=0;i<zones.length;i++)result.put(zones[i],new JSONArray(car.zones[i]));
        return result;
    }
    private NativeEntityTextures(){}
}
