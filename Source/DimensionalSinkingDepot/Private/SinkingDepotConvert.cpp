#include "SinkingDepotConvert.h"

#include "SinkingDepotConstants.h"
#include "SinkingDepotConvertTypes.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotSubsystem.h"

#include "Async/Async.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "Buildables/FGBuildableWire.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGBlueprintProxy.h"
#include "FGBuildableSubsystem.h"
#include "FGCircuitConnectionComponent.h"
#include "FGFactoryConnectionComponent.h"
#include "FGGameState.h"
#include "FGInventoryComponent.h"
#include "FGPowerConnectionComponent.h"
#include "FGRecipe.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"

// The helpers live in SinkingDepot::Convert (not the global anonymous namespace) so a unity build that merges this
// file with SinkingDepotDevCommands.cpp can't clash with that file's helpers of the same name (FindInput).
namespace SinkingDepot::Convert
{
namespace
{
	/** How long to wait for the backup save before giving up (nothing is converted then). */
	constexpr float BackupTimeoutSeconds = 120.0f;

	/** At most this many per-building problem lines go to chat or the console; the log gets all of them. */
	constexpr int32 MaxProblemLines = 8;

	struct FTally
	{
		int32 Done = 0;
		int32 Failed = 0;
		int32 Gone = 0;
		int32 WiresRemoved = 0;
		int32 WiresRestored = 0;
		int32 WiresNotRestored = 0;
		int64 Items = 0;
		TArray<FString> Problems;
	};

	/** Logs the line and passes it to the caller (chat or console), if it can still take output. */
	void Emit(const FReporter& Reporter, const FString& Line)
	{
		UE_LOG(LogSinkingDepot, Log, TEXT("[convert] %s"), *Line);
		if (Reporter.Say)
		{
			Reporter.Say(Line);
		}
	}

	void EmitProblems(const FReporter& Reporter, const TArray<FString>& Problems)
	{
		for (int32 Index = 0; Index < Problems.Num(); ++Index)
		{
			if (Index < MaxProblemLines)
			{
				Emit(Reporter, FString::Printf(TEXT("  - %s"), *Problems[Index]));
			}
			else
			{
				UE_LOG(LogSinkingDepot, Log, TEXT("[convert]   - %s"), *Problems[Index]);
			}
		}
		if (Problems.Num() > MaxProblemLines)
		{
			Emit(Reporter, FString::Printf(TEXT("  ... and %d more (the full list is in the game log)"), Problems.Num() - MaxProblemLines));
		}
	}

	UFGFactoryConnectionComponent* FindInput(AActor* Actor)
	{
		TInlineComponentArray<UFGFactoryConnectionComponent*> Connections(Actor);
		for (UFGFactoryConnectionComponent* Connection : Connections)
		{
			if (Connection && Connection->GetDirection() == EFactoryConnectionDirection::FCD_INPUT)
			{
				return Connection;
			}
		}
		return nullptr;
	}

	UFGCircuitConnectionComponent* FindPowerConnector(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UFGPowerConnectionComponent>() : nullptr;
	}

	UFGCircuitConnectionComponent* FindConnectorByName(AActor* Actor, const FName Name)
	{
		if (!IsValid(Actor))
		{
			return nullptr;
		}
		TInlineComponentArray<UFGCircuitConnectionComponent*> Connectors(Actor);
		for (UFGCircuitConnectionComponent* Connector : Connectors)
		{
			if (Connector && Connector->GetFName() == Name)
			{
				return Connector;
			}
		}
		return nullptr;
	}

	FString Describe(const AActor* Building)
	{
		const FVector Location = Building->GetActorLocation() / 100.0;
		return FString::Printf(TEXT("%s at (%.0f, %.0f, %.0f) m"), *Building->GetName(), Location.X, Location.Y, Location.Z);
	}

	/** Empty if the building can be swapped right now, otherwise why not (it is left alone and can be retried). */
	FString WhyBusy(const AFGBuildable* Building)
	{
		if (!Building->HasActorBegunPlay() || Building->IsActorBeingDestroyed())
		{
			return TEXT("it is being built or removed right now");
		}
		if (Building->IsPlayingBuildEffect())
		{
			return TEXT("it is still playing its build animation");
		}
		return FString();
	}

	TArray<AFGCentralStorageContainer*> GetSinkingDepots(UWorld* World)
	{
		TArray<AFGCentralStorageContainer*> Result;
		for (TActorIterator<AFGCentralStorageContainer> It(World); It; ++It)
		{
			if (SinkingDepot::IsSinkingDepot(*It))
			{
				Result.Add(*It);
			}
		}
		return Result;
	}

	/**
	 * Step 1 of a swap: spawns a NewClass building on Old's spot with Old's paint, blueprint designer and blueprint
	 * link, and copies Old's buffered items into it. Old is not changed; on failure the new building is destroyed again
	 * and OutError says why.
	 */
	AFGCentralStorageContainer* SpawnReplacement(AFGCentralStorageContainer* Old, UClass* NewClass, const TSubclassOf<UFGRecipe> Recipe,
		const bool bSkipDefaultPaint, FString& OutError)
	{
		AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(Old->GetWorld());
		if (!Buildables)
		{
			OutError = TEXT("the game's buildable subsystem is missing");
			return nullptr;
		}
		const FTransform Transform = Old->GetActorTransform();
		AFGBuildable* Spawned = Buildables->BeginSpawnBuildable(NewClass, Transform);
		if (!Spawned)
		{
			OutError = TEXT("the new building could not be spawned");
			return nullptr;
		}
		Spawned->SetBuiltWithRecipe(Recipe);
		Spawned->SetCustomizationData_Native(Old->GetCustomizationData_Native());
		if (AFGBuildableBlueprintDesigner* Designer = Old->GetBlueprintDesigner())
		{
			Spawned->SetInsideBlueprintDesigner(Designer);
		}
		if (AFGBlueprintProxy* Proxy = Old->GetBlueprintProxy())
		{
			Spawned->SetBlueprintProxy(Proxy);
		}
		if (bSkipDefaultPaint)
		{
			// A restored Sinking Depot keeps the paint copied above; the "new building" default paint must not replace it.
			Spawned->Tags.AddUnique(SinkingDepot::LoadedFromSaveTag);
		}
		Spawned->FinishSpawning(Transform);

		AFGCentralStorageContainer* New = Cast<AFGCentralStorageContainer>(Spawned);
		UFGInventoryComponent* OldInventory = Old->GetStorageInventory();
		UFGInventoryComponent* NewInventory = New ? New->GetStorageInventory() : nullptr;
		if (!New || !OldInventory || !NewInventory || !FindInput(New) || NewInventory->GetSizeLinear() < OldInventory->GetSizeLinear())
		{
			OutError = TEXT("the new building came out incomplete (no inventory or belt input)");
			Spawned->Destroy();
			return nullptr;
		}
		for (int32 Index = 0; Index < OldInventory->GetSizeLinear(); ++Index)
		{
			FInventoryStack Stack;
			if (OldInventory->GetStackFromIndex(Index, Stack) && Stack.HasItems() && NewInventory->AddStackToIndex(Index, Stack, false) != Stack.NumItems)
			{
				OutError = TEXT("its buffered items did not fit into the new building");
				Spawned->Destroy();
				return nullptr;
			}
		}
		return New;
	}

	/**
	 * Step 2 of a swap (can't fail): empties Old (its items were copied in step 1), moves the belt, hands the blueprint
	 * designer and blueprint links over to New, and destroys Old. Returns the number of items moved.
	 */
	int64 CompleteSwap(AFGCentralStorageContainer* Old, AFGCentralStorageContainer* New)
	{
		int64 Items = 0;
		if (UFGInventoryComponent* OldInventory = Old->GetStorageInventory())
		{
			TArray<FInventoryStack> Stacks;
			OldInventory->GetInventoryStacks(Stacks);
			for (const FInventoryStack& Stack : Stacks)
			{
				Items += Stack.NumItems;
			}
			OldInventory->Empty();
		}
		UFGFactoryConnectionComponent* OldInput = FindInput(Old);
		if (UFGFactoryConnectionComponent* Belt = OldInput ? OldInput->GetConnection() : nullptr)
		{
			OldInput->ClearConnection();
			FindInput(New)->SetConnection(Belt);
		}

		// Register the new building first: a blueprint proxy that runs out of buildings removes itself.
		AFGBuildableBlueprintDesigner* Designer = Old->GetBlueprintDesigner();
		AFGBlueprintProxy* Proxy = Old->GetBlueprintProxy();
		if (IsValid(Designer) && !Designer->GetBuildablesInBlueprintDesigner().Contains(New))
		{
			Designer->OnBuildableConstructedInsideDesigner(New);
		}
		if (IsValid(Proxy) && !Proxy->GetBuildables().Contains(New))
		{
			Proxy->RegisterBuildable(New);
		}
		Old->Destroy();
		// Destroying may already have unregistered Old; only tell them about it if it didn't.
		if (IsValid(Designer) && Designer->GetBuildablesInBlueprintDesigner().Contains(Old))
		{
			Designer->OnBuildableDismantledInsideDesigner(Old);
		}
		if (IsValid(Proxy) && Proxy->GetBuildables().Contains(Old))
		{
			Proxy->UnregisterBuildable(Old);
		}
		return Items;
	}

	/** Removes every wire on the Sinking Depot's connector, recording each one's far end for the undo. */
	void RemoveWires(AFGCentralStorageContainer* Depot, FSinkingDepotConvertRecord& Record, FTally& Tally)
	{
		UFGCircuitConnectionComponent* Connector = FindPowerConnector(Depot);
		if (!Connector)
		{
			return;
		}
		TArray<AFGBuildableWire*> Wires;
		Connector->GetWires(Wires);
		for (AFGBuildableWire* Wire : Wires)
		{
			if (!IsValid(Wire))
			{
				continue;
			}
			const UFGCircuitConnectionComponent* Other = Wire->GetOppositeConnection(Connector);
			if (Other && Other->GetOwner())
			{
				FSinkingDepotWireRecord& Entry = Record.Wires.AddDefaulted_GetRef();
				Entry.OtherActor = Other->GetOwner();
				Entry.OtherConnector = Other->GetFName();
				Entry.bOtherEndConverted = SinkingDepot::IsSinkingDepot(Other->GetOwner()); // Fixed up after the pass.
				Entry.WireClass = Wire->GetClass();
				Entry.WireRecipe = Wire->GetBuiltWithRecipe();
				Entry.WirePaint = Wire->GetCustomizationData_Native();
			}
			AFGBuildableBlueprintDesigner* Designer = Wire->GetBlueprintDesigner();
			Wire->Destroy();
			// A wire inside a blueprint designer is listed there too; make sure it doesn't stay listed.
			if (IsValid(Designer) && Designer->GetBuildablesInBlueprintDesigner().Contains(Wire))
			{
				Designer->OnBuildableDismantledInsideDesigner(Wire);
			}
			++Tally.WiresRemoved;
		}
	}

	/** Spawns a wire between the two connectors, as the original was (class, recipe, paint). */
	bool RestoreWire(UWorld* World, const FSinkingDepotWireRecord& Entry, UFGCircuitConnectionComponent* Ours, UFGCircuitConnectionComponent* Other)
	{
		AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World);
		UClass* WireClass = Entry.WireClass ? Entry.WireClass.Get() : LoadClass<AFGBuildableWire>(nullptr, SinkingDepot::PowerLineClassPath);
		if (!Buildables || !WireClass || Ours->GetNumFreeConnections() <= 0 || Other->GetNumFreeConnections() <= 0)
		{
			return false;
		}
		const FTransform Transform(Ours->GetComponentLocation());
		AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Buildables->BeginSpawnBuildable(WireClass, Transform));
		if (!Wire)
		{
			return false;
		}
		Wire->SetBuiltWithRecipe(Entry.WireRecipe ? Entry.WireRecipe : TSubclassOf<UFGRecipe>(LoadClass<UFGRecipe>(nullptr, SinkingDepot::PowerLineRecipePath)));
		Wire->SetCustomizationData_Native(Entry.WirePaint);
		// Both ends in the same blueprint designer: the wire belongs to that designer as well.
		const AFGBuildable* OursBuilding = Cast<AFGBuildable>(Ours->GetOwner());
		const AFGBuildable* OtherBuilding = Cast<AFGBuildable>(Other->GetOwner());
		AFGBuildableBlueprintDesigner* Designer = OursBuilding ? OursBuilding->GetBlueprintDesigner() : nullptr;
		if (Designer && (!OtherBuilding || OtherBuilding->GetBlueprintDesigner() != Designer))
		{
			Designer = nullptr;
		}
		if (Designer)
		{
			Wire->SetInsideBlueprintDesigner(Designer);
		}
		const bool bConnected = Wire->Connect(Ours, Other);
		Wire->FinishSpawning(Transform);
		if (!bConnected)
		{
			Wire->Destroy();
			if (Designer && Designer->GetBuildablesInBlueprintDesigner().Contains(Wire))
			{
				Designer->OnBuildableDismantledInsideDesigner(Wire);
			}
			return false;
		}
		if (Designer && !Designer->GetBuildablesInBlueprintDesigner().Contains(Wire))
		{
			Designer->OnBuildableConstructedInsideDesigner(Wire);
		}
		return true;
	}

	void ConvertNow(UWorld* World, const FCommandNames& Names, const FReporter& Reporter)
	{
		UClass* VanillaClass = LoadClass<AFGBuildable>(nullptr, SinkingDepot::VanillaUploaderClassPath);
		const TSubclassOf<UFGRecipe> VanillaRecipe = LoadClass<UFGRecipe>(nullptr, SinkingDepot::VanillaUploaderRecipePath);
		const TSubclassOf<UFGRecipe> OwnRecipe = LoadClass<UFGRecipe>(nullptr, SinkingDepot::RecipeClassPath);
		if (!VanillaClass || !VanillaRecipe)
		{
			Emit(Reporter, TEXT("Could not load the vanilla Dimensional Depot Uploader; nothing was converted."));
			return;
		}

		FTally Tally;
		TArray<FSinkingDepotConvertRecord> Records;
		TMap<AActor*, AActor*> Replacements;
		const TArray<AFGCentralStorageContainer*> Depots = GetSinkingDepots(World);
		for (AFGCentralStorageContainer* Old : Depots)
		{
			const FString Name = Describe(Old);
			FString Error = WhyBusy(Old);
			AFGCentralStorageContainer* New = Error.IsEmpty() ? SpawnReplacement(Old, VanillaClass, VanillaRecipe, false, Error) : nullptr;
			if (!New)
			{
				++Tally.Failed;
				Tally.Problems.Add(FString::Printf(TEXT("%s: %s; left as it was"), *Name, *Error));
				continue;
			}
			FSinkingDepotConvertRecord& Record = Records.AddDefaulted_GetRef();
			Record.Uploader = New;
			Record.OriginalRecipe = Old->GetBuiltWithRecipe() ? Old->GetBuiltWithRecipe() : OwnRecipe;
			RemoveWires(Old, Record, Tally);
			Tally.Items += CompleteSwap(Old, New);
			Replacements.Add(Old, New);
			++Tally.Done;
		}

		// Wires between two Sinking Depots: point the record at the other one's replacement, so undo can find it.
		for (FSinkingDepotConvertRecord& Record : Records)
		{
			for (FSinkingDepotWireRecord& Entry : Record.Wires)
			{
				if (Entry.bOtherEndConverted)
				{
					if (AActor* const* Replacement = Replacements.Find(Entry.OtherActor))
					{
						Entry.OtherActor = *Replacement;
					}
					else
					{
						Entry.bOtherEndConverted = false; // It failed to convert and is still the same Sinking Depot.
					}
				}
			}
		}

		ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
		const FString Summary = FString::Printf(TEXT("Converted %d of %d Dimensional Sinking Depot(s) into vanilla uploaders: %lld buffered item(s) moved, %d power wire(s) removed."),
			Tally.Done, Depots.Num(), Tally.Items, Tally.WiresRemoved);
		Emit(Reporter, Summary);
		if (Tally.Failed > 0)
		{
			Emit(Reporter, FString::Printf(TEXT("%d could not be converted:"), Tally.Failed));
			EmitProblems(Reporter, Tally.Problems);
			Emit(Reporter, FString::Printf(TEXT("Run '%s confirm nobackup' again to retry those."), *Names.Convert));
		}
		if (Tally.Done > 0)
		{
			if (Subsystem)
			{
				Subsystem->SetUndoRecords(MoveTemp(Records));
				Emit(Reporter, FString::Printf(TEXT("To undo: '%s' (works while the mod is installed, also after saving and reloading)."), *Names.Undo));
			}
			else
			{
				Emit(Reporter, TEXT("The mod's subsystem is missing, so this conversion can't be undone with the undo command."));
			}
			if (Tally.Failed == 0)
			{
				Emit(Reporter, TEXT("To remove the mod: save the game now, then remove the mod."));
			}
		}
		if (Subsystem)
		{
			Subsystem->SetLastConvertResult(Tally.Failed > 0 ? FString::Printf(TEXT("%s %d could not be converted (see the log)."), *Summary, Tally.Failed) : Summary);
		}
	}

	void StartBackupThenConvert(UWorld* World, const FCommandNames& Names, const FReporter& Reporter)
	{
		UFGSaveSession* Session = UFGSaveSession::Get(World);
		if (!Session)
		{
			Emit(Reporter, TEXT("Could not save a backup (the game's save session is missing); nothing was converted."));
			return;
		}
		if (Session->HasSaveGameOperationPending())
		{
			Emit(Reporter, TEXT("The game is saving right now; run the command again in a few seconds. Nothing was converted."));
			return;
		}
		const AFGGameState* GameState = World->GetGameState<AFGGameState>();
		const FString SessionName = GameState ? GameState->GetSessionName() : FString();
		const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
		FString SaveName = UFGSaveSystem::SanitizeSaveName(FString::Printf(TEXT("%s_before_SinkingDepot_convert_%s"),
			SessionName.IsEmpty() ? TEXT("World") : *SessionName, *Stamp));
		if (!UFGSaveSystem::IsValidSaveName(SaveName))
		{
			SaveName = FString::Printf(TEXT("SinkingDepot_backup_before_convert_%s"), *Stamp);
		}
		Emit(Reporter, FString::Printf(TEXT("Saving a backup as '%s'; the conversion starts as soon as it is written."), *SaveName));
		if (!Reporter.bCanSayLater)
		{
			Emit(Reporter, TEXT("The result goes to the server log; run SinkingDepot.Status in a few seconds to see it."));
		}

		// The save completes later (end of frame, then a background write). Lines after that only go where they can.
		const FReporter Later = Reporter.bCanSayLater ? Reporter : FReporter{};
		const TSharedRef<bool> bSettled = MakeShared<bool>(false);
		const TWeakObjectPtr<UWorld> WeakWorld(World);

		FTimerHandle Timeout;
		World->GetTimerManager().SetTimer(Timeout, FTimerDelegate::CreateLambda([bSettled, Later, WeakWorld]()
		{
			if (*bSettled)
			{
				return;
			}
			*bSettled = true;
			const FString Line = TEXT("The backup save did not finish in time; nothing was converted. Try again, or use 'confirm nobackup' after saving by hand.");
			Emit(Later, Line);
			if (ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(WeakWorld.Get()))
			{
				Subsystem->SetLastConvertResult(Line);
			}
		}), BackupTimeoutSeconds, false);

		Session->SaveGame(SaveName, FOnSaveGameComplete::CreateLambda([bSettled, Later, WeakWorld, Names, SaveName](const bool bSuccess, const FText& Error, void*)
		{
			TFunction<void()> Continue = [bSettled, Later, WeakWorld, Names, SaveName, bSuccess, ErrorText = Error.ToString()]()
			{
				UWorld* World = WeakWorld.Get();
				if (*bSettled || !World)
				{
					return;
				}
				*bSettled = true;
				if (!bSuccess)
				{
					const FString Line = FString::Printf(TEXT("The backup save '%s' failed (%s); nothing was converted."), *SaveName, *ErrorText);
					Emit(Later, Line);
					if (ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World))
					{
						Subsystem->SetLastConvertResult(Line);
					}
					return;
				}
				Emit(Later, FString::Printf(TEXT("Backup saved as '%s'."), *SaveName));
				// Convert on the next frame, from the game thread's normal tick, not from inside the save callback.
				World->GetTimerManager().SetTimerForNextTick([WeakWorld, Names, Later]()
				{
					if (UWorld* NextWorld = WeakWorld.Get())
					{
						ConvertNow(NextWorld, Names, Later);
					}
				});
			};
			if (IsInGameThread())
			{
				Continue();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(Continue));
			}
		}), nullptr);
	}

	bool IsWord(const TArray<FString>& Args, const int32 Index, const TCHAR* Word)
	{
		return Args.IsValidIndex(Index) && Args[Index].Equals(Word, ESearchCase::IgnoreCase);
	}

	bool CheckAuthority(UWorld* World, const FReporter& Reporter)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			Emit(Reporter, TEXT("Only the host or the server can run this."));
			return false;
		}
		return true;
	}
}
}

void SinkingDepot::Convert::RunConvert(UWorld* World, const TArray<FString>& Args, const FCommandNames& Names, const FReporter& Reporter)
{
	if (!CheckAuthority(World, Reporter))
	{
		return;
	}
	const TArray<AFGCentralStorageContainer*> Depots = GetSinkingDepots(World);
	if (Depots.IsEmpty())
	{
		Emit(Reporter, TEXT("There are no Dimensional Sinking Depots in this world, so there is nothing to convert. You can remove the mod."));
		return;
	}
	if (!IsWord(Args, 0, TEXT("confirm")))
	{
		int32 Wired = 0;
		TSet<AFGBuildableWire*> Wires; // A wire between two Sinking Depots counts once.
		for (AFGCentralStorageContainer* Depot : Depots)
		{
			if (const UFGCircuitConnectionComponent* Connector = FindPowerConnector(Depot))
			{
				TArray<AFGBuildableWire*> Found;
				Connector->GetWires(Found);
				Wired += Found.Num() > 0 ? 1 : 0;
				Wires.Append(Found);
			}
		}
		Emit(Reporter, FString::Printf(TEXT("This turns all %d Dimensional Sinking Depot(s) in this world into vanilla Dimensional Depot Uploaders, so you can remove the mod without losing them. Belts, buffered items and paint carry over."), Depots.Num()));
		Emit(Reporter, Wired > 0
			? FString::Printf(TEXT("%d of them have power wires (%d in all). Vanilla uploaders take no power, so those wires are removed. Nothing is refunded, including the AWESOME Sink half of the cost."), Wired, Wires.Num())
			: FString(TEXT("Nothing is refunded, including the AWESOME Sink half of the cost.")));
		FString UndoLine = FString::Printf(TEXT("You can undo it with '%s' while the mod is still installed."), *Names.Undo);
		const ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
		if (Subsystem && !Subsystem->GetUndoRecords().IsEmpty())
		{
			UndoLine += FString::Printf(TEXT(" This replaces what the last conversion can undo (%d uploader(s))."), Subsystem->GetUndoRecords().Num());
		}
		Emit(Reporter, UndoLine);
		Emit(Reporter, FString::Printf(TEXT("To go ahead: '%s confirm backup' saves a backup of the world first; '%s confirm nobackup' skips the backup."), *Names.Convert, *Names.Convert));
		return;
	}
	if (IsWord(Args, 1, TEXT("backup")))
	{
		StartBackupThenConvert(World, Names, Reporter);
	}
	else if (IsWord(Args, 1, TEXT("nobackup")))
	{
		ConvertNow(World, Names, Reporter);
	}
	else
	{
		Emit(Reporter, FString::Printf(TEXT("Nothing was converted yet. Choose: '%s confirm backup' saves a backup of the world first; '%s confirm nobackup' skips the backup."), *Names.Convert, *Names.Convert));
	}
}

void SinkingDepot::Convert::RunUndo(UWorld* World, const TArray<FString>& Args, const FCommandNames& Names, const FReporter& Reporter)
{
	if (!CheckAuthority(World, Reporter))
	{
		return;
	}
	ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
	UClass* VanillaClass = LoadClass<AFGBuildable>(nullptr, SinkingDepot::VanillaUploaderClassPath);
	if (!VanillaClass)
	{
		// Without it every record would look dismantled and be thrown away.
		Emit(Reporter, TEXT("Could not load the vanilla Dimensional Depot Uploader class; nothing was changed."));
		return;
	}
	const TArray<FSinkingDepotConvertRecord> Records = Subsystem ? Subsystem->GetUndoRecords() : TArray<FSinkingDepotConvertRecord>();
	int32 Present = 0;
	int32 Wires = 0;
	for (const FSinkingDepotConvertRecord& Record : Records)
	{
		if (IsValid(Record.Uploader) && Record.Uploader->GetClass() == VanillaClass)
		{
			++Present;
			Wires += Record.Wires.Num();
		}
	}
	if (Present == 0)
	{
		Emit(Reporter, Records.IsEmpty()
			? FString::Printf(TEXT("Nothing to undo: no conversion with '%s' has been made in this world (or it was already undone)."), *Names.Convert)
			: FString(TEXT("Nothing to undo: every uploader from the last conversion has been dismantled since.")));
		if (Subsystem && !Records.IsEmpty())
		{
			Subsystem->SetUndoRecords({});
		}
		return;
	}
	if (!IsWord(Args, 0, TEXT("confirm")))
	{
		Emit(Reporter, FString::Printf(TEXT("This turns the %d vanilla uploader(s) from the last conversion back into Dimensional Sinking Depots, for free. Belts, buffered items and paint carry over."), Present));
		if (Wires > 0)
		{
			Emit(Reporter, FString::Printf(TEXT("Their %d power wire(s) are put back where the building at the other end still exists and has a free slot."), Wires));
		}
		if (Present < Records.Num())
		{
			Emit(Reporter, FString::Printf(TEXT("%d uploader(s) from that conversion have been dismantled since and are skipped."), Records.Num() - Present));
		}
		Emit(Reporter, FString::Printf(TEXT("Uploaders you built yourself are never touched. To go ahead: '%s confirm'."), *Names.Undo));
		return;
	}

	UClass* OwnClass = LoadClass<AFGBuildable>(nullptr, SinkingDepot::BuildClassPath);
	const TSubclassOf<UFGRecipe> OwnRecipe = LoadClass<UFGRecipe>(nullptr, SinkingDepot::RecipeClassPath);
	if (!OwnClass)
	{
		Emit(Reporter, TEXT("Could not load the Dimensional Sinking Depot building; nothing was changed."));
		return;
	}

	FTally Tally;
	TArray<FSinkingDepotConvertRecord> Keep; // Busy or failed ones stay undoable.
	TMap<AActor*, AActor*> Restored;          // Uploader -> the Sinking Depot that replaced it.
	TArray<TPair<AFGCentralStorageContainer*, const FSinkingDepotConvertRecord*>> WireWork;
	for (const FSinkingDepotConvertRecord& Record : Records)
	{
		AFGCentralStorageContainer* Old = IsValid(Record.Uploader) && Record.Uploader->GetClass() == VanillaClass ? Cast<AFGCentralStorageContainer>(Record.Uploader) : nullptr;
		if (!Old)
		{
			++Tally.Gone;
			Tally.WiresNotRestored += Record.Wires.Num();
			continue;
		}
		const FString Name = Describe(Old);
		FString Error = WhyBusy(Old);
		AFGCentralStorageContainer* New = Error.IsEmpty() ? SpawnReplacement(Old, OwnClass, Record.OriginalRecipe ? Record.OriginalRecipe : OwnRecipe, true, Error) : nullptr;
		if (!New)
		{
			++Tally.Failed;
			Tally.Problems.Add(FString::Printf(TEXT("%s: %s; left as it was"), *Name, *Error));
			Keep.Add(Record);
			continue;
		}
		Tally.Items += CompleteSwap(Old, New);
		Restored.Add(Old, New);
		WireWork.Emplace(New, &Record);
		++Tally.Done;
	}

	// Wires after all buildings, so a wire between two restored Sinking Depots finds both ends.
	for (const TPair<AFGCentralStorageContainer*, const FSinkingDepotConvertRecord*>& Work : WireWork)
	{
		UFGCircuitConnectionComponent* Ours = FindPowerConnector(Work.Key);
		for (const FSinkingDepotWireRecord& Entry : Work.Value->Wires)
		{
			AActor* OtherActor = Entry.OtherActor;
			if (Entry.bOtherEndConverted)
			{
				AActor* const* Replacement = Restored.Find(OtherActor);
				OtherActor = Replacement ? *Replacement : nullptr;
			}
			UFGCircuitConnectionComponent* Other = FindConnectorByName(OtherActor, Entry.OtherConnector);
			if (Ours && Other && RestoreWire(World, Entry, Ours, Other))
			{
				++Tally.WiresRestored;
			}
			else
			{
				++Tally.WiresNotRestored;
			}
		}
	}
	if (Subsystem)
	{
		Subsystem->SetUndoRecords(MoveTemp(Keep));
	}

	FString Summary = FString::Printf(TEXT("Turned %d uploader(s) back into Dimensional Sinking Depots: %lld buffered item(s) moved, %d power wire(s) put back"),
		Tally.Done, Tally.Items, Tally.WiresRestored);
	Summary += Tally.WiresNotRestored > 0
		? FString::Printf(TEXT(", %d not (the building at the other end is gone or has no free slot)."), Tally.WiresNotRestored)
		: FString(TEXT("."));
	Emit(Reporter, Summary);
	if (Tally.Gone > 0)
	{
		Emit(Reporter, FString::Printf(TEXT("%d uploader(s) from that conversion had been dismantled and were skipped."), Tally.Gone));
	}
	if (Tally.Failed > 0)
	{
		Emit(Reporter, FString::Printf(TEXT("%d could not be turned back and stay vanilla uploaders for now:"), Tally.Failed));
		EmitProblems(Reporter, Tally.Problems);
		Emit(Reporter, FString::Printf(TEXT("Run '%s confirm' again to retry those."), *Names.Undo));
	}
	if (Subsystem)
	{
		Subsystem->SetLastConvertResult(Summary);
	}
}
