#include "HeyZedConstructionPlanner.h"
#include "HeyZedConstructionSolid.h"
#include "Dom/JsonObject.h"
#include "Algo/Reverse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace {
FString String(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,const FString& Default=FString()){FString V;return O&&O->TryGetStringField(Key,V)?V:Default;}
bool Flag(const TSharedPtr<FJsonObject>& O,const TCHAR* Key){bool V=false;return O&&O->TryGetBoolField(Key,V)&&V;}
FVector Vector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key){const TArray<TSharedPtr<FJsonValue>>* V=nullptr;if(!O->TryGetArrayField(Key,V)||V->Num()<3)return FVector::ZeroVector;return FVector((*V)[0]->AsNumber(),(*V)[1]->AsNumber(),(*V)[2]->AsNumber());}
HeyZedConstruction::FSolid SlabBox(double X,double Y,double Bottom,double Top)
{
    const FVector A(X,Y,Bottom),B(X+100,Y,Bottom),C(X+100,Y+100,Bottom),D(X,Y+100,Bottom);
    const FVector Up(0,0,Top-Bottom);
    return {{A,D,C,B},{A+Up,B+Up,C+Up,D+Up},{A,B,B+Up,A+Up},{B,C,C+Up,B+Up},{C,D,D+Up,C+Up},{D,A,A+Up,D+Up}};
}
}

TSharedRef<FJsonObject> FHeyZedConstructionPlanner::SlabRecipe(int32 X,int32 Y,int32 Z)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedConstruction_PlanSlab);
    using namespace HeyZedConstruction;
    TArray<FHost> Hosts;
    for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)
        if(const auto* Cell=Cells.Find(Key(X+DX,Y+DY,Z-1)))for(const auto& Item:(*Cell)->GetArrayField(TEXT("walls")))
        {
            const auto Wall=Item->AsObject();if(!Flag(Wall,TEXT("wall")))continue;
            FString Boundary=String(Wall,TEXT("boundary"),TEXT("boundary.wall"));
            for(const auto& SourceName:Wall->GetArrayField(TEXT("sources")))if(auto Source=Resources->Source(SourceName->AsString()))
                if(String(Source,TEXT("archetype"))==TEXT("boundary"))Boundary=String(Source,TEXT("type"),Boundary);
            const auto Definition=Resources->Definition(Boundary);if(!Definition)continue;
            FHost Host;Host.North=String(Wall,TEXT("axis"))==TEXT("N");Host.Origin=FVector(DX*100,DY*100,0);
            Host.Key=(Host.North?TEXT("x:"):TEXT("y:"))+Key(X+DX,Y+DY,Z-1);Host.Type=Boundary;
            double Rank=1;Definition->TryGetNumberField(TEXT("rank"),Rank);Host.Rank=Rank;
            const TArray<TSharedPtr<FJsonValue>>* Operations=nullptr;
            if(Definition->TryGetArrayField(TEXT("joinOperations"),Operations))for(const auto& O:*Operations)Host.Operations.Add(O->AsString());
            Host.Solids=HeyZedConstruction::Read(Definition->GetArrayField(TEXT("solids")));
            FBox Bounds(ForceInit);for(const auto& Solid:Host.Solids)for(const auto& Face:Solid)for(auto V:Face)Bounds+=V;
            if(!Bounds.IsValid||!FMath::IsNearlyEqual(Bounds.Max.Z,Floor-Slab,.001)||!FMath::IsNearlyZero(Bounds.Min.Z,.001))continue;
            Transform(Host.Solids,FTransform(FRotator(0,Host.North?0:90,0),Host.Origin));Hosts.Add(MoveTemp(Host));
        }
    Join(Hosts);FSolids Cuts;
    for(auto& Host:Hosts)
    {
        for(auto& Solid:Host.Solids)for(auto& Face:Solid)for(auto& V:Face)V.Z=-Slab+V.Z*Slab/(Floor-Slab);
        Cuts.Append(MoveTemp(Host.Solids));
    }
    auto Recipe=MakeShared<FJsonObject>();Recipe->SetNumberField(TEXT("bottom"),-Slab);Recipe->SetNumberField(TEXT("top"),0);
    Recipe->SetArrayField(TEXT("solids"),HeyZedConstruction::Write(Subtract(FSolids{SlabBox(0,0,-Slab,0)},Cuts)));
    return Recipe;
}

bool FHeyZedConstructionPlanner::RebuildTyped(const FString& Address,int32 X,int32 Y,int32 Z,bool Save)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedConstruction_PlanWalls);
    using namespace HeyZedConstruction;
    if(Resources->Definitions.IsEmpty())return false;
    if(const auto* Current=Cells.Find(Address))if((*Current)->GetArrayField(TEXT("walls")).IsEmpty())return true;
    struct FEdge{TSharedPtr<FJsonObject> Wall,Insert,Cut;FString Type,State,Owner;FVector Offset;bool North,Mirror=false;int32 Host=INDEX_NONE;};
    TArray<FEdge> Edges;TArray<FHost> Hosts;
    TSet<FIntPoint> Offsets;
    for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)Offsets.Add(FIntPoint(DX,DY));
    // Continuations point to the one actual assembly owner. This also admits
    // observed garages wider than the original three-cell catalogue specimen.
    for(const auto Offset:Offsets.Array())if(const auto* Cell=Cells.Find(Key(X+Offset.X,Y+Offset.Y,Z)))for(auto& W:(*Cell)->GetArrayField(TEXT("walls")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Anchor=nullptr;if(W->AsObject()->TryGetArrayField(TEXT("assemblyAnchor"),Anchor)&&Anchor->Num()>=2)Offsets.Add(FIntPoint((*Anchor)[0]->AsNumber()-X,(*Anchor)[1]->AsNumber()-Y));
    }
    for(const auto Offset:Offsets)
    {
        const int32 DX=Offset.X,DY=Offset.Y;
        auto* Cell=Cells.Find(Key(X+DX,Y+DY,Z));if(!Cell)continue;
        for(auto& Item:(*Cell)->GetArrayField(TEXT("walls")))
        {
            FEdge E;E.Wall=Item->AsObject();E.Owner=Key(X+DX,Y+DY,Z);E.Offset=FVector(DX*100,DY*100,0);E.North=String(E.Wall,TEXT("axis"))==TEXT("N");E.Mirror=Flag(E.Wall,TEXT("mirrorU"));
            E.Type=String(E.Wall,TEXT("insert"));E.State=String(E.Wall,TEXT("state"),TEXT("closed"));FString Boundary=String(E.Wall,TEXT("boundary"),TEXT("boundary.wall"));
            for(auto& SourceName:E.Wall->GetArrayField(TEXT("sources")))if(auto Source=Resources->Source(SourceName->AsString()))
            {
                auto Kind=String(Source,TEXT("archetype"));if(Kind==TEXT("boundary"))Boundary=String(Source,TEXT("type"),Boundary);
                else if(E.Type.IsEmpty()&&(Flag(E.Wall,TEXT("door"))||Flag(E.Wall,TEXT("window")))){E.Type=String(Source,TEXT("type"));const TSharedPtr<FJsonObject>* Evidence=nullptr;if(Source->TryGetObjectField(TEXT("nativeEvidence"),Evidence)){E.State=String(*Evidence,TEXT("spriteState"),E.State);E.Mirror=Flag(*Evidence,TEXT("mirrorU"));}}
            }
            if(Flag(E.Wall,TEXT("glassRemoved")))E.State=TEXT("glassRemoved");else if(Flag(E.Wall,TEXT("broken")))E.State=TEXT("broken");else if(Flag(E.Wall,TEXT("open")))E.State=TEXT("open");
            E.Insert=Resources->Definition(E.Type);
            if(!E.Insert&&(Flag(E.Wall,TEXT("door"))||Flag(E.Wall,TEXT("window")))){E.Type=Flag(E.Wall,TEXT("door"))?TEXT("door.standard"):TEXT("window.standard");E.Insert=Resources->Definition(E.Type);}
            const TSharedPtr<FJsonObject>* Opening=nullptr;
            if(E.Wall->TryGetObjectField(TEXT("openingConfig"),Opening))E.Cut=Resources->Definition(String(*Opening,TEXT("type")));
            if(!E.Cut)E.Cut=E.Insert;
            if(!E.Cut&&!String(E.Wall,TEXT("opening")).IsEmpty())E.Cut=Resources->Definition(String(E.Wall,TEXT("opening"))==TEXT("door")?TEXT("door.standard"):TEXT("window.standard"));
            if(Flag(E.Wall,TEXT("assemblyContinuation"))){E.Insert.Reset();E.Cut.Reset();}
            if(FMath::Abs(DX)<=1&&FMath::Abs(DY)<=1&&Flag(E.Wall,TEXT("wall")))if(auto Definition=Resources->Definition(Boundary))
            {
                FHost Host;Host.Key=(E.North?TEXT("x:"):TEXT("y:"))+E.Owner;Host.Type=Boundary;Host.North=E.North;Host.Origin=E.Offset;double Rank=1;Definition->TryGetNumberField(TEXT("rank"),Rank);Host.Rank=Rank;
                const TArray<TSharedPtr<FJsonValue>>* Operations=nullptr;if(Definition->TryGetArrayField(TEXT("joinOperations"),Operations))for(auto& O:*Operations)Host.Operations.Add(O->AsString());
                Host.Solids=HeyZedConstruction::Read(Definition->GetArrayField(TEXT("solids")));Transform(Host.Solids,FTransform(FRotator(0,E.North?0:90,0),E.Offset));
                E.Host=Hosts.Add(MoveTemp(Host));
            }
            Edges.Add(MoveTemp(E));
        }
    }
    Join(Hosts);
    FSolids Cuts;
    for(auto& E:Edges)if(E.Cut)
    {
        auto Cut=HeyZedConstruction::Read(E.Cut->GetArrayField(TEXT("cut")));
        // A mirrored aperture and its insert share the same occupied edge.
        double Width=100;if(E.Insert){double Count=1;E.Insert->TryGetNumberField(TEXT("cells"),Count);Width=Count*100;}
        Transform(Cut,FTransform(FRotator(0,E.North?0:90,0),E.Offset+(E.Mirror?FRotator(0,E.North?0:90,0).RotateVector(FVector(Width,0,0)):FVector::ZeroVector),FVector(E.Mirror?-1:1,1,1)));
        if(E.Mirror)for(auto& S:Cut)for(auto& F:S)Algo::Reverse(F);
        Cuts.Append(MoveTemp(Cut));
    }
    const FVector World(X*100.,Y*100.,Z*Floor);
    auto Surface=[&](const FString& Name,FName Default){if(auto* Bound=Resources->SurfaceBindings.Find(Name))return *Bound;return Resources->Materials.Contains(FName(*Name))?FName(*Name):Default;};
    for(auto& E:Edges)if(E.Owner==Address)
    {
        const float Yaw=E.North?0:90;const FRotator Rotation(0,Yaw,0);
        if(E.Host!=INDEX_NONE&&!Flag(E.Wall,TEXT("contextOnly")))
        {
            auto Solids=Subtract(Hosts[E.Host].Solids,Cuts);Transform(Solids,FTransform(Rotation,FVector::ZeroVector).Inverse());
            auto Recipe=MakeShared<FJsonObject>();Recipe->SetArrayField(TEXT("solids"),HeyZedConstruction::Write(Solids));
            Place(Address,Recipe,World,Yaw,{Surface(String(E.Wall,TEXT("surfaceA")),TEXT("Plaster")),Surface(String(E.Wall,TEXT("surfaceB")),TEXT("Plaster")),TEXT("Cut")},Save);
            const bool SlabAtEdge=HasSlabAbove(X,Y,Z)||HasSlabAbove(X-(E.North?0:1),Y-(E.North?1:0),Z);
            bool WallAbove=false;
            if(const auto* Above=Cells.Find(Key(X,Y,Z+1)))for(const auto& W:(*Above)->GetArrayField(TEXT("walls")))
                if(Flag(W->AsObject(),TEXT("wall"))&&String(W->AsObject(),TEXT("axis"))==String(E.Wall,TEXT("axis")))WallAbove=true;
            if(SlabAtEdge||WallAbove)
            {
                FBox HostBounds(ForceInit);for(const auto& Solid:Hosts[E.Host].Solids)for(const auto& Face:Solid)for(auto V:Face)HostBounds+=V;
                if(FMath::IsNearlyEqual(HostBounds.Max.Z,Floor-Slab,.001)&&FMath::IsNearlyZero(HostBounds.Min.Z,.001))
                {
                    auto Band=Hosts[E.Host].Solids;
                    for(auto& Solid:Band)for(auto& Face:Solid)for(auto& V:Face)V.Z=Floor-Slab+V.Z*Slab/(Floor-Slab);
                    if(!Band.IsEmpty())
                    {
                        Transform(Band,FTransform(Rotation,FVector::ZeroVector).Inverse());
                        auto Closure=MakeShared<FJsonObject>();Closure->SetArrayField(TEXT("solids"),HeyZedConstruction::Write(Band));
                        Place(Address,Closure,World,Yaw,{Surface(String(E.Wall,TEXT("surfaceA")),TEXT("Plaster")),Surface(String(E.Wall,TEXT("surfaceB")),TEXT("Plaster")),TEXT("Cut")},Save);
                    }
                }
            }
        }
        if(!E.Insert||Flag(E.Wall,TEXT("assemblyContinuation"))||Flag(E.Wall,TEXT("contextOnly")))continue;
        const auto States=E.Insert->GetObjectField(TEXT("states"));const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;
        // Hinged leaves stay in their closed reference pose; HeyZedScene owns
        // their visual rotation, independent of the next authoritative fact.
        bool Hinged=false;const TArray<TSharedPtr<FJsonValue>>* OpenParts=nullptr;
        if(States->TryGetArrayField(TEXT("open"),OpenParts))for(auto& P:*OpenParts){const TSharedPtr<FJsonObject>* Motion=nullptr;double Angle=0;if(P->AsObject()->TryGetObjectField(TEXT("motion"),Motion)&&(*Motion)->TryGetNumberField(TEXT("angle"),Angle)&&FMath::Abs(Angle)>1.e-4)Hinged=true;}
        const bool Door=Flag(E.Wall,TEXT("door"))&&String(E.Insert,TEXT("kind"))==TEXT("door")&&Hinged;
        if(!States->TryGetArrayField(Door?TEXT("closed"):*E.State,Parts)&&!States->TryGetArrayField(TEXT("closed"),Parts))continue;
        double Count=1;E.Insert->TryGetNumberField(TEXT("cells"),Count);const double Width=Count*100;
        auto PlacePart=[&](const TSharedPtr<FJsonObject>& Part,const FVector& Extra=FVector::ZeroVector,const FVector& ExtraScale=FVector::OneVector)
        {
            auto Recipe=MakeShared<FJsonObject>();FString Mesh=String(Part,TEXT("mesh"));const TArray<TSharedPtr<FJsonValue>>* Mirrors=nullptr;
            const int32 Reflection=(E.Mirror?1:0)|(ExtraScale.Y<0?2:0);bool Reflected=false;
            if(Part->TryGetArrayField(TEXT("mirrors"),Mirrors)&&Mirrors->IsValidIndex(Reflection)){Mesh=(*Mirrors)[Reflection]->AsString();Reflected=true;}
            Recipe->SetStringField(TEXT("mesh"),Mesh);auto P=Vector(Part,TEXT("position"))*ExtraScale+Extra;
            Recipe->SetBoolField(TEXT("localUV"),Part->HasField(TEXT("motion")));
            if(E.Mirror)P.X=Width-P.X;
            const FString Name=String(Part,TEXT("material"));FName Mat=Name==TEXT("glass")?TEXT("Glass"):Name==TEXT("wood")?TEXT("Wood"):Name==TEXT("fabric")?TEXT("Fabric"):Name==TEXT("metal")||Name==TEXT("hardware")?TEXT("Metal"):TEXT("Frame");
            if(Resources->Materials.Contains(FName(*Name)))Mat=FName(*Name);
            if(Name==TEXT("charred"))Mat=TEXT("Wood_Dark");else if(Name==TEXT("cubicle"))Mat=TEXT("Metal_Light");
            const bool Moving=Door&&Part->HasField(TEXT("motion"));
            const int32 DoorCell=FMath::Clamp(FMath::FloorToInt(P.X/100),0,FMath::Max(0,int32(Count)-1));
            const FString DoorAddress=Key(X+(E.North?DoorCell:0),Y+(E.North?0:DoorCell),Z)+(E.North?TEXT(":N"):TEXT(":W"));
            double LocalAngle=0;const TSharedPtr<FJsonObject>* Motion=nullptr;if(!Door&&Part->TryGetObjectField(TEXT("motion"),Motion))(*Motion)->TryGetNumberField(TEXT("angle"),LocalAngle);
            if(Reflection==1||Reflection==2)LocalAngle=-LocalAngle;
            const FVector Scale=Reflected?FVector(FMath::Abs(ExtraScale.X),FMath::Abs(ExtraScale.Y),FMath::Abs(ExtraScale.Z)):ExtraScale*FVector(E.Mirror?-1:1,1,1);
            Place(Address,Recipe,World+Rotation.RotateVector(P),Yaw+LocalAngle,{Mat},Save,Moving?DoorAddress:FString(),Scale);
        };
        for(auto& P:*Parts)PlacePart(P->AsObject());
        const TArray<TSharedPtr<FJsonValue>>* Attachments=nullptr;
        if(E.Wall->TryGetArrayField(TEXT("attachments"),Attachments))for(auto& A:*Attachments)
        {
            auto Attachment=A->AsObject();auto Definition=Resources->Definition(String(Attachment,TEXT("definition"),TEXT("attachment.")+String(Attachment,TEXT("family"))));if(!Definition)continue;
            const auto& Reference=Definition->GetArrayField(TEXT("referenceBounds"));const auto& Bounds=E.Insert->GetArrayField(TEXT("bounds"));
            const double SX=(Bounds[1]->AsNumber()-Bounds[0]->AsNumber())/(Reference[1]->AsNumber()-Reference[0]->AsNumber()),SZ=(Bounds[3]->AsNumber()-Bounds[2]->AsNumber())/(Reference[3]->AsNumber()-Reference[2]->AsNumber());
            const FString NativeSide=String(Attachment,TEXT("edgeSide"),String(Attachment,TEXT("side"))==TEXT("outside")?TEXT("A"):TEXT("B"));
            double AuthoredSign=Flag(Definition,TEXT("nativeFit"))?(String(Attachment,TEXT("side"))==TEXT("outside")?1:-1):-1;
            Attachment->TryGetNumberField(TEXT("prototypeNormal"),AuthoredSign);
            Definition->TryGetNumberField(TEXT("authoredNormal"),AuthoredSign);
            double DesiredSign=String(Attachment,TEXT("side"))==TEXT("outside")?1:-1;
            if(String(Definition,TEXT("category"))==TEXT("covering"))
            {
                // Legacy side names encode native N/W versus S/E, not room membership.
                DesiredSign=(NativeSide==TEXT("A")?1:-1)*(E.North?1:-1);
                const TArray<TSharedPtr<FJsonValue>>* Mount=nullptr;
                if(Attachment->TryGetArrayField(TEXT("mountCell"),Mount)&&Mount->Num()==3)
                {
                    const FVector Center(((*Mount)[0]->AsNumber()-X+.5)*100,((*Mount)[1]->AsNumber()-Y+.5)*100,0);
                    const double Normal=FVector::DotProduct(Center,Rotation.RotateVector(FVector::YAxisVector));
                    DesiredSign=Normal>=0?1:-1;
                }
            }
            const double Sign=DesiredSign/AuthoredSign;
            const FVector Offset(Bounds[0]->AsNumber()-Reference[0]->AsNumber()*SX,0,Bounds[2]->AsNumber()-Reference[2]->AsNumber()*SZ);
            const TArray<TSharedPtr<FJsonValue>>* Models=nullptr;if(Definition->GetObjectField(TEXT("states"))->TryGetArrayField(*String(Attachment,TEXT("state"),TEXT("closed")),Models))for(auto& Model:*Models)PlacePart(Model->AsObject(),Offset,FVector(SX,Sign,SZ));
        }
    }
    return true;
}
