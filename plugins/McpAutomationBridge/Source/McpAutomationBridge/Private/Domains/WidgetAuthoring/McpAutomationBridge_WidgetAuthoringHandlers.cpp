#include "Domains/WidgetAuthoring/McpAutomationBridge_WidgetAuthoringActions.h"

#include "Dom/JsonObject.h"
#include "Foundation/BridgeHelpers/McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"
#include "Transport/WebSocket/McpBridgeWebSocket.h"
#include "Foundation/HandlerUtils/McpHandlerUtils.h"

bool UMcpAutomationBridgeSubsystem::HandleManageWidgetAuthoringAction(
    const FString& RequestId,
    const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
    if (Action != TEXT("manage_widget_authoring"))
    {
        return false;
    }

    FString SubAction = GetJsonStringField(Payload, TEXT("subAction"));
    if (SubAction.IsEmpty())
    {
        SubAction = GetJsonStringField(Payload, TEXT("action"));
    }

    // The add_* component handlers read the NEW widget's name from "slotName"
    // (defaulting to the widget type, e.g. "TextBlock"). The tool schema also
    // advertises "componentName"/"name", so accept those as aliases when
    // "slotName" is absent -- otherwise callers using the documented params get
    // a default-named widget that a C++ BindWidget can never resolve. Creation
    // subactions read "name" directly for the Blueprint name and ignore
    // "slotName", so this aliasing is safe for them.
    if (GetJsonStringField(Payload, TEXT("slotName")).IsEmpty())
    {
        FString NameAlias = GetJsonStringField(Payload, TEXT("componentName"));
        if (NameAlias.IsEmpty())
        {
            NameAlias = GetJsonStringField(Payload, TEXT("name"));
        }
        if (!NameAlias.IsEmpty())
        {
            Payload->SetStringField(TEXT("slotName"), NameAlias);
        }
    }

    TSharedPtr<FJsonObject> ResultJson = McpHandlerUtils::CreateResultObject();
    using namespace WidgetAuthoringHandlers;
    static constexpr FWidgetAuthoringActionHandler Handlers[] = {
        HandleWidgetAuthoringCreation,
        HandleWidgetAuthoringPanelBasics,
        HandleWidgetAuthoringBasicVisuals,
        HandleWidgetAuthoringValueWidgets,
        HandleWidgetAuthoringInfo,
        HandleWidgetAuthoringGridPanels,
        HandleWidgetAuthoringScrollScalePanels,
        HandleWidgetAuthoringBorderPanel,
        HandleWidgetAuthoringInputWidgets,
        HandleWidgetAuthoringCollectionWidgets,
        HandleWidgetAuthoringCanvasSlotGeometry,
        HandleWidgetAuthoringSlotAppearance,
        HandleWidgetAuthoringStyleClipping,
        HandleWidgetAuthoringPropertyBindings,
        HandleWidgetAuthoringEventBindings,
        HandleWidgetAuthoringAnimationCore,
        HandleWidgetAuthoringMenuTemplates,
        HandleWidgetAuthoringHudElements,
        HandleWidgetAuthoringPreview,
        HandleWidgetAuthoringGenericComponent,
        HandleWidgetAuthoringUnifiedBinding,
        HandleWidgetAuthoringStyleVariables,
        HandleWidgetAuthoringSettingsTemplate,
        HandleWidgetAuthoringLoadingMinimapTemplates,
        HandleWidgetAuthoringObjectiveDamageTemplates,
        HandleWidgetAuthoringInventoryTemplate,
        HandleWidgetAuthoringDialogRadialTemplates,
        HandleWidgetAuthoringManipulation,
        HandleWidgetAuthoringAdditionalPanels,
        HandleWidgetAuthoringAdvancedStyling,
        HandleWidgetAuthoringAnimationQueries,
        HandleWidgetAuthoringLocalization,
        HandleWidgetAuthoringCreditsTemplate,
        HandleWidgetAuthoringShopTemplate,
        HandleWidgetAuthoringQuestTemplate
    };

    for (FWidgetAuthoringActionHandler Handler : Handlers)
    {
        if (Handler(*this, RequestId, SubAction, Payload, RequestingSocket, ResultJson))
        {
            return true;
        }
    }

    return false;
}
