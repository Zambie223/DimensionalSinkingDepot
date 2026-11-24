#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

/** Calling the game's Blueprint functions by name, for widgets whose classes only exist as Blueprints. */
namespace SinkingDepot::Reflection
{
	/**
	 * Calls the named function. Fill sets the parameters before the call, Read looks at them after it (out and return
	 * values). False if the object or function doesn't exist.
	 *
	 * Only the parameters are initialised and destroyed, as the engine does. UStruct::InitializeStruct must not be used here:
	 * it covers the whole function struct, including a Blueprint function's locals, and the buffer only holds ParmsSize.
	 */
	inline bool Call(UObject* Object, const TCHAR* FunctionName, TFunctionRef<void(UFunction*, uint8*)> Fill, TFunctionRef<void(UFunction*, uint8*)> Read)
	{
		UFunction* Function = Object ? Object->FindFunction(FName(FunctionName)) : nullptr;
		if (!Function)
		{
			return false;
		}
		uint8* Params = static_cast<uint8*>(FMemory_Alloca_Aligned(FMath::Max<int32>(Function->ParmsSize, 1), Function->GetMinAlignment()));
		FMemory::Memzero(Params, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}
		Fill(Function, Params);
		Object->ProcessEvent(Function, Params);
		Read(Function, Params);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
		return true;
	}

	inline bool Call(UObject* Object, const TCHAR* FunctionName, TFunctionRef<void(UFunction*, uint8*)> Fill)
	{
		return Call(Object, FunctionName, Fill, [](UFunction*, uint8*) {});
	}

	inline bool Call(UObject* Object, const TCHAR* FunctionName)
	{
		return Call(Object, FunctionName, [](UFunction*, uint8*) {}, [](UFunction*, uint8*) {});
	}

	// Typed setters: each does nothing unless the parameter exists and really has that type.

	inline void SetBool(UFunction* Function, uint8* Params, const TCHAR* Name, const bool bValue)
	{
		if (const FBoolProperty* Property = CastField<FBoolProperty>(Function->FindPropertyByName(FName(Name))))
		{
			Property->SetPropertyValue_InContainer(Params, bValue);
		}
	}

	inline void SetDouble(UFunction* Function, uint8* Params, const TCHAR* Name, const double Value)
	{
		if (const FDoubleProperty* Property = CastField<FDoubleProperty>(Function->FindPropertyByName(FName(Name))))
		{
			Property->SetPropertyValue_InContainer(Params, Value);
		}
	}

	inline void SetInt64(UFunction* Function, uint8* Params, const TCHAR* Name, const int64 Value)
	{
		if (const FInt64Property* Property = CastField<FInt64Property>(Function->FindPropertyByName(FName(Name))))
		{
			Property->SetPropertyValue_InContainer(Params, Value);
		}
	}

	inline void SetText(UFunction* Function, uint8* Params, const TCHAR* Name, const FText& Value)
	{
		if (const FTextProperty* Property = CastField<FTextProperty>(Function->FindPropertyByName(FName(Name))))
		{
			Property->SetPropertyValue_InContainer(Params, Value);
		}
	}

	inline void SetObject(UFunction* Function, uint8* Params, const TCHAR* Name, UObject* Value)
	{
		const FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(Function->FindPropertyByName(FName(Name)));
		if (Property && (!Value || Value->IsA(Property->PropertyClass)))
		{
			Property->SetObjectPropertyValue_InContainer(Params, Value);
		}
	}

	inline void SetColor(UFunction* Function, uint8* Params, const TCHAR* Name, const FLinearColor& Value)
	{
		const FStructProperty* Property = CastField<FStructProperty>(Function->FindPropertyByName(FName(Name)));
		if (Property && Property->Struct == TBaseStructure<FLinearColor>::Get())
		{
			*Property->ContainerPtrToValuePtr<FLinearColor>(Params) = Value;
		}
	}

	/** An int32 out parameter after the call, or Default. */
	inline int32 GetInt(UFunction* Function, const uint8* Params, const TCHAR* Name, const int32 Default)
	{
		const FIntProperty* Property = CastField<FIntProperty>(Function->FindPropertyByName(FName(Name)));
		return Property ? Property->GetPropertyValue_InContainer(Params) : Default;
	}
}
