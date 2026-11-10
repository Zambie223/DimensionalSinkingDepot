#include "SinkingDepotWindow.h"

#include "SinkingDepotConstants.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotSubsystem.h"

#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "FGFactoryColoringTypes.h"
#include "TimerManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Patching/NativeHookManager.h"

namespace SinkingDepot::Window
{
	namespace
	{
		UMaterialInterface* GrinderMaterial()
		{
			// Loaded on first use (game thread). The material lives in the mod's pak, so it stays loaded once found.
			static TWeakObjectPtr<UMaterialInterface> Cached;
			if (!Cached.IsValid())
			{
				Cached = LoadObject<UMaterialInterface>(nullptr, GrinderMaterialPath);
				if (!Cached.IsValid())
				{
					UE_LOG(LogSinkingDepot, Error, TEXT("Could not load %s; Sinking Depots keep the vanilla window"), GrinderMaterialPath);
				}
			}
			return Cached.Get();
		}

		bool IsVanillaWindow(const UMaterialInterface* Material)
		{
			static const FName WindowName(VanillaWindowMaterialName);
			return Material && Material->GetFName() == WindowName;
		}

		bool IsOurGrinder(const UMaterialInterface* Material, const UMaterialInterface* Grinder)
		{
			const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
			return Dynamic && Grinder && Dynamic->Parent == Grinder;
		}

		void SetParameters(UMaterialInstanceDynamic* Dynamic, const bool bActive)
		{
			Dynamic->SetScalarParameterValue(TEXT("Speed"), bActive ? GrinderActiveSpeed : 0.0f);
			Dynamic->SetScalarParameterValue(TEXT("Glow"), bActive ? GrinderActiveGlow : 0.0f);
		}

		/** Visits every grinder window instance on the building, creating them first if bCreate is set. */
		template <typename FunctionType>
		int32 ForEachGrinder(AActor* Building, const bool bCreate, FunctionType&& Function)
		{
			UMaterialInterface* Grinder = GrinderMaterial();
			if (!Building || !Grinder || Building->GetNetMode() == NM_DedicatedServer)
			{
				return 0;
			}
			int32 Count = 0;
			TInlineComponentArray<UMeshComponent*> Meshes(Building);
			for (UMeshComponent* Mesh : Meshes)
			{
				for (int32 Slot = 0; Mesh && Slot < Mesh->GetNumMaterials(); ++Slot)
				{
					UMaterialInterface* Material = Mesh->GetMaterial(Slot);
					UMaterialInstanceDynamic* Dynamic = nullptr;
					if (IsOurGrinder(Material, Grinder))
					{
						Dynamic = Cast<UMaterialInstanceDynamic>(Material);
					}
					else if (bCreate && IsVanillaWindow(Material))
					{
						Dynamic = UMaterialInstanceDynamic::Create(Grinder, Mesh);
						Mesh->SetMaterial(Slot, Dynamic);
					}
					if (Dynamic)
					{
						Function(Dynamic);
						++Count;
					}
				}
			}
			return Count;
		}

		void OnBuildingReady(AActor* Building)
		{
			if (SinkingDepot::IsSinkingDepot(Building))
			{
				Apply(Building);
			}
		}

		/**
		 * Server: on the tick after BeginPlay (the build gun applies the player's paint before BeginPlay, but give it
		 * a tick anyway), paint a newly placed building with the default scheme, unless it came out of a save or the
		 * player's paint for new buildings isn't the factory default.
		 */
		void ScheduleDefaultPaint(AFGCentralStorageContainer* Self)
		{
			if (!SinkingDepot::IsSinkingDepot(Self) || !Self->HasAuthority() || !Self->GetWorld())
			{
				return;
			}
			const TWeakObjectPtr<AFGCentralStorageContainer> Weak(Self);
			Self->GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(Self, [Weak]()
			{
				AFGCentralStorageContainer* Building = Weak.Get();
				if (!Building || Building->Tags.Contains(LoadedFromSaveTag))
				{
					return;
				}
				static const FName FactoryDefault(FactoryDefaultSwatchName);
				const TSubclassOf<UFGFactoryCustomizationDescriptor_Swatch> Swatch = Building->GetCustomizationData_Native().SwatchDesc;
				if (Swatch && Swatch->GetFName() != FactoryDefault)
				{
					return; // The player chose a paint for new buildings; respect it.
				}
				ApplyDefaultPaint(Building);
			}));
		}
	}

	void ApplyDefaultPaint(AFGBuildable* Building)
	{
		UClass* Custom = LoadClass<UFGFactoryCustomizationDescriptor_Swatch>(nullptr, CustomSwatchClassPath);
		if (!Building || !Custom)
		{
			return;
		}
		FFactoryCustomizationData Data = Building->GetCustomizationData_Native();
		Data.SwatchDesc = Custom;
		Data.OverrideColorData.PrimaryColor = DefaultPrimaryColor;
		Data.OverrideColorData.SecondaryColor = DefaultSecondaryColor;
		Building->SetCustomizationData_Native(Data);
	}

	int32 Apply(AActor* Building)
	{
		// The replicated subsystem knows whether this building is sinking right now (also for late joiners and
		// buildings that stream in after the state changed).
		const ASinkingDepotSubsystem* Subsystem = Building ? ASinkingDepotSubsystem::Get(Building->GetWorld()) : nullptr;
		const bool bActive = Subsystem && Subsystem->IsGrinderActive(Building);
		return ForEachGrinder(Building, true, [bActive](UMaterialInstanceDynamic* Dynamic) { SetParameters(Dynamic, bActive); });
	}

	void SetActive(AActor* Building, const bool bActive)
	{
		ForEachGrinder(Building, false, [bActive](UMaterialInstanceDynamic* Dynamic) { SetParameters(Dynamic, bActive); });
	}

	void InstallHooks()
	{
#if !WITH_EDITOR
		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCentralStorageContainer::BeginPlay, GetMutableDefault<AFGCentralStorageContainer>(),
			[](AFGCentralStorageContainer* Self)
			{
				OnBuildingReady(Self);
				ScheduleDefaultPaint(Self);
			});
		// Buildings coming out of a save get PostLoadGame (before their BeginPlay); mark them so the default paint skips them.
		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCentralStorageContainer::PostLoadGame_Implementation, GetMutableDefault<AFGCentralStorageContainer>(),
			[](AFGCentralStorageContainer* Self, int32 SaveVersion, int32 GameVersion)
			{
				if (SinkingDepot::IsSinkingDepot(Self))
				{
					Self->Tags.AddUnique(LoadedFromSaveTag);
				}
			});
		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGBuildableFactory::OnBuildEffectFinished, GetMutableDefault<AFGCentralStorageContainer>(),
			[](AFGBuildableFactory* Self) { OnBuildingReady(Self); });
		UE_LOG(LogSinkingDepot, Log, TEXT("Installed the grinder window hooks"));
#endif
	}
}
