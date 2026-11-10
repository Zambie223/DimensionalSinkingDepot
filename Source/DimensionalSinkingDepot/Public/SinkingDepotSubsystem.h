#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "Templates/SubclassOf.h"
#include "SinkingDepotSubsystem.generated.h"

class AFGSchematicManager;
class UConfigPropertySection;
class UFGSchematic;

/**
 * Glue for the Dimensional Sinking Depot. Spawned on the server or host and replicated to clients.
 *
 * Server:
 * - Mirrors the host's "sink when the uploader can't keep up" setting into the factory-tick hook.
 * - Caches the depot and AWESOME Sink subsystems for the hook, which may run off the game thread.
 * - Grants the mod's schematic whenever the vanilla Dimensional Depot research is purchased, including saves that
 *   already had it before the mod was installed.
 * - Publishes which Sinking Depots are sinking right now (ActiveSinkers), for the grinder window.
 * Everyone (except a dedicated server, which draws nothing): turns grinder windows on and off from ActiveSinkers.
 */
UCLASS()
class DIMENSIONALSINKINGDEPOT_API ASinkingDepotSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ASinkingDepotSubsystem();

	/** Returns the subsystem in this world, or null (main menu, clients). */
	static ASinkingDepotSubsystem* Get(UWorld* World);

	/** Returns this mod's live configuration root section, or null if the configuration is not registered. */
	static UConfigPropertySection* GetConfigRootSection(const UWorld* World);

	/** True for a setting key the console can change (SinkWhenUploaderCantKeepUp, RequirePower). */
	static bool IsSettingKey(const FString& Key);

	/** Sets one of this machine's settings, saves it to the config file, and applies it (server, host or single player only). */
	static bool SetBoolSetting(const UWorld* World, const FString& Key, bool bValue, FString& OutMessage);

	/** Writes the current setting, cached subsystems, counters and unlock state to the output device and the log. */
	void DescribeStatus(FOutputDevice& Ar) const;

	/** Grants our schematic if the Dimensional Depot research is purchased. Returns true if it is (now) unlocked. */
	bool GrantIfResearched(const TCHAR* Reason);

	TSubclassOf<UFGSchematic> GetDepotResearch() const { return DepotResearch; }

	/** True if this building's grinder should be running (it sank something within the last moment). */
	bool IsGrinderActive(const AActor* Building) const;

	/** Server: keeps a building's grinder running regardless of activity (used by the development showcase). */
	void SetForcedActive(AActor* Building, bool bForced);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void Init() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void CacheGameSubsystems();
	void ClearGameSubsystems() const;
	void BindDelegates();
	void UnbindDelegates();
	void ApplyConfig(const TCHAR* Reason);
	bool ReadBoolSetting(const TCHAR* Key, bool bDefault) const;
	static float ReadSinkPowerMW();

	void OnNextTickAfterBeginPlay();
	void OnWatchdog();

	/** Server: rebuilds ActiveSinkers from the hook's activity record and pushes it out if it changed. */
	void UpdateActiveSinkers();

	/** Everyone: switches grinder windows to match ActiveSinkers, changing only buildings whose state differs. */
	void ApplyActiveSinkers();

	UFUNCTION()
	void OnRep_ActiveSinkers();

	UFUNCTION()
	void OnConfigValueChanged();

	UFUNCTION()
	void OnSchematicPurchased(TSubclassOf<UFGSchematic> Schematic);

	/** Loaded once; held here so they stay loaded for the world's lifetime. */
	UPROPERTY(Transient)
	TSubclassOf<UFGSchematic> OwnSchematic;

	UPROPERTY(Transient)
	TSubclassOf<UFGSchematic> DepotResearch;

	TWeakObjectPtr<UConfigPropertySection> BoundConfigSection;
	TWeakObjectPtr<AFGSchematicManager> BoundSchematicManager;

	/** Sinking Depots that sank something recently. Replicated; it only changes when a building starts or stops. */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveSinkers)
	TArray<TObjectPtr<AActor>> ActiveSinkers;

	/** What this machine's windows were last set to, so only changes are applied. */
	TSet<TWeakObjectPtr<AActor>> AppliedActive;

	/** Server: buildings kept running by SetForcedActive. */
	TSet<TWeakObjectPtr<AActor>> ForcedActive;

	bool bAppliedCantKeepUp = true;
	bool bAppliedRequirePower = false;
	FTimerHandle WatchdogTimer;
	FTimerHandle ActivityTimer;
};
