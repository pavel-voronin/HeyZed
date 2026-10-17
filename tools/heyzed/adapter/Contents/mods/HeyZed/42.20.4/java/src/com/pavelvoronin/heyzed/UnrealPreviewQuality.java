package com.pavelvoronin.heyzed;

import me.zed_0xff.zombie_buddy.Patch;
import zombie.core.Core;
import zombie.core.PerformanceSettings;

public final class UnrealPreviewQuality {
    private static boolean applied;
    private static boolean previewWorld;
    private static zombie.iso.IsoWorld configuredWorld;
    private static boolean compositing;

    public static void applyWorld() {
        if(!applied || zombie.iso.IsoWorld.instance==null || zombie.iso.IsoWorld.instance==configuredWorld)return;
        configuredWorld=zombie.iso.IsoWorld.instance;
        previewWorld=Boolean.parseBoolean(System.getProperty("heyzed.unreal.previewWorld","false"));
        configuredWorld.setDrawWorld(previewWorld);
        System.out.println("[heyzed] Isolated Unreal backend native world raster="+previewWorld+"; native simulation and render context retained");
    }

    public static boolean backendOnly(){return applied&&!previewWorld&&configuredWorld!=null&&zombie.GameWindow.isIngameState();}
    public static void beginComposite(){compositing=backendOnly();}
    public static void endComposite(){compositing=false;}
    public static boolean composite(zombie.core.textures.Texture texture){
        if(!compositing||zombie.ui.UIManager.uiFbo==null||texture!=zombie.ui.UIManager.uiFbo.getTexture())return false;
        drawStatus(false);return true;
    }
    public static boolean suppressNativeDebug(Object value){return compositing&&value instanceof imgui.ImDrawData;}
    public static boolean suppressNativeCursor(){return compositing;}
    public static void drawStatus(boolean offscreen) {
        Core core=Core.getInstance();
        zombie.core.SpriteRenderer.instance.setDoAdditive(false);
        zombie.IndieGL.enableBlend();
        zombie.IndieGL.disableDepthTest();
        if(offscreen) {
            zombie.core.SpriteRenderer.instance.setDefaultStyle(zombie.core.Styles.UIFBOStyle.instance);
            zombie.IndieGL.glBlendFuncSeparate(770,771,1,771);
        } else {zombie.core.SpriteRenderer.instance.setDefaultStyle(zombie.core.Styles.TransparentStyle.instance);zombie.IndieGL.glBlendFunc(770,771);}
        try {
            zombie.core.SpriteRenderer.instance.render(null,0,0,core.getScreenWidth(),core.getScreenHeight(),0.035f,0.035f,0.035f,1f,null);
            zombie.ui.TextManager.instance.DrawStringCentre(zombie.ui.UIFont.Medium,
                    core.getScreenWidth()/2.0,core.getScreenHeight()/2.0,
                    zombie.core.Translator.getText("UI_heyzed_BackendActive"),0.85,0.85,0.85,1.0);
        } finally {
            if(offscreen) {
                zombie.core.SpriteRenderer.instance.setDefaultStyle(zombie.core.Styles.TransparentStyle.instance);
                zombie.IndieGL.glBlendFunc(770,771);
            }
        }
    }
    public static boolean renderBackendStatus() {
        if(!backendOnly()||UnrealUI.hasConsumer())return false;
        if(zombie.ui.UIManager.useUiFbo&&!Core.getInstance().uiRenderThisFrame)return true;
        drawStatus(zombie.ui.UIManager.useUiFbo);return true;
    }

    @Patch(className="zombie.core.Core",methodName="EndFrameUI",warmUp=true)
    public static class CompositeScope {
        @Patch.OnEnter public static void enter(){UnrealPreviewQuality.beginComposite();}
        @Patch.OnExit public static void exit(){UnrealPreviewQuality.endComposite();}
    }
    @Patch(className="zombie.core.SpriteRenderer",methodName="renderi",warmUp=true)
    public static class Composite {
        @Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.Argument(0) zombie.core.textures.Texture texture){return UnrealPreviewQuality.composite(texture);}
    }
    @Patch(className="zombie.core.SpriteRenderer",methodName="render",warmUp=true)
    public static class NativeDebug {
        @Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.Argument(0) Object value){return UnrealPreviewQuality.suppressNativeDebug(value);}
    }
    @Patch(className="zombie.input.Mouse",methodName="renderCursorTexture",warmUp=true)
    public static class NativeCursor {
        @Patch.OnEnter(skipOn=true) public static boolean enter(){return UnrealPreviewQuality.suppressNativeCursor();}
    }

    @Patch(className="zombie.ui.UIManager",methodName="render",warmUp=true)
    public static class BackendStatus {
        @Patch.OnEnter(skipOn=true) public static boolean enter(){return UnrealPreviewQuality.renderBackendStatus();}
    }

    public static void maintainWorldResources(zombie.iso.IsoWorld world) {
        if(!applied || world!=configuredWorld || previewWorld)return;
        try {
            world.sceneCullZombies();
            world.sceneCullAnimals();
        } catch(Exception error) {zombie.core.logger.ExceptionLogger.logException(error);}
    }

    @Patch(className="zombie.iso.IsoWorld",methodName="render",warmUp=true)
    public static class WorldResources {
        @Patch.OnEnter public static void enter(@Patch.This zombie.iso.IsoWorld world) {
            UnrealPreviewQuality.maintainWorldResources(world);
        }
    }

    public static void apply() {
        if(applied || Integer.getInteger("heyzed.unreal.port",0)==0
                || System.getProperty("modstudio.scenario","").isEmpty()
                || !Boolean.parseBoolean(System.getProperty("heyzed.unreal.lowPreviewQuality","true")))return;
        String cache=zombie.ZomboidFileSystem.instance.getCacheDir();
        if(cache==null || !java.nio.file.Files.isRegularFile(java.nio.file.Path.of(cache,"profile-owner.json")))return;
        Core core=Core.getInstance();
        core.setOptionVSync(false);
        core.setOptionUIFBO(true);
        core.setOptionUIRenderFPS(10);
        core.setPerfSkybox(2);
        core.setPerfPuddles(3);
        core.setPerfReflections(false);
        core.setOptionRenderPrecipitation(3);
        core.setOptionCorpseShadows(false);
        core.setOptionBloodDecals(0);
        core.setOptionDoVideoEffects(false);
        core.setOptionDoWindSpriteEffects(false);
        core.setOptionDoDoorSpriteEffects(false);
        core.setOption3DGroundItem(false);
        core.setOptionHighResPlacedItems(false);
        boolean previewSprites=Boolean.parseBoolean(System.getProperty("heyzed.unreal.previewSprites","true"));
        zombie.debug.DebugOptions.instance.isoSprite.renderSprites.setValue(previewSprites);
        boolean previewModels=Boolean.parseBoolean(System.getProperty("heyzed.unreal.previewModels","false"));
        zombie.debug.DebugOptions.instance.isoSprite.renderModels.setValue(previewModels);
        PerformanceSettings.instance.setWaterQuality(2);
        PerformanceSettings.instance.setPuddlesQuality(2);
        PerformanceSettings.instance.setFogQuality(2);
        applied=true;
        System.out.println("[heyzed] Low-quality native preview enabled for isolated Unreal bridge session; native character raster="+previewModels+"; native sprite raster="+previewSprites);
    }

    @Patch(className="zombie.core.Core",methodName="loadOptions",warmUp=true)
    public static class LoadedOptions {
        @Patch.OnExit public static void exit(){UnrealPreviewQuality.apply();}
    }

    private UnrealPreviewQuality(){}
}
