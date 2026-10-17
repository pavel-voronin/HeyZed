#include "HeyZedConstructionPlanner.h"
#include "HeyZedConstructionSolid.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace {
TSharedRef<FJsonObject> Read(const FString& Text)
{
    TSharedPtr<FJsonObject> Value;
    const bool Parsed=FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Value);
    check(Parsed&&Value);
    if(!Parsed||!Value)return MakeShared<FJsonObject>();
    return Value.ToSharedRef();
}
FString Write(const TSharedRef<FJsonObject>& Value)
{
    FString Text;FJsonSerializer::Serialize(Value,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));return Text;
}
TSharedRef<FJsonObject> Prism(const TArray<FVector2D>& Polygon,double Bottom,double Top)
{
    auto Value=MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Points;
    for (auto P : Polygon) Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y)}));
    Value->SetArrayField(TEXT("polygon"),Points); Value->SetNumberField(TEXT("bottom"),Bottom); Value->SetNumberField(TEXT("top"),Top); return Value;
}
TSharedRef<FJsonObject> Box(double X0,double Y0,double Z0,double X1,double Y1,double Z1)
{
    return Prism({{X0,Y0},{X1,Y0},{X1,Y1},{X0,Y1}},Z0,Z1);
}
FName MaterialFor(const FString& Source)
{
    const FString Lower=Source.ToLower();
    if (Lower.Contains(TEXT("brick")) || Lower.Contains(TEXT("exterior"))) return TEXT("Brick");
    if (Lower.Contains(TEXT("wood")) || Lower.Contains(TEXT("carpentry"))) return TEXT("Wood");
    if (Lower.Contains(TEXT("metal"))) return TEXT("Metal");
    return TEXT("Plaster");
}
}
TSharedPtr<FJsonObject> FHeyZedConstructionCatalogView::Definition(const FString& Name)
{
    if(auto* Found=ParsedDefinitions.Find(Name))return *Found;
    if(const auto* Text=Definitions.Find(Name)){auto Value=Read(*Text);ParsedDefinitions.Add(Name,Value);return Value;}
    return nullptr;
}
TSharedPtr<FJsonObject> FHeyZedConstructionCatalogView::Source(const FString& Name)
{
    if(auto* Found=ParsedSources.Find(Name))return *Found;
    if(const auto* Text=SourceBindings.Find(Name)){auto Value=Read(*Text);ParsedSources.Add(Name,Value);return Value;}
    return nullptr;
}
FHeyZedConstructionPlanner::FHeyZedConstructionPlanner(const FHeyZedConstructionSnapshot& Snapshot)
    :Library(*Snapshot.Catalog),Resources(&Library),OwnedFloors(Snapshot.OwnedFloors),Interior(Snapshot.Interior),Floor(Snapshot.Floor),Thickness(Snapshot.Thickness),Slab(Snapshot.Slab)
{
    for(const auto& Pair:Snapshot.Cells)Cells.Add(Pair.Key,Read(Pair.Value));
}
FString FHeyZedConstructionPlanner::Key(int32 X,int32 Y,int32 Z){return FString::Printf(TEXT("%d,%d,%d"),X,Y,Z);}
TArray<FHeyZedConstructionPlacement> FHeyZedConstructionPlanner::Plan(const FString& Address)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedConstruction_Plan);
    GatherParts(Address,false);return MoveTemp(CollectedParts);
}
void FHeyZedConstructionPlanner::Place(const FString&,const TSharedRef<FJsonObject>& Recipe,const FVector& Position,float Yaw,const TArray<FName>& Materials,bool,const FString& Door,const FVector& Scale)
{
    CollectedParts.Add({Recipe,Position,Yaw,Materials,Door,Scale,Write(Recipe)});
}
bool FHeyZedConstructionPlanner::HasSlabAbove(int32 X,int32 Y,int32 Z) const
{
    const FString Above=Key(X,Y,Z+1);
    return OwnedFloors.Contains(Above)||(Interior.Contains(Key(X,Y,Z))&&!Interior.Contains(Above));
}
void FHeyZedConstructionPlanner::GatherParts(const FString& Address,bool Save)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_Rebuild);
    const auto* Found=Cells.Find(Address);if (!Found) return;
    const auto Cell=*Found;const auto& P=Cell->GetArrayField(TEXT("address"));
    const int32 X=P[0]->AsNumber(),Y=P[1]->AsNumber(),Z=P[2]->AsNumber();
    const FVector Position(X*100.,Y*100.,Z*Floor);
    FString FloorSource;
    if (Cell->TryGetStringField(TEXT("floor"),FloorSource) && !FloorSource.IsEmpty())
    {
        FName Surface=TEXT("Floor");if(auto* Binding=Resources->SurfaceBindings.Find(FloorSource))Surface=*Binding;
        Place(Address,SlabRecipe(X,Y,Z),Position,0,{Surface},Save);
    }
    if(Interior.Contains(Address)&&!Interior.Contains(Key(X,Y,Z+1))&&!OwnedFloors.Contains(Key(X,Y,Z+1)))
        Place(Address,SlabRecipe(X,Y,Z+1),Position+FVector(0,0,Floor),0,{TEXT("Plaster_Light")},Save);
    auto Has=[&](int32 XX,int32 YY,const FString& Axis)
    {
        const auto* Neighbor=Cells.Find(Key(XX,YY,Z));if (!Neighbor) return false;
        for (const auto& W : (*Neighbor)->GetArrayField(TEXT("walls"))) {bool Present=false;W->AsObject()->TryGetBoolField(TEXT("wall"),Present);if(Present && W->AsObject()->GetStringField(TEXT("axis"))==Axis) return true;}
        return false;
    };
    if(!RebuildTyped(Address,X,Y,Z,Save)) for (const auto& Item : Cell->GetArrayField(TEXT("walls")))
    {
        const auto Wall=Item->AsObject();const bool North=Wall->GetStringField(TEXT("axis"))==TEXT("N");
        TArray<FVector2D> Polygon={{-Thickness/2,-Thickness/2},{100+Thickness/2,-Thickness/2},{100+Thickness/2,Thickness/2},{-Thickness/2,Thickness/2}};
        for (int32 End=0;End<2;++End)
        {
            const int32 NX=X+(North?End:0),NY=Y+(North?0:End);
            const bool Straight=North?Has(X+(End?1:-1),Y,TEXT("N")):Has(X,Y+(End?1:-1),TEXT("W"));
            const bool Positive=North?Has(NX,NY,TEXT("W")):Has(NX-1,NY,TEXT("N"));
            const bool Negative=North?Has(NX,NY-1,TEXT("W")):Has(NX,NY,TEXT("N"));
            const double Sign=End?-1:1,At=End?100:0;
            if (Straight || (Positive && Negative))
            {
                const bool Through=North;
                const double Gap=(!Through && (Positive || Negative))?Thickness/2:0;
                Polygon=HeyZedConstruction::Clip2D(Polygon,Sign,0,Sign*At+Gap);
            }
            else if (Positive || Negative) Polygon=HeyZedConstruction::Clip2D(Polygon,Sign,Positive?-1:1,Sign*At);
        }
        const FString Opening=Wall->GetStringField(TEXT("opening"));
        FString OpeningType;Wall->TryGetStringField(TEXT("openingType"),OpeningType);
        const bool Storefront=OpeningType==TEXT("window.storefront");
        auto Shape=Prism(Polygon,0,Floor-Slab);TArray<TSharedPtr<FJsonValue>> Holes;
        for (int32 DY=-1;DY<=1;++DY) for (int32 DX=-1;DX<=1;++DX)
        {
            const auto* Other=Cells.Find(Key(X+DX,Y+DY,Z));if(!Other) continue;
            for(const auto& CutValue : (*Other)->GetArrayField(TEXT("walls")))
            {
                const auto Cut=CutValue->AsObject();const FString Kind=Cut->GetStringField(TEXT("opening"));if(Kind.IsEmpty()) continue;
                FString Type;Cut->TryGetStringField(TEXT("openingType"),Type);const bool Full=Type==TEXT("window.storefront");
                const double L=Full?0:10,R=Full?100:90,B=Kind==TEXT("door") || Full?0:85,T=Full?Floor-Slab:205;
                const bool CutNorth=Cut->GetStringField(TEXT("axis"))==TEXT("N");
                double MinU=1.e10,MinV=1.e10,MaxU=-1.e10,MaxV=-1.e10;
                for(double U : {L,R}) for(double V : {-Thickness/2-.01,Thickness/2+.01})
                {
                    const double GX=DX*100+(CutNorth?U:-V),GY=DY*100+(CutNorth?V:U);
                    const double LocalU=North?GX:GY,LocalV=North?GY:-GX;
                    MinU=FMath::Min(MinU,LocalU);MaxU=FMath::Max(MaxU,LocalU);MinV=FMath::Min(MinV,LocalV);MaxV=FMath::Max(MaxV,LocalV);
                }
                if(MaxU<=-Thickness/2 || MinU>=100+Thickness/2 || MaxV<=-Thickness/2 || MinV>=Thickness/2) continue;
                TArray<TSharedPtr<FJsonValue>> Values;for(double Number : {MinU,MinV,B,MaxU,MaxV,T}) Values.Add(MakeShared<FJsonValueNumber>(Number));
                Holes.Add(MakeShared<FJsonValueArray>(Values));
            }
        }
        Shape->SetArrayField(TEXT("holes"),Holes);
        const auto& Sources=Wall->GetArrayField(TEXT("sources"));const FName Surface=Sources.IsEmpty()?FName(TEXT("Plaster")):MaterialFor(Sources[0]->AsString());
        bool HasWall=false;Wall->TryGetBoolField(TEXT("wall"),HasWall);
        FString FaceA,FaceB;Wall->TryGetStringField(TEXT("surfaceA"),FaceA);Wall->TryGetStringField(TEXT("surfaceB"),FaceB);
        if (HasWall) Place(Address,Shape,Position,North?0:90,{FaceA.IsEmpty()?Surface:FName(*FaceA),FaceB.IsEmpty()?Surface:FName(*FaceB),TEXT("Cut")},Save);
        const float Rotation=North?0:90;
        auto Part=[&](const TSharedRef<FJsonObject>& Recipe,FName Material,double Offset=0,const FString& DoorId=FString(),double Up=0)
        {
            const FVector Anchor=Position+FRotator(0,Rotation,0).RotateVector(FVector(Offset,0,Up));Place(Address,Recipe,Anchor,Rotation,{Material},Save,DoorId);
        };
        bool DoorPresent=false,WindowPresent=false;Wall->TryGetBoolField(TEXT("door"),DoorPresent);Wall->TryGetBoolField(TEXT("window"),WindowPresent);
        if (DoorPresent || WindowPresent)
        {
            const double Left=Storefront?0:10,Right=Storefront?100:90,Bottom=DoorPresent || Storefront?0:85,Top=Storefront?Floor-Slab:205;
            Part(Box(Left,-8,Bottom,Left+2.7,8,Top),TEXT("Frame"));Part(Box(Right-2.7,-8,Bottom,Right,8,Top),TEXT("Frame"));Part(Box(Left+2.7,-8,Top-2.7,Right-2.7,8,Top),TEXT("Frame"));
            if (WindowPresent)
            {
                Part(Box(Left+2.7,-8,Bottom,Right-2.7,8,Bottom+2.7),TEXT("Frame"));
                if (!Storefront) Part(Box(Left+2.7,-2,144,Right-2.7,2,146.7),TEXT("Frame"));
                bool Removed=false;Wall->TryGetBoolField(TEXT("glassRemoved"),Removed);
                if (!Removed)
                {
                    bool Open=false;Wall->TryGetBoolField(TEXT("open"),Open);
                    if (Storefront) {auto Glass=Box(Left+2.7,-.2,Bottom+2.7,Right-2.7,.2,Top-2.7);Glass->SetBoolField(TEXT("glass"),true);Part(Glass,TEXT("Glass"));}
                    else
                    {
                        auto Upper=Box(Left+2.7,2.2,146.7,Right-2.7,2.6,Top-2.7);Upper->SetBoolField(TEXT("glass"),true);Part(Upper,TEXT("Glass"));
                        auto Lower=Box(Left+2.7,-2.6,Bottom+2.7,Right-2.7,-2.2,144);Lower->SetBoolField(TEXT("glass"),true);Part(Lower,TEXT("Glass"),0,FString(),Open?56.3:0);
                    }
                }
            }
            if (DoorPresent)
            {
                const FString DoorId=Address+(North?TEXT(":N"):TEXT(":W"));bool Glazed=false;Wall->TryGetBoolField(TEXT("doorGlass"),Glazed);
                if (!Glazed) Part(Box(0,-2.25,.3,74,2.25,202),TEXT("Wood"),13,DoorId);
                else
                {
                    Part(Box(0,-2.25,.3,7,2.25,202),TEXT("Wood"),13,DoorId);Part(Box(67,-2.25,.3,74,2.25,202),TEXT("Wood"),13,DoorId);
                    Part(Box(7,-2.25,.3,67,2.25,65),TEXT("Wood"),13,DoorId);Part(Box(7,-2.25,190,67,2.25,202),TEXT("Wood"),13,DoorId);
                    auto Glass=Box(7,-.2,65,67,.2,190);Glass->SetBoolField(TEXT("glass"),true);Part(Glass,TEXT("Glass"),13,DoorId);
                }
                Part(Box(63,-6,94,70,-2.25,95.6),TEXT("Metal"),13,DoorId);Part(Box(63,2.25,94,70,6,95.6),TEXT("Metal"),13,DoorId);
            }
        }
    }
    const TSharedPtr<FJsonObject>* Stairs=nullptr;
    if (Cell->TryGetObjectField(TEXT("stairs"),Stairs))
    {
        const auto Stair=*Stairs;const int32 Section=Stair->GetNumberField(TEXT("part"));const bool North=Stair->GetStringField(TEXT("axis"))==TEXT("N");
        for (int32 I=0;I<5;++I)
        {
            const double Top=(Section+(I+1)/5.)*Floor/3;
            const auto Shape=North?Box(0,80-I*20,Top-Slab-Floor/15,100,100-I*20,Top):Box(80-I*20,0,Top-Slab-Floor/15,100-I*20,100,Top);
            Place(Address,Shape,Position,0,{TEXT("Wood")},Save);
        }
    }
}
