#include "Magic/SASpellDefinition.h"
#include "Misc/DataValidation.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SpellFire, "Spell.Fire");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SpellFireRay, "Spell.FireRay");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SpellSlowBurst, "Spell.SlowBurst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SpellRestoreMana, "Spell.RestoreMana");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CooldownFire, "Cooldown.Spell.Fire");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CooldownSlowBurst, "Cooldown.Spell.SlowBurst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CooldownRestoreMana, "Cooldown.Spell.RestoreMana");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EffectBurn, "Effect.Burn");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EffectSlow, "Effect.Slow");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EffectMana, "Effect.RestoreMana");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EffectSilence, "Effect.Silence");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EffectDisarm, "Effect.Disarm");

bool FSAEffectSpec::Validate(FString& Error) const
{
    const bool bModifier = Kind == ESAEffectKind::Slow || Kind == ESAEffectKind::Silence || Kind == ESAEffectKind::Disarm;
    if (!EffectTag.IsValid() || uint8(Kind) > uint8(ESAEffectKind::Disarm)
        || uint8(Lifetime) > uint8(ESAEffectLifetime::Duration) || uint8(StackRule) > uint8(ESAEffectStackRule::Ignore)
        || !FMath::IsFinite(Magnitude) || Magnitude < 0.f || Magnitude > 1.e6f || !FMath::IsFinite(Duration) || Duration < 0.f
        || !FMath::IsFinite(Period) || Period < 0.f || !FMath::IsFinite(Chance) || Chance < 0.f || Chance > 1.f
        || MaxStacks < 1 || MaxStacks > 32 || (Kind == ESAEffectKind::Slow && Magnitude > 1.f)
        || (Lifetime == ESAEffectLifetime::Instant && (bModifier || Period != 0.f || Duration != 0.f))
        || (Lifetime == ESAEffectLifetime::Duration && (Duration <= 0.f || Duration > 3600.f
            || (bModifier ? Period != 0.f : (Period < .05f || Period > Duration)))))
    {
        Error = TEXT("Invalid effect: check tag, lifetime, kind, finite values, stacks and period (minimum 0.05s).");
        return false;
    }
    return true;
}

bool USASpellDefinition::Validate(FString& Error) const
{
    const auto FiniteRange = [](float Value, float Min, float Max)
    { return FMath::IsFinite(Value) && Value >= Min && Value <= Max; };
    if (uint8(Delivery) > uint8(ESASpellDelivery::Ray)) { Error = TEXT("Unsupported delivery."); return false; }
    if (!SpellTag.IsValid() || !CooldownGroup.IsValid() || uint8(TargetPolicy) > uint8(ESASpellTargetPolicy::Self)
        || !FiniteRange(ManaCost, 0.f, 1.e6f) || !FiniteRange(Damage, 0.f, 1.e6f)
        || !FiniteRange(WindupSeconds, 0.f, 60.f) || !FiniteRange(RecoverySeconds, 0.f, 60.f)
        || !FiniteRange(CooldownSeconds, 0.f, 3600.f) || !FiniteRange(RangeCm, 1.f, 100000.f)
        || !FiniteRange(ProjectileRadiusCm, .1f, 200.f) || !FiniteRange(ProjectileSpeedCmS, .1f, 100000.f)
        || !FiniteRange(AreaRadiusCm, 0.f, 10000.f) || MaxAreaTargets < 1 || MaxAreaTargets > 64
        || (!bUseComponentOrigin && MuzzleSocket.IsNone()) || ImpactEffects.Num() > 16
        || (Delivery == ESASpellDelivery::Projectile && ProjectileClass.IsNull())
        || (Delivery != ESASpellDelivery::Instant && (AreaRadiusCm > 0.f || TargetPolicy == ESASpellTargetPolicy::Self))
        || (TargetPolicy == ESASpellTargetPolicy::Self && AreaRadiusCm > 0.f)
        || !FiniteRange(WindupPolicy.Movement.MoveInputScale, 0.f, 1.f)
        || !FiniteRange(RecoveryPolicy.Movement.MoveInputScale, 0.f, 1.f))
    {
        Error = TEXT("Invalid spell data: tags, assets, finite ranges, delivery/target policy or movement.");
        return false;
    }
    for (const auto& Effect : ImpactEffects) { if (!Effect.Validate(Error)) { return false; } }
    Error.Reset();
    return true;
}

#if WITH_EDITOR
EDataValidationResult USASpellDefinition::IsDataValid(FDataValidationContext& Context) const
{
    FString Error;
    if (!Validate(Error)) { Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid; }
    return EDataValidationResult::Valid;
}
#endif
