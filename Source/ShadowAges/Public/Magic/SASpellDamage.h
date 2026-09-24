#pragma once
#include "Magic/SAMagicTypes.h"

namespace SA
{
    SHADOWAGES_API bool CanApplySpell(AActor* Target, const FSASpellPayload& Payload);
    SHADOWAGES_API FSADamageResult ApplySpellImpact(AActor* Target, FSASpellPayload Payload);
}
