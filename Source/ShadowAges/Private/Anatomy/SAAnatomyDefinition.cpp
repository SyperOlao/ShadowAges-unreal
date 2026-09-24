#include "Anatomy/SAAnatomyDefinition.h"

bool USAAnatomyDefinition::Validate(FString& Error) const
{
    TSet<FName> Ids, Roots;
    for (const FSAAnatomyZone& Zone : Zones)
    {
        if (Zone.ZoneId.IsNone() || Zone.RootBone.IsNone() || Ids.Contains(Zone.ZoneId)
            || Roots.Contains(Zone.RootBone) || !FMath::IsFinite(Zone.InjuryThreshold)
            || Zone.InjuryThreshold <= 0.f || !FMath::IsFinite(Zone.DamageCap)
            || Zone.DamageCap < Zone.InjuryThreshold || !FMath::IsFinite(Zone.DamageMultiplier)
            || Zone.DamageMultiplier < 0.f || !FMath::IsFinite(Zone.SeverThreshold)
            || Zone.SeverThreshold <= 0.f || (Zone.bAllowCorpseSlice
                && (Zone.SeverThreshold > Zone.DamageCap || Zone.SeverDamageType.IsNone())))
        {
            Error = FString::Printf(TEXT("Invalid or duplicate anatomy zone: %s"), *Zone.ZoneId.ToString());
            return false;
        }
        Ids.Add(Zone.ZoneId);
        Roots.Add(Zone.RootBone);
    }
    Error = Zones.IsEmpty() ? TEXT("Anatomy requires at least one zone.") : TEXT("");
    return !Zones.IsEmpty();
}
