#include "Inventory/SAItemDefinition.h"
#include "Misc/DataValidation.h"
FPrimaryAssetId USAItemDefinition::GetPrimaryAssetId() const
{ return DefinitionKey.IsNone() ? FPrimaryAssetId() : FPrimaryAssetId(TEXT("SAItem"), DefinitionKey); }
bool USAItemDefinition::Validate() const
{
    return !DefinitionKey.IsNone() && uint8(UseKind) <= uint8(ESAItemUseKind::Consumable)
        && MaxStack > 0 && FMath::IsFinite(MaxDurability) && MaxDurability >= 0
        && (MaxDurability == 0 || MaxStack == 1)
        && (UseKind != ESAItemUseKind::MeleeWeapon || (!EquipmentActorClass.IsNull() && !Moveset.IsNull()))
        && (UseKind != ESAItemUseKind::Spell || !Spell.IsNull())
        && (UseKind != ESAItemUseKind::Consumable || (FMath::IsFinite(Healing) && Healing > 0
            && FMath::IsFinite(ConsumeDelay) && ConsumeDelay >= 0 && ConsumeDelay <= 30));
}
#if WITH_EDITOR
EDataValidationResult USAItemDefinition::IsDataValid(FDataValidationContext& Context) const
{
    if (!Validate()) { Context.AddError(FText::FromString(TEXT("Invalid item key, stack, durability or use data."))); return EDataValidationResult::Invalid; }
    return EDataValidationResult::Valid;
}
#endif
