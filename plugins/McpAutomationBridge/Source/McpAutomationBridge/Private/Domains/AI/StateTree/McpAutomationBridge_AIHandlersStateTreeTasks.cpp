#include "Domains/AI/McpAutomationBridge_AIHandlerContext.h"

#if WITH_EDITOR
#include "Domains/AI/StateTree/McpAutomationBridge_AIStateTreeFeature.h"

// Explicit includes for the types Gate 2 + Gate 3 reference. The shared
// feature header pulls these in too under nested __has_include guards, but
// a direct include here removes any inclusion-order fragility for this
// translation unit.
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
#include "StateTreeTaskBase.h"
#include "StateTreeEditorNode.h"
#include "GameplayTagContainer.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonValue.h"
#endif

namespace McpAIHandlers
{
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
// Gate 3: map one JSON value onto a single FProperty on an instance-data
// struct. Covers the property types Canopy's tasks actually use:
//   numeric (int/float/double/byte)
//   bool
//   string / name / text
//   enum (accepts string enum-value name OR numeric)
//   struct: FGameplayTag, FVector, FVector2D, FSoftObjectPath
//   object (soft or hard) refs by path
// Anything else logs a warning and is skipped (leaves default). Return
// value is whether the field was applied; failed / skipped fields are
// noted via OutErrors.
static bool SetPropertyFromJson(
    FProperty* Prop, void* Container,
    const TSharedPtr<FJsonValue>& Value,
    const FString& KeyForDiag,
    TArray<FString>& OutErrors)
{
    if (!Prop || !Value.IsValid())
    {
        return false;
    }

    if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
    {
        double N = 0.0;
        if (!Value->TryGetNumber(N))
        {
            OutErrors.Add(FString::Printf(TEXT("%s: expected number, got %s"), *KeyForDiag, *FString::FromInt(int32(Value->Type))));
            return false;
        }
        IntProp->SetPropertyValue(Container, static_cast<int32>(N));
        return true;
    }
    if (const FInt64Property* I64Prop = CastField<FInt64Property>(Prop))
    {
        double N = 0.0;
        if (!Value->TryGetNumber(N)) return false;
        I64Prop->SetPropertyValue(Container, static_cast<int64>(N));
        return true;
    }
    if (const FFloatProperty* FProp = CastField<FFloatProperty>(Prop))
    {
        double N = 0.0;
        if (!Value->TryGetNumber(N)) return false;
        FProp->SetPropertyValue(Container, static_cast<float>(N));
        return true;
    }
    if (const FDoubleProperty* DProp = CastField<FDoubleProperty>(Prop))
    {
        double N = 0.0;
        if (!Value->TryGetNumber(N)) return false;
        DProp->SetPropertyValue(Container, N);
        return true;
    }
    if (const FBoolProperty* BProp = CastField<FBoolProperty>(Prop))
    {
        bool B = false;
        if (!Value->TryGetBool(B)) return false;
        BProp->SetPropertyValue(Container, B);
        return true;
    }
    if (const FStrProperty* SProp = CastField<FStrProperty>(Prop))
    {
        FString S;
        if (!Value->TryGetString(S)) return false;
        SProp->SetPropertyValue(Container, S);
        return true;
    }
    if (const FNameProperty* NProp = CastField<FNameProperty>(Prop))
    {
        FString S;
        if (!Value->TryGetString(S)) return false;
        NProp->SetPropertyValue(Container, FName(*S));
        return true;
    }
    if (const FTextProperty* TProp = CastField<FTextProperty>(Prop))
    {
        FString S;
        if (!Value->TryGetString(S)) return false;
        TProp->SetPropertyValue(Container, FText::FromString(S));
        return true;
    }

    // Enum: accept either enum-value NAME string ("PickUpFood") or numeric.
    if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
    {
        FString S;
        if (Value->TryGetString(S))
        {
            const UEnum* Enum = EnumProp->GetEnum();
            const int64 EnumVal = Enum ? Enum->GetValueByNameString(S) : INDEX_NONE;
            if (EnumVal == INDEX_NONE)
            {
                OutErrors.Add(FString::Printf(TEXT("%s: enum value '%s' not found in %s"),
                    *KeyForDiag, *S, Enum ? *Enum->GetName() : TEXT("<null>")));
                return false;
            }
            EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(Container, EnumVal);
            return true;
        }
        double N = 0.0;
        if (Value->TryGetNumber(N))
        {
            EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(Container, static_cast<int64>(N));
            return true;
        }
        return false;
    }
    // Byte-enum: same treatment but on a plain FByteProperty with an Enum.
    if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
    {
        if (ByteProp->Enum)
        {
            FString S;
            if (Value->TryGetString(S))
            {
                const int64 EnumVal = ByteProp->Enum->GetValueByNameString(S);
                if (EnumVal == INDEX_NONE)
                {
                    OutErrors.Add(FString::Printf(TEXT("%s: enum value '%s' not found in %s"),
                        *KeyForDiag, *S, *ByteProp->Enum->GetName()));
                    return false;
                }
                ByteProp->SetPropertyValue(Container, static_cast<uint8>(EnumVal));
                return true;
            }
        }
        double N = 0.0;
        if (!Value->TryGetNumber(N)) return false;
        ByteProp->SetPropertyValue(Container, static_cast<uint8>(N));
        return true;
    }

    // Object refs (both TObjectPtr<X> and TSoftObjectPtr<X>) by path string.
    if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
    {
        FString Path;
        if (!Value->TryGetString(Path)) return false;
        UObject* Loaded = StaticLoadObject(ObjProp->PropertyClass, nullptr, *Path);
        ObjProp->SetObjectPropertyValue(Container, Loaded);
        return Loaded != nullptr;
    }
    if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
    {
        FString Path;
        if (!Value->TryGetString(Path)) return false;
        const FSoftObjectPath SoftPath(Path);
        FSoftObjectPtr Soft(SoftPath);
        SoftProp->SetPropertyValue(Container, Soft);
        return true;
    }

    // Struct properties -- handle the common wire-format-friendly ones.
    if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
    {
        UScriptStruct* SS = StructProp->Struct;
        if (!SS) return false;

        // FGameplayTag: accept a tag name string like "Canopy.Agent.HasTarget".
        if (SS == FGameplayTag::StaticStruct())
        {
            FString S;
            if (!Value->TryGetString(S)) return false;
            const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*S),
                /*ErrorIfNotFound*/ false);
            *reinterpret_cast<FGameplayTag*>(Container) = Tag;
            return true;
        }

        // FVector: accept {x, y, z} object or [x, y, z] array.
        if (SS == TBaseStructure<FVector>::Get())
        {
            FVector V(0);
            const TSharedPtr<FJsonObject>* Obj = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            if (Value->TryGetObject(Obj) && Obj && (*Obj).IsValid())
            {
                double X = 0, Y = 0, Z = 0;
                (*Obj)->TryGetNumberField(TEXT("x"), X);
                (*Obj)->TryGetNumberField(TEXT("y"), Y);
                (*Obj)->TryGetNumberField(TEXT("z"), Z);
                V = FVector(X, Y, Z);
            }
            else if (Value->TryGetArray(Arr) && Arr && Arr->Num() >= 3)
            {
                double X = 0, Y = 0, Z = 0;
                (*Arr)[0]->TryGetNumber(X);
                (*Arr)[1]->TryGetNumber(Y);
                (*Arr)[2]->TryGetNumber(Z);
                V = FVector(X, Y, Z);
            }
            else
            {
                return false;
            }
            *reinterpret_cast<FVector*>(Container) = V;
            return true;
        }

        // FSoftObjectPath: accept path string.
        if (SS == TBaseStructure<FSoftObjectPath>::Get())
        {
            FString Path;
            if (!Value->TryGetString(Path)) return false;
            *reinterpret_cast<FSoftObjectPath*>(Container) = FSoftObjectPath(Path);
            return true;
        }

        OutErrors.Add(FString::Printf(TEXT("%s: struct type %s not supported in Gate 3 (skipped)"),
            *KeyForDiag, *SS->GetName()));
        return false;
    }

    OutErrors.Add(FString::Printf(TEXT("%s: property type %s not supported (skipped)"),
        *KeyForDiag, *Prop->GetClass()->GetName()));
    return false;
}

// Walk a JSON object, applying each key -> FProperty on the given
// instance-data FInstancedStruct.
static void ApplyJsonPropertiesToInstance(
    const TSharedPtr<FJsonObject>& Properties,
    FInstancedStruct& Instance,
    TArray<FString>& OutErrors,
    int32& OutAppliedCount)
{
    OutAppliedCount = 0;
    if (!Properties.IsValid()) return;
    const UScriptStruct* Struct = Instance.GetScriptStruct();
    if (!Struct) { OutErrors.Add(TEXT("instance has no struct type")); return; }
    uint8* Memory = Instance.GetMutableMemory();
    if (!Memory)  { OutErrors.Add(TEXT("instance has no memory"));     return; }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
    {
        const FString& Key = Pair.Key;
        FProperty* Prop = Struct->FindPropertyByName(FName(*Key));
        if (!Prop)
        {
            OutErrors.Add(FString::Printf(TEXT("Property '%s' not found on %s"),
                *Key, *Struct->GetName()));
            continue;
        }
        void* Container = Prop->ContainerPtrToValuePtr<void>(Memory);
        if (SetPropertyFromJson(Prop, Container, Pair.Value, Key, OutErrors))
        {
            ++OutAppliedCount;
        }
    }
}
#endif // MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
} // namespace McpAIHandlers

namespace McpAIHandlers
{
bool HandleConfigureStateTreeTask(UMcpAutomationBridgeSubsystem* Self, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
    const FString SubAction = TEXT("configure_state_tree_task");
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    if (SubAction == TEXT("configure_state_tree_task"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        FString TaskType = GetStringFieldAI(Payload, TEXT("taskType"), TEXT(""));

        if (StateTreePath.IsEmpty() || StateName.IsEmpty())
        {
            Self->SendAutomationError(RequestingSocket, RequestId, TEXT("stateTreePath and stateName are required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        // Load the StateTree
        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            Self->SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            Self->SendAutomationError(RequestingSocket, RequestId, TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find the state
        UStateTreeState* FoundState = nullptr;
        TFunction<UStateTreeState*(UStateTreeState*, const FString&)> FindState;
        FindState = [&FindState](UStateTreeState* State, const FString& Name) -> UStateTreeState* {
            if (!State) return nullptr;
            if (State->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
            {
                return State;
            }
            for (UStateTreeState* Child : State->Children)
            {
                if (UStateTreeState* Found = FindState(Child, Name))
                {
                    return Found;
                }
            }
            return nullptr;
        };

        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            FoundState = FindState(SubTree, StateName);
            if (FoundState) break;
        }

        if (!FoundState)
        {
            Self->SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("State '%s' not found"), *StateName), TEXT("NOT_FOUND"));
            return true;
        }

        // Optional: insert a C++ StateTree task node onto the found state.
        // Consumers set nodeClass to a full struct path
        // (e.g. "/Script/CanopyDemo.CanopyAntForageTask") to add a task that
        // derives from FStateTreeTaskBase. The task's instance data is auto-
        // initialised from its FStateTreeNodeBase::GetInstanceDataType() (the
        // same pattern StateTreeState.h::AddTask<T>() uses). Instance data
        // property values from a "properties" payload object are wired up in
        // Gate 3; Gate 2 here inserts the task with default-constructed
        // instance data.
        FString AddedNodeClassPath;
        FGuid   AddedNodeId;
        bool    bTaskAdded = false;
        {
            const FString NodeClass = GetStringFieldAI(Payload, TEXT("nodeClass"), TEXT(""));
            if (!NodeClass.IsEmpty())
            {
                UScriptStruct* TaskStruct = LoadObject<UScriptStruct>(nullptr, *NodeClass);
                if (!TaskStruct)
                {
                    TaskStruct = FindObject<UScriptStruct>(nullptr, *NodeClass);
                }
                if (!TaskStruct)
                {
                    Self->SendAutomationError(RequestingSocket, RequestId,
                        FString::Printf(TEXT("Task nodeClass not found: %s"), *NodeClass),
                        TEXT("NOT_FOUND"));
                    return true;
                }
                if (!TaskStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
                {
                    Self->SendAutomationError(RequestingSocket, RequestId,
                        FString::Printf(TEXT("nodeClass is not FStateTreeTaskBase-derived: %s"), *NodeClass),
                        TEXT("INVALID_ARGUMENT"));
                    return true;
                }

                FStateTreeEditorNode& TaskItem = FoundState->Tasks.AddDefaulted_GetRef();
                TaskItem.ID = FGuid::NewGuid();
                TaskItem.Node.InitializeAs(TaskStruct);
                const FStateTreeNodeBase& NodeRef = TaskItem.Node.GetMutable<FStateTreeNodeBase>();
                if (const UScriptStruct* InstanceType =
                        Cast<const UScriptStruct>(NodeRef.GetInstanceDataType()))
                {
                    TaskItem.Instance.InitializeAs(InstanceType);
                }
                // Gate 3: apply Payload->"properties" onto TaskItem.Instance here.

                AddedNodeClassPath = TaskStruct->GetPathName();
                AddedNodeId        = TaskItem.ID;
                bTaskAdded         = true;

                // Gate 3: apply Payload["properties"] JSON object onto the
                // task's instance-data struct via FProperty reflection.
                // Missing / unsupported fields land in PropertyWarnings for
                // the caller to inspect; they do NOT fail the request (the
                // task is still added, just with defaults for those fields).
                const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
                if (Payload->TryGetObjectField(TEXT("properties"), PropertiesObj)
                    && PropertiesObj && (*PropertiesObj).IsValid())
                {
                    TArray<FString> Warnings;
                    int32 Applied = 0;
                    ApplyJsonPropertiesToInstance(*PropertiesObj, TaskItem.Instance,
                        Warnings, Applied);

                    Result->SetNumberField(TEXT("propertiesApplied"), Applied);
                    if (Warnings.Num() > 0)
                    {
                        TArray<TSharedPtr<FJsonValue>> WarnJson;
                        WarnJson.Reserve(Warnings.Num());
                        for (const FString& W : Warnings)
                        {
                            WarnJson.Add(MakeShared<FJsonValueString>(W));
                        }
                        Result->SetArrayField(TEXT("propertyWarnings"), WarnJson);
                    }
                }
            }
        }

        // Configure state properties from payload
        if (Payload->HasField(TEXT("selectionBehavior")))
        {
            FString Behavior = GetStringFieldAI(Payload, TEXT("selectionBehavior"));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 7
            if (Behavior.Equals(TEXT("TryEnterState"), ESearchCase::IgnoreCase))
            {
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
            }
            else if (Behavior.Equals(TEXT("TrySelectChildrenInOrder"), ESearchCase::IgnoreCase))
            {
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
            }
            else if (Behavior.Equals(TEXT("TrySelectChildrenAtRandom"), ESearchCase::IgnoreCase))
            {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;
#else
            UE_LOG(LogMcpAIHandlers, Warning, TEXT("TrySelectChildrenAtRandom requires UE 5.5+. Using TrySelectChildrenInOrder instead."));

                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
#endif
            }
            else if (Behavior.Equals(TEXT("TrySelectChildrenWithHighestUtility"), ESearchCase::IgnoreCase))
            {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility;
#else
                UE_LOG(LogMcpAIHandlers, Warning, TEXT("TrySelectChildrenWithHighestUtility requires UE 5.4+. Using TryEnterState instead."));
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
#endif
            }
            else
            {
                UE_LOG(LogMcpAIHandlers, Warning, TEXT("Unknown selection behavior: %s"), *Behavior);
            }
#else
            // UE 5.7+: SelectionBehavior API was refactored - skip setting
            (void)Behavior; // Suppress unused warning
#endif
}

        // Save
        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetNumberField(TEXT("taskCount"), FoundState->Tasks.Num());
        if (bTaskAdded)
        {
            Result->SetStringField(TEXT("addedNodeClass"), AddedNodeClassPath);
            Result->SetStringField(TEXT("addedNodeId"),    AddedNodeId.ToString());
        }
        Result->SetStringField(TEXT("message"),
            bTaskAdded ? TEXT("Task added to state") : TEXT("State task configuration updated"));
        Self->SendAutomationResponse(RequestingSocket, RequestId, true,
            bTaskAdded ? TEXT("Task added") : TEXT("Task configured"), Result);
#elif MCP_HAS_STATE_TREE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetStringField(TEXT("message"), TEXT("Task configuration registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        Self->SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Task configured"), Result);
#else
        Self->SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // 16.7 Smart Objects (4 actions)
    // =========================================================================

    return true;
}
}
#endif
