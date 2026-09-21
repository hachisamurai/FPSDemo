#include "AI/DemoBossDiveSettings.h"
#include "Debug/DemoLog.h"
bool FDemoBossDiveSettings::IsValid() const
{
    UE_LOG(LogFPSDemo,Log,TEXT("%hs"),__FUNCTION__);
    return FMath::IsFinite(Height)&&Height>=300&&Height<=1000
        &&FMath::IsFinite(RiseSeconds)&&RiseSeconds>=.3f&&RiseSeconds<=2
        &&FMath::IsFinite(DiveSeconds)&&DiveSeconds>=.3f&&DiveSeconds<=2
        &&FMath::IsFinite(MaxDiveSpeed)&&MaxDiveSpeed>=1000&&MaxDiveSpeed<=6000
        &&FMath::IsFinite(Radius)&&Radius>=150&&Radius<=500
        &&FMath::IsFinite(Knockback)&&Knockback>=0&&Knockback<=1600
        &&FMath::IsFinite(KnockUp)&&KnockUp>=0&&KnockUp<=500
        &&FMath::IsFinite(RecoverySeconds)&&RecoverySeconds>=.5f&&RecoverySeconds<=3
        &&FMath::IsFinite(Cooldown)&&Cooldown>=8&&Cooldown<=60
        &&FMath::IsFinite(InitialDelay)&&InitialDelay>=3&&InitialDelay<=60;
}
