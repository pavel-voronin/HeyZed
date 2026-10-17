#include "HeyZedFixture.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
AHeyZedWorldFixture::AHeyZedWorldFixture()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
void AHeyZedWorldFixture::Configure(const FString& Kind,const FHeyZedFixtureProfile& Profile,UHeyZedFixtureLibrary* Library,bool Support)
{
    const FString Key=Kind+(Support?TEXT(":support"):TEXT(""));
    if(Signature==Key)return;
    Signature=Key;bPowerInitialized=false;
    const auto Mobility=Kind==TEXT("held")?EComponentMobility::Movable:EComponentMobility::Static;
    RootComponent->SetMobility(Mobility);
    for(auto& Part:Parts)if(Part)Part->DestroyComponent();Parts.Reset();Emissions.Reset();
    if(Label){Label->DestroyComponent();Label=nullptr;}
    auto Part=[&](UStaticMesh* Mesh,FVector Position,FVector Scale,UMaterialInterface* Material,bool Shadow=true)
    {
        auto* C=NewObject<UStaticMeshComponent>(this);C->SetupAttachment(RootComponent);
        C->SetMobility(Mobility);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);C->SetCastShadow(Shadow);C->SetStaticMesh(Mesh);
        C->SetRelativeLocation(Position);C->SetRelativeScale3D(Scale);
        if(Material)C->SetMaterial(0,Material);
        C->RegisterComponent();Parts.Add(C);return C;
    };
    UStaticMesh* Cube=Library->Cube;
    if(Profile.Sign)
    {
        Part(Cube,FVector(-40,0,190),FVector(.08,1.2,.4),Library->HousingMaterial);
        auto* Face=Part(Cube,FVector(-35.8,0,190),FVector(.002,1.12,.32),Library->EmissionMaterial,false);
        Emissions.Add(Face->CreateDynamicMaterialInstance(0));
        Label=NewObject<UTextRenderComponent>(this);Label->SetupAttachment(RootComponent);
        Label->SetMobility(Mobility);
        Label->SetRelativeLocation(FVector(-35.5,0,190));Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetVerticalAlignment(EVRTA_TextCenter);Label->SetWorldSize(22);
        Label->SetText(FText::FromString(TEXT("OPEN")));Label->SetTextRenderColor(FColor(10,18,15));Label->RegisterComponent();
    }
    else if(Kind==TEXT("switch"))
    {
        Part(Cube,Profile.MeshOffset,Profile.MeshScale,Library->HousingMaterial);
        Part(Cube,Profile.MeshOffset+FVector(1.1,0,0),FVector(.012,.05,.08),nullptr);
    }
    else if(Profile.Mesh)
    {
        auto* Model=Part(Profile.Mesh,Profile.MeshOffset,Profile.MeshScale,nullptr);
        Model->SetRelativeRotation(Profile.MeshRotation);
        for(int32 I=0;I<Model->GetNumMaterials();++I)
            if(auto* Material=Model->GetMaterial(I);Material&&Material->GetName().Contains(TEXT("FixtureEmission")))
                Emissions.Add(Model->CreateDynamicMaterialInstance(I));
    }
    if(Profile.Bulb)
    {
        UStaticMesh* Sphere=Library->Sphere;
        auto* Bulb=Part(Sphere,FVector(0,0,-12),FVector(.065,.065,.105),Library->EmissionMaterial,false);
        Emissions.Add(Bulb->CreateDynamicMaterialInstance(0));
    }
    if(Support)
    {
        Part(Cube,FVector(0,0,72.5),FVector(.8,.6,.05),Library->SupportMaterial);
        for(float X:{-32.f,32.f})for(float Y:{-22.f,22.f})
            Part(Cube,FVector(X,Y,35),FVector(.05,.05,.7),Library->SupportMaterial);
    }
}
void AHeyZedWorldFixture::Power(bool Enabled,const FLinearColor& Color)
{
    if(bPowerInitialized&&bEnabled==Enabled&&LastColor.Equals(Color))return;
    bPowerInitialized=true;bEnabled=Enabled;LastColor=Color;
    for(auto& Material:Emissions)if(Material)
        Material->SetVectorParameterValue(TEXT("Emission"),Enabled?Color*4.f:FLinearColor::Black);
}
