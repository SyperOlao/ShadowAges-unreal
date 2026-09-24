#include "Magic/SASpellDamage.h"
#include "Combat/SAMeleeDamageReceiverComponent.h"
#include "Magic/SAStatusEffectComponent.h"
#include "GameFramework/Actor.h"
#include "Vitals/SAVitalsComponent.h"

bool SA::CanApplySpell(AActor* Target, const FSASpellPayload& Payload)
{
    const auto* Receiver = IsValid(Target) ? Target->FindComponentByClass<USAMeleeDamageReceiverComponent>() : nullptr;
    return Receiver && Receiver->CanReceiveSpell(Payload);
}

FSADamageResult SA::ApplySpellImpact(AActor* Target, FSASpellPayload Payload)
{
    auto* Receiver = IsValid(Target) ? Target->FindComponentByClass<USAMeleeDamageReceiverComponent>() : nullptr;
    if (!Receiver) { return {}; }
    const TWeakObjectPtr<AActor> WeakTarget = Target;
    const FSADamageResult Result = Receiver->ResolveSpellHit(Payload);
    if (!Result.bAccepted || Result.bBlocked || Result.bParried || Result.bFatal) { return Result; }
    for (const FSAEffectSpec& Effect : Payload.Effects)
    {
        if (!WeakTarget.IsValid() || WeakTarget->IsActorBeingDestroyed()) { break; }
        auto* Vitals = WeakTarget->FindComponentByClass<USAVitalsComponent>();
        auto* Effects = WeakTarget->FindComponentByClass<USAStatusEffectComponent>();
        if (!Vitals || !Vitals->IsAlive() || !Effects) { break; }
        Effects->ApplyEffect(Effect, Payload);
    }
    return Result;
}
