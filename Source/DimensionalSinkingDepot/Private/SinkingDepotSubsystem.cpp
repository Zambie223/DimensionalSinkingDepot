#include "SinkingDepotSubsystem.h"

#include "SinkingDepotConstants.h"
#include "SinkingDepotDevCommands.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotWindow.h"

#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGCentralStorageSubsystem.h"
#include "FGResourceSinkSubsystem.h"
#include "FGSchematic.h"
#include "FGSchematicManager.h"
#include "Subsystem/SubsystemActorManager.h"
#include "TimerManager.h"

namespace
{
	/** How often the server re-checks bindings and cached subsystems, in case something was re-created. */
	constexpr float WatchdogIntervalSeconds = 5.0f;

	/** How often the server re-evaluates which grinders run, and how long one keeps running after its last sink. */
	constexpr float ActivityIntervalSeconds = 0.5f;
	constexpr double ActivityHoldSeconds = 1.5;

	UConfigPropertyBool* FindBoolProperty(UConfigPropertySection* Section, const FString& Key)
	{
		if (!Section)
		{
			return nullptr;
		}
		for (const TPair<FString, TObjectPtr<UConfigProperty>>& Pair : Section->SectionProperties)
		{
			if (Pair.Key.Equals(Key, ESearchCase::IgnoreCase))
			{
				return Cast<UConfigPropertyBool>(Pair.Value);
			}
		}
		return nullptr;
	}

	const TCHAR* NetModeName(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("no world");
		}
		switch (World->GetNetMode())
		{
		case NM_Standalone: return TEXT("single player");
		case NM_DedicatedServer: return TEXT("dedicated server");
		case NM_ListenServer: return TEXT("listen server (host)");
		case NM_Client: return TEXT("client");
		default: return TEXT("unknown");
		}
	}
}

ASinkingDepotSubsystem::ASinkingDepotSubsystem()
{
	// Replicated only for the grinder windows; everything else here is server-side.
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.0f);
}

void ASinkingDepotSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASinkingDepotSubsystem, ActiveSinkers);
}

bool ASinkingDepotSubsystem::IsGrinderActive(const AActor* Building) const
{
	if (!Building)
	{
		return false;
	}
	for (const TObjectPtr<AActor>& Active : ActiveSinkers)
	{
		if (Active.Get() == Building)
		{
			return true;
		}
	}
	return false;
}

void ASinkingDepotSubsystem::SetForcedActive(AActor* Building, const bool bForced)
{
	if (!HasAuthority() || !Building)
	{
		return;
	}
	if (bForced)
	{
		ForcedActive.Add(Building);
	}
	else
	{
		ForcedActive.Remove(Building);
	}
	UpdateActiveSinkers();
}

void ASinkingDepotSubsystem::UpdateActiveSinkers()
{
	// Power first: the factory-tick hook reads this cache (the circuit map is game-thread only).
	SinkingDepot::Overflow::RefreshPowered(GetWorld());

	TArray<AActor*> Active;
	SinkingDepot::Overflow::CollectActive(ActivityHoldSeconds, Active);
	for (auto It = ForcedActive.CreateIterator(); It; ++It)
	{
		if (AActor* Forced = It->Get())
		{
			Active.AddUnique(Forced);
		}
		else
		{
			It.RemoveCurrent();
		}
	}
	// Order-independent compare, so the property (and the network) only changes when a grinder starts or stops.
	bool bChanged = Active.Num() != ActiveSinkers.Num();
	for (int32 Index = 0; !bChanged && Index < Active.Num(); ++Index)
	{
		bChanged = !IsGrinderActive(Active[Index]);
	}
	if (!bChanged)
	{
		return;
	}
	ActiveSinkers.Reset(Active.Num());
	for (AActor* Building : Active)
	{
		ActiveSinkers.Add(Building);
	}
	ForceNetUpdate();
	ApplyActiveSinkers(); // The host sees its own windows too; a dedicated server draws nothing and skips inside.
}

void ASinkingDepotSubsystem::OnRep_ActiveSinkers()
{
	ApplyActiveSinkers();
}

void ASinkingDepotSubsystem::ApplyActiveSinkers()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	TSet<TWeakObjectPtr<AActor>> Now;
	for (AActor* Building : ActiveSinkers)
	{
		if (Building) // A client may not have this building yet; its BeginPlay asks IsGrinderActive later.
		{
			Now.Add(Building);
		}
	}
	for (const TWeakObjectPtr<AActor>& Was : AppliedActive)
	{
		if (!Now.Contains(Was))
		{
			SinkingDepot::Window::SetActive(Was.Get(), false);
		}
	}
	for (const TWeakObjectPtr<AActor>& Is : Now)
	{
		if (!AppliedActive.Contains(Is))
		{
			SinkingDepot::Window::SetActive(Is.Get(), true);
		}
	}
	AppliedActive = MoveTemp(Now);
}

ASinkingDepotSubsystem* ASinkingDepotSubsystem::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	USubsystemActorManager* Manager = World->GetSubsystem<USubsystemActorManager>();
	return Manager ? Manager->GetSubsystemActor<ASinkingDepotSubsystem>() : nullptr;
}

UConfigPropertySection* ASinkingDepotSubsystem::GetConfigRootSection(const UWorld* World)
{
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UConfigManager* ConfigManager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	if (!ConfigManager)
	{
		return nullptr;
	}
	FConfigId ConfigId;
	ConfigId.ModReference = SinkingDepot::ModReference;
	return ConfigManager->GetConfigurationRootSection(ConfigId);
}

void ASinkingDepotSubsystem::Init()
{
	Super::Init();
	if (!HasAuthority())
	{
		return;
	}
	OwnSchematic = LoadClass<UFGSchematic>(nullptr, SinkingDepot::SchematicClassPath);
	DepotResearch = LoadClass<UFGSchematic>(nullptr, SinkingDepot::DepotResearchClassPath);
	if (!OwnSchematic)
	{
		UE_LOG(LogSinkingDepot, Error, TEXT("Could not load %s; the building can't be unlocked"), SinkingDepot::SchematicClassPath);
	}
	if (!DepotResearch)
	{
		UE_LOG(LogSinkingDepot, Error, TEXT("Could not load the vanilla research %s; the building can't be unlocked automatically"), SinkingDepot::DepotResearchClassPath);
	}
	UE_LOG(LogSinkingDepot, Log, TEXT("Init on %s"), NetModeName(GetWorld()));
	CacheGameSubsystems();
	BindDelegates();
	ApplyConfig(TEXT("Init"));
	GrantIfResearched(TEXT("Init"));
}

void ASinkingDepotSubsystem::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		SinkingDepot::Dev::OnClientWorldReady(GetWorld());
		return;
	}
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.SetTimerForNextTick(this, &ASinkingDepotSubsystem::OnNextTickAfterBeginPlay);
	Timers.SetTimer(WatchdogTimer, this, &ASinkingDepotSubsystem::OnWatchdog, WatchdogIntervalSeconds, true);
	Timers.SetTimer(ActivityTimer, this, &ASinkingDepotSubsystem::UpdateActiveSinkers, ActivityIntervalSeconds, true);
	SinkingDepot::Dev::OnWorldReady(GetWorld());
}

void ASinkingDepotSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	UnbindDelegates();
	ClearGameSubsystems();
	Super::EndPlay(EndPlayReason);
}

void ASinkingDepotSubsystem::OnNextTickAfterBeginPlay()
{
	// Everything has begun play now; the game's subsystems certainly exist.
	CacheGameSubsystems();
	BindDelegates();
	ApplyConfig(TEXT("PostBeginPlay"));
	GrantIfResearched(TEXT("PostBeginPlay"));
}

void ASinkingDepotSubsystem::OnWatchdog()
{
	const UConfigPropertySection* PreviousSection = BoundConfigSection.Get();
	CacheGameSubsystems();
	BindDelegates();
	if (BoundConfigSection.Get() != PreviousSection)
	{
		ApplyConfig(TEXT("Config re-registered"));
	}
}

void ASinkingDepotSubsystem::CacheGameSubsystems()
{
	UWorld* World = GetWorld();
	SinkingDepot::Overflow::FRuntime& Runtime = SinkingDepot::Overflow::Runtime();
	Runtime.Depot.store(AFGCentralStorageSubsystem::Get(World), std::memory_order_release);
	Runtime.Sink.store(AFGResourceSinkSubsystem::Get(World), std::memory_order_release);
}

void ASinkingDepotSubsystem::ClearGameSubsystems() const
{
	// Only clear what belongs to this world, in case another world already cached its own.
	const UWorld* World = GetWorld();
	SinkingDepot::Overflow::FRuntime& Runtime = SinkingDepot::Overflow::Runtime();
	AFGCentralStorageSubsystem* Depot = Runtime.Depot.load(std::memory_order_acquire);
	if (Depot && Depot->GetWorld() == World)
	{
		Runtime.Depot.store(nullptr, std::memory_order_release);
	}
	AFGResourceSinkSubsystem* Sink = Runtime.Sink.load(std::memory_order_acquire);
	if (Sink && Sink->GetWorld() == World)
	{
		Runtime.Sink.store(nullptr, std::memory_order_release);
	}
}

void ASinkingDepotSubsystem::BindDelegates()
{
	UWorld* World = GetWorld();

	UConfigPropertySection* Root = GetConfigRootSection(World);
	if (Root && BoundConfigSection.Get() != Root)
	{
		if (UConfigPropertySection* Old = BoundConfigSection.Get())
		{
			Old->OnPropertyValueChanged.RemoveDynamic(this, &ASinkingDepotSubsystem::OnConfigValueChanged);
		}
		Root->OnPropertyValueChanged.AddUniqueDynamic(this, &ASinkingDepotSubsystem::OnConfigValueChanged);
		BoundConfigSection = Root;
	}

	AFGSchematicManager* Schematics = AFGSchematicManager::Get(World);
	if (Schematics && BoundSchematicManager.Get() != Schematics)
	{
		if (AFGSchematicManager* Old = BoundSchematicManager.Get())
		{
			Old->PurchasedSchematicDelegate.RemoveDynamic(this, &ASinkingDepotSubsystem::OnSchematicPurchased);
		}
		Schematics->PurchasedSchematicDelegate.AddUniqueDynamic(this, &ASinkingDepotSubsystem::OnSchematicPurchased);
		BoundSchematicManager = Schematics;
	}
}

void ASinkingDepotSubsystem::UnbindDelegates()
{
	if (UConfigPropertySection* Section = BoundConfigSection.Get())
	{
		Section->OnPropertyValueChanged.RemoveDynamic(this, &ASinkingDepotSubsystem::OnConfigValueChanged);
	}
	BoundConfigSection.Reset();

	if (AFGSchematicManager* Schematics = BoundSchematicManager.Get())
	{
		Schematics->PurchasedSchematicDelegate.RemoveDynamic(this, &ASinkingDepotSubsystem::OnSchematicPurchased);
	}
	BoundSchematicManager.Reset();
}

bool ASinkingDepotSubsystem::ReadBoolSetting(const TCHAR* Key, const bool bDefault) const
{
	const UConfigPropertyBool* Property = FindBoolProperty(GetConfigRootSection(GetWorld()), Key);
	if (!Property)
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("Config key '%s' missing or not a bool; using the default (%s)"), Key, bDefault ? TEXT("on") : TEXT("off"));
		return bDefault;
	}
	return Property->Value;
}

float ASinkingDepotSubsystem::ReadSinkPowerMW()
{
	// The AWESOME Sink's own mPowerConsumption, so a rebalance in the game carries over. Read by reflection because
	// the field is protected.
	UClass* SinkClass = LoadClass<AFGBuildableFactory>(nullptr, SinkingDepot::ResourceSinkBuildClassPath);
	const FFloatProperty* Field = SinkClass ? FindFProperty<FFloatProperty>(SinkClass, TEXT("mPowerConsumption")) : nullptr;
	const float Value = Field ? Field->GetPropertyValue_InContainer(SinkClass->GetDefaultObject()) : 0.0f;
	if (!(Value > 0.0f) || !FMath::IsFinite(Value))
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("Could not read the AWESOME Sink's power use; using %.1f MW"), SinkingDepot::FallbackSinkPowerMW);
		return SinkingDepot::FallbackSinkPowerMW;
	}
	return Value;
}

void ASinkingDepotSubsystem::ApplyConfig(const TCHAR* Reason)
{
	SinkingDepot::Overflow::FRuntime& Runtime = SinkingDepot::Overflow::Runtime();
	const bool bCantKeepUp = ReadBoolSetting(SinkingDepot::KeySinkWhenUploaderCantKeepUp, SinkingDepot::DefaultSinkWhenUploaderCantKeepUp);
	const bool bRequirePower = ReadBoolSetting(SinkingDepot::KeyRequirePower, SinkingDepot::DefaultRequirePower);
	Runtime.bSinkWhenUploaderCantKeepUp.store(bCantKeepUp, std::memory_order_relaxed);
	Runtime.bRequirePower.store(bRequirePower, std::memory_order_relaxed);
	const bool bFirst = FCString::Strcmp(Reason, TEXT("Init")) == 0;
	if (bFirst)
	{
		Runtime.PowerMW.store(ReadSinkPowerMW(), std::memory_order_relaxed);
	}
	if (bFirst || bCantKeepUp != bAppliedCantKeepUp || bRequirePower != bAppliedRequirePower)
	{
		UE_LOG(LogSinkingDepot, Log, TEXT("Applied (%s): %s = %s, %s = %s (%.1f MW while sinking)"), Reason,
			SinkingDepot::KeySinkWhenUploaderCantKeepUp, bCantKeepUp ? TEXT("on") : TEXT("off"),
			SinkingDepot::KeyRequirePower, bRequirePower ? TEXT("on") : TEXT("off"), Runtime.PowerMW.load());
	}
	bAppliedCantKeepUp = bCantKeepUp;
	bAppliedRequirePower = bRequirePower;
}

bool ASinkingDepotSubsystem::IsSettingKey(const FString& Key)
{
	return Key.Equals(SinkingDepot::KeySinkWhenUploaderCantKeepUp, ESearchCase::IgnoreCase)
		|| Key.Equals(SinkingDepot::KeyRequirePower, ESearchCase::IgnoreCase);
}

bool ASinkingDepotSubsystem::SetBoolSetting(const UWorld* World, const FString& Key, const bool bValue, FString& OutMessage)
{
	if (World && World->GetNetMode() == NM_Client)
	{
		OutMessage = TEXT("Refused: this is a client. Settings come from the host or server.");
		return false;
	}
	if (!IsSettingKey(Key))
	{
		OutMessage = FString::Printf(TEXT("Unknown key '%s'. Valid keys: %s, %s"), *Key, SinkingDepot::KeySinkWhenUploaderCantKeepUp, SinkingDepot::KeyRequirePower);
		return false;
	}
	UConfigPropertyBool* Property = FindBoolProperty(GetConfigRootSection(World), Key);
	if (!Property)
	{
		OutMessage = FString::Printf(TEXT("Configuration '%s' or key '%s' not found."), SinkingDepot::ModReference, *Key);
		return false;
	}
	Property->Value = bValue;
	// MarkDirty saves the file and fires OnPropertyValueChanged, which applies the value.
	Property->MarkDirty();
	OutMessage = FString::Printf(TEXT("%s set to %s and saved."), *Key, bValue ? TEXT("on") : TEXT("off"));
	return true;
}

void ASinkingDepotSubsystem::OnConfigValueChanged()
{
	ApplyConfig(TEXT("Config changed"));
}

void ASinkingDepotSubsystem::OnSchematicPurchased(const TSubclassOf<UFGSchematic> Schematic)
{
	if (!DepotResearch || Schematic != DepotResearch)
	{
		return;
	}
	// Don't grant from inside the schematic manager's own broadcast; do it on the next tick.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		GrantIfResearched(TEXT("Depot research purchased"));
	}));
}

bool ASinkingDepotSubsystem::GrantIfResearched(const TCHAR* Reason)
{
	AFGSchematicManager* Schematics = AFGSchematicManager::Get(GetWorld());
	if (!HasAuthority() || !Schematics || !OwnSchematic || !DepotResearch)
	{
		return false;
	}
	if (Schematics->IsSchematicPurchased(OwnSchematic))
	{
		return true;
	}
	if (!Schematics->IsSchematicPurchased(DepotResearch))
	{
		return false;
	}
	Schematics->GiveAccessToSchematic(OwnSchematic, nullptr, ESchematicUnlockFlags::BlockTelemetry);
	const bool bNowUnlocked = Schematics->IsSchematicPurchased(OwnSchematic);
	UE_LOG(LogSinkingDepot, Log, TEXT("Granted the Dimensional Sinking Depot schematic (%s): %s"), Reason, bNowUnlocked ? TEXT("unlocked") : TEXT("FAILED"));
	return bNowUnlocked;
}

void ASinkingDepotSubsystem::DescribeStatus(FOutputDevice& Ar) const
{
	UWorld* World = GetWorld();
	const SinkingDepot::Overflow::FRuntime& Runtime = SinkingDepot::Overflow::Runtime();
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Dimensional Sinking Depot status (%s)"), NetModeName(World)));
	Lines.Add(FString::Printf(TEXT("  Settings (this machine's Mod Options): %s = %s, %s = %s (%.1f MW while sinking)"),
		SinkingDepot::KeySinkWhenUploaderCantKeepUp, Runtime.bSinkWhenUploaderCantKeepUp.load() ? TEXT("on") : TEXT("off"),
		SinkingDepot::KeyRequirePower, Runtime.bRequirePower.load() ? TEXT("on") : TEXT("off"), Runtime.PowerMW.load()));
	Lines.Add(FString::Printf(TEXT("  Depot subsystem: %s, AWESOME Sink subsystem: %s"),
		Runtime.Depot.load() ? TEXT("found") : TEXT("MISSING"), Runtime.Sink.load() ? TEXT("found") : TEXT("MISSING")));

	const AFGSchematicManager* Schematics = AFGSchematicManager::Get(World);
	auto Purchased = [Schematics](const TSubclassOf<UFGSchematic> Schematic)
	{
		return Schematics && Schematic && Schematics->IsSchematicPurchased(Schematic);
	};
	Lines.Add(FString::Printf(TEXT("  Dimensional Depot research: %s, Sinking Depot unlocked: %s"),
		Purchased(DepotResearch) ? TEXT("done") : TEXT("not done"), Purchased(OwnSchematic) ? TEXT("yes") : TEXT("no")));

	int32 SinkingDepots = 0;
	int32 VanillaUploaders = 0;
	int32 Unpowered = 0;
	if (World)
	{
		for (TActorIterator<AFGCentralStorageContainer> It(World); It; ++It)
		{
			const bool bOurs = SinkingDepot::IsSinkingDepot(*It);
			++(bOurs ? SinkingDepots : VanillaUploaders);
			Unpowered += bOurs && SinkingDepot::Overflow::IsBlockedByPower(*It) ? 1 : 0;
		}
	}
	Lines.Add(FString::Printf(TEXT("  Buildings: %d Dimensional Sinking Depot(s), %d vanilla uploader(s)"), SinkingDepots, VanillaUploaders));
	if (Runtime.bRequirePower.load())
	{
		Lines.Add(FString::Printf(TEXT("  Without power (not sinking): %d"), Unpowered));
	}
	Lines.Add(FString::Printf(TEXT("  Grinders running right now: %d"), ActiveSinkers.Num()));
	Lines.Add(FString::Printf(TEXT("  Sunk since the game was launched: %lld (depot full), %lld (uploader couldn't keep up)"),
		Runtime.SunkDepotFull.load(), Runtime.SunkCantKeepUp.load()));
	const int64 Returned = Runtime.ReturnedToBuffer.load();
	const int64 Lost = Runtime.Lost.load();
	if (Returned > 0 || Lost > 0)
	{
		Lines.Add(FString::Printf(TEXT("  Sink refused %lld item(s) after they left the belt: %lld kept in the uploader, %lld lost"), Returned + Lost, Returned, Lost));
	}

	for (const FString& Line : Lines)
	{
		Ar.Log(Line);
		UE_LOG(LogSinkingDepot, Log, TEXT("%s"), *Line);
	}
}
