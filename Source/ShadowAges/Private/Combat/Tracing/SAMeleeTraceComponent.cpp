// Source/ShadowAges/Private/Combat/Tracing/SAMeleeTraceComponent.cpp
#include "Combat/Tracing/SAMeleeTraceComponent.h"

#include "Combat/SACombatComponent.h"
#include "Combat/SAMeleeDamageReceiverComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Templates/UnrealTemplate.h"

USAMeleeTraceComponent::USAMeleeTraceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void USAMeleeTraceComponent::BeginPlay()
{
    Super::BeginPlay();
    TArray<USACombatComponent*> Combats;
    TArray<USAMeleeTraceComponent*> Traces;
    if (AActor* Owner = GetOwner())
    {
        Owner->GetComponents(Combats);
        Owner->GetComponents(Traces);
    }
    if (Combats.Num() != 1 || Traces.Num() != 1)
    {
        LastError = TEXT("Exactly one Combat and one MeleeTrace per attacker are required.");
        return;
    }
    Combat = Combats[0];
    StartedSubscription = Combat->OnMeleeStepStarted.AddUObject(this,
        &USAMeleeTraceComponent::HandleStepStarted);
    PoseSubscription = Combat->OnMeleePoseAdvanced.AddUObject(this,
        &USAMeleeTraceComponent::HandlePoseAdvanced);
    ClosedSubscription = Combat->OnMeleeStepClosed.AddUObject(this,
        &USAMeleeTraceComponent::HandleStepClosed);
}

void USAMeleeTraceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bEndingPlay = true;
    const FSAActionHandle OldAction = ActiveKey.Action;
    ResetStep();
    bConfigured = false;
    if (IsValid(Combat))
    {
        Combat->OnMeleeStepStarted.Remove(StartedSubscription);
        Combat->OnMeleePoseAdvanced.Remove(PoseSubscription);
        Combat->OnMeleeStepClosed.Remove(ClosedSubscription);
        Combat->CancelActionIfCurrent(OldAction, ESAActionEndReason::Interrupted);
    }
    Super::EndPlay(EndPlayReason);
}

bool USAMeleeTraceComponent::ConfigureBlade(UStaticMeshComponent* Blade,
    USABladeTraceProfile* Profile, FName ReachAnchorSocket,
    const TArray<AActor*>& ExtraIgnoredActors)
{
    if (bEndingPlay || bProcessing || !HasBegunPlay()
        || !IsValid(Combat) || (Combat->IsBusy() && !Combat->IsReservedAction(Combat->GetCurrentAction(), ESAActionKind::Equip))) return false;
    FString Error;
    USkeletalMeshComponent* Parent = IsValid(Blade)
        ? Cast<USkeletalMeshComponent>(Blade->GetAttachParent()) : nullptr;
    if (!IsValid(Blade) || !IsValid(Profile) || !Profile->Validate(Error)
        || !Parent || Parent->GetOwner() != GetOwner()
        || !IsValid(Blade->GetStaticMesh()) || Blade->IsSimulatingPhysics()
        || Blade->IsUsingAbsoluteLocation() || Blade->IsUsingAbsoluteRotation()
        || Blade->IsUsingAbsoluteScale() || ReachAnchorSocket.IsNone()
        || !Parent->DoesSocketExist(ReachAnchorSocket)
        || !Blade->DoesSocketExist(Profile->BaseSocket)
        || !Blade->DoesSocketExist(Profile->TipSocket)
        || (!Blade->GetAttachSocketName().IsNone()
            && !Parent->DoesSocketExist(Blade->GetAttachSocketName())))
    {
        LastError = Error.IsEmpty() ? TEXT("Invalid blade, direct attachment or socket setup.") : Error;
        return false;
    }
    const FVector Base = Blade->GetSocketTransform(Profile->BaseSocket, RTS_Component).GetLocation();
    const FVector Tip = Blade->GetSocketTransform(Profile->TipSocket, RTS_Component).GetLocation();
    const FTransform Pose = Blade->GetRelativeTransform()
        * Parent->GetSocketTransform(Blade->GetAttachSocketName(), RTS_World);
    const FVector Scale = Pose.GetScale3D();
    if (Pose.ContainsNaN() || !Pose.GetRotation().IsNormalized() || Scale.X <= UE_SMALL_NUMBER || Scale.X > 1000.0
        || !FMath::IsNearlyEqual(Scale.X, Scale.Y, 1.e-6 * double(Scale.X))
        || !FMath::IsNearlyEqual(Scale.X, Scale.Z, 1.e-6 * double(Scale.X)))
    {
        LastError = TEXT("Weapon requires a positive uniform world scale.");
        return false;
    }
    FSABladeTraceKernel NewKernel;
    if (!NewKernel.Configure(Profile->Settings, Base, Tip,
        GetOwner(), Blade->GetOwner(), ExtraIgnoredActors, Error))
    {
        LastError = Error;
        return false;
    }
    const double HalfSpacing = FVector::Dist(Base, Tip) / (2.0 * (NewKernel.GetSampleCount() - 1));
    const double BaseRadius = FMath::Sqrt(FMath::Square(Profile->Settings.GameplayRadius)
        + FMath::Square(HalfSpacing)) * Scale.X;
    if (!FMath::IsFinite(BaseRadius) || BaseRadius > Profile->Settings.MaxQueryRadiusWorld)
    {
        LastError = TEXT("Scaled blade radius exceeds MaxQueryRadiusWorld.");
        return false;
    }
    Kernel = MoveTemp(NewKernel);
    WeaponMesh = Blade;
    ConfiguredMeshAsset = Blade->GetStaticMesh();
    CharacterMesh = Parent;
    WeaponActor = Blade->GetOwner();
    ConfiguredProfile = Profile;
    ConfiguredProfileId = Profile->ProfileId;
    Settings = Profile->Settings;
    AnchorSocket = ReachAnchorSocket;
    ConfiguredAttachSocket = Blade->GetAttachSocketName();
    LocalBase = Base;
    LocalTip = Tip;
    ConfiguredScale = Scale.X;
    bConfigured = true;
    LastBudget.Reset(Settings);
    ResetStep();
    LastError.Reset();
    return true;
}

void USAMeleeTraceComponent::ClearBlade()
{
    const FSAActionHandle OldAction = ActiveKey.Action;
    bConfigured = false;
    ResetStep();
    WeaponMesh.Reset();
    ConfiguredMeshAsset.Reset();
    CharacterMesh.Reset();
    WeaponActor.Reset();
    ConfiguredProfile = nullptr;
    if (IsValid(Combat))
        Combat->CancelActionIfCurrent(OldAction, ESAActionEndReason::Interrupted);
}

void USAMeleeTraceComponent::ResetStep()
{
    ActiveKey = {};
    StepTargets.Reset();
    ClosedWindows.Reset();
    bHasPose = false;
    bWaitingForFirstRevision = false;
    CreditController.Reset();
    CreditId.Invalidate();
}

void USAMeleeTraceComponent::HandleStepStarted(FSAMeleePlaybackKey Key)
{
    if (bEndingPlay || !IsValid(Combat) || !Combat->IsCurrentPlayback(Key)) return;
    ResetStep();
    ActiveKey = Key;
    LastError.Reset();
    BaselineIntervalsSkipped = 0;
    LastBudget.Reset(Settings);
    if (!bConfigured || !WeaponMesh.IsValid() || !CharacterMesh.IsValid())
    {
        Fail(Key, TEXT("ConfigureBlade must succeed before melee input is enabled."));
        return;
    }
    const AActor* Owner = GetOwner();
    const USAMeleeDamageReceiverComponent* Receiver = Owner
        ? Owner->FindComponentByClass<USAMeleeDamageReceiverComponent>() : nullptr;
    if (!IsValid(Receiver) || !Receiver->HasBegunPlay())
    {
        Fail(Key, TEXT("Attacker requires an initialized MeleeDamageReceiver."));
        return;
    }
    CreditId = Receiver->GetSourceCreditId();
    if (const APawn* Pawn = Cast<APawn>(Owner)) CreditController = Pawn->GetController();
    LastBoneRevision = CharacterMesh->GetBoneTransformRevisionNumber();
    bWaitingForFirstRevision = true;
}

void USAMeleeTraceComponent::HandleStepClosed(FSAMeleePlaybackKey Key)
{
    if (ActiveKey == Key) ResetStep();
}

bool USAMeleeTraceComponent::IsCurrent(FSAMeleePlaybackKey Key) const
{
    UStaticMeshComponent* Blade = WeaponMesh.Get();
    USkeletalMeshComponent* Mesh = CharacterMesh.Get();
    return !bEndingPlay && bConfigured && ActiveKey == Key
        && IsValid(Combat) && Combat->IsCurrentPlayback(Key)
        && IsValid(Blade) && IsValid(Mesh) && WeaponActor.IsValid()
        && !WeaponActor->IsActorBeingDestroyed()
        && Blade->GetOwner() == WeaponActor.Get()
        && Blade->GetAttachParent() == Mesh
        && Blade->GetAttachSocketName() == ConfiguredAttachSocket
        && Blade->GetStaticMesh() == ConfiguredMeshAsset.Get()
        && !Blade->IsSimulatingPhysics()
        && !Blade->IsUsingAbsoluteLocation() && !Blade->IsUsingAbsoluteRotation()
        && !Blade->IsUsingAbsoluteScale();
}

bool USAMeleeTraceComponent::ReadPose(FTransform& OutPose,
    FVector& OutAnchor, FString& OutError) const
{
    UStaticMeshComponent* Blade = WeaponMesh.Get();
    USkeletalMeshComponent* Mesh = CharacterMesh.Get();
    if (!IsValid(Blade) || !IsValid(Mesh) || !WeaponActor.IsValid()
        || Blade->GetOwner() != WeaponActor.Get()
        || Blade->GetStaticMesh() != ConfiguredMeshAsset.Get()
        || Blade->GetAttachParent() != Mesh
        || Blade->GetAttachSocketName() != ConfiguredAttachSocket || Blade->IsSimulatingPhysics()
        || Blade->IsUsingAbsoluteLocation() || Blade->IsUsingAbsoluteRotation()
        || Blade->IsUsingAbsoluteScale() || !Mesh->DoesSocketExist(AnchorSocket)
        || (!Blade->GetAttachSocketName().IsNone()
            && !Mesh->DoesSocketExist(Blade->GetAttachSocketName())))
    {
        OutError = TEXT("Blade asset or attachment became invalid; reconfigure in idle.");
        return false;
    }
    OutPose = Blade->GetRelativeTransform()
        * Mesh->GetSocketTransform(Blade->GetAttachSocketName(), RTS_World);
    OutAnchor = Mesh->GetSocketTransform(AnchorSocket, RTS_World).GetLocation();
    const FVector Scale = OutPose.GetScale3D();
    const double Tolerance = 1.e-6 * ConfiguredScale;
    if (OutPose.ContainsNaN() || OutAnchor.ContainsNaN()
        || !OutPose.GetRotation().IsNormalized()
        || !FMath::IsNearlyEqual(Scale.X, ConfiguredScale, Tolerance)
        || !FMath::IsNearlyEqual(Scale.Y, ConfiguredScale, Tolerance)
        || !FMath::IsNearlyEqual(Scale.Z, ConfiguredScale, Tolerance))
    {
        OutError = TEXT("Invalid transform or changed/nonuniform world scale.");
        return false;
    }
    return true;
}

void USAMeleeTraceComponent::Fail(FSAMeleePlaybackKey Key, const FString& Error)
{
    LastError = Error;
    if (IsValid(Combat) && ActiveKey == Key && Combat->IsCurrentPlayback(Key))
        Combat->CancelActionIfCurrent(Key.Action, ESAActionEndReason::Failed);
}

void USAMeleeTraceComponent::HandlePoseAdvanced(const FSAMeleePoseFrame& Frame)
{
    if (!IsCurrent(Frame.Key))
    {
        if (!bEndingPlay && ActiveKey == Frame.Key)
            Fail(Frame.Key, TEXT("Active blade configuration was lost."));
        return;
    }
    if (bProcessing)
    {
        Fail(Frame.Key, TEXT("Reentrant pose collection is forbidden."));
        return;
    }
    TGuardValue<bool> Guard(bProcessing, true);
    if (Frame.Mesh.Get() != CharacterMesh.Get()
        || Frame.Step.TraceProfile != ConfiguredProfileId)
    {
        Fail(Frame.Key, TEXT("Combat mesh or step TraceProfile does not match configured blade."));
        return;
    }
    FTransform CurrentPose;
    FVector CurrentAnchor;
    FString Error;
    if (!ReadPose(CurrentPose, CurrentAnchor, Error))
    {
        Fail(Frame.Key, Error);
        return;
    }
    UWorld* World = GetWorld();
    if (!World || !FMath::IsFinite(Frame.CurrentWorldTime)
        || !FMath::IsFinite(Frame.PreviousWorldTime))
    {
        Fail(Frame.Key, TEXT("Invalid world or pose timestamps."));
        return;
    }
    USkeletalMeshComponent* Mesh = CharacterMesh.Get();
    const uint32 Revision = Mesh->GetBoneTransformRevisionNumber();
    if (bWaitingForFirstRevision && Revision == LastBoneRevision)
    {
        BaselineIntervalsSkipped += Frame.HitSlices.Num();
        LastError = TEXT("Waiting for first changed bone revision; no old-pose sweep.");
        return;
    }
    const double MontageDelta = static_cast<double>(Frame.CurrentPosition) - Frame.PreviousPosition;
    if (bHasPose && MontageDelta > 0.0 && Revision == LastBoneRevision)
    {
        Fail(Frame.Key, TEXT("Montage advanced but bone revision repeated; check animation tick settings."));
        return;
    }
    LastError.Reset();
    const bool bBaseline = !bHasPose;
    if (!bBaseline)
    {
        const double Delta = Frame.CurrentWorldTime - PreviousPoseTime;
        const double Angle = PreviousPose.GetRotation().AngularDistance(CurrentPose.GetRotation());
        if (!FMath::IsFinite(Delta) || Delta < 0.0 || Delta > Settings.MaxPoseDeltaSeconds
            || FMath::Abs(PreviousPosePosition - Frame.PreviousPosition) > 1.e-4f
            || FVector::Dist(PreviousPose.GetLocation(), CurrentPose.GetLocation()) > Settings.MaxOriginTravelWorld
            || FVector::Dist(PreviousAnchor, CurrentAnchor) > Settings.MaxOriginTravelWorld
            || FMath::RadiansToDegrees(Angle) > Settings.MaxFrameAngleDegrees)
        {
            Fail(Frame.Key, TEXT("Pose discontinuity: time gap, montage gap, teleport or excessive angle."));
            return;
        }
    }
    TArray<FPreparedWindow> Windows;
    LastBudget.Reset(Settings);
    for (int32 Serial = 0; Serial < Frame.Step.HitWindows.Num(); ++Serial)
    {
        if (ClosedWindows.Contains(Serial)) continue;
        const FSAMeleeHitWindow& Window = Frame.Step.HitWindows[Serial];
        const bool bActiveNow = Frame.CurrentPosition >= Window.BeginSeconds
            && Frame.CurrentPosition < Window.EndSeconds;
        const FSAMeleeWindowSlice* Slice = Frame.HitSlices.FindByPredicate(
            [Serial](const FSAMeleeWindowSlice& Value) { return Value.WindowSerial == Serial; });
        if (bBaseline && Slice) ++BaselineIntervalsSkipped;
        if ((bBaseline && !bActiveNow) || (!bBaseline && !Slice)) continue;
        FPreparedWindow& Prepared = Windows.AddDefaulted_GetRef();
        Prepared.Serial = Serial;
        ESABladeTraceStatus Status;
        if (bBaseline)
        {
            Status = Kernel.CollectSnapshot(*World, CurrentPose, CurrentAnchor,
                LastBudget, Prepared.Contacts, Prepared.WorldBlock, bDrawDebug);
        }
        else
        {
            FSABladeTraceFrame Geometry;
            Geometry.PreviousPose = PreviousPose;
            Geometry.CurrentPose = CurrentPose;
            Geometry.PreviousReachAnchor = PreviousAnchor;
            Geometry.CurrentReachAnchor = CurrentAnchor;
            Geometry.DeltaSeconds = Frame.CurrentWorldTime - PreviousPoseTime;
            Geometry.ActiveBegin = (Slice->BeginPosition - Frame.PreviousPosition) / MontageDelta;
            Geometry.ActiveEnd = (Slice->EndPosition - Frame.PreviousPosition) / MontageDelta;
            Status = Kernel.Collect(*World, Geometry, LastBudget,
                Prepared.Contacts, Prepared.WorldBlock, bDrawDebug);
        }
        if (Status != ESABladeTraceStatus::Collected && Status != ESABladeTraceStatus::EmptyInterval)
        {
            Fail(Frame.Key, FString::Printf(TEXT("Collect failed: status=%d; no contacts from this pose applied."), static_cast<int32>(Status)));
            return;
        }
    }
    PreviousPose = CurrentPose;
    PreviousAnchor = CurrentAnchor;
    PreviousPoseTime = Frame.CurrentWorldTime;
    PreviousPosePosition = Frame.CurrentPosition;
    LastBoneRevision = Revision;
    bHasPose = true;
    bWaitingForFirstRevision = false;
    if (!PrepareContacts(*World, Frame, Windows, LastBudget)) return;
    DispatchContacts(Frame, Windows);
}

bool USAMeleeTraceComponent::PrepareContacts(UWorld& World,
    const FSAMeleePoseFrame& Frame, TArray<FPreparedWindow>& Windows,
    FSABladeTraceBudget& Budget)
{
    for (FPreparedWindow& Window : Windows)
    {
        TArray<FSABladeContact> Accepted;
        for (const FSABladeContact& Contact : Window.Contacts)
        {
            AActor* Target = Contact.Hit.GetActor();
            const bool bBeforeWall = !Window.WorldBlock.IsSet()
                || Contact.FrameAlpha < Window.WorldBlock.GetValue().FrameAlpha - 1.e-6;
            if (!bBeforeWall || !IsValid(Target) || StepTargets.Contains(TWeakObjectPtr<AActor>(Target))
                || !Combat->CanAttemptMeleeContact(Target)) continue;
            bool bReachable = false;
            const ESABladeTraceStatus Status = Kernel.CheckReach(World, Contact, Budget, bReachable);
            if (Status != ESABladeTraceStatus::Collected)
            {
                Fail(Frame.Key, TEXT("Reach query failed or exhausted shared pose budget; no contacts applied."));
                return false;
            }
            if (bReachable) Accepted.Add(Contact);
        }
        Window.Contacts = MoveTemp(Accepted);
    }
    return IsCurrent(Frame.Key);
}

void USAMeleeTraceComponent::DispatchContacts(const FSAMeleePoseFrame& Frame,
    const TArray<FPreparedWindow>& Windows)
{
    const FSAMeleePlaybackKey Key = Frame.Key;
    for (const FPreparedWindow& Window : Windows)
    {
        if (!IsCurrent(Key)) return;
        for (int32 Index = 0; Index < Window.Contacts.Num(); ++Index)
        {
            if (!IsCurrent(Key)) return;
            const FSABladeContact Contact = Window.Contacts[Index];
            AActor* Target = Contact.Hit.GetActor();
            if (!IsValid(Target) || StepTargets.Contains(TWeakObjectPtr<AActor>(Target))
                || !Combat->CanAttemptMeleeContact(Target)) continue;
            StepTargets.Add(TWeakObjectPtr<AActor>(Target));
            FSAMeleeHitRequest Request;
            Request.Context = MakeContext(Frame, Window.Serial, Contact);
            Request.ProposedDamage = Frame.Step.Damage;
            Request.ProposedPoiseDamage = Frame.Step.PoiseDamage;
            Combat->ResolveMeleeContact(Request);
            if (!IsCurrent(Key)) return;
        }
        if (Window.WorldBlock.IsSet())
        {
            const FSABladeContact Contact = Window.WorldBlock.GetValue();
            const FSAHitContext Context = MakeContext(Frame, Window.Serial, Contact);
            ClosedWindows.Add(Window.Serial);
            Combat->NotifyMeleeWorldContact(Context);
            if (!IsCurrent(Key)) return;
        }
    }
}

FSAHitContext USAMeleeTraceComponent::MakeContext(const FSAMeleePoseFrame& Frame,
    int32 WindowSerial, const FSABladeContact& Contact) const
{
    FSAHitContext Context;
    Context.Playback = Frame.Key;
    Context.DamageType = Frame.Step.DamageType;
    Context.WindowSerial = WindowSerial;
    Context.SourceActor = GetOwner();
    Context.SourceController = CreditController;
    Context.DamageCauser = WeaponActor;
    Context.TargetActor = Contact.Hit.GetActor();
    Context.SourceCreditId = CreditId;
    Context.Hit = Contact.Hit;
    Context.WeaponTransform = Contact.WeaponPose;
    Context.BladeDirection = Contact.WeaponPose.TransformVectorNoScale(LocalTip - LocalBase).GetSafeNormal();
    Context.BladeVelocity = Contact.BladeVelocity;
    Context.FrameAlpha = Contact.FrameAlpha;
    Context.ContactQuality = Contact.Quality;
    Context.bInterpolatedPose = Contact.bInterpolatedPose;
    return Context;
}

FString USAMeleeTraceComponent::GetDebugString() const
{
    return FString::Printf(TEXT("Configured=%d Step=%d Samples=%d Queries=%lld/%d Raw=%lld/%d Candidates=%lld/%d HitActors=%d ClosedWindows=%d BaselineIntervalsSkipped=%d Error=%s"),
        bConfigured ? 1 : 0, ActiveKey.StepIndex, Kernel.GetSampleCount(),
        static_cast<long long>(LastBudget.UsedQueries), LastBudget.MaxQueries,
        static_cast<long long>(LastBudget.UsedRawHits), LastBudget.MaxRawHits,
        static_cast<long long>(LastBudget.UsedCandidates), LastBudget.MaxCandidates,
        StepTargets.Num(), ClosedWindows.Num(), BaselineIntervalsSkipped, *LastError);
}
