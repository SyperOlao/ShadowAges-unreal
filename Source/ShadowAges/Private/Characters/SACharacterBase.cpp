#include "Characters/SACharacterBase.h"
#include "Inventory/SAInventoryComponent.h"
#include "Inventory/SAEquipmentComponent.h"
#include "Anatomy/SAAnatomyComponent.h"
#include "Anatomy/SACorpseSliceAdapterComponent.h"
#include "AI/SAAnatomyAIController.h"
#include "Magic/SASpellcastingComponent.h"
#include "Magic/SASpellTargetingComponent.h"
#include "Magic/SAStatusEffectComponent.h"
#include "Combat/SACombatComponent.h"
#include "Combat/SAMeleeDamageReceiverComponent.h"
#include "Combat/Tracing/SAMeleeTraceComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Vitals/SAVitalsComponent.h"

ASACharacterBase::ASACharacterBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    Vitals = CreateDefaultSubobject<USAVitalsComponent>(TEXT("Vitals"));
    Combat = CreateDefaultSubobject<USACombatComponent>(TEXT("Combat"));
    MeleeReceiver = CreateDefaultSubobject<USAMeleeDamageReceiverComponent>(TEXT("MeleeReceiver"));
    MeleeTrace = CreateDefaultSubobject<USAMeleeTraceComponent>(TEXT("MeleeTrace"));
    Anatomy = CreateDefaultSubobject<USAAnatomyComponent>(TEXT("Anatomy"));
    CorpseSliceAdapter = CreateDefaultSubobject<USACorpseSliceAdapterComponent>(TEXT("CorpseSliceAdapter"));
    Spellcasting = CreateDefaultSubobject<USASpellcastingComponent>(TEXT("Spellcasting"));
    SpellTargeting = CreateDefaultSubobject<USASpellTargetingComponent>(TEXT("SpellTargeting"));
    StatusEffects = CreateDefaultSubobject<USAStatusEffectComponent>(TEXT("StatusEffects"));
    Inventory = CreateDefaultSubobject<USAInventoryComponent>(TEXT("Inventory"));
    Equipment = CreateDefaultSubobject<USAEquipmentComponent>(TEXT("Equipment"));
    AIControllerClass = ASAAnatomyAIController::StaticClass();
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Overlap);
}

void ASACharacterBase::BeginPlay()
{
    Super::BeginPlay();
    DeathHandle = Vitals->OnDied.AddUObject(this, &ASACharacterBase::HandleDeath);
    SpellTargeting->SetMuzzleComponent(GetMesh());
    Spellcasting->ConfigureTargeting(SpellTargeting, GetMesh());
    Equipment->ConfigureMesh(GetMesh());
}

void ASACharacterBase::HandleDeath()
{
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
    GetMesh()->SetSimulatePhysics(true);
}

void ASACharacterBase::EndPlay(const EEndPlayReason::Type Reason)
{
    Vitals->OnDied.Remove(DeathHandle);
    Super::EndPlay(Reason);
}
