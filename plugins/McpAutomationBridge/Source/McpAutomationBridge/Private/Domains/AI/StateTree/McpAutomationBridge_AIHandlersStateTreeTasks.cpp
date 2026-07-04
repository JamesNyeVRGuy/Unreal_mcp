#include "Domains/AI/McpAutomationBridge_AIHandlerContext.h"

#if WITH_EDITOR
#include "Domains/AI/StateTree/McpAutomationBridge_AIStateTreeFeature.h"

// Explicit includes for the types Gate 2 references. The shared feature
// header pulls these in too under nested __has_include guards, but a direct
// include here removes any inclusion-order fragility for translation units
// that reach FStateTreeTaskBase / FStateTreeEditorNode.
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
#include "StateTreeTaskBase.h"
#include "StateTreeEditorNode.h"
#endif

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
