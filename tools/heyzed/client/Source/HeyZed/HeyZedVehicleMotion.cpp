#include "HeyZedVehicleMotion.h"
#include "HeyZedEntitySample.h"

bool HeyZedEntityRootDiscontinuity(const FHeyZedEntityRootSample& Previous,double Time,const FTransform& Current,double Speed,bool Vehicle)
{
    const double Interval=Time-Previous.Time;
    if(Interval<=0)return true;
    const double Distance=400.+(Vehicle&&Interval<.5&&FMath::IsFinite(Speed)?FMath::Max(0.,Speed)*Interval:0.);
    return FVector::DistSquared(Previous.Transform.GetLocation(),Current.GetLocation())>FMath::Square(Distance);
}
