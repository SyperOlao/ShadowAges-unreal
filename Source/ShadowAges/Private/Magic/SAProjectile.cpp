#include "Magic/SAProjectile.h"
#include "Magic/SASpellDamage.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

ASAProjectile::ASAProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    SetRootComponent(Collision);
    Collision->SetSphereRadius(8.f);
    Collision->SetCollisionProfileName(TEXT("SpellProjectile"));
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Collision->SetGenerateOverlapEvents(true);
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->SetUpdatedComponent(Collision);
    Movement->bAutoActivate = false;
    Movement->bInitialVelocityInLocalSpace = false;
    Movement->ProjectileGravityScale = 0.f;
    Movement->bSweepCollision = true;
    Movement->bRotationFollowsVelocity = true;
}
void ASAProjectile::BeginPlay()
{
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Movement->Deactivate();
    Collision->OnComponentBeginOverlap.AddDynamic(this, &ASAProjectile::HandleOverlap);
    Movement->OnProjectileStop.AddDynamic(this, &ASAProjectile::HandleStop);
    Super::BeginPlay();
}
bool ASAProjectile::Prepare(const FSASpellPayload& Payload, const FVector& Direction, float Speed, float Radius, float Range)
{
    if (bActive || bImpactConsumed || !IsValid(Collision) || !IsValid(Movement) || GetRootComponent() != Collision
        || Direction.ContainsNaN() || Direction.IsNearlyZero() || !FMath::IsFinite(Speed) || Speed <= 0.f
        || !FMath::IsFinite(Radius) || Radius <= 0.f || !FMath::IsFinite(Range) || Range <= 0.f) { return false; }
    Snapshot = Payload;
    PreparedVelocity = Direction.GetSafeNormal() * Speed;
    TimeToLive = Range / Speed;
    Collision->SetSimulatePhysics(false);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Collision->SetSphereRadius(Radius);
    if (Payload.Context.SourceActor.IsValid())
    {
        Collision->IgnoreActorWhenMoving(Payload.Context.SourceActor.Get(), true);
        TArray<AActor*> AttachedActors;
        Payload.Context.SourceActor->GetAttachedActors(AttachedActors, true, true);
        for (AActor* Attached : AttachedActors) { Collision->IgnoreActorWhenMoving(Attached, true); }
    }
    Movement->StopMovementImmediately();
    Movement->Deactivate();
    Movement->InitialSpeed = Speed;
    Movement->MaxSpeed = Speed;
    Movement->ProjectileGravityScale = 0.f;
    bPrepared = true;
    return true;
}
void ASAProjectile::ActivatePrepared()
{
    if (!bPrepared || bActive || bImpactConsumed || IsActorBeingDestroyed()) { return; }
    bActive = true;
    Movement->SetUpdatedComponent(Collision);
    Movement->Velocity = PreparedVelocity;
    SetLifeSpan(TimeToLive);
    const TWeakObjectPtr<ASAProjectile> Self = this;
    Movement->Activate();
    // Last operation: enabling collision can immediately invoke an overlap callback.
    if (Self.IsValid() && !Self->IsActorBeingDestroyed() && IsValid(Self->Collision))
    { Self->Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly); }
}
void ASAProjectile::HandleStop(const FHitResult& Hit) { ConsumeImpact(Hit); }
void ASAProjectile::HandleOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent* OtherComponent,
    int32, bool bFromSweep, const FHitResult& Hit)
{
    if (!IsValid(Other) || Other == Snapshot.Context.SourceActor.Get()) { return; }
    ConsumeImpact(bFromSweep ? Hit : FHitResult(Other, OtherComponent, GetActorLocation(), -PreparedVelocity.GetSafeNormal()));
}
void ASAProjectile::ConsumeImpact(const FHitResult& Hit)
{
    if (!bActive || bImpactConsumed || (Snapshot.Context.SourceActor.IsValid() && Hit.GetActor() == Snapshot.Context.SourceActor.Get())) { return; }
    bImpactConsumed = true;
    const TWeakObjectPtr<ASAProjectile> Self = this;
    const TWeakObjectPtr<UProjectileMovementComponent> LocalMovement = Movement;
    FSASpellPayload Payload = Snapshot;
    Payload.Context.Hit = Hit;
    Payload.Context.TargetActor = Hit.GetActor();
    Payload.Context.DamageCauser = this;
    Payload.Context.ContactQuality = Hit.bStartPenetrating ? ESAContactQuality::InitialOverlap : ESAContactQuality::Swept;
    Payload.Context.bInterpolatedPose = false;
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (LocalMovement.IsValid())
    {
        LocalMovement->StopMovementImmediately();
        LocalMovement->SetComponentTickEnabled(false);
        LocalMovement->Deactivate(); // Last access: deactivation delegates can destroy this component.
    }
    const FSADamageResult Result = SA::ApplySpellImpact(Hit.GetActor(), MoveTemp(Payload));
    if (Self.IsValid() && !Self->IsActorBeingDestroyed()) { Self->OnImpact.Broadcast(Hit, Result); }
    if (Self.IsValid()) { Self->Destroy(); }
}
