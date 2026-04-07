#pragma once

#include "Items/RPGItem.h"
#include "RPGSkillItem.generated.h"

/** Native base class for skills, should be blueprinted */
UCLASS()
class ACTIONRPG_API URPGSkillItem : public URPGItem
{
	GENERATED_BODY()

public:
	/** Constructor */
	URPGSkillItem()
	{
		ItemType = URPGAssetManager::SkillItemType;
	}
};