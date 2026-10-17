#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HeyZedFirstPerson.generated.h"

UCLASS(Transient)
class UHeyZedFirstPersonInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
