#pragma once
#include "CoreMinimal.h"

struct FHeyZedEntityRootSample
{
    double Time=0;
    FTransform Transform;
    TArray<FTransform> VehicleLocals;
};

inline void HeyZedSampleEntityRoot(const TArray<FHeyZedEntityRootSample>& Samples,double Time,FTransform& Root,TArray<FTransform>& Locals)
{
    if(Samples.IsEmpty())return;
    Root=Samples.Last().Transform;Locals=Samples.Last().VehicleLocals;
    if(Time<=Samples[0].Time){Root=Samples[0].Transform;Locals=Samples[0].VehicleLocals;return;}
    for(int32 Index=1;Index<Samples.Num();++Index)if(Time<=Samples[Index].Time){
        const auto& A=Samples[Index-1];const auto& B=Samples[Index];
        const float Alpha=float(FMath::Clamp((Time-A.Time)/FMath::Max(B.Time-A.Time,.000001),0.,1.));
        Root.Blend(A.Transform,B.Transform,Alpha);
        if(A.VehicleLocals.Num()==B.VehicleLocals.Num()){
            Locals.SetNum(B.VehicleLocals.Num());
            for(int32 Part=0;Part<B.VehicleLocals.Num();++Part)Locals[Part].Blend(A.VehicleLocals[Part],B.VehicleLocals[Part],Alpha);
        }
        return;
    }
}
