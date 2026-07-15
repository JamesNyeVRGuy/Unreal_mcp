#include "Domains/WidgetAuthoring/McpAutomationBridge_WidgetAuthoringActions.h"
#include "Domains/WidgetAuthoring/Support/McpAutomationBridge_WidgetAuthoringBlueprintLoading.h"
#include "Domains/WidgetAuthoring/Support/McpAutomationBridge_WidgetAuthoringTreeMutation.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/WrapBoxSlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Layout/Margin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Foundation/BridgeHelpers/McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"
#include "Transport/WebSocket/McpBridgeWebSocket.h"
#include "Foundation/HandlerUtils/McpHandlerUtils.h"
#include "WidgetBlueprint.h"

namespace WidgetAuthoringHandlers
{
using namespace WidgetAuthoringHelpers;

bool HandleWidgetAuthoringManipulation(
    UMcpAutomationBridgeSubsystem& Subsystem,
    const FString& RequestId,
    const FString& SubAction,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket,
    TSharedPtr<FJsonObject> ResultJson)
{
    // =========================================================================
    // 19.11 Widget Manipulation Actions
    // =========================================================================

    if (SubAction.Equals(TEXT("remove_widget"), ESearchCase::IgnoreCase))
    {
        FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        FString SlotName = GetJsonStringField(Payload, TEXT("slotName"));

        if (WidgetPath.IsEmpty() || SlotName.IsEmpty())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Missing required parameters: widgetPath, slotName"), TEXT("MISSING_PARAMETER"));
            return true;
        }

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP || !WidgetBP->WidgetTree)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }

        UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*SlotName));
        if (!TargetWidget)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, FString::Printf(TEXT("Widget '%s' not found"), *SlotName), TEXT("NOT_FOUND"));
            return true;
        }

        // TACB-929: drop the widget's (and its children's) GUID-map entries before removing it,
        // else a stale name lingers in WidgetVariableNameToGuidMap and trips the compiler's
        // "was deleted but still has a GUID" ensure (WidgetBlueprintCompiler.cpp:828).
        UnregisterWidgetAndChildren(WidgetBP, TargetWidget);
        WidgetBP->WidgetTree->RemoveWidget(TargetWidget);
        // TACB-929: reconcile the widget-variable GUID map to the live tree before the
        // compile-triggering structural-modify -- prunes stale entries left by a prior
        // rename/remove and registers any newly added/renamed widget, silencing the
        // WidgetVariableNameToGuidMap ensure (WidgetBlueprintCompiler.cpp:794/828).
        RegisterAllWidgetGuids(WidgetBP);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

        ResultJson->SetBoolField(TEXT("success"), true);
        ResultJson->SetStringField(TEXT("widgetPath"), WidgetPath);
        ResultJson->SetStringField(TEXT("removedWidget"), SlotName);

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Removed widget"), ResultJson);
        return true;
    }

    if (SubAction.Equals(TEXT("rename_widget"), ESearchCase::IgnoreCase))
    {
        FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        FString OldName = GetJsonStringField(Payload, TEXT("slotName"));
        FString NewName = GetJsonStringField(Payload, TEXT("newName"));

        if (WidgetPath.IsEmpty() || OldName.IsEmpty() || NewName.IsEmpty())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Missing required parameters: widgetPath, slotName, newName"), TEXT("MISSING_PARAMETER"));
            return true;
        }

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP || !WidgetBP->WidgetTree)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }

        UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*OldName));
        if (!TargetWidget)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, FString::Printf(TEXT("Widget '%s' not found"), *OldName), TEXT("NOT_FOUND"));
            return true;
        }

        // Rename requires FBlueprintEditorUtils for proper undo/redo support
        TargetWidget->Rename(*NewName);
        // TACB-929: reconcile the widget-variable GUID map to the live tree before the
        // compile-triggering structural-modify -- prunes stale entries left by a prior
        // rename/remove and registers any newly added/renamed widget, silencing the
        // WidgetVariableNameToGuidMap ensure (WidgetBlueprintCompiler.cpp:794/828).
        RegisterAllWidgetGuids(WidgetBP);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

        ResultJson->SetBoolField(TEXT("success"), true);
        ResultJson->SetStringField(TEXT("widgetPath"), WidgetPath);
        ResultJson->SetStringField(TEXT("oldName"), OldName);
        ResultJson->SetStringField(TEXT("newName"), NewName);

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Renamed widget"), ResultJson);
        return true;
    }

    if (SubAction.Equals(TEXT("reparent_widget"), ESearchCase::IgnoreCase))
    {
        FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        FString SlotName = GetJsonStringField(Payload, TEXT("slotName"));
        FString NewParent = GetJsonStringField(Payload, TEXT("newParent"));

        if (WidgetPath.IsEmpty() || SlotName.IsEmpty() || NewParent.IsEmpty())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Missing required parameters: widgetPath, slotName, newParent"), TEXT("MISSING_PARAMETER"));
            return true;
        }

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP || !WidgetBP->WidgetTree)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }

        UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*SlotName));
        if (!TargetWidget)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, FString::Printf(TEXT("Widget '%s' not found"), *SlotName), TEXT("NOT_FOUND"));
            return true;
        }

        UPanelWidget* NewParentWidget = Cast<UPanelWidget>(WidgetBP->WidgetTree->FindWidget(FName(*NewParent)));
        if (!NewParentWidget)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, FString::Printf(TEXT("New parent '%s' not found or not a panel"), *NewParent), TEXT("NOT_FOUND"));
            return true;
        }

        // Remove from current parent and add to new parent
        if (UPanelWidget* OldParent = TargetWidget->GetParent())
        {
            OldParent->RemoveChild(TargetWidget);
        }
        NewParentWidget->AddChild(TargetWidget);

        // TACB-929: reconcile the widget-variable GUID map to the live tree before the
        // compile-triggering structural-modify -- prunes stale entries left by a prior
        // rename/remove and registers any newly added/renamed widget, silencing the
        // WidgetVariableNameToGuidMap ensure (WidgetBlueprintCompiler.cpp:794/828).
        RegisterAllWidgetGuids(WidgetBP);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

        ResultJson->SetBoolField(TEXT("success"), true);
        ResultJson->SetStringField(TEXT("widgetPath"), WidgetPath);
        ResultJson->SetStringField(TEXT("widget"), SlotName);
        ResultJson->SetStringField(TEXT("newParent"), NewParent);

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Reparented widget"), ResultJson);
        return true;
    }

    if (SubAction.Equals(TEXT("get_widget_slot_info"), ESearchCase::IgnoreCase))
    {
        FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        FString SlotName = GetJsonStringField(Payload, TEXT("slotName"));

        if (WidgetPath.IsEmpty() || SlotName.IsEmpty())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Missing required parameters: widgetPath, slotName"), TEXT("MISSING_PARAMETER"));
            return true;
        }

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP || !WidgetBP->WidgetTree)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }

        UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*SlotName));
        if (!TargetWidget)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, FString::Printf(TEXT("Widget '%s' not found"), *SlotName), TEXT("NOT_FOUND"));
            return true;
        }

        ResultJson->SetBoolField(TEXT("success"), true);
        ResultJson->SetStringField(TEXT("widgetPath"), WidgetPath);
        ResultJson->SetStringField(TEXT("slotName"), SlotName);
        ResultJson->SetStringField(TEXT("widgetClass"), TargetWidget->GetClass()->GetName());
        ResultJson->SetBoolField(TEXT("isVisible"), TargetWidget->IsVisible());

        if (UPanelSlot* Slot = TargetWidget->Slot)
        {
            ResultJson->SetStringField(TEXT("slotClass"), Slot->GetClass()->GetName());

            if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
            {
                TSharedPtr<FJsonObject> SlotInfo = McpHandlerUtils::CreateResultObject();
                FAnchors Anchors = CanvasSlot->GetAnchors();
                SlotInfo->SetNumberField(TEXT("anchorMinX"), Anchors.Minimum.X);
                SlotInfo->SetNumberField(TEXT("anchorMinY"), Anchors.Minimum.Y);
                SlotInfo->SetNumberField(TEXT("anchorMaxX"), Anchors.Maximum.X);
                SlotInfo->SetNumberField(TEXT("anchorMaxY"), Anchors.Maximum.Y);
                FVector2D Alignment = CanvasSlot->GetAlignment();
                SlotInfo->SetNumberField(TEXT("alignmentX"), Alignment.X);
                SlotInfo->SetNumberField(TEXT("alignmentY"), Alignment.Y);
                FVector2D Position = CanvasSlot->GetPosition();
                SlotInfo->SetNumberField(TEXT("positionX"), Position.X);
                SlotInfo->SetNumberField(TEXT("positionY"), Position.Y);
                FVector2D Size = CanvasSlot->GetSize();
                SlotInfo->SetNumberField(TEXT("sizeX"), Size.X);
                SlotInfo->SetNumberField(TEXT("sizeY"), Size.Y);
                SlotInfo->SetNumberField(TEXT("zOrder"), CanvasSlot->GetZOrder());
                ResultJson->SetObjectField(TEXT("canvasSlotInfo"), SlotInfo);
            }
        }

        if (UPanelWidget* Parent = TargetWidget->GetParent())
        {
            ResultJson->SetStringField(TEXT("parentName"), Parent->GetName());
            ResultJson->SetStringField(TEXT("parentClass"), Parent->GetClass()->GetName());
        }

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Retrieved widget slot info"), ResultJson);
        return true;
    }

    // set_slot -- generic slot-property setter for widgets living in box /
    // overlay / grid panels. The granular set_position / set_anchor / set_size
    // actions only cover Canvas panels; without set_slot, box/overlay/grid
    // slots have no scriptable path to their alignment, size rule or padding.
    // Ported from feat/custom-tooling@d9fea10 WidgetAuthoringHandlers.cpp:3184.
    // Adapted for the split file layout: uses SlotName-string lookup like the
    // rest of Manipulation.cpp instead of the old FindWidgetFromPayload helper.
    if (SubAction.Equals(TEXT("set_slot"), ESearchCase::IgnoreCase))
    {
        const FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        FString SlotName = GetJsonStringField(Payload, TEXT("slotName"));
        if (SlotName.IsEmpty())
        {
            // Accept widgetName as an alias for convenience.
            SlotName = GetJsonStringField(Payload, TEXT("widgetName"));
        }
        if (WidgetPath.IsEmpty() || SlotName.IsEmpty())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Missing required parameters: widgetPath, slotName"),
                TEXT("MISSING_PARAMETER"));
            return true;
        }

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP || !WidgetBP->WidgetTree)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }
        UWidget* Widget = WidgetBP->WidgetTree->FindWidget(FName(*SlotName));
        if (!Widget)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Widget '%s' not found"), *SlotName),
                TEXT("WIDGET_NOT_FOUND"));
            return true;
        }

        auto ParseHAlign = [](const FString& Str) -> TOptional<EHorizontalAlignment>
        {
            if (Str.Equals(TEXT("Fill"),   ESearchCase::IgnoreCase)) return EHorizontalAlignment::HAlign_Fill;
            if (Str.Equals(TEXT("Left"),   ESearchCase::IgnoreCase)) return EHorizontalAlignment::HAlign_Left;
            if (Str.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) return EHorizontalAlignment::HAlign_Center;
            if (Str.Equals(TEXT("Right"),  ESearchCase::IgnoreCase)) return EHorizontalAlignment::HAlign_Right;
            return {};
        };
        auto ParseVAlign = [](const FString& Str) -> TOptional<EVerticalAlignment>
        {
            if (Str.Equals(TEXT("Fill"),   ESearchCase::IgnoreCase)) return EVerticalAlignment::VAlign_Fill;
            if (Str.Equals(TEXT("Top"),    ESearchCase::IgnoreCase)) return EVerticalAlignment::VAlign_Top;
            if (Str.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) return EVerticalAlignment::VAlign_Center;
            if (Str.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase)) return EVerticalAlignment::VAlign_Bottom;
            return {};
        };

        const FString SizeRuleStr = GetJsonStringField(Payload, TEXT("sizeRule"));
        const double  FillWeight  = GetJsonNumberField(Payload, TEXT("fillWeight"), -1.0);
        const FString HAlignStr   = GetJsonStringField(Payload, TEXT("horizontalAlignment"));
        const FString VAlignStr   = GetJsonStringField(Payload, TEXT("verticalAlignment"));

        FMargin PaddingMargin(0);
        bool bHasPadding = false;
        if (Payload->HasField(TEXT("padding")))
        {
            const double Uniform = GetJsonNumberField(Payload, TEXT("padding"), 0.0);
            PaddingMargin = FMargin(Uniform);
            bHasPadding = true;
        }
        if (Payload->HasField(TEXT("left"))  || Payload->HasField(TEXT("top")) ||
            Payload->HasField(TEXT("right")) || Payload->HasField(TEXT("bottom")))
        {
            PaddingMargin.Left   = GetJsonNumberField(Payload, TEXT("left"),   PaddingMargin.Left);
            PaddingMargin.Top    = GetJsonNumberField(Payload, TEXT("top"),    PaddingMargin.Top);
            PaddingMargin.Right  = GetJsonNumberField(Payload, TEXT("right"),  PaddingMargin.Right);
            PaddingMargin.Bottom = GetJsonNumberField(Payload, TEXT("bottom"), PaddingMargin.Bottom);
            bHasPadding = true;
        }

        FString SlotType = TEXT("unknown");
        bool bApplied = false;

        auto ApplyBoxSize = [&](auto* BoxSlot)
        {
            if (!SizeRuleStr.IsEmpty())
            {
                FSlateChildSize ChildSize;
                if (SizeRuleStr.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
                {
                    ChildSize.SizeRule = ESlateSizeRule::Fill;
                    ChildSize.Value    = (FillWeight > 0.0) ? static_cast<float>(FillWeight) : 1.0f;
                }
                else
                {
                    ChildSize.SizeRule = ESlateSizeRule::Automatic;
                    ChildSize.Value    = 1.0f;
                }
                BoxSlot->SetSize(ChildSize);
                bApplied = true;
            }
            else if (FillWeight > 0.0)
            {
                FSlateChildSize ChildSize;
                ChildSize.SizeRule = ESlateSizeRule::Fill;
                ChildSize.Value    = static_cast<float>(FillWeight);
                BoxSlot->SetSize(ChildSize);
                bApplied = true;
            }
        };

        if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
        {
            SlotType = TEXT("HorizontalBoxSlot");
            ApplyBoxSize(HBoxSlot);
            if (auto H = ParseHAlign(HAlignStr)) { HBoxSlot->SetHorizontalAlignment(H.GetValue()); bApplied = true; }
            if (auto V = ParseVAlign(VAlignStr)) { HBoxSlot->SetVerticalAlignment(V.GetValue());   bApplied = true; }
            if (bHasPadding) { HBoxSlot->SetPadding(PaddingMargin); bApplied = true; }
        }
        else if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
        {
            SlotType = TEXT("VerticalBoxSlot");
            ApplyBoxSize(VBoxSlot);
            if (auto H = ParseHAlign(HAlignStr)) { VBoxSlot->SetHorizontalAlignment(H.GetValue()); bApplied = true; }
            if (auto V = ParseVAlign(VAlignStr)) { VBoxSlot->SetVerticalAlignment(V.GetValue());   bApplied = true; }
            if (bHasPadding) { VBoxSlot->SetPadding(PaddingMargin); bApplied = true; }
        }
        else if (UOverlaySlot* OvSlot = Cast<UOverlaySlot>(Widget->Slot))
        {
            SlotType = TEXT("OverlaySlot");
            if (auto H = ParseHAlign(HAlignStr)) { OvSlot->SetHorizontalAlignment(H.GetValue()); bApplied = true; }
            if (auto V = ParseVAlign(VAlignStr)) { OvSlot->SetVerticalAlignment(V.GetValue());   bApplied = true; }
            if (bHasPadding) { OvSlot->SetPadding(PaddingMargin); bApplied = true; }
        }
        else if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Widget->Slot))
        {
            SlotType = TEXT("BorderSlot");
            if (auto H = ParseHAlign(HAlignStr)) { BorderSlot->SetHorizontalAlignment(H.GetValue()); bApplied = true; }
            if (auto V = ParseVAlign(VAlignStr)) { BorderSlot->SetVerticalAlignment(V.GetValue());   bApplied = true; }
            if (bHasPadding) { BorderSlot->SetPadding(PaddingMargin); bApplied = true; }
        }
        else if (UGridSlot* GridSlot = Cast<UGridSlot>(Widget->Slot))
        {
            // TACB-947: place a child in a Grid Panel (inventory/crafting grids) by row/column + span.
            SlotType = TEXT("GridSlot");
            if (Payload->HasField(TEXT("row")))        { GridSlot->SetRow(static_cast<int32>(GetJsonNumberField(Payload, TEXT("row"), 0)));               bApplied = true; }
            if (Payload->HasField(TEXT("column")))     { GridSlot->SetColumn(static_cast<int32>(GetJsonNumberField(Payload, TEXT("column"), 0)));         bApplied = true; }
            if (Payload->HasField(TEXT("rowSpan")))    { GridSlot->SetRowSpan(static_cast<int32>(GetJsonNumberField(Payload, TEXT("rowSpan"), 1)));       bApplied = true; }
            if (Payload->HasField(TEXT("columnSpan"))) { GridSlot->SetColumnSpan(static_cast<int32>(GetJsonNumberField(Payload, TEXT("columnSpan"), 1))); bApplied = true; }
            if (Payload->HasField(TEXT("layer")))      { GridSlot->SetLayer(static_cast<int32>(GetJsonNumberField(Payload, TEXT("layer"), 0)));           bApplied = true; }
            if (auto H = ParseHAlign(HAlignStr)) { GridSlot->SetHorizontalAlignment(H.GetValue()); bApplied = true; }
            if (auto V = ParseVAlign(VAlignStr)) { GridSlot->SetVerticalAlignment(V.GetValue());   bApplied = true; }
            if (bHasPadding) { GridSlot->SetPadding(PaddingMargin); bApplied = true; }
        }
        else if (UUniformGridSlot* UniSlot = Cast<UUniformGridSlot>(Widget->Slot))
        {
            SlotType = TEXT("UniformGridSlot");
            if (Payload->HasField(TEXT("row")))    { UniSlot->SetRow(static_cast<int32>(GetJsonNumberField(Payload, TEXT("row"), 0)));       bApplied = true; }
            if (Payload->HasField(TEXT("column"))) { UniSlot->SetColumn(static_cast<int32>(GetJsonNumberField(Payload, TEXT("column"), 0))); bApplied = true; }
            if (auto H = ParseHAlign(HAlignStr)) { UniSlot->SetHorizontalAlignment(H.GetValue()); bApplied = true; }
            if (auto V = ParseVAlign(VAlignStr)) { UniSlot->SetVerticalAlignment(V.GetValue());   bApplied = true; }
        }
        else if (UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(Widget->Slot))
        {
            SlotType = TEXT("WrapBoxSlot");
            if (auto H = ParseHAlign(HAlignStr)) { WrapSlot->SetHorizontalAlignment(H.GetValue()); bApplied = true; }
            if (auto V = ParseVAlign(VAlignStr)) { WrapSlot->SetVerticalAlignment(V.GetValue());   bApplied = true; }
            if (bHasPadding) { WrapSlot->SetPadding(PaddingMargin); bApplied = true; }
            if (Payload->HasField(TEXT("fillEmptySpace")))       { WrapSlot->SetFillEmptySpace(Payload->GetBoolField(TEXT("fillEmptySpace")));                 bApplied = true; }
            if (Payload->HasField(TEXT("fillSpanWhenLessThan"))) { WrapSlot->SetFillSpanWhenLessThan(GetJsonNumberField(Payload, TEXT("fillSpanWhenLessThan"), 0.0)); bApplied = true; }
        }
        else if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(Widget->Slot))
        {
            SlotType = TEXT("ScrollBoxSlot");
            if (auto H = ParseHAlign(HAlignStr)) { ScrollSlot->SetHorizontalAlignment(H.GetValue()); bApplied = true; }
            if (auto V = ParseVAlign(VAlignStr)) { ScrollSlot->SetVerticalAlignment(V.GetValue());   bApplied = true; }
            if (bHasPadding) { ScrollSlot->SetPadding(PaddingMargin); bApplied = true; }
        }
        else if (Cast<UCanvasPanelSlot>(Widget->Slot))
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Widget is in a CanvasPanel -- use set_anchor, set_position, set_size instead"),
                TEXT("WRONG_SLOT_TYPE"));
            return true;
        }

        if (!bApplied)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("No properties applied. Slot type: %s"), *SlotType),
                TEXT("NO_CHANGES"));
            return true;
        }

        // TACB-929: reconcile the widget-variable GUID map to the live tree before the
        // compile-triggering structural-modify -- prunes stale entries left by a prior
        // rename/remove and registers any newly added/renamed widget, silencing the
        // WidgetVariableNameToGuidMap ensure (WidgetBlueprintCompiler.cpp:794/828).
        RegisterAllWidgetGuids(WidgetBP);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

        ResultJson->SetBoolField(TEXT("success"),   true);
        ResultJson->SetStringField(TEXT("slotType"), SlotType);
        ResultJson->SetStringField(TEXT("message"),
            FString::Printf(TEXT("Slot properties set (%s)"), *SlotType));

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Slot set"), ResultJson);
        return true;
    }

    // set_child_index -- TACB-928: reorder an existing child within its box/overlay/grid parent.
    // The add_* actions only ever append, so a header added after a populated column lands at the
    // bottom; without a reorder the audit loop cannot put section headers at the top. ShiftChild
    // reorders in place and preserves the existing slot (unlike Remove+InsertChildAt).
    if (SubAction.Equals(TEXT("set_child_index"), ESearchCase::IgnoreCase))
    {
        const FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        FString SlotName = GetJsonStringField(Payload, TEXT("slotName"));
        if (SlotName.IsEmpty())
        {
            SlotName = GetJsonStringField(Payload, TEXT("widgetName"));
        }
        if (WidgetPath.IsEmpty() || SlotName.IsEmpty() || !Payload->HasField(TEXT("index")))
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Missing required parameters: widgetPath, slotName, index"),
                TEXT("MISSING_PARAMETER"));
            return true;
        }
        const int32 TargetIndex = static_cast<int32>(GetJsonNumberField(Payload, TEXT("index"), 0.0));

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP || !WidgetBP->WidgetTree)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }
        UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*SlotName));
        if (!TargetWidget)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Widget '%s' not found"), *SlotName), TEXT("NOT_FOUND"));
            return true;
        }
        UPanelWidget* Parent = TargetWidget->GetParent();
        if (!Parent)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Widget '%s' has no panel parent to reorder within"), *SlotName),
                TEXT("NO_PARENT"));
            return true;
        }
        const int32 ChildCount = Parent->GetChildrenCount();
        const int32 ClampedIndex = FMath::Clamp(TargetIndex, 0, FMath::Max(0, ChildCount - 1));
        Parent->ShiftChild(ClampedIndex, TargetWidget);
        // TACB-929: reconcile the widget-variable GUID map to the live tree before the
        // compile-triggering structural-modify -- prunes stale entries left by a prior
        // rename/remove and registers any newly added/renamed widget, silencing the
        // WidgetVariableNameToGuidMap ensure (WidgetBlueprintCompiler.cpp:794/828).
        RegisterAllWidgetGuids(WidgetBP);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

        ResultJson->SetBoolField(TEXT("success"), true);
        ResultJson->SetStringField(TEXT("widgetPath"), WidgetPath);
        ResultJson->SetStringField(TEXT("slotName"), SlotName);
        ResultJson->SetStringField(TEXT("parentName"), Parent->GetName());
        ResultJson->SetNumberField(TEXT("index"), ClampedIndex);
        ResultJson->SetStringField(TEXT("message"),
            FString::Printf(TEXT("Moved '%s' to index %d in '%s'"), *SlotName, ClampedIndex, *Parent->GetName()));

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Child reordered"), ResultJson);
        return true;
    }

    return false;
}
}
