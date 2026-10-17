#include "HeyZedVehicleMotion.h"
#include "HeyZedEntitySample.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeyZedVehicleMotionTest,"heyzed.Vehicles.SharedPresentationTime",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::EngineFilter)
bool FHeyZedVehicleMotionTest::RunTest(const FString& Parameters)
{
    TArray<FHeyZedEntityRootSample> Samples;
    FHeyZedEntityRootSample A;A.Time=10;A.Transform=FTransform(FVector(0,0,0));A.VehicleLocals.Add(FTransform(FRotator(0,0,0)));
    FHeyZedEntityRootSample B;B.Time=10.1;B.Transform=FTransform(FVector(100,0,0));B.VehicleLocals.Add(FTransform(FRotator(0,90,0)));
    Samples.Add(A);Samples.Add(B);
    FTransform Root;TArray<FTransform> Locals;
    HeyZedSampleEntityRoot(Samples,10.05,Root,Locals);
    TestTrue(TEXT("translation uses native sample midpoint"),Root.GetLocation().Equals(FVector(50,0,0),.001));
    TestTrue(TEXT("vehicle local rotation uses the same midpoint"),FMath::IsNearlyEqual(Locals[0].Rotator().Yaw,45.,.001));
    TArray<FHeyZedEntityRootSample> Seat=Samples;Seat[0].Transform.AddToTranslation(FVector(0,40,0));Seat[1].Transform.AddToTranslation(FVector(0,40,0));
    FTransform SeatRoot;TArray<FTransform> Unused;HeyZedSampleEntityRoot(Seat,10.05,SeatRoot,Unused);
    TestTrue(TEXT("passenger retains offset at the vehicle presentation time"),(SeatRoot.GetLocation()-Root.GetLocation()).Equals(FVector(0,40,0),.001));
    HeyZedSampleEntityRoot(Samples,9,Root,Locals);TestTrue(TEXT("before buffer retains first confirmation"),Root.Equals(A.Transform));
    HeyZedSampleEntityRoot(Samples,11,Root,Locals);TestTrue(TEXT("after buffer does not invent motion"),Root.Equals(B.Transform));
    Samples[0].VehicleLocals.Reset();HeyZedSampleEntityRoot(Samples,10.05,Root,Locals);TestEqual(TEXT("appearance revision with different part counts is bounded"),Locals.Num(),1);
    FHeyZedEntityRootSample Moving;Moving.Time=20;Moving.Transform=FTransform::Identity;
    const FTransform FastCar(FVector(556,0,0));
    TestFalse(TEXT("normal 100 km/h motion across a 200 ms delivery gap is not a teleport"),HeyZedEntityRootDiscontinuity(Moving,20.2,FastCar,2780,true));
    TestTrue(TEXT("walking retains the original discontinuity boundary"),HeyZedEntityRootDiscontinuity(Moving,20.2,FastCar,2780,false));
    TestTrue(TEXT("a distant vehicle teleport still resets the buffer"),HeyZedEntityRootDiscontinuity(Moving,20.2,FTransform(FVector(10000,0,0)),2780,true));
    TestTrue(TEXT("stale motion does not enlarge the interpolation boundary"),HeyZedEntityRootDiscontinuity(Moving,22,FastCar,2780,true));
    TestTrue(TEXT("native clock reset invalidates history"),HeyZedEntityRootDiscontinuity(Moving,19,FastCar,2780,true));
    return true;
}
#endif
