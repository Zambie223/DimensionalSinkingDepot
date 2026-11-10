// Console commands that ship with the mod. On a dedicated server they run from the Server Manager console.

#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "SinkingDepotConstants.h"
#include "SinkingDepotSubsystem.h"

namespace
{
	FAutoConsoleCommandWithWorldArgsAndOutputDevice GStatusCommand(
		TEXT("SinkingDepot.Status"),
		TEXT("SinkingDepot.Status - shows the Dimensional Sinking Depot setting in effect, the unlock state and how much has been sunk."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (World && World->GetNetMode() == NM_Client)
			{
				Ar.Log(TEXT("Dimensional Sinking Depot runs on the host or server; run this there."));
				return;
			}
			if (const ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World))
			{
				Subsystem->DescribeStatus(Ar);
			}
			else
			{
				Ar.Log(TEXT("Dimensional Sinking Depot is not active in this world (main menu, or the mod failed to start; check the log)."));
			}
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GSetCommand(
		TEXT("SinkingDepot.Set"),
		TEXT("SinkingDepot.Set <SinkWhenUploaderCantKeepUp|RequirePower> <on|off> - changes and saves a setting on the host or server; applies immediately."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			const auto ParseOnOff = [](const FString& Text, bool& Out)
			{
				for (const TCHAR* On : {TEXT("on"), TEXT("1"), TEXT("true")})
				{
					if (Text.Equals(On, ESearchCase::IgnoreCase))
					{
						Out = true;
						return true;
					}
				}
				for (const TCHAR* Off : {TEXT("off"), TEXT("0"), TEXT("false")})
				{
					if (Text.Equals(Off, ESearchCase::IgnoreCase))
					{
						Out = false;
						return true;
					}
				}
				return false;
			};
			bool bValue = false;
			if (Args.Num() != 2 || !ASinkingDepotSubsystem::IsSettingKey(Args[0]) || !ParseOnOff(Args[1], bValue))
			{
				Ar.Logf(TEXT("Usage: SinkingDepot.Set <%s|%s> <on|off>"), SinkingDepot::KeySinkWhenUploaderCantKeepUp, SinkingDepot::KeyRequirePower);
				return;
			}
			FString Message;
			// Use the key as defined (the config lookup is case-insensitive, but the saved message reads better).
			const FString Key = Args[0].Equals(SinkingDepot::KeyRequirePower, ESearchCase::IgnoreCase) ? FString(SinkingDepot::KeyRequirePower) : FString(SinkingDepot::KeySinkWhenUploaderCantKeepUp);
			ASinkingDepotSubsystem::SetBoolSetting(World, Key, bValue, Message);
			Ar.Log(Message);
		}));
}
