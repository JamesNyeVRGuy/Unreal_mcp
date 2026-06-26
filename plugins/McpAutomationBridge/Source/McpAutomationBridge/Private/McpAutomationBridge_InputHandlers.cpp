#include "McpAutomationBridgeGlobals.h"
#include "Dom/JsonObject.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"

// Enhanced Input (Editor Only)
#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
#include "EnhancedInputEditorSubsystem.h"
#endif
#include "Factories/Factory.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Misc/PackageName.h"

namespace InputHandlerHelpers
{
    // Resolve asset path supporting plugin mount points
    FString ResolveInputAssetPath(const FString& InPath)
    {
        if (InPath.IsEmpty()) return FString();

        // Try sanitize first (works for /Game/ paths)
        FString Sanitized = SanitizeProjectRelativePath(InPath);
        if (!Sanitized.IsEmpty()) return Sanitized;

        // Check if it's a valid mount point path (e.g. /Canopy/Input/...)
        FText MountReason;
        if (FPackageName::IsValidLongPackageName(InPath / TEXT("DummyAsset"), false, &MountReason))
        {
            return InPath;
        }
        if (FPackageName::IsValidLongPackageName(InPath, false, &MountReason))
        {
            return InPath;
        }

        return FString();
    }

    // Create an input modifier from a type string and optional params.
    // Outer must be the owning IMC/IA so the modifier serializes with the asset.
    UInputModifier* CreateModifier(const FString& Type, const TSharedPtr<FJsonObject>& Params, UObject* Outer = GetTransientPackage())
    {
        if (Type == TEXT("Negate") || Type == TEXT("InputModifierNegate"))
        {
            UInputModifierNegate* Mod = NewObject<UInputModifierNegate>(Outer);
            if (Params)
            {
                bool bX = true, bY = true, bZ = true;
                Params->TryGetBoolField(TEXT("x"), bX);
                Params->TryGetBoolField(TEXT("y"), bY);
                Params->TryGetBoolField(TEXT("z"), bZ);
                Mod->bX = bX;
                Mod->bY = bY;
                Mod->bZ = bZ;
            }
            return Mod;
        }
        else if (Type == TEXT("Swizzle") || Type == TEXT("SwizzleInputAxisValues") || Type == TEXT("InputModifierSwizzleAxis"))
        {
            UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(Outer);
            if (Params)
            {
                FString Order;
                if (Params->TryGetStringField(TEXT("order"), Order))
                {
                    if (Order == TEXT("YXZ"))
                        Mod->Order = EInputAxisSwizzle::YXZ;
                    else if (Order == TEXT("ZYX"))
                        Mod->Order = EInputAxisSwizzle::ZYX;
                    else if (Order == TEXT("XZY"))
                        Mod->Order = EInputAxisSwizzle::XZY;
                    else if (Order == TEXT("YZX"))
                        Mod->Order = EInputAxisSwizzle::YZX;
                    else if (Order == TEXT("ZXY"))
                        Mod->Order = EInputAxisSwizzle::ZXY;
                }
            }
            return Mod;
        }
        else if (Type == TEXT("DeadZone") || Type == TEXT("InputModifierDeadZone"))
        {
            UInputModifierDeadZone* Mod = NewObject<UInputModifierDeadZone>(Outer);
            if (Params)
            {
                double Lower = 0.2, Upper = 1.0;
                Params->TryGetNumberField(TEXT("lowerThreshold"), Lower);
                Params->TryGetNumberField(TEXT("upperThreshold"), Upper);
                Mod->LowerThreshold = (float)Lower;
                Mod->UpperThreshold = (float)Upper;
            }
            return Mod;
        }
        else if (Type == TEXT("Scalar") || Type == TEXT("InputModifierScalar"))
        {
            UInputModifierScalar* Mod = NewObject<UInputModifierScalar>(Outer);
            if (Params)
            {
                double X = 1.0, Y = 1.0, Z = 1.0;
                Params->TryGetNumberField(TEXT("x"), X);
                Params->TryGetNumberField(TEXT("y"), Y);
                Params->TryGetNumberField(TEXT("z"), Z);
                Mod->Scalar = FVector(X, Y, Z);
            }
            return Mod;
        }

        UE_LOG(LogMcpAutomationBridgeSubsystem, Warning,
            TEXT("Unknown modifier type: %s"), *Type);
        return nullptr;
    }

    // Create a trigger from a type string.
    // Outer must be the owning IMC/IA so the trigger serializes with the asset.
    UInputTrigger* CreateTrigger(const FString& Type, const TSharedPtr<FJsonObject>& Params, UObject* Outer = GetTransientPackage())
    {
        if (Type == TEXT("Down") || Type == TEXT("InputTriggerDown"))
        {
            return NewObject<UInputTriggerDown>(Outer);
        }
        else if (Type == TEXT("Pressed") || Type == TEXT("InputTriggerPressed"))
        {
            return NewObject<UInputTriggerPressed>(Outer);
        }
        else if (Type == TEXT("Released") || Type == TEXT("InputTriggerReleased"))
        {
            return NewObject<UInputTriggerReleased>(Outer);
        }
        else if (Type == TEXT("Hold") || Type == TEXT("InputTriggerHold"))
        {
            UInputTriggerHold* Trig = NewObject<UInputTriggerHold>(Outer);
            if (Params)
            {
                double HoldTime = 1.0;
                Params->TryGetNumberField(TEXT("holdTimeThreshold"), HoldTime);
                Trig->HoldTimeThreshold = (float)HoldTime;
                bool bOneShot = false;
                Params->TryGetBoolField(TEXT("isOneShot"), bOneShot);
                Trig->bIsOneShot = bOneShot;
            }
            return Trig;
        }
        else if (Type == TEXT("Tap") || Type == TEXT("InputTriggerTap"))
        {
            UInputTriggerTap* Trig = NewObject<UInputTriggerTap>(Outer);
            if (Params)
            {
                double TapRelease = 0.2;
                Params->TryGetNumberField(TEXT("tapReleaseTimeThreshold"), TapRelease);
                Trig->TapReleaseTimeThreshold = (float)TapRelease;
            }
            return Trig;
        }
        else if (Type == TEXT("Chord") || Type == TEXT("ChordAction") || Type == TEXT("InputTriggerChordAction")
              || Type == TEXT("ChordBlocker") || Type == TEXT("InputTriggerChordBlocker"))
        {
            // Chord requires a target Input Action -- the modifier key the mapping
            // depends on (e.g. IA_ModifierCtrl for "Ctrl+4"). Accepts the asset
            // path under either 'chordAction' or 'actionPath' (the latter mirrors
            // existing input-handler conventions).
            const bool bBlocker = Type.Contains(TEXT("Blocker"));
            UInputTriggerChordAction* Trig = bBlocker
                ? NewObject<UInputTriggerChordBlocker>(Outer)
                : NewObject<UInputTriggerChordAction>(Outer);
            if (Params)
            {
                FString ChordActionPath;
                Params->TryGetStringField(TEXT("chordAction"), ChordActionPath);
                if (ChordActionPath.IsEmpty())
                {
                    Params->TryGetStringField(TEXT("actionPath"), ChordActionPath);
                }
                if (!ChordActionPath.IsEmpty())
                {
                    UInputAction* ChordIA = Cast<UInputAction>(
                        UEditorAssetLibrary::LoadAsset(ChordActionPath));
                    if (!ChordIA)
                    {
                        ChordIA = LoadObject<UInputAction>(nullptr, *ChordActionPath);
                    }
                    if (ChordIA)
                    {
                        Trig->ChordAction = ChordIA;
                    }
                    else
                    {
                        UE_LOG(LogMcpAutomationBridgeSubsystem, Warning,
                            TEXT("Chord trigger: chordAction not loadable: %s"), *ChordActionPath);
                    }
                }
                else
                {
                    UE_LOG(LogMcpAutomationBridgeSubsystem, Warning,
                        TEXT("Chord trigger created without chordAction -- it will never fire."));
                }
            }
            return Trig;
        }
        else if (Type == TEXT("Pulse") || Type == TEXT("InputTriggerPulse"))
        {
            UInputTriggerPulse* Trig = NewObject<UInputTriggerPulse>(Outer);
            if (Params)
            {
                double Interval = 0.5;
                if (Params->TryGetNumberField(TEXT("interval"), Interval))
                {
                    Trig->Interval = (float)Interval;
                }
                int32 TriggerLimit = 0;
                if (Params->TryGetNumberField(TEXT("triggerLimit"), TriggerLimit))
                {
                    Trig->TriggerLimit = TriggerLimit;
                }
                bool bTriggerOnStart = true;
                if (Params->TryGetBoolField(TEXT("triggerOnStart"), bTriggerOnStart))
                {
                    Trig->bTriggerOnStart = bTriggerOnStart;
                }
            }
            return Trig;
        }

        UE_LOG(LogMcpAutomationBridgeSubsystem, Warning,
            TEXT("Unknown trigger type: %s"), *Type);
        return nullptr;
    }

    // Parse modifiers array from JSON and apply to mapping.
    // Outer is passed to CreateModifier so the UObject serializes with the asset.
    void ApplyModifiersToMapping(FEnhancedActionKeyMapping& Mapping, const TArray<TSharedPtr<FJsonValue>>& ModifiersArray, UObject* Outer)
    {
        for (const TSharedPtr<FJsonValue>& ModVal : ModifiersArray)
        {
            if (ModVal->Type == EJson::String)
            {
                UInputModifier* Mod = CreateModifier(ModVal->AsString(), nullptr, Outer);
                if (Mod) Mapping.Modifiers.Add(Mod);
            }
            else if (ModVal->Type == EJson::Object)
            {
                TSharedPtr<FJsonObject> ModObj = ModVal->AsObject();
                FString ModType;
                ModObj->TryGetStringField(TEXT("type"), ModType);
                if (!ModType.IsEmpty())
                {
                    UInputModifier* Mod = CreateModifier(ModType, ModObj, Outer);
                    if (Mod) Mapping.Modifiers.Add(Mod);
                }
            }
        }
    }

    // Parse triggers array from JSON and apply to mapping.
    // Outer is passed to CreateTrigger so the UObject serializes with the asset.
    void ApplyTriggersToMapping(FEnhancedActionKeyMapping& Mapping, const TArray<TSharedPtr<FJsonValue>>& TriggersArray, UObject* Outer)
    {
        for (const TSharedPtr<FJsonValue>& TrigVal : TriggersArray)
        {
            if (TrigVal->Type == EJson::String)
            {
                UInputTrigger* Trig = CreateTrigger(TrigVal->AsString(), nullptr, Outer);
                if (Trig) Mapping.Triggers.Add(Trig);
            }
            else if (TrigVal->Type == EJson::Object)
            {
                TSharedPtr<FJsonObject> TrigObj = TrigVal->AsObject();
                FString TrigType;
                TrigObj->TryGetStringField(TEXT("type"), TrigType);
                if (!TrigType.IsEmpty())
                {
                    UInputTrigger* Trig = CreateTrigger(TrigType, TrigObj, Outer);
                    if (Trig) Mapping.Triggers.Add(Trig);
                }
            }
        }
    }
}

// Dump an input modifier/trigger node's class + all editable UPROPERTYs.
// Shared by both the InputAction and InputMappingContext branches of
// get_input_info so callers can fully reproduce a binding (e.g. SwizzleAxis
// Order, Negate bX/bY/bZ, trigger thresholds).
static TSharedPtr<FJsonObject> DumpInputNodeProps(UObject* Node)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    if (!Node) return Obj;
    Obj->SetStringField(TEXT("type"), Node->GetClass()->GetName());
    TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
    for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
    {
        FProperty* Prop = *It;
        if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
        FString ValueStr;
        Prop->ExportTextItem_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(Node), nullptr, nullptr, PPF_None);
        Props->SetStringField(Prop->GetName(), ValueStr);
    }
    Obj->SetObjectField(TEXT("properties"), Props);
    return Obj;
}

#endif

bool UMcpAutomationBridgeSubsystem::HandleInputAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  if (Action != TEXT("manage_input")) {
    return false;
  }

#if WITH_EDITOR
  using namespace InputHandlerHelpers;

  if (!Payload.IsValid()) {
    SendAutomationError(RequestingSocket, RequestId, TEXT("Missing payload."),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  FString SubAction;
  if (!Payload->TryGetStringField(TEXT("action"), SubAction)) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("Missing 'action' field in payload."),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  UE_LOG(LogMcpAutomationBridgeSubsystem, Log, TEXT("HandleInputAction: %s"),
         *SubAction);

  if (SubAction == TEXT("create_input_action")) {
    FString Name;
    Payload->TryGetStringField(TEXT("name"), Name);
    FString Path;
    Payload->TryGetStringField(TEXT("path"), Path);

    if (Name.IsEmpty() || Path.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Name and path are required."),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    FString ResolvedPath = ResolveInputAssetPath(Path);
    if (ResolvedPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Invalid path: '%s'"), *Path),
                          TEXT("INVALID_PATH"));
      return true;
    }

    if (Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT(".."))) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Invalid asset name '%s'"), *Name),
                          TEXT("INVALID_NAME"));
      return true;
    }

    const FString FullPath = FString::Printf(TEXT("%s/%s"), *ResolvedPath, *Name);
    if (UEditorAssetLibrary::DoesAssetExist(FullPath)) {
      SendAutomationError(
          RequestingSocket, RequestId,
          FString::Printf(TEXT("Asset already exists at %s"), *FullPath),
          TEXT("ASSET_EXISTS"));
      return true;
    }

    IAssetTools &AssetTools =
        FModuleManager::Get()
            .LoadModuleChecked<FAssetToolsModule>("AssetTools")
            .Get();

    UClass *ActionClass = UInputAction::StaticClass();
    UObject *NewAsset =
        AssetTools.CreateAsset(Name, ResolvedPath, ActionClass, nullptr);

    if (NewAsset) {
      // Set value type if provided
      UInputAction* NewAction = Cast<UInputAction>(NewAsset);
      if (NewAction)
      {
          FString ValueTypeStr;
          if (Payload->TryGetStringField(TEXT("valueType"), ValueTypeStr))
          {
              if (ValueTypeStr == TEXT("Boolean") || ValueTypeStr == TEXT("Bool") || ValueTypeStr == TEXT("Digital"))
                  NewAction->ValueType = EInputActionValueType::Boolean;
              else if (ValueTypeStr == TEXT("Axis1D") || ValueTypeStr == TEXT("Float"))
                  NewAction->ValueType = EInputActionValueType::Axis1D;
              else if (ValueTypeStr == TEXT("Axis2D") || ValueTypeStr == TEXT("Vector2D"))
                  NewAction->ValueType = EInputActionValueType::Axis2D;
              else if (ValueTypeStr == TEXT("Axis3D") || ValueTypeStr == TEXT("Vector"))
                  NewAction->ValueType = EInputActionValueType::Axis3D;
          }
          else
          {
              // Try numeric value type
              double ValueTypeNum = -1;
              if (Payload->TryGetNumberField(TEXT("valueType"), ValueTypeNum))
              {
                  int32 VT = (int32)ValueTypeNum;
                  if (VT >= 0 && VT <= 3)
                      NewAction->ValueType = (EInputActionValueType)VT;
              }
          }
      }

      SaveLoadedAssetThrottled(NewAsset, -1.0, true);
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      Result->SetStringField(TEXT("assetPath"), NewAsset->GetPathName());
      if (NewAction)
          Result->SetStringField(TEXT("valueType"), FString::FromInt((int32)NewAction->ValueType));
      AddAssetVerification(Result, NewAsset);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Input Action created."), Result);
    } else {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Failed to create Input Action."),
                          TEXT("CREATION_FAILED"));
    }
  } else if (SubAction == TEXT("create_input_mapping_context")) {
    FString Name;
    Payload->TryGetStringField(TEXT("name"), Name);
    FString Path;
    Payload->TryGetStringField(TEXT("path"), Path);

    if (Name.IsEmpty() || Path.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Name and path are required."),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    FString ResolvedPath = ResolveInputAssetPath(Path);
    if (ResolvedPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Invalid path: '%s'"), *Path),
                          TEXT("INVALID_PATH"));
      return true;
    }

    if (Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT(".."))) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Invalid asset name '%s'"), *Name),
                          TEXT("INVALID_NAME"));
      return true;
    }

    const FString FullPath = FString::Printf(TEXT("%s/%s"), *ResolvedPath, *Name);
    if (UEditorAssetLibrary::DoesAssetExist(FullPath)) {
      SendAutomationError(
          RequestingSocket, RequestId,
          FString::Printf(TEXT("Asset already exists at %s"), *FullPath),
          TEXT("ASSET_EXISTS"));
      return true;
    }

    IAssetTools &AssetTools =
        FModuleManager::Get()
            .LoadModuleChecked<FAssetToolsModule>("AssetTools")
            .Get();

    UClass *ContextClass = UInputMappingContext::StaticClass();
    UObject *NewAsset =
        AssetTools.CreateAsset(Name, ResolvedPath, ContextClass, nullptr);

    if (NewAsset) {
      SaveLoadedAssetThrottled(NewAsset, -1.0, true);
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      Result->SetStringField(TEXT("assetPath"), NewAsset->GetPathName());
      AddAssetVerification(Result, NewAsset);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Input Mapping Context created."), Result);
    } else {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Failed to create Input Mapping Context."),
                          TEXT("CREATION_FAILED"));
    }
  } else if (SubAction == TEXT("add_mapping") || SubAction == TEXT("map_input_action")) {
    FString ContextPath;
    Payload->TryGetStringField(TEXT("contextPath"), ContextPath);
    FString ActionPath;
    Payload->TryGetStringField(TEXT("actionPath"), ActionPath);
    FString KeyName;
    Payload->TryGetStringField(TEXT("key"), KeyName);

    FString ResolvedContextPath = ResolveInputAssetPath(ContextPath);
    FString ResolvedActionPath = ResolveInputAssetPath(ActionPath);

    if (ResolvedContextPath.IsEmpty() || ResolvedActionPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Invalid context or action path."),
                          TEXT("INVALID_PATH"));
      return true;
    }

    UInputMappingContext *Context =
        Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(ResolvedContextPath));
    UInputAction *InAction =
        Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ResolvedActionPath));

    if (!Context || !InAction || KeyName.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Context or action not found, or key is empty. Context: %s, Action: %s"),
                                        *ResolvedContextPath, *ResolvedActionPath),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    FKey Key = FKey(FName(*KeyName));
    if (!Key.IsValid()) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Invalid key name: %s"), *KeyName),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    FEnhancedActionKeyMapping &Mapping = Context->MapKey(InAction, Key);

    // Apply modifiers if provided
    const TArray<TSharedPtr<FJsonValue>>* ModifiersArray;
    if (Payload->TryGetArrayField(TEXT("modifiers"), ModifiersArray))
    {
        ApplyModifiersToMapping(Mapping, *ModifiersArray, Context);
    }

    // Apply triggers if provided
    const TArray<TSharedPtr<FJsonValue>>* TriggersArray;
    if (Payload->TryGetArrayField(TEXT("triggers"), TriggersArray))
    {
        ApplyTriggersToMapping(Mapping, *TriggersArray, Context);
    }

    SaveLoadedAssetThrottled(Context, -1.0, true);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("contextPath"), ResolvedContextPath);
    Result->SetStringField(TEXT("actionPath"), ResolvedActionPath);
    Result->SetStringField(TEXT("key"), KeyName);
    Result->SetNumberField(TEXT("modifierCount"), Mapping.Modifiers.Num());
    Result->SetNumberField(TEXT("triggerCount"), Mapping.Triggers.Num());
    AddAssetVerificationNested(Result, TEXT("contextVerification"), Context);
    AddAssetVerificationNested(Result, TEXT("actionVerification"), InAction);
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Mapping added."), Result);
  } else if (SubAction == TEXT("remove_mapping")) {
    FString ContextPath;
    Payload->TryGetStringField(TEXT("contextPath"), ContextPath);
    FString ActionPath;
    Payload->TryGetStringField(TEXT("actionPath"), ActionPath);
    FString KeyName;
    Payload->TryGetStringField(TEXT("key"), KeyName);

    FString ResolvedContextPath = ResolveInputAssetPath(ContextPath);
    FString ResolvedActionPath = ResolveInputAssetPath(ActionPath);

    if (ResolvedContextPath.IsEmpty() || ResolvedActionPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Invalid context or action path."),
                          TEXT("INVALID_PATH"));
      return true;
    }

    UInputMappingContext *Context =
        Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(ResolvedContextPath));
    UInputAction *InAction =
        Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ResolvedActionPath));

    if (!Context || !InAction) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Context or action not found. Context: %s, Action: %s"),
                                        *ResolvedContextPath, *ResolvedActionPath),
                          TEXT("NOT_FOUND"));
      return true;
    }

    // Remove specific key or all mappings for this action
    TArray<FKey> KeysToRemove;
    if (!KeyName.IsEmpty())
    {
        FKey SpecificKey = FKey(FName(*KeyName));
        for (const FEnhancedActionKeyMapping &Mapping : Context->GetMappings())
        {
            if (Mapping.Action == InAction && Mapping.Key == SpecificKey)
            {
                KeysToRemove.Add(Mapping.Key);
            }
        }
    }
    else
    {
        for (const FEnhancedActionKeyMapping &Mapping : Context->GetMappings())
        {
            if (Mapping.Action == InAction)
            {
                KeysToRemove.Add(Mapping.Key);
            }
        }
    }

    for (const FKey &KeyToRemove : KeysToRemove) {
      Context->UnmapKey(InAction, KeyToRemove);
    }
    SaveLoadedAssetThrottled(Context, -1.0, true);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("contextPath"), ResolvedContextPath);
    Result->SetStringField(TEXT("actionPath"), ResolvedActionPath);
    Result->SetNumberField(TEXT("keysRemoved"), KeysToRemove.Num());
    TArray<TSharedPtr<FJsonValue>> RemovedKeys;
    for (const FKey &Key : KeysToRemove) {
      RemovedKeys.Add(MakeShared<FJsonValueString>(Key.ToString()));
    }
    Result->SetArrayField(TEXT("removedKeys"), RemovedKeys);
    AddAssetVerificationNested(Result, TEXT("contextVerification"), Context);
    AddAssetVerificationNested(Result, TEXT("actionVerification"), InAction);
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Mappings removed."), Result);
  } else if (SubAction == TEXT("set_input_trigger")) {
    FString ContextPath;
    Payload->TryGetStringField(TEXT("contextPath"), ContextPath);
    FString ActionPath;
    Payload->TryGetStringField(TEXT("actionPath"), ActionPath);
    FString KeyName;
    Payload->TryGetStringField(TEXT("key"), KeyName);
    FString TriggerType;
    Payload->TryGetStringField(TEXT("triggerType"), TriggerType);

    FString ResolvedActionPath = ResolveInputAssetPath(ActionPath);
    if (ResolvedActionPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Invalid action path."), TEXT("INVALID_PATH"));
      return true;
    }

    // If contextPath + key provided, set trigger on the specific mapping
    if (!ContextPath.IsEmpty() && !KeyName.IsEmpty())
    {
        FString ResolvedContextPath = ResolveInputAssetPath(ContextPath);
        UInputMappingContext* Context =
            Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(ResolvedContextPath));
        UInputAction* InAction =
            Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ResolvedActionPath));

        if (Context && InAction)
        {
            FKey Key = FKey(FName(*KeyName));
            for (FEnhancedActionKeyMapping& Mapping : const_cast<TArray<FEnhancedActionKeyMapping>&>(Context->GetMappings()))
            {
                if (Mapping.Action == InAction && Mapping.Key == Key)
                {
                    UInputTrigger* Trig = CreateTrigger(TriggerType, Payload, Context);
                    if (Trig) Mapping.Triggers.Add(Trig);
                    break;
                }
            }
            SaveLoadedAssetThrottled(Context, -1.0, true);
        }
    }
    else
    {
        // Set trigger on the action itself
        UInputAction* InAction =
            Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ResolvedActionPath));
        if (InAction)
        {
            UInputTrigger* Trig = CreateTrigger(TriggerType, Payload, InAction);
            if (Trig) InAction->Triggers.Add(Trig);
            SaveLoadedAssetThrottled(InAction, -1.0, true);
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("actionPath"), ResolvedActionPath);
    Result->SetStringField(TEXT("triggerType"), TriggerType);
    Result->SetBoolField(TEXT("triggerSet"), true);
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           FString::Printf(TEXT("Trigger '%s' set."), *TriggerType), Result);
  } else if (SubAction == TEXT("set_input_modifier")) {
    FString ContextPath;
    Payload->TryGetStringField(TEXT("contextPath"), ContextPath);
    FString ActionPath;
    Payload->TryGetStringField(TEXT("actionPath"), ActionPath);
    FString KeyName;
    Payload->TryGetStringField(TEXT("key"), KeyName);
    FString ModifierType;
    Payload->TryGetStringField(TEXT("modifierType"), ModifierType);

    FString ResolvedActionPath = ResolveInputAssetPath(ActionPath);
    if (ResolvedActionPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Invalid action path."), TEXT("INVALID_PATH"));
      return true;
    }

    // If contextPath + key provided, set modifier on the specific mapping
    if (!ContextPath.IsEmpty() && !KeyName.IsEmpty())
    {
        FString ResolvedContextPath = ResolveInputAssetPath(ContextPath);
        UInputMappingContext* Context =
            Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(ResolvedContextPath));
        UInputAction* InAction =
            Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ResolvedActionPath));

        if (Context && InAction)
        {
            FKey Key = FKey(FName(*KeyName));
            for (FEnhancedActionKeyMapping& Mapping : const_cast<TArray<FEnhancedActionKeyMapping>&>(Context->GetMappings()))
            {
                if (Mapping.Action == InAction && Mapping.Key == Key)
                {
                    UInputModifier* Mod = CreateModifier(ModifierType, Payload, Context);
                    if (Mod) Mapping.Modifiers.Add(Mod);
                    break;
                }
            }
            SaveLoadedAssetThrottled(Context, -1.0, true);
        }
    }
    else
    {
        // Set modifier on the action itself
        UInputAction* InAction =
            Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ResolvedActionPath));
        if (InAction)
        {
            UInputModifier* Mod = CreateModifier(ModifierType, Payload, InAction);
            if (Mod) InAction->Modifiers.Add(Mod);
            SaveLoadedAssetThrottled(InAction, -1.0, true);
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("actionPath"), ResolvedActionPath);
    Result->SetStringField(TEXT("modifierType"), ModifierType);
    Result->SetBoolField(TEXT("modifierSet"), true);
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           FString::Printf(TEXT("Modifier '%s' set."), *ModifierType), Result);
  } else if (SubAction == TEXT("enable_input_mapping")) {
    FString ContextPath;
    Payload->TryGetStringField(TEXT("contextPath"), ContextPath);
    int32 Priority = 0;
    Payload->TryGetNumberField(TEXT("priority"), Priority);

    FString ResolvedContextPath = ResolveInputAssetPath(ContextPath);
    if (ResolvedContextPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Invalid context path."), TEXT("INVALID_PATH"));
      return true;
    }

    UInputMappingContext *Context =
        Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(ResolvedContextPath));

    if (!Context) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Context not found: %s"), *ResolvedContextPath),
                          TEXT("NOT_FOUND"));
      return true;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("contextPath"), ResolvedContextPath);
    Result->SetNumberField(TEXT("priority"), Priority);
    Result->SetBoolField(TEXT("enabled"), true);
    AddAssetVerification(Result, Context);
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Input mapping context enabled (requires PIE for runtime effect)."), Result);
  } else if (SubAction == TEXT("disable_input_action")) {
    FString ActionPath;
    Payload->TryGetStringField(TEXT("actionPath"), ActionPath);

    FString ResolvedActionPath = ResolveInputAssetPath(ActionPath);
    if (ResolvedActionPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Invalid action path."), TEXT("INVALID_PATH"));
      return true;
    }

    UInputAction *InAction =
        Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ResolvedActionPath));

    if (!InAction) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Action not found: %s"), *ResolvedActionPath),
                          TEXT("NOT_FOUND"));
      return true;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("actionPath"), ResolvedActionPath);
    Result->SetBoolField(TEXT("disabled"), true);
    AddAssetVerification(Result, InAction);
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Input action disabled."), Result);
  } else if (SubAction == TEXT("get_input_info")) {
    FString AssetPath;
    Payload->TryGetStringField(TEXT("assetPath"), AssetPath);

    if (AssetPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("assetPath is required."),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    FString ResolvedAssetPath = ResolveInputAssetPath(AssetPath);
    if (ResolvedAssetPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Invalid asset path: '%s'"), *AssetPath),
                          TEXT("INVALID_PATH"));
      return true;
    }

    UObject *Asset = UEditorAssetLibrary::LoadAsset(ResolvedAssetPath);
    if (!Asset) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Asset not found: %s"), *ResolvedAssetPath),
                          TEXT("NOT_FOUND"));
      return true;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("assetPath"), ResolvedAssetPath);
    Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
    Result->SetStringField(TEXT("assetName"), Asset->GetName());

    if (UInputAction *InputAction = Cast<UInputAction>(Asset)) {
      Result->SetStringField(TEXT("type"), TEXT("InputAction"));
      // Return human-readable value type
      FString VTName;
      switch(InputAction->ValueType)
      {
          case EInputActionValueType::Boolean: VTName = TEXT("Boolean"); break;
          case EInputActionValueType::Axis1D: VTName = TEXT("Axis1D"); break;
          case EInputActionValueType::Axis2D: VTName = TEXT("Axis2D"); break;
          case EInputActionValueType::Axis3D: VTName = TEXT("Axis3D"); break;
          default: VTName = TEXT("Unknown"); break;
      }
      Result->SetStringField(TEXT("valueType"), VTName);
      Result->SetNumberField(TEXT("valueTypeIndex"), (int32)InputAction->ValueType);
      Result->SetBoolField(TEXT("consumeInput"), InputAction->bConsumeInput);
      Result->SetBoolField(TEXT("triggerWhenPaused"), InputAction->bTriggerWhenPaused);
      Result->SetBoolField(TEXT("reserveAllMappings"), InputAction->bReserveAllMappings);
      Result->SetNumberField(TEXT("modifierCount"), InputAction->Modifiers.Num());
      Result->SetNumberField(TEXT("triggerCount"), InputAction->Triggers.Num());

      // Action-level modifiers/triggers with full properties (applied to every
      // mapping of this action, after the per-mapping IMC modifiers).
      TArray<TSharedPtr<FJsonValue>> ActionMods;
      for (UInputModifier* Mod : InputAction->Modifiers)
      {
          if (Mod) ActionMods.Add(MakeShared<FJsonValueObject>(DumpInputNodeProps(Mod)));
      }
      Result->SetArrayField(TEXT("modifiers"), ActionMods);

      TArray<TSharedPtr<FJsonValue>> ActionTrigs;
      for (UInputTrigger* Trig : InputAction->Triggers)
      {
          if (Trig) ActionTrigs.Add(MakeShared<FJsonValueObject>(DumpInputNodeProps(Trig)));
      }
      Result->SetArrayField(TEXT("triggers"), ActionTrigs);
    } else if (UInputMappingContext *Context = Cast<UInputMappingContext>(Asset)) {
      Result->SetStringField(TEXT("type"), TEXT("InputMappingContext"));
      Result->SetNumberField(TEXT("mappingCount"), Context->GetMappings().Num());

      // List all mappings with details
      TArray<TSharedPtr<FJsonValue>> MappingsArray;
      for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
      {
          TSharedPtr<FJsonObject> MappingObj = MakeShared<FJsonObject>();
          MappingObj->SetStringField(TEXT("action"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT("null"));
          MappingObj->SetStringField(TEXT("actionName"), Mapping.Action ? Mapping.Action->GetName() : TEXT("null"));
          MappingObj->SetStringField(TEXT("key"), Mapping.Key.ToString());
          MappingObj->SetNumberField(TEXT("modifierCount"), Mapping.Modifiers.Num());
          MappingObj->SetNumberField(TEXT("triggerCount"), Mapping.Triggers.Num());

          // Modifiers + triggers with full editable properties (shared dump),
          // so a caller can recreate WASD->Axis2D directionality (SwizzleAxis
          // Order, Negate bX/bY/bZ) and trigger config exactly.
          TArray<TSharedPtr<FJsonValue>> Mods;
          for (UInputModifier* Mod : Mapping.Modifiers)
          {
              if (Mod) Mods.Add(MakeShared<FJsonValueObject>(DumpInputNodeProps(Mod)));
          }
          MappingObj->SetArrayField(TEXT("modifiers"), Mods);

          TArray<TSharedPtr<FJsonValue>> Trigs;
          for (UInputTrigger* Trig : Mapping.Triggers)
          {
              if (Trig) Trigs.Add(MakeShared<FJsonValueObject>(DumpInputNodeProps(Trig)));
          }
          MappingObj->SetArrayField(TEXT("triggers"), Trigs);

          MappingsArray.Add(MakeShared<FJsonValueObject>(MappingObj));
      }
      Result->SetArrayField(TEXT("mappings"), MappingsArray);
    }

    AddAssetVerification(Result, Asset);
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Input asset info retrieved."), Result);
  } else {
    SendAutomationError(
        RequestingSocket, RequestId,
        FString::Printf(TEXT("Unknown sub-action: %s"), *SubAction),
        TEXT("UNKNOWN_ACTION"));
  }

  return true;
#else
  SendAutomationError(RequestingSocket, RequestId,
                      TEXT("Input management requires Editor build."),
                      TEXT("NOT_AVAILABLE"));
  return true;
#endif
}
