#pragma once
#include "CoreMinimal.h"

struct FHeyZedEntityRootSample;
bool HeyZedEntityRootDiscontinuity(const FHeyZedEntityRootSample& Previous,double Time,const FTransform& Current,double Speed,bool Vehicle);
