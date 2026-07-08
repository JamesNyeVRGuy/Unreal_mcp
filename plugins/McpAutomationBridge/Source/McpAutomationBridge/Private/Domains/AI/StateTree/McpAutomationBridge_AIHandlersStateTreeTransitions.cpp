#include "Domains/AI/McpAutomationBridge_AIHandlerContext.h"

#if WITH_EDITOR
#include "Domains/AI/StateTree/McpAutomationBridge_AIStateTreeFeature.h"

namespace McpAIHandlers
{
bool HandleAddStateTreeTransition(UMcpAutomationBridgeSubsystem* Self, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
    const FString SubAction = TEXT("add_state_tree_transition");
    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    if (SubAction == TEXT("add_state_tree_transition"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString FromState = GetStringFieldAI(Payload, TEXT("fromState"));
        FString ToState = GetStringFieldAI(Payload, TEXT("toState"));
        FString TriggerType = GetStringFieldAI(Payload, TEXT("triggerType"), TEXT("OnStateCompleted"));

        if (StateTreePath.IsEmpty() || FromState.IsEmpty() || ToState.IsEmpty())
        {
            Self->SendAutomationError(RequestingSocket, RequestId, TEXT("stateTreePath, fromState, and toState are required"), TEXT("INVALID_PARAMS"));
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

        // Find source and target states
        UStateTreeState* SourceState = nullptr;
        UStateTreeState* TargetState = nullptr;

        // Helper lambda to find state recursively
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

        // Terminal targets: "Succeeded" / "Failed" / "NextState" complete the
        // TREE (EStateTreeTransitionType) rather than jumping to a named state.
        // Without these, action-style trees cannot get the terminal completion
        // transition their orchestrators rely on (TACB-780).
        EStateTreeTransitionType TransitionType = EStateTreeTransitionType::GotoState;
        if (ToState.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
        {
            TransitionType = EStateTreeTransitionType::Succeeded;
        }
        else if (ToState.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
        {
            TransitionType = EStateTreeTransitionType::Failed;
        }
        else if (ToState.Equals(TEXT("NextState"), ESearchCase::IgnoreCase))
        {
            TransitionType = EStateTreeTransitionType::NextState;
        }
        const bool bTerminalTarget = TransitionType != EStateTreeTransitionType::GotoState;

        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            if (!SourceState) SourceState = FindState(SubTree, FromState);
            if (!bTerminalTarget && !TargetState) TargetState = FindState(SubTree, ToState);
        }

        if (!SourceState)
        {
            Self->SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Source state '%s' not found"), *FromState), TEXT("NOT_FOUND"));
            return true;
        }

        if (!bTerminalTarget && !TargetState)
        {
            Self->SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Target state '%s' not found"), *ToState), TEXT("NOT_FOUND"));
            return true;
        }

        // Determine trigger type
        EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
        if (TriggerType.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase))
        {
            Trigger = EStateTreeTransitionTrigger::OnStateFailed;
        }
        else if (TriggerType.Equals(TEXT("OnTick"), ESearchCase::IgnoreCase))
        {
            Trigger = EStateTreeTransitionTrigger::OnTick;
        }
        else if (TriggerType.Equals(TEXT("OnEvent"), ESearchCase::IgnoreCase))
        {
            Trigger = EStateTreeTransitionTrigger::OnEvent;
        }

        // Add transition (terminal targets pass no state -- the type carries it)
        FStateTreeTransition& Transition = SourceState->AddTransition(Trigger, TransitionType, bTerminalTarget ? nullptr : TargetState);

        // Save
        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("fromState"), FromState);
        Result->SetStringField(TEXT("toState"), ToState);
        Result->SetStringField(TEXT("triggerType"), TriggerType);
        Result->SetStringField(TEXT("transitionId"), Transition.ID.ToString());
        Result->SetStringField(TEXT("message"), TEXT("Transition added"));
        Self->SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Transition added"), Result);
#elif MCP_HAS_STATE_TREE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString FromState = GetStringFieldAI(Payload, TEXT("fromState"));
        FString ToState = GetStringFieldAI(Payload, TEXT("toState"));
        Result->SetStringField(TEXT("fromState"), FromState);
        Result->SetStringField(TEXT("toState"), ToState);
        Result->SetStringField(TEXT("message"), TEXT("Transition registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        Self->SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Transition registered"), Result);
#else
        Self->SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    return true;
}
}
#endif
