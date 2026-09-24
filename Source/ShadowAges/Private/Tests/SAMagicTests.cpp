#include "Magic/SASpellcastingComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Magic/SASpellDefinition.h"
#include "Magic/SASpellTargetingComponent.h"
#include "Magic/SAStatusEffectComponent.h"
#include "Magic/SASpellDamage.h"
#include "Magic/SAProjectile.h"
#include "Combat/SACombatComponent.h"
#include "Combat/SAMeleeDamageReceiverComponent.h"
#include "Vitals/SAVitalsComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace SAMagicTests
{
struct FActorSetup
{
    AActor* Actor;
    USAVitalsComponent* Vitals;
    USACombatComponent* Combat;
    USAMeleeDamageReceiverComponent* Receiver;
    USASpellcastingComponent* Caster;
    USASpellTargetingComponent* Targeting;
    USAStatusEffectComponent* Effects;
    template<typename T> T* Add()
    { T* Component = NewObject<T>(Actor); Actor->AddInstanceComponent(Component); Component->RegisterComponent(); return Component; }
    explicit FActorSetup(UWorld* World, FVector Location = FVector::ZeroVector)
    {
        Actor = World->SpawnActor<AActor>();
        auto* Root = Add<USceneComponent>(); Actor->SetRootComponent(Root); Actor->SetActorLocation(Location);
        Vitals = Add<USAVitalsComponent>(); Combat = Add<USACombatComponent>(); Receiver = Add<USAMeleeDamageReceiverComponent>();
        Effects = Add<USAStatusEffectComponent>(); Targeting = Add<USASpellTargetingComponent>(); Caster = Add<USASpellcastingComponent>();
        Actor->DispatchBeginPlay();
        Targeting->SetMuzzleComponent(Root); Caster->ConfigureTargeting(Targeting, nullptr);
    }
    FSASpellPayload Source() const
    {
        FSASpellPayload Payload;
        Payload.Context.SourceActor = Actor; Payload.Context.SourceCreditId = Receiver->GetSourceCreditId();
        Payload.Context.Playback.Action.Id = FGuid::NewGuid();
        return Payload;
    }
};
struct FScene
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FActorSetup Source{World};
    FActorSetup Target{World, FVector(500,0,0)};
    ~FScene() { World->DestroyWorld(false); }
    USASpellDefinition* Spell(ESASpellDelivery Delivery = ESASpellDelivery::Projectile)
    {
        auto* Def = NewObject<USASpellDefinition>(Source.Actor);
        Def->SpellTag = FGameplayTag::RequestGameplayTag(TEXT("Spell.Fire"));
        Def->CooldownGroup = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Spell.Fire"));
        Def->Delivery = Delivery; Def->ProjectileClass = ASAProjectile::StaticClass(); Def->bUseComponentOrigin = true;
        return Def;
    }
    FSAEffectSpec Burn()
    {
        FSAEffectSpec Spec; Spec.EffectTag = FGameplayTag::RequestGameplayTag(TEXT("Effect.Burn"));
        Spec.Lifetime = ESAEffectLifetime::Duration; Spec.Duration = 5.f; Spec.Period = 1.f;
        Spec.Magnitude = 5.f; Spec.MaxStacks = 3;
        return Spec;
    }
    ASAProjectile* Projectile() const
    { for (TActorIterator<ASAProjectile> It(World); It; ++It) { if (!It->IsActorBeingDestroyed()) { return *It; } } return nullptr; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAManaTest, "ShadowAges.Magic.ManaReservations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAManaTest::RunTest(const FString& Parameters)
{
    SAMagicTests::FScene S;
    int32 Events = 0;
    S.Source.Vitals->OnManaChanged.AddLambda([&](float, float) { ++Events; });
    FSAActionHandle Action; Action.Id = FGuid::NewGuid();
    FGuid Reservation;
    TestTrue(TEXT("Reserve mana"), S.Source.Vitals->TryReserveMana(Action, 60.f, Reservation));
    TestEqual(TEXT("Reserve is not payment"), S.Source.Vitals->GetMana(), 100.f);
    TestEqual(TEXT("Available excludes reserve"), S.Source.Vitals->GetAvailableMana(), 40.f);
    FSAActionHandle Other; Other.Id = FGuid::NewGuid(); FGuid Rejected;
    TestFalse(TEXT("Cannot spend reserved mana"), S.Source.Vitals->TryReserveMana(Other, 50.f, Rejected));
    S.Source.Vitals->ReleaseMana(Reservation);
    TestEqual(TEXT("Cancellation generates no gain event"), Events, 0);
    TestTrue(TEXT("Zero cost has a valid reservation"), S.Source.Vitals->TryReserveMana(Action, 0.f, Reservation));
    FSAManaMutation Mutation;
    TestTrue(TEXT("Commit zero cost"), S.Source.Vitals->CommitManaWithoutEvents(Reservation, Mutation));
    S.Source.Vitals->PublishManaChange(Mutation);
    TestEqual(TEXT("Zero cost has no change event"), Events, 0);
    S.Source.Vitals->TryReserveMana(Other, 30.f, Reservation);
    S.Source.Vitals->CommitManaWithoutEvents(Reservation, Mutation);
    TestEqual(TEXT("Commit is silent"), Events, 0);
    S.Source.Vitals->PublishManaChange(Mutation); S.Source.Vitals->PublishManaChange(Mutation);
    TestEqual(TEXT("Publication idempotent"), Events, 1);
    S.Source.Vitals->RestoreMana(999.f); S.Source.Vitals->RestoreMana(1.f);
    TestEqual(TEXT("Restore clamps"), S.Source.Vitals->GetMana(), 100.f);
    TestEqual(TEXT("Full regen silent"), Events, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSACastLifecycleTest, "ShadowAges.Magic.CastLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSACastLifecycleTest::RunTest(const FString& Parameters)
{
    SAMagicTests::FScene S;
    auto* Def = S.Spell(); S.Source.Caster->SetPreparedSpell(Def);
    int32 Finishes = 0;
    S.Source.Combat->OnActionFinished.AddLambda([&](FSAActionHandle, ESAActionEndReason) { ++Finishes; });
    FSASpellAim Aim; Aim.ViewDirection = FVector(1,0,1);
    FSAActionHandle First;
    TestTrue(TEXT("Cast accepted"), S.Source.Caster->RequestCast(Aim, First) == ESACastFailure::None);
    TestEqual(TEXT("Windup reserves only"), S.Source.Vitals->GetMana(), 100.f);
    TestEqual(TEXT("Windup available mana"), S.Source.Vitals->GetAvailableMana(), 50.f);
    S.Source.Combat->CancelActionIfCurrent(First, ESAActionEndReason::Interrupted);
    S.Source.Caster->ReleaseCast(First);
    TestEqual(TEXT("Cancelled reserve returned"), S.Source.Vitals->GetAvailableMana(), 100.f);
    TestNull(TEXT("Stale release does not spawn"), S.Projectile());
    TestEqual(TEXT("One finish"), Finishes, 1);
    FSAActionHandle Second;
    S.Source.Caster->RequestCast(Aim, Second);
    S.Source.Caster->ReleaseCast(Second); S.Source.Caster->ReleaseCast(Second);
    TestEqual(TEXT("Repeated release spends once"), S.Source.Vitals->GetMana(), 50.f);
    ASAProjectile* Projectile = S.Projectile();
    TestNotNull(TEXT("Projectile spawned"), Projectile);
    if (Projectile)
    {
        TestTrue(TEXT("Vertical velocity uses normalized aim"), FMath::IsNearlyEqual(Projectile->Movement->Velocity.Z, 500./FMath::Sqrt(2.), .01));
    }
    TestTrue(TEXT("Cooldown starts at release"), S.Source.Caster->GetCooldownRemaining(Def->CooldownGroup) > 0.f);
    S.Source.Caster->AdvanceCast(100.);
    TestEqual(TEXT("Recovery owns second completion"), Finishes, 2);
    FSAActionHandle Rejected;
    TestTrue(TEXT("Cooldown rejects before reserve"), S.Source.Caster->RequestCast(Aim, Rejected) == ESACastFailure::Cooldown);
    TestFalse(TEXT("Rejected cast has no handle"), Rejected.IsValid());
    TestEqual(TEXT("Rejected cast has no completion"), Finishes, 2);
    const auto NewAction = S.Source.Combat->ReserveAction(ESAActionKind::Cast);
    if (Projectile)
    {
        FHitResult Hit(S.Target.Actor, nullptr, S.Target.Actor->GetActorLocation(), -FVector::ForwardVector);
        Projectile->ConsumeImpact(Hit); Projectile->ConsumeImpact(Hit);
        TestEqual(TEXT("Projectile impacts once"), S.Target.Vitals->GetHealth(), 50.f);
        TestTrue(TEXT("Old projectile cannot end new action"), S.Source.Combat->IsCurrentAction(NewAction));
    }
    S.Source.Combat->CancelActionIfCurrent(NewAction, ESAActionEndReason::Cancelled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSACastRejectionsTest, "ShadowAges.Magic.RejectionsAndBlockedMuzzle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSACastRejectionsTest::RunTest(const FString& Parameters)
{
    SAMagicTests::FScene S;
    auto* Def = S.Spell(); Def->ManaCost = 101.f; S.Source.Caster->SetPreparedSpell(Def);
    FSASpellAim Aim; FSAActionHandle Handle;
    TestTrue(TEXT("Insufficient mana rejected"), S.Source.Caster->RequestCast(Aim, Handle) == ESACastFailure::NotEnoughMana);
    Def->ManaCost = 50.f; Def->Delivery = static_cast<ESASpellDelivery>(255);
    TestTrue(TEXT("Unknown executor rejected"), S.Source.Caster->RequestCast(Aim, Handle) == ESACastFailure::UnsupportedDelivery);
    Def->Delivery = ESASpellDelivery::Projectile;
    AActor* Wall = S.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall); Wall->AddInstanceComponent(Box); Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(30)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
    TestTrue(TEXT("Windup accepted before geometry release check"), S.Source.Caster->RequestCast(Aim, Handle) == ESACastFailure::None);
    S.Source.Caster->ReleaseCast(Handle);
    TestEqual(TEXT("Blocked release spends nothing"), S.Source.Vitals->GetMana(), 100.f);
    TestEqual(TEXT("Blocked release returns reserve"), S.Source.Vitals->GetAvailableMana(), 100.f);
    TestEqual(TEXT("Blocked release no cooldown"), S.Source.Caster->GetCooldownRemaining(Def->CooldownGroup), 0.f);
    TestFalse(TEXT("Accepted failure completed"), S.Source.Combat->IsBusy());
    TestNull(TEXT("Blocked muzzle no projectile"), S.Projectile());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAEffectsTest, "ShadowAges.Magic.EffectGridAndStacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAEffectsTest::RunTest(const FString& Parameters)
{
    SAMagicTests::FScene S;
    FSAEffectSpec Burn = S.Burn();
    auto Source = S.Source.Source();
    const FGuid Id = S.Target.Effects->ApplyEffect(Burn, Source);
    TestTrue(TEXT("Burn accepted"), Id.IsValid());
    TestEqual(TEXT("No immediate burn tick"), S.Target.Vitals->GetHealth(), 100.f);
    S.Target.Effects->AdvanceEffects(1.);
    TestEqual(TEXT("First periodic tick"), S.Target.Vitals->GetHealth(), 95.f);
    Burn.Magnitude = 50.f;
    TestTrue(TEXT("Refresh reuses identity"), S.Target.Effects->ApplyEffect(Burn, Source) == Id);
    auto Records = S.Target.Effects->GetActiveEffects();
    TestEqual(TEXT("Refresh keeps original magnitude"), Records[0].Spec.Magnitude, 5.f);
    TestEqual(TEXT("Refresh preserves next tick"), Records[0].NextTickIndex, int64(2));
    S.Target.Effects->AdvanceEffects(5.);
    TestEqual(TEXT("Burn 5/1 includes endpoint tick"), S.Target.Vitals->GetHealth(), 75.f);
    TestEqual(TEXT("Expiration removes record"), S.Target.Effects->GetActiveEffects().Num(), 0);
    Burn = S.Burn(); Burn.StackRule = ESAEffectStackRule::AddStacks;
    S.Target.Effects->ApplyEffect(Burn, Source);
    for (int32 I = 0; I < 10; ++I) { S.Target.Effects->ApplyEffect(Burn, Source); }
    TestEqual(TEXT("Stacks capped"), S.Target.Effects->GetActiveEffects()[0].Stacks, 3);
    S.Target.Effects->ClearEffects();
    FSAEffectSpec Slow;
    Slow.EffectTag = FGameplayTag::RequestGameplayTag(TEXT("Effect.Slow")); Slow.Kind = ESAEffectKind::Slow;
    Slow.Lifetime = ESAEffectLifetime::Duration; Slow.Duration = 2.f; Slow.Magnitude = .3f;
    const auto SlowId = S.Target.Effects->ApplyEffect(Slow, Source);
    TestTrue(TEXT("Period-zero duration is a modifier"), FMath::IsNearlyEqual(S.Target.Effects->GetMovementMultiplier(), .7f));
    S.Target.Effects->RemoveEffect(SlowId);
    TestEqual(TEXT("Removal recomputes base"), S.Target.Effects->GetMovementMultiplier(), 1.f);
    Slow.Kind = ESAEffectKind::Damage;
    FString Error;
    TestFalse(TEXT("Duration damage without positive period invalid"), Slow.Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSALateBurnTest, "ShadowAges.Magic.LateBurnAndTickBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSALateBurnTest::RunTest(const FString& Parameters)
{
    SAMagicTests::FScene S;
    auto Burn = S.Burn(); Burn.Period = .05f; Burn.Duration = 2.f; Burn.Magnitude = 0.f;
    const auto Source = S.Source.Source();
    S.Target.Effects->ApplyEffect(Burn, Source);
    S.Target.Effects->AdvanceEffects(2.);
    TestTrue(TEXT("Hitch carries debt instead of unbounded loop"), S.Target.Effects->EffectTickDebt > 0);
    S.Target.Effects->AdvanceEffects(2.); S.Target.Effects->AdvanceEffects(2.);
    TestEqual(TEXT("Debt drained deterministically"), S.Target.Effects->GetActiveEffects().Num(), 0);
    Burn = S.Burn(); Burn.Magnitude = 25.f;
    S.Target.Effects->ApplyEffect(Burn, Source);
    int32 Deaths = 0;
    S.Target.Vitals->OnDied.AddLambda([&]() { ++Deaths; });
    S.Source.Actor->Destroy();
    S.Target.Effects->AdvanceEffects(5.);
    TestEqual(TEXT("Burn survives source destruction"), S.Target.Vitals->GetHealth(), 0.f);
    TestEqual(TEXT("One death"), Deaths, 1);
    TestTrue(TEXT("Persistent kill attribution"), S.Target.Vitals->GetLastDamageContext().SourceCreditId == Source.Context.SourceCreditId);
    TestEqual(TEXT("Death clears effects during scheduler callback"), S.Target.Effects->GetActiveEffects().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSASpellDeliveryTest, "ShadowAges.Magic.RayInstantAndArea",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSASpellDeliveryTest::RunTest(const FString& Parameters)
{
    SAMagicTests::FScene S;
    const auto AddBody = [](SAMagicTests::FActorSetup& Setup)
    {
        auto* Body = NewObject<USphereComponent>(Setup.Actor);
        Setup.Actor->AddInstanceComponent(Body); Body->SetupAttachment(Setup.Actor->GetRootComponent());
        Body->SetSphereRadius(25.f); Body->SetCollisionObjectType(ECC_Pawn);
        Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Body->SetCollisionResponseToAllChannels(ECR_Block);
        Body->RegisterComponent();
    };
    AddBody(S.Target);
    FSASpellAim Aim;
    auto* Ray = S.Spell(ESASpellDelivery::Ray); Ray->ManaCost = 10.f; Ray->Damage = 10.f; Ray->CooldownSeconds = 0.f;
    S.Source.Caster->SetPreparedSpell(Ray);
    FSAActionHandle Action;
    S.Source.Caster->RequestCast(Aim, Action); S.Source.Caster->ReleaseCast(Action); S.Source.Caster->AdvanceCast(100.);
    TestEqual(TEXT("Ray damages nearest actual collider"), S.Target.Vitals->GetHealth(), 90.f);
    TestNull(TEXT("Ray does not spawn a fake fast projectile"), S.Projectile());
    auto* Instant = S.Spell(ESASpellDelivery::Instant);
    Instant->TargetPolicy = ESASpellTargetPolicy::Self; Instant->Damage = 0.f; Instant->CooldownSeconds = 0.f;
    FSAEffectSpec Restore; Restore.EffectTag = FGameplayTag::RequestGameplayTag(TEXT("Effect.RestoreMana"));
    Restore.Kind = ESAEffectKind::RestoreMana; Restore.Magnitude = 20.f; Instant->ImpactEffects.Add(Restore);
    S.Source.Caster->SetPreparedSpell(Instant);
    TArray<FVector2D> Changes;
    S.Source.Vitals->OnManaChanged.AddLambda([&](float Old, float New) { Changes.Add(FVector2D(Old, New)); });
    S.Source.Caster->RequestCast(Aim, Action); S.Source.Caster->ReleaseCast(Action); S.Source.Caster->AdvanceCast(100.);
    TestEqual(TEXT("Instant restores mana once after paying"), S.Source.Vitals->GetMana(), 60.f);
    TestEqual(TEXT("Cost and instant restore each publish"), Changes.Num(), 2);
    if (Changes.Num() == 2)
    {
        TestTrue(TEXT("Deferred cost published before restore"), Changes[0].Equals(FVector2D(90,40)) && Changes[1].Equals(FVector2D(40,60)));
    }
    SAMagicTests::FActorSetup Second(S.World, FVector(550,40,0)); AddBody(Second); AddBody(S.Target);
    auto* Area = S.Spell(ESASpellDelivery::Instant); Area->ManaCost = 0.f; Area->Damage = 10.f;
    Area->AreaRadiusCm = 150.f; Area->MaxAreaTargets = 1;
    S.Source.Caster->SetPreparedSpell(Area);
    S.Source.Caster->RequestCast(Aim, Action); S.Source.Caster->ReleaseCast(Action);
    TestEqual(TEXT("Area actor deduplicated across two components"), S.Target.Vitals->GetHealth(), 80.f);
    TestEqual(TEXT("Nearest valid target wins cap"), Second.Vitals->GetHealth(), 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSASilenceAndDeathTest, "ShadowAges.Magic.SilenceAndDeathCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSASilenceAndDeathTest::RunTest(const FString& Parameters)
{
    SAMagicTests::FScene S;
    S.Source.Caster->SetPreparedSpell(S.Spell());
    FSASpellAim Aim; FSAActionHandle Action;
    S.Source.Caster->RequestCast(Aim, Action);
    FSAEffectSpec Silence; Silence.EffectTag = FGameplayTag::RequestGameplayTag(TEXT("Effect.Silence"));
    Silence.Kind = ESAEffectKind::Silence; Silence.Lifetime = ESAEffectLifetime::Duration; Silence.Duration = 2.f;
    auto Source = S.Target.Source();
    const auto First = S.Source.Effects->ApplyEffect(Silence, Source);
    TestFalse(TEXT("Silence interrupts active windup"), S.Source.Combat->IsBusy());
    TestEqual(TEXT("Silence returns unpaid reserve"), S.Source.Vitals->GetAvailableMana(), 100.f);
    Source.Context.SourceCreditId = FGuid::NewGuid();
    const auto Second = S.Source.Effects->ApplyEffect(Silence, Source);
    S.Source.Effects->RemoveEffect(First);
    TestTrue(TEXT("Removing one silence preserves another source"), S.Source.Caster->ValidateCast(Aim) == ESACastFailure::Silenced);
    S.Source.Effects->RemoveEffect(Second);
    TestTrue(TEXT("Removing last source lifts silence"), S.Source.Caster->ValidateCast(Aim) == ESACastFailure::None);
    S.Source.Caster->RequestCast(Aim, Action);
    S.Source.Vitals->ApplyHealthLoss(100.f);
    S.Source.Caster->ReleaseCast(Action);
    TestFalse(TEXT("Death finishes cast"), S.Source.Combat->IsBusy());
    TestNull(TEXT("Death never releases unpaid projectile"), S.Projectile());
    TestEqual(TEXT("Death during windup does not pay mana"), S.Source.Vitals->GetMana(), 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAMagicExampleTest, "ShadowAges.Magic.ExampleDefinitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAMagicExampleTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* Name : { TEXT("DA_Spell_Fire"), TEXT("DA_Spell_FireRay"), TEXT("DA_Spell_SlowBurst"), TEXT("DA_Spell_RestoreMana") })
    {
        const FString Path = FString::Printf(TEXT("/Game/Combat/Magic/%s.%s"), Name, Name);
        auto* Def = LoadObject<USASpellDefinition>(nullptr, *Path);
        if (TestNotNull(*Path, Def))
        {
            FString Error;
            TestTrue(*FString::Printf(TEXT("Example validates: %s (%s)"), Name, *Error), Def->Validate(Error));
        }
    }
    return true;
}
#endif
