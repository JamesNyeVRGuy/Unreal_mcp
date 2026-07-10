#include "Domains/WidgetAuthoring/McpAutomationBridge_WidgetAuthoringActions.h"
#include "Domains/WidgetAuthoring/Support/McpAutomationBridge_WidgetAuthoringBlueprintLoading.h"
#include "Domains/WidgetAuthoring/McpAutomationBridge_WidgetAuthoringPayload.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "JsonObjectConverter.h"
#include "Foundation/BridgeHelpers/McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"
#include "Transport/WebSocket/McpBridgeWebSocket.h"
#include "Core/Compatibility/McpVersionCompatibility.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace WidgetAuthoringHandlers
{
using namespace WidgetAuthoringHelpers;

bool HandleWidgetAuthoringStyleClipping(
    UMcpAutomationBridgeSubsystem& Subsystem,
    const FString& RequestId,
    const FString& SubAction,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket,
    TSharedPtr<FJsonObject> ResultJson)
{
    if (SubAction.Equals(TEXT("set_style"), ESearchCase::IgnoreCase) ||
        SubAction.Equals(TEXT("set_clipping"), ESearchCase::IgnoreCase))
    {
        FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        FString SlotName = GetSlotName(Payload);

        if (WidgetPath.IsEmpty() || SlotName.IsEmpty())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Missing required parameters: widgetPath and slotName"), TEXT("MISSING_PARAMETER"));
            return true;
        }

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }

        UWidget* Widget = WidgetBP->WidgetTree->FindWidget(FName(*SlotName));
        if (!Widget)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Widget not found"), TEXT("WIDGET_NOT_FOUND"));
            return true;
        }

        if (SubAction.Equals(TEXT("set_clipping"), ESearchCase::IgnoreCase))
        {
            FString ClippingStr = GetJsonStringField(Payload, TEXT("clipping"), TEXT("Inherit"));
            EWidgetClipping Clipping = EWidgetClipping::Inherit;
            if (ClippingStr.Equals(TEXT("ClipToBounds"), ESearchCase::IgnoreCase))
            {
                Clipping = EWidgetClipping::ClipToBounds;
            }
            else if (ClippingStr.Equals(TEXT("ClipToBoundsWithoutIntersecting"), ESearchCase::IgnoreCase))
            {
                Clipping = EWidgetClipping::ClipToBoundsWithoutIntersecting;
            }
            else if (ClippingStr.Equals(TEXT("ClipToBoundsAlways"), ESearchCase::IgnoreCase))
            {
                Clipping = EWidgetClipping::ClipToBoundsAlways;
            }
            else if (ClippingStr.Equals(TEXT("OnDemand"), ESearchCase::IgnoreCase))
            {
                Clipping = EWidgetClipping::OnDemand;
            }
            Widget->SetClipping(Clipping);
            WidgetBP->MarkPackageDirty();
            const bool bSaveSucceeded = McpSafeAssetSave(WidgetBP);

            ResultJson->SetStringField(TEXT("mode"), TEXT("write"));
            ResultJson->SetStringField(TEXT("propertyName"), TEXT("Clipping"));
            ResultJson->SetStringField(TEXT("value"), ClippingStr);
            ResultJson->SetStringField(TEXT("widgetName"), SlotName);
            ResultJson->SetStringField(TEXT("widgetClass"), Widget->GetClass()->GetName());
            ResultJson->SetBoolField(TEXT("saveSucceeded"), bSaveSucceeded);
            if (!bSaveSucceeded)
            {
                ResultJson->SetStringField(TEXT("warning"), TEXT("Clipping changed in editor memory, but package save did not complete in the current headless session."));
            }
        }
        else if (SubAction.Equals(TEXT("set_style"), ESearchCase::IgnoreCase))
        {
            // TACB-727 / TACB-662: convenience styling layer. The generic setter
            // below sets a named property via reflection (and defaults to a
            // "Style" struct property, which only Button-family widgets have).
            // TextBlock/Border/Image expose font/brush families instead, so map
            // the documented high-level params to typed setters here. Anything
            // not handled falls through to the generic propertyName/value path.
            auto ColorParam = [&](const TCHAR* Key) -> FLinearColor
            {
                const TSharedPtr<FJsonObject>* Obj = nullptr;
                if (Payload->TryGetObjectField(Key, Obj) && Obj)
                {
                    return GetColorFromJsonWidget(*Obj);
                }
                return FLinearColor::White;
            };
            auto Vec2Param = [&](const TCHAR* Key, FVector2D Def) -> FVector2D
            {
                const TSharedPtr<FJsonObject>* Obj = nullptr;
                if (Payload->TryGetObjectField(Key, Obj) && Obj)
                {
                    return FVector2D((*Obj)->HasField(TEXT("x")) ? (*Obj)->GetNumberField(TEXT("x")) : Def.X,
                                     (*Obj)->HasField(TEXT("y")) ? (*Obj)->GetNumberField(TEXT("y")) : Def.Y);
                }
                return Def;
            };
            auto BrushDrawAs = [&](const FString& D, ESlateBrushDrawType::Type Cur) -> ESlateBrushDrawType::Type
            {
                if (D.Equals(TEXT("RoundedBox"), ESearchCase::IgnoreCase)) return ESlateBrushDrawType::RoundedBox;
                if (D.Equals(TEXT("Box"), ESearchCase::IgnoreCase))        return ESlateBrushDrawType::Box;
                if (D.Equals(TEXT("Border"), ESearchCase::IgnoreCase))     return ESlateBrushDrawType::Border;
                if (D.Equals(TEXT("Image"), ESearchCase::IgnoreCase))      return ESlateBrushDrawType::Image;
                if (D.Equals(TEXT("NoDrawType"), ESearchCase::IgnoreCase)) return ESlateBrushDrawType::NoDrawType;
                return Cur;
            };

            TArray<FString> Applied;

            if (UTextBlock* AsText = Cast<UTextBlock>(Widget))
            {
                if (Payload->HasField(TEXT("text")))
                {
                    AsText->SetText(FText::FromString(GetJsonStringField(Payload, TEXT("text"))));
                    Applied.Add(TEXT("text"));
                }
                if (Payload->HasField(TEXT("fontSize")))
                {
                    FSlateFontInfo Font = AsText->GetFont();
                    Font.Size = FMath::RoundToInt(Payload->GetNumberField(TEXT("fontSize")));
                    AsText->SetFont(Font);
                    Applied.Add(TEXT("fontSize"));
                }
                if (Payload->HasField(TEXT("colorAndOpacity")))
                {
                    AsText->SetColorAndOpacity(FSlateColor(ColorParam(TEXT("colorAndOpacity"))));
                    Applied.Add(TEXT("colorAndOpacity"));
                }
                if (Payload->HasField(TEXT("justification")))
                {
                    const FString J = GetJsonStringField(Payload, TEXT("justification"));
                    ETextJustify::Type JT = ETextJustify::Left;
                    if (J.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) JT = ETextJustify::Center;
                    else if (J.Equals(TEXT("Right"), ESearchCase::IgnoreCase)) JT = ETextJustify::Right;
                    AsText->SetJustification(JT);
                    Applied.Add(TEXT("justification"));
                }
                if (Payload->HasField(TEXT("autoWrap")))
                {
                    AsText->SetAutoWrapText(Payload->GetBoolField(TEXT("autoWrap")));
                    Applied.Add(TEXT("autoWrap"));
                }
                if (Payload->HasField(TEXT("shadowColorAndOpacity")))
                {
                    AsText->SetShadowColorAndOpacity(ColorParam(TEXT("shadowColorAndOpacity")));
                    Applied.Add(TEXT("shadowColorAndOpacity"));
                }
                if (Payload->HasField(TEXT("shadowOffset")))
                {
                    AsText->SetShadowOffset(Vec2Param(TEXT("shadowOffset"), FVector2D::ZeroVector));
                    Applied.Add(TEXT("shadowOffset"));
                }
            }
            else if (UBorder* AsBorder = Cast<UBorder>(Widget))
            {
                if (Payload->HasField(TEXT("brushColor")))
                {
                    AsBorder->SetBrushColor(ColorParam(TEXT("brushColor")));
                    Applied.Add(TEXT("brushColor"));
                }
                if (Payload->HasField(TEXT("contentColorAndOpacity")))
                {
                    AsBorder->SetContentColorAndOpacity(ColorParam(TEXT("contentColorAndOpacity")));
                    Applied.Add(TEXT("contentColorAndOpacity"));
                }
                if (Payload->HasField(TEXT("padding")))
                {
                    const TSharedPtr<FJsonObject>* PadObj = nullptr;
                    if (Payload->TryGetObjectField(TEXT("padding"), PadObj) && PadObj)
                    {
                        const FMargin Pad(
                            (*PadObj)->HasField(TEXT("left"))   ? (*PadObj)->GetNumberField(TEXT("left"))   : 0.0,
                            (*PadObj)->HasField(TEXT("top"))    ? (*PadObj)->GetNumberField(TEXT("top"))    : 0.0,
                            (*PadObj)->HasField(TEXT("right"))  ? (*PadObj)->GetNumberField(TEXT("right"))  : 0.0,
                            (*PadObj)->HasField(TEXT("bottom")) ? (*PadObj)->GetNumberField(TEXT("bottom")) : 0.0);
                        AsBorder->SetPadding(Pad);
                        Applied.Add(TEXT("padding"));
                    }
                }
                // Background FSlateBrush: the rounded-box "card" look.
                FSlateBrush Brush = AsBorder->Background;
                bool bBrushChanged = false;
                if (Payload->HasField(TEXT("drawAs")))
                {
                    Brush.DrawAs = BrushDrawAs(GetJsonStringField(Payload, TEXT("drawAs")), Brush.DrawAs);
                    bBrushChanged = true;
                }
                if (Payload->HasField(TEXT("tintColor")))
                {
                    Brush.TintColor = FSlateColor(ColorParam(TEXT("tintColor")));
                    bBrushChanged = true;
                }
                if (Payload->HasField(TEXT("cornerRadius")))
                {
                    const double R = Payload->GetNumberField(TEXT("cornerRadius"));
                    Brush.OutlineSettings.CornerRadii = FVector4(R, R, R, R);
                    Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
                    bBrushChanged = true;
                }
                if (Payload->HasField(TEXT("outlineColor")))
                {
                    Brush.OutlineSettings.Color = FSlateColor(ColorParam(TEXT("outlineColor")));
                    bBrushChanged = true;
                }
                if (Payload->HasField(TEXT("outlineWidth")))
                {
                    Brush.OutlineSettings.Width = Payload->GetNumberField(TEXT("outlineWidth"));
                    bBrushChanged = true;
                }
                if (bBrushChanged)
                {
                    AsBorder->SetBrush(Brush);
                    Applied.Add(TEXT("background"));
                }
            }
            else if (UImage* AsImage = Cast<UImage>(Widget))
            {
                if (Payload->HasField(TEXT("tintColor")) || Payload->HasField(TEXT("colorAndOpacity")))
                {
                    const TCHAR* Key = Payload->HasField(TEXT("tintColor")) ? TEXT("tintColor") : TEXT("colorAndOpacity");
                    AsImage->SetColorAndOpacity(ColorParam(Key));
                    Applied.Add(TEXT("colorAndOpacity"));
                }
                if (Payload->HasField(TEXT("texturePath")))
                {
                    const FString TexPath = GetJsonStringField(Payload, TEXT("texturePath"));
                    if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TexPath))
                    {
                        AsImage->SetBrushFromTexture(Tex);
                        Applied.Add(TEXT("texture"));
                    }
                }
                if (Payload->HasField(TEXT("materialPath")))
                {
                    const FString MatPath = GetJsonStringField(Payload, TEXT("materialPath"));
                    if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MatPath))
                    {
                        AsImage->SetBrushFromMaterial(Mat);
                        Applied.Add(TEXT("material"));
                    }
                }
                FSlateBrush Brush = AsImage->GetBrush();
                bool bBrushChanged = false;
                if (Payload->HasField(TEXT("brushSize")))
                {
                    Brush.ImageSize = Vec2Param(TEXT("brushSize"), Brush.ImageSize);
                    bBrushChanged = true;
                }
                if (Payload->HasField(TEXT("drawAs")))
                {
                    Brush.DrawAs = BrushDrawAs(GetJsonStringField(Payload, TEXT("drawAs")), Brush.DrawAs);
                    bBrushChanged = true;
                }
                if (bBrushChanged)
                {
                    AsImage->SetBrush(Brush);
                    Applied.Add(TEXT("brush"));
                }
            }

            if (Applied.Num() > 0)
            {
                WidgetBP->MarkPackageDirty();
                const bool bSaveSucceeded = McpSafeAssetSave(WidgetBP);
                const FString Msg = FString::Printf(TEXT("set_style applied [%s] to %s (%s)"),
                    *FString::Join(Applied, TEXT(",")), *SlotName, *Widget->GetClass()->GetName());
                ResultJson->SetStringField(TEXT("mode"), TEXT("write"));
                ResultJson->SetStringField(TEXT("widgetName"), SlotName);
                ResultJson->SetStringField(TEXT("widgetClass"), Widget->GetClass()->GetName());
                ResultJson->SetStringField(TEXT("appliedParams"), FString::Join(Applied, TEXT(",")));
                ResultJson->SetBoolField(TEXT("saveSucceeded"), bSaveSucceeded);
                ResultJson->SetBoolField(TEXT("success"), true);
                ResultJson->SetStringField(TEXT("message"), Msg);
                Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, Msg, ResultJson);
                return true;
            }

            // Generic property setter via UE reflection — works on any widget class, any property
            FString PropertyName = GetJsonStringField(Payload, TEXT("propertyName"));
            FString Value;
            bool bHasValueField = Payload->HasField(TEXT("value"));
            bool bUseJsonConverter = false;
            TSharedPtr<FJsonValue> RawJsonValue;

            // Extract value from JSON — handle string, number, bool, object, and array types
            if (bHasValueField)
            {
                const TSharedPtr<FJsonValue> ValField = Payload->TryGetField(TEXT("value"));
                if (ValField.IsValid())
                {
                    if (ValField->Type == EJson::String)
                    {
                        Value = ValField->AsString();
                    }
                    else if (ValField->Type == EJson::Number)
                    {
                        Value = FString::SanitizeFloat(ValField->AsNumber());
                    }
                    else if (ValField->Type == EJson::Boolean)
                    {
                        Value = ValField->AsBool() ? TEXT("True") : TEXT("False");
                    }
                    else if (ValField->Type == EJson::Object || ValField->Type == EJson::Array)
                    {
                        // Defer to FJsonObjectConverter for struct-backed properties
                        bUseJsonConverter = true;
                        RawJsonValue = ValField;
                    }
                    else if (ValField->Type == EJson::Null)
                    {
                        Subsystem.SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Null JSON value is not supported for property mutation"), TEXT("UNSUPPORTED_VALUE_TYPE"));
                        return true;
                    }
                }
            }

            if (PropertyName.IsEmpty())
            {
                // Legacy path: if no propertyName given, try "style" param against "Style" property
                // Reset state from any prior "value" field extraction to avoid stale data
                bUseJsonConverter = false;
                RawJsonValue.Reset();
                Value.Empty();

                PropertyName = TEXT("Style");
                bHasValueField = Payload->HasField(TEXT("style"));
                if (bHasValueField)
                {
                    const TSharedPtr<FJsonValue> StyleField = Payload->TryGetField(TEXT("style"));
                    if (StyleField.IsValid() && (StyleField->Type == EJson::Object || StyleField->Type == EJson::Array))
                    {
                        bUseJsonConverter = true;
                        RawJsonValue = StyleField;
                    }
                    else
                    {
                        Value = GetJsonStringField(Payload, TEXT("style"));
                    }
                }
            }

            if (PropertyName.IsEmpty())
            {
                Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Missing required parameter: propertyName"), TEXT("MISSING_PARAMETER"));
                return true;
            }

            FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
            if (!Prop)
            {
                Subsystem.SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Property '%s' not found on widget '%s' (class %s)"), *PropertyName, *SlotName, *Widget->GetClass()->GetName()),
                    TEXT("PROPERTY_NOT_FOUND"));
                return true;
            }

            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);

            if (!bHasValueField)
            {
                // READ mode — value field not present, export and return current value
                FString ExportedValue;
                MCP_PROPERTY_EXPORT_TEXT(Prop, ExportedValue, ValuePtr, ValuePtr, Widget, PPF_None);

                ResultJson->SetStringField(TEXT("mode"), TEXT("read"));
                ResultJson->SetStringField(TEXT("propertyName"), PropertyName);
                ResultJson->SetStringField(TEXT("value"), ExportedValue);
                ResultJson->SetStringField(TEXT("widgetName"), SlotName);
                ResultJson->SetStringField(TEXT("widgetClass"), Widget->GetClass()->GetName());
            }
            else
            {
                // WRITE mode — set the property value
                Widget->Modify();

                bool bWriteSuccess = false;
                if (bUseJsonConverter && RawJsonValue.IsValid())
                {
                    // Use FJsonObjectConverter for struct-backed properties (Object/Array JSON)
                    bWriteSuccess = FJsonObjectConverter::JsonValueToUProperty(RawJsonValue, Prop, ValuePtr, 0, 0);
                }
                else
                {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
                    const TCHAR* ImportResult = Prop->ImportText_Direct(*Value, ValuePtr, Widget, PPF_None);
#else
                    const TCHAR* ImportResult = Prop->ImportText(*Value, ValuePtr, PPF_None, Widget);
#endif
                    bWriteSuccess = (ImportResult != nullptr);
                }
                if (!bWriteSuccess)
                {
                    Subsystem.SendAutomationError(RequestingSocket, RequestId,
                        FString::Printf(TEXT("Failed to set '%s' to '%s' on widget '%s'"), *PropertyName, *Value, *SlotName),
                        TEXT("SET_PROPERTY_FAILED"));
                    return true;
                }

                FPropertyChangedEvent ChangeEvent(Prop);
                Widget->PostEditChangeProperty(ChangeEvent);

                // Export the value back to verify what was actually set
                FString ExportedValue;
                MCP_PROPERTY_EXPORT_TEXT(Prop, ExportedValue, ValuePtr, ValuePtr, Widget, PPF_None);

                ResultJson->SetStringField(TEXT("mode"), TEXT("write"));
                ResultJson->SetStringField(TEXT("propertyName"), PropertyName);
                ResultJson->SetStringField(TEXT("value"), Value);
                ResultJson->SetStringField(TEXT("exportedValue"), ExportedValue);
                ResultJson->SetStringField(TEXT("widgetName"), SlotName);
                ResultJson->SetStringField(TEXT("widgetClass"), Widget->GetClass()->GetName());

                // Property change — mark dirty and save, do NOT recompile (that wipes instance values)
                WidgetBP->MarkPackageDirty();
                McpSafeAssetSave(WidgetBP);
            }
        }

        ResultJson->SetBoolField(TEXT("success"), true);
        FString ModeStr;
        bool bIsRead = ResultJson->TryGetStringField(TEXT("mode"), ModeStr) && ModeStr == TEXT("read");
        FString Msg = bIsRead
            ? FString::Printf(TEXT("%s property read"), *SubAction)
            : FString::Printf(TEXT("%s applied"), *SubAction);
        ResultJson->SetStringField(TEXT("message"), Msg);

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, Msg, ResultJson);
        return true;
    }

    return false;
}
}
