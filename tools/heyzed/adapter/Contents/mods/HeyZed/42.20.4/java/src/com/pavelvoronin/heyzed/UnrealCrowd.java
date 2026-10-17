package com.pavelvoronin.heyzed;

import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import org.json.*;
import me.zed_0xff.zombie_buddy.Patch;
import zombie.characters.IsoPlayer;
import zombie.characters.IsoZombie;
import zombie.core.Core;

/** Presentation observations; virtual handles are not native entity identifiers. */
public final class UnrealCrowd {
    @Patch(className="zombie.popman.ZombiePopulationManager",methodName="updateMain",warmUp=true)
    public static class PopulationUpdate {
        @Patch.OnExit public static void exit(){UnrealCrowd.populationUpdated();}
    }
    static final double NEAR_IN=32,NEAR_OUT=40;
    private static final double VIRTUAL_RANGE=Double.parseDouble(System.getProperty("heyzed.unreal.crowd.virtualRange","800"));
    private record Observation(String id,long time,float x,float y,float z,double vx,double vy,float yaw,String action,int variant,boolean detail,boolean heightKnown,boolean virtual){
        JSONArray json(){return new JSONArray().put(id).put(time).put(x).put(y).put(z).put(vx).put(vy).put(yaw).put(action).put(variant).put(detail).put(heightKnown).put(virtual);}
    }
    private static final Map<String,Observation> observations=new LinkedHashMap<>(),lastQueued=new HashMap<>();
    private record Delta(long epoch,Observation point){}
    private static final ConcurrentHashMap<String,Delta> outgoing=new ConcurrentHashMap<>();
    private static final ConcurrentLinkedQueue<String> order=new ConcurrentLinkedQueue<>();
    private static final Set<String> queued=ConcurrentHashMap.newKeySet();
    private static volatile long generation;
    private record PopulationInput(long epoch,long time,float x,float y,float[] xy,List<Observation> previous){}
    private record PopulationResult(long epoch,Map<String,Observation> points){}
    private static final AtomicReference<PopulationInput> rawPopulation=new AtomicReference<>();
    private static final AtomicReference<PopulationResult> preparedPopulation=new AtomicReference<>();
    private static final AtomicBoolean populationWorking=new AtomicBoolean();
    private static final ExecutorService populationWorker=Executors.newSingleThreadExecutor(task->{Thread thread=new Thread(task,"heyzed-population-view");thread.setDaemon(true);return thread;});
    private static long readyEpoch,enabledUntil;
    private static final Map<String,Observation> virtual=new LinkedHashMap<>();
    private static final Map<Long,List<Observation>> virtualBuckets=new HashMap<>();
    private static final Set<String> activatedVirtualIds=new HashSet<>();
    private static final float[] radar=new float[131072];
    private static long nextCapture,nextRadar,nextHandle;
    private static boolean unavailable;
    private static int radarCount,coincident,radarRetries,radarResponses;
    private static long populationUpdates;
    private static long nativePointSamples,positivePopulationQueries,lastPositivePopulation;
    private static int peakPopulationPoints;
    private static long virtualActivations;
    private static final AtomicLong publishedVirtualSamples=new AtomicLong();
    private static String radarStatus="not_requested";
    private static double radarLatency;
    private static String action(IsoZombie zombie,double speed){
        String state=zombie.getActionContext().getCurrentStateName();state=state==null?"":state.toLowerCase(Locale.ROOT);
        if(state.contains("climb")||state.contains("fence")||state.contains("vault"))return "climb";
        if(state.contains("thump")||state.contains("attack"))return "attack";
        if(zombie.isSitAgainstWall()||state.contains("sit"))return "sit";
        if(zombie.isCrawling())return speed<.03?"crawl_idle":"crawl";
        return speed<.03?"idle":speed>1.5?"run":speed<.6?"walk_slow":"walk";
    }
    static void capture(IsoPlayer player,long epoch,long now){
        if(now<nextCapture)return;nextCapture=now+200_000_000L;generation=epoch;
        PopulationResult prepared=preparedPopulation.getAndSet(null);
        if(prepared!=null&&prepared.epoch==epoch){virtual.clear();virtual.putAll(prepared.points);virtualBuckets.clear();for(Observation point:virtual.values())virtualBuckets.computeIfAbsent(cell(point.x,point.y),key->new ArrayList<>()).add(point);}
        Set<String> alive=new HashSet<>();Map<Long,List<Observation>> realBuckets=new HashMap<>();
        for(IsoZombie zombie:player.getCell().getZombieList()){
            if(zombie.getCurrentSquare()==null||zombie.isDead())continue;
            String id=UnrealEntities.crowdId(zombie);alive.add(id);Observation prior=observations.get(id);
            double dt=prior==null?0:(now-prior.time)*1e-9;
            double vx=dt>0&&dt<2?(zombie.getX()-prior.x)/dt:0,vy=dt>0&&dt<2?(zombie.getY()-prior.y)/dt:0;
            if(Math.hypot(vx,vy)>15){vx=0;vy=0;}
            double distance=Math.hypot(zombie.getX()-player.getX(),zombie.getY()-player.getY());
            boolean detail=prior!=null&&prior.detail?distance<=NEAR_OUT:distance<=NEAR_IN;
            float yaw=(float)Math.toDegrees(Math.atan2(zombie.getForwardDirectionY(),zombie.getForwardDirectionX()));
            Observation current=new Observation(id,now,zombie.getX(),zombie.getY(),zombie.getZ(),vx,vy,yaw,action(zombie,Math.hypot(vx,vy)),zombie.getPersistentOutfitID(),detail,true,false);
            observations.put(id,current);
            realBuckets.computeIfAbsent(cell(current.x,current.y),key->new ArrayList<>()).add(current);
        }
        coincident=0;
        for(Observation point:virtual.values()){
            boolean overlaps=alive.contains(point.id);
            for(int cx=-1;cx<=1&&!overlaps;++cx)for(int cy=-1;cy<=1&&!overlaps;++cy)for(Observation real:realBuckets.getOrDefault(cell(point.x+cx*8,point.y+cy*8),List.of()))if(Math.abs(real.x-point.x)<.35&&Math.abs(real.y-point.y)<.35){overlaps=true;break;}
            if(overlaps){++coincident;continue;}
            alive.add(point.id);observations.put(point.id,point);
        }
        for(var iterator=observations.entrySet().iterator();iterator.hasNext();){var item=iterator.next();if(!alive.contains(item.getKey())){offer(item.getKey(),null);lastQueued.remove(item.getKey());iterator.remove();}}
        for(Observation point:observations.values()){
            Observation old=lastQueued.get(point.id);
            if(old==null||point.time-old.time>=1_000_000_000L||point.x!=old.x||point.y!=old.y||point.z!=old.z||point.yaw!=old.yaw||!point.action.equals(old.action)||point.detail!=old.detail){offer(point.id,point);lastQueued.put(point.id,point);}
        }
    }
    static String claimVirtual(IsoZombie zombie){
        if(zombie.getCurrentSquare()==null||zombie.isDead())return null;
        return claimVirtual(zombie.getX(),zombie.getY(),zombie.getZ(),System.nanoTime());
    }
    private static String claimVirtual(float x,float y,float z,long now){
        if(!Float.isFinite(x)||!Float.isFinite(y)||Math.abs(z)>.01||!Float.isFinite(z))return null;
        Observation match=null;
        for(int cx=-1;cx<=1;++cx)for(int cy=-1;cy<=1;++cy)for(Observation point:virtualBuckets.getOrDefault(cell(x+cx*8,y+cy*8),List.of())){
            if(virtual.get(point.id)!=point||activatedVirtualIds.contains(point.id)||now-point.time<0||now-point.time>1_500_000_000L||Math.hypot(x-point.x,y-point.y)>.35)continue;
            Observation active=observations.get(point.id);if(active!=null&&!active.virtual)continue;
            if(match!=null)return null;match=point;
        }
        if(match==null)return null;virtual.remove(match.id);activatedVirtualIds.add(match.id);++virtualActivations;return match.id;
    }
    static boolean wantsDetail(String id){Observation point=observations.get(id);return !enabled(generation)||point==null||point.detail;}
    public static void populationUpdated(){++populationUpdates;IsoPlayer player=IsoPlayer.getInstance();if(player!=null&&generation!=0)readRadar(player,System.nanoTime());}
    private static void readRadar(IsoPlayer player,long now){
        if(unavailable||now<nextRadar||!zombie.GameWindow.isIngameState()||Core.lastStand)return;
        nextRadar=now+1_000_000_000L;
        try{
            long started=System.nanoTime();int count=UnrealPopulationView.snapshot(player.getX(),player.getY(),VIRTUAL_RANGE,radar);
            radarCount=count;radarLatency=(System.nanoTime()-started)*1e-6;++radarResponses;
            nativePointSamples+=count;peakPopulationPoints=Math.max(peakPopulationPoints,count);
            if(count>0){++positivePopulationQueries;lastPositivePopulation=now;}
            rawPopulation.set(new PopulationInput(generation,now,player.getX(),player.getY(),Arrays.copyOf(radar,count*2),virtual.values().stream().filter(point->{Observation active=observations.get(point.id);return active==null||active.virtual;}).toList()));schedulePopulation();
            radarStatus=count==radar.length/2?"capacity_reached":"ready";
        }catch(Throwable failure){unavailable=true;radarStatus="unavailable:"+failure.getClass().getSimpleName();System.err.println("[heyzed] Native population observations: "+failure);}
    }
    private static long cell(float x,float y){return ((long)(int)Math.floor(x/8)<<32)^((int)Math.floor(y/8)&0xffffffffL);}
    private static void schedulePopulation(){
        if(!populationWorking.compareAndSet(false,true))return;
        populationWorker.execute(()->{try{PopulationInput input;while((input=rawPopulation.getAndSet(null))!=null){Map<String,Observation> points=reconcileRadar(input);if(input.epoch==generation)preparedPopulation.set(new PopulationResult(input.epoch,points));}}finally{populationWorking.set(false);if(rawPopulation.get()!=null)schedulePopulation();}});
    }
    private static Map<String,Observation> reconcileRadar(PopulationInput input){
        final long now=input.time;final int count=input.xy.length/2;final float[] radar=input.xy;
        Map<Long,List<Observation>> buckets=new HashMap<>();for(Observation old:input.previous)buckets.computeIfAbsent(cell(old.x,old.y),key->new ArrayList<>()).add(old);
        Map<String,Observation> next=new LinkedHashMap<>();Set<String> used=new HashSet<>();
        for(int index=0;index<count;++index){
            float x=radar[index*2],y=radar[index*2+1];if(!Float.isFinite(x)||!Float.isFinite(y)||Math.hypot(x-input.x,y-input.y)>VIRTUAL_RANGE)continue;
            Observation nearest=null;double best=Double.POSITIVE_INFINITY;
            for(int cx=-1;cx<=1;++cx)for(int cy=-1;cy<=1;++cy)for(Observation old:buckets.getOrDefault(cell(x+cx*8,y+cy*8),List.of())){
                if(used.contains(old.id))continue;double dt=(now-old.time)*1e-9;if(dt<=0||dt>5)continue;
                double distance=Math.hypot(x-old.x,y-old.y),limit=Math.min(6,1+dt*4);
                if(distance<=limit&&distance<best){best=distance;nearest=old;}
            }
            String id=nearest==null?"population-view:"+(++nextHandle):nearest.id;used.add(id);
            double dt=nearest==null?0:(now-nearest.time)*1e-9,vx=dt>0?(x-nearest.x)/dt:0,vy=dt>0?(y-nearest.y)/dt:0;
            double speed=Math.hypot(vx,vy);float yaw=speed>.03?(float)Math.toDegrees(Math.atan2(vy,vx)):nearest==null?0:nearest.yaw;
            next.put(id,new Observation(id,now,x,y,0,vx,vy,yaw,speed<.03?"idle":speed>1.5?"run":speed<.6?"walk_slow":"walk",nearest==null?id.hashCode():nearest.variant,false,false,true));
        }
        return next;
    }
    private static void offer(String id,Observation point){outgoing.put(id,new Delta(generation,point));if(queued.add(id))order.add(id);}
    static void flush(UnrealMemory memory){
        for(int batchIndex=0;batchIndex<4&&memory.canOffer("entities");++batchIndex){
            final long epoch=generation;JSONArray points=new JSONArray(),removed=new JSONArray();Map<String,Delta> batch=new LinkedHashMap<>();
            for(String id:order){Delta delta=outgoing.get(id);if(delta==null||delta.epoch!=epoch){if(delta!=null)outgoing.remove(id,delta);order.remove(id);queued.remove(id);if(outgoing.containsKey(id)&&queued.add(id))order.add(id);continue;}if(batch.containsKey(id))continue;batch.put(id,delta);if(delta.point==null)removed.put(id);else points.put(delta.point.json());if(batch.size()>=64)break;}
            if(batch.isEmpty())return;
            JSONObject event=new JSONObject().put("type","entity_crowd").put("schema",1).put("epoch",epoch).put("points",points).put("removed",removed);
            if(epoch!=generation||!memory.offer("entities",event.toString()))return;
            for(Delta delta:batch.values())if(delta.point!=null&&delta.point.virtual)publishedVirtualSamples.incrementAndGet();
            for(var item:batch.entrySet()){
                String id=item.getKey();outgoing.remove(id,item.getValue());order.remove(id);queued.remove(id);
                if(outgoing.containsKey(id)&&queued.add(id))order.add(id);
            }
        }
    }
    static boolean command(JSONObject command,long epoch){
        if(command.optLong("generation",-1)!=epoch||command.optInt("schema",0)!=1)return false;
        readyEpoch=epoch;enabledUntil=command.optBoolean("enabled")?System.nanoTime()+5_000_000_000L:0;return true;
    }
    private static boolean enabled(long epoch){return readyEpoch==epoch&&System.nanoTime()<enabledUntil;}
    static JSONObject status(){return new JSONObject().put("real",observations.size()-virtual.size()+coincident).put("virtual_xy",virtual.size()).put("population_source","native_debug_callbacks").put("native_points_seen",UnrealPopulationView.seen()).put("native_point_samples",nativePointSamples).put("positive_population_queries",positivePopulationQueries).put("peak_population_points",peakPopulationPoints).put("last_positive_population_age_ms",lastPositivePopulation==0?-1:(System.nanoTime()-lastPositivePopulation)*1e-6).put("published_virtual_samples",publishedVirtualSamples.get()).put("virtual_activations",virtualActivations).put("radar_count",radarCount).put("radar_responses",radarResponses).put("radar_retries",radarRetries).put("population_updates",populationUpdates).put("radar_status",radarStatus).put("radar_latency_ms",radarLatency).put("virtual_range",VIRTUAL_RANGE).put("coincident_suppressed",coincident).put("native_ids",false).put("client_ready",enabled(generation)).put("pending_points",outgoing.size()).put("population_preparing",populationWorking.get());}
    static void reconcile(){lastQueued.clear();}
    static void reset(){observations.clear();lastQueued.clear();virtual.clear();virtualBuckets.clear();activatedVirtualIds.clear();outgoing.clear();order.clear();queued.clear();nextCapture=nextRadar=0;unavailable=false;enabledUntil=0;rawPopulation.set(null);preparedPopulation.set(null);radarCount=radarResponses=radarRetries=peakPopulationPoints=0;populationUpdates=nativePointSamples=positivePopulationQueries=lastPositivePopulation=virtualActivations=0;publishedVirtualSamples.set(0);radarStatus="not_requested";}
    private UnrealCrowd(){}
}
