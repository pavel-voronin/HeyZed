#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// The laboratory's convex join/cut kernel, in native centimetres.
namespace HeyZedConstruction {
inline TArray<FVector2D> Clip2D(const TArray<FVector2D>& Polygon,double A,double B,double C)
{
    TArray<FVector2D> Result;
    for (int32 I=0;I<Polygon.Num();++I)
    {
        const auto P=Polygon[I],Q=Polygon[(I+1)%Polygon.Num()];
        const double D=A*P.X+B*P.Y-C,E=A*Q.X+B*Q.Y-C;
        if (D>=-1.e-7) Result.Add(P);
        if ((D>0 && E<0) || (D<0 && E>0)) Result.Add(P+(Q-P)*(D/(D-E)));
    }
    return Result;
}

using FFace=TArray<FVector>;
using FSolid=TArray<FFace>;
using FSolids=TArray<FSolid>;
inline FVector Normal(const FFace& Face)
{
    for(int32 I=1;I+1<Face.Num();++I){auto N=FVector::CrossProduct(Face[I]-Face[0],Face[I+1]-Face[0]).GetSafeNormal();if(!N.IsNearlyZero())return N;}
    return FVector::ZeroVector;
}
inline double Volume(const FSolid& Solid)
{
    double V=0;for(const auto& Face:Solid)for(int32 I=1;I+1<Face.Num();++I)V+=FVector::DotProduct(Face[0],FVector::CrossProduct(Face[I],Face[I+1]))/6;
    return FMath::Abs(V);
}
inline FSolid Clip(const FSolid& Solid,const FVector& N,double C)
{
    constexpr double E=1.e-6;bool Inside=false,Outside=false;
    for(const auto& Face:Solid)for(auto P:Face){const double D=FVector::DotProduct(N,P)-C;Inside|=D<-E;Outside|=D>E;}
    if(!Outside)return Solid;if(!Inside)return {};
    FSolid Result;FFace Cap;
    auto Unique=[&](FVector P){for(auto Q:Cap)if(P.Equals(Q,E))return;Cap.Add(P);};
    for(const auto& Face:Solid)
    {
        FFace Loop;
        for(int32 I=0;I<Face.Num();++I)
        {
            auto A=Face[I],B=Face[(I+1)%Face.Num()];double DA=FVector::DotProduct(N,A)-C,DB=FVector::DotProduct(N,B)-C;
            if(DA<=E)Loop.Add(A);
            if((DA<-E&&DB>E)||(DA>E&&DB<-E)){auto P=A+(B-A)*(DA/(DA-DB));Loop.Add(P);Unique(P);}
            if(FMath::Abs(DA)<=E)Unique(A);
        }
        for(int32 I=Loop.Num()-1;I>=0&&Loop.Num()>1;--I)if(Loop[I].Equals(Loop[(I+1)%Loop.Num()],E))Loop.RemoveAt(I);
        if(Loop.Num()>=3&&!Normal(Loop).IsNearlyZero())Result.Add(MoveTemp(Loop));
    }
    if(Cap.Num()>=3)
    {
        FVector Center=FVector::ZeroVector;for(auto P:Cap)Center+=P;Center/=Cap.Num();
        auto U=FVector::CrossProduct(FMath::Abs(N.X)<.8?FVector::ForwardVector:FVector::RightVector,N).GetSafeNormal(),V=FVector::CrossProduct(N,U);
        Cap.Sort([&](auto A,auto B){return FMath::Atan2(FVector::DotProduct(A-Center,V),FVector::DotProduct(A-Center,U))<FMath::Atan2(FVector::DotProduct(B-Center,V),FVector::DotProduct(B-Center,U));});
        Result.Add(MoveTemp(Cap));
    }
    return Result.Num()>=4&&Volume(Result)>1.e-5?Result:FSolid{};
}
inline FSolids Subtract(const FSolid& A,const FSolid& B)
{
    FBox BoundsA(ForceInit),BoundsB(ForceInit);for(auto& F:A)for(auto P:F)BoundsA+=P;for(auto& F:B)for(auto P:F)BoundsB+=P;
    // A face/edge contact removes no volume. Avoid running the clipping kernel
    // for adjacent canonical cells without introducing a geometric clearance.
    if(!BoundsA.Intersect(BoundsB)||BoundsA.Max.X<=BoundsB.Min.X||BoundsB.Max.X<=BoundsA.Min.X
        ||BoundsA.Max.Y<=BoundsB.Min.Y||BoundsB.Max.Y<=BoundsA.Min.Y
        ||BoundsA.Max.Z<=BoundsB.Min.Z||BoundsB.Max.Z<=BoundsA.Min.Z)return {A};
    auto Rest=A;
    for(auto& F:B){auto N=Normal(F);Rest=Clip(Rest,N,FVector::DotProduct(N,F[0]));if(Rest.IsEmpty())return {A};}
    FSolids Result;Rest=A;
    for(auto& F:B){auto N=Normal(F);double C=FVector::DotProduct(N,F[0]);auto Outside=Clip(Rest,-N,-C);if(!Outside.IsEmpty())Result.Add(MoveTemp(Outside));Rest=Clip(Rest,N,C);if(Rest.IsEmpty())break;}
    return Result;
}
inline FSolids Subtract(FSolids Parts,const FSolids& Cuts)
{
    for(auto& Cut:Cuts){FSolids Next;for(auto& Part:Parts)Next.Append(Subtract(Part,Cut));Parts=MoveTemp(Next);}return Parts;
}
inline FSolids Read(const TArray<TSharedPtr<FJsonValue>>& Values)
{
    FSolids Result;for(auto& S:Values){FSolid Solid;for(auto& F:S->AsArray()){FFace Face;for(auto& P:F->AsArray()){auto& V=P->AsArray();Face.Add(FVector(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber()));}Solid.Add(MoveTemp(Face));}Result.Add(MoveTemp(Solid));}return Result;
}
inline TArray<TSharedPtr<FJsonValue>> Write(const FSolids& Solids)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for(auto& Solid:Solids){TArray<TSharedPtr<FJsonValue>> Faces;for(auto& Face:Solid){TArray<TSharedPtr<FJsonValue>> Points;for(auto P:Face){for(int32 I=0;I<3;++I)P[I]=FMath::RoundToDouble(P[I]*10000)/10000;Points.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)}));}Faces.Add(MakeShared<FJsonValueArray>(Points));}Result.Add(MakeShared<FJsonValueArray>(Faces));}return Result;
}
inline void Transform(FSolids& Solids,const FTransform& Transform){for(auto& Solid:Solids)for(auto& Face:Solid)for(auto& P:Face)P=Transform.TransformPosition(P);}
struct FHost
{
    FString Key,Type;int32 Rank=1;FVector Origin=FVector::ZeroVector;bool North=true;
    TArray<FString> Operations;FSolids Solids;
};
inline void Join(TArray<FHost>& Hosts)
{
    struct FEnd{int32 Host;FVector Point,Direction;};TMap<FString,TArray<FEnd>> Nodes;
    for(int32 I=0;I<Hosts.Num();++I)
    {
        auto& H=Hosts[I];if(H.Solids.IsEmpty())continue;auto D=H.North?FVector(1,0,0):FVector(0,1,0);
        for(int32 End=0;End<2;++End){auto P=H.Origin+D*(End*100);Nodes.FindOrAdd(P.ToString()).Add({I,P,End?-D:D});}
    }
    TArray<FString> Keys;Nodes.GetKeys(Keys);Keys.Sort();
    for(auto& Key:Keys)
    {
        auto Participants=Nodes[Key];if(Participants.Num()<2)continue;
        Participants.Sort([&](auto& A,auto& B){if(Hosts[A.Host].North!=Hosts[B.Host].North)return Hosts[A.Host].North;return Hosts[A.Host].Key<Hosts[B.Host].Key;});
        TArray<FEnd> Occupied;
        auto Permits=[&](const FEnd& E,const FString& Op){return Hosts[E.Host].Operations.IsEmpty()||Hosts[E.Host].Operations.Contains(Op);};
        auto Abut=[&](const FEnd& E){if(!Permits(E,TEXT("abut")))return;for(auto& Other:Occupied)Hosts[E.Host].Solids=Subtract(MoveTemp(Hosts[E.Host].Solids),Hosts[Other.Host].Solids);Occupied.Add(E);};
        auto Plane=[&](const FEnd& E,FVector N){FSolids Next;for(auto& S:Hosts[E.Host].Solids){auto Part=Clip(S,-N,-FVector::DotProduct(N,E.Point));if(!Part.IsEmpty())Next.Add(MoveTemp(Part));}Hosts[E.Host].Solids=MoveTemp(Next);};
        auto Assemble=[&](const TArray<FEnd>& Group)
        {
            if(Group.IsEmpty())return;if(!Occupied.IsEmpty()){for(auto& E:Group)Abut(E);return;}
            int32 A=INDEX_NONE,B=INDEX_NONE;
            for(int32 Axis=0;Axis<2&&A==INDEX_NONE;++Axis)for(int32 I=0;I<Group.Num();++I)if(Group[I].Direction[Axis]==1)for(int32 J=0;J<Group.Num();++J)if(Group[J].Direction[Axis]==-1){A=I;B=J;break;}
            if(A!=INDEX_NONE&&Permits(Group[A],TEXT("middle"))&&Permits(Group[B],TEXT("middle")))
            {Plane(Group[A],Group[A].Direction);Plane(Group[B],Group[B].Direction);Occupied.Add(Group[A]);Occupied.Add(Group[B]);for(int32 I=0;I<Group.Num();++I)if(I!=A&&I!=B)Abut(Group[I]);}
            else if(A==INDEX_NONE&&Group.Num()==2&&Permits(Group[0],TEXT("diagonal"))&&Permits(Group[1],TEXT("diagonal")))
            {auto N=Group[0].Direction-Group[1].Direction;Plane(Group[0],N);Plane(Group[1],-N);Occupied.Append(Group);}
            else for(auto& E:Group){if(Occupied.IsEmpty())Occupied.Add(E);else Abut(E);}
        };
        for(int32 Rank=1;Rank<=2;++Rank)
        {
            TMap<FString,TArray<FEnd>> Groups;for(auto& E:Participants)if(Hosts[E.Host].Rank==Rank)Groups.FindOrAdd(Hosts[E.Host].Type).Add(E);
            TArray<FString> Types;Groups.GetKeys(Types);Types.Sort();TSet<int32> Used;
            for(auto& Type:Types)if(Groups[Type].Num()>1){Assemble(Groups[Type]);for(auto& E:Groups[Type])Used.Add(E.Host);}
            TArray<FEnd> Others;for(auto& E:Participants)if(Hosts[E.Host].Rank==Rank&&!Used.Contains(E.Host))Others.Add(E);Assemble(Others);
        }
    }
}
}
