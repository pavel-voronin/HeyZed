package com.pavelvoronin.heyzed;

public final class ConstructionVisitsRegression {
    private static void check(boolean value){if(!value)throw new AssertionError("Construction discovery lifecycle mismatch");}
    public static void main(String[] args){
        var visits=new UnrealConstructionVisits();Object first=new Object(),second=new Object();
        visits.add(-1,-9,-32,first,1);visits.add(-1,-9,31,first,1);
        check(visits.contains(-1,-9,-32,first,1));check(visits.contains(-1,-9,31,first,1));
        check(!visits.contains(-1,-9,0,first,1));check(!visits.contains(-1,-9,-32,first,2));check(!visits.contains(-1,-9,-32,second,1));
        visits.add(-1,-9,0,first,2);check(!visits.contains(-1,-9,-32,first,1));
        visits.loaded(-1,-2);check(!visits.contains(-1,-9,0,first,2));
        visits.add(0,0,0,first,7);visits.add(800,800,0,second,7);
        visits.retain(0,0,14);check(visits.contains(0,0,0,first,7));check(!visits.contains(800,800,0,second,7));
        visits.loaded(0,0);check(!visits.contains(0,0,0,first,7));
        visits.add(0,0,0,first,7);visits.clear();check(!visits.contains(0,0,0,first,7));
        UnrealArchitecture.reset();
        for(int x=0;x<10000;x++)UnrealArchitecture.dirty(x,0,0);
        try{
            var field=UnrealArchitecture.class.getDeclaredField("dirty");field.setAccessible(true);
            @SuppressWarnings("unchecked") var queue=(java.util.Queue<UnrealConstructionSource.Address>)field.get(null);
            var all=new java.util.HashSet<String>();
            for(var point:queue)all.add(point.key());
            var rescan=UnrealArchitecture.class.getDeclaredMethod("rescan");rescan.setAccessible(true);
            for(int i=0;i<100000;i++){
                var point=(UnrealConstructionSource.Address)rescan.invoke(null);if(point==null)break;all.add(point.key());
                if(i==99999)throw new AssertionError("Overflow rescan failed to drain");
            }
            for(int x=0;x<10000;x++)check(all.contains(x+",0,0"));
        }catch(ReflectiveOperationException error){throw new AssertionError(error);}
        finally{UnrealArchitecture.reset();}
        System.out.println("PASS: chunk identity/load generation, reload, floors, eviction, reconciliation and lossless dirty overflow");
    }
}
