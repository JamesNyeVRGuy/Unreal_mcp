#include "Domains/ControlActor/McpAutomationBridge_ControlActorSupport.h"

// P24 Gate 2: enhanced call_function + new map_entry handlers need the
// reflection helpers for JSON <-> property memory.
#include "Foundation/Reflection/McpPropertyReflection.h"
#include "Safety/McpSafeOperations.h"
#include "UObject/UnrealType.h"

bool UMcpAutomationBridgeSubsystem::HandleControlActorSetBlueprintVariables(
    const FString &RequestId, const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  FString TargetName;
  Payload->TryGetStringField(TEXT("actorName"), TargetName);
  if (TargetName.IsEmpty()) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("INVALID_ARGUMENT"),
                              TEXT("actorName required"), nullptr);
    return true;
  }

  const TSharedPtr<FJsonObject> *VariablesPtr = nullptr;
  if (!(Payload->TryGetObjectField(TEXT("variables"), VariablesPtr) &&
        VariablesPtr && VariablesPtr->IsValid())) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("INVALID_ARGUMENT"),
                              TEXT("variables object required"), nullptr);
    return true;
  }

  AActor *Found = FindActorByName(TargetName);
  if (!Found) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("ACTOR_NOT_FOUND"),
                              TEXT("Actor not found"), nullptr);
    return true;
  }

  UClass *ActorClass = Found->GetClass();
  Found->Modify();
  TArray<FString> Applied;
  TArray<FString> Warnings;

  for (const auto &Pair : (*VariablesPtr)->Values) {
    const FString VariableName(*Pair.Key);
    FProperty *Property = ActorClass->FindPropertyByName(*VariableName);
    if (!Property) {
      Warnings.Add(FString::Printf(TEXT("Property not found: %s"), *VariableName));
      continue;
    }

    FString ApplyError;
    if (ApplyJsonValueToProperty(Found, Property, Pair.Value, ApplyError))
      Applied.Add(VariableName);
    else
      Warnings.Add(FString::Printf(TEXT("Failed to set %s: %s"), *VariableName,
                                   *ApplyError));
  }

  Found->MarkComponentsRenderStateDirty();
  Found->MarkPackageDirty();

  TSharedPtr<FJsonObject> Data = McpHandlerUtils::CreateResultObject();
  if (Applied.Num() > 0) {
    TArray<TSharedPtr<FJsonValue>> AppliedArray;
    for (const FString &Name : Applied)
      AppliedArray.Add(MakeShared<FJsonValueString>(Name));
    Data->SetArrayField(TEXT("updated"), AppliedArray);
  }

  SendStandardSuccessResponse(this, Socket, RequestId,
                              TEXT("Variables updated"), Data, Warnings);
  return true;
#else
  return false;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleControlActorCreateSnapshot(
    const FString &RequestId, const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  FString TargetName;
  Payload->TryGetStringField(TEXT("actorName"), TargetName);
  if (TargetName.IsEmpty()) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("INVALID_ARGUMENT"),
                              TEXT("actorName required"), nullptr);
    return true;
  }

  FString SnapshotName;
  Payload->TryGetStringField(TEXT("snapshotName"), SnapshotName);
  if (SnapshotName.IsEmpty()) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("INVALID_ARGUMENT"),
                              TEXT("snapshotName required"), nullptr);
    return true;
  }

  AActor *Found = FindActorByName(TargetName);
  if (!Found) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("ACTOR_NOT_FOUND"),
                              TEXT("Actor not found"), nullptr);
    return true;
  }

  const FString SnapshotKey =
      FString::Printf(TEXT("%s::%s"), *Found->GetPathName(), *SnapshotName);
  CachedActorSnapshots.Add(SnapshotKey, Found->GetActorTransform());

  TSharedPtr<FJsonObject> Data = McpHandlerUtils::CreateResultObject();
  Data->SetStringField(TEXT("snapshotName"), SnapshotName);
  Data->SetStringField(TEXT("actorName"), Found->GetActorLabel());
  SendStandardSuccessResponse(this, Socket, RequestId, TEXT("Snapshot created"),
                              Data);
  return true;
#else
  return false;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleControlActorRestoreSnapshot(
    const FString &RequestId, const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  FString TargetName;
  Payload->TryGetStringField(TEXT("actorName"), TargetName);
  if (TargetName.IsEmpty()) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("INVALID_ARGUMENT"),
                              TEXT("actorName required"), nullptr);
    return true;
  }

  FString SnapshotName;
  Payload->TryGetStringField(TEXT("snapshotName"), SnapshotName);
  if (SnapshotName.IsEmpty()) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("INVALID_ARGUMENT"),
                              TEXT("snapshotName required"), nullptr);
    return true;
  }

  AActor *Found = FindActorByName(TargetName);
  if (!Found) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("ACTOR_NOT_FOUND"),
                              TEXT("Actor not found"), nullptr);
    return true;
  }

  const FString SnapshotKey =
      FString::Printf(TEXT("%s::%s"), *Found->GetPathName(), *SnapshotName);
  if (!CachedActorSnapshots.Contains(SnapshotKey)) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("SNAPSHOT_NOT_FOUND"),
                              TEXT("Snapshot not found"), nullptr);
    return true;
  }

  const FTransform &SavedTransform = CachedActorSnapshots[SnapshotKey];
  Found->Modify();
  Found->SetActorTransform(SavedTransform);
  Found->MarkComponentsRenderStateDirty();
  Found->MarkPackageDirty();

  TSharedPtr<FJsonObject> Data = McpHandlerUtils::CreateResultObject();
  Data->SetStringField(TEXT("snapshotName"), SnapshotName);
  Data->SetStringField(TEXT("actorName"), Found->GetActorLabel());
  SendStandardSuccessResponse(this, Socket, RequestId,
                              TEXT("Snapshot restored"), Data);
  return true;
#else
  return false;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleControlActorExport(
    const FString &RequestId, const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  FString TargetName;
  Payload->TryGetStringField(TEXT("actorName"), TargetName);
  if (TargetName.IsEmpty()) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("INVALID_ARGUMENT"),
                              TEXT("actorName required"), nullptr);
    return true;
  }

  AActor *Found = FindActorByName(TargetName);
  if (!Found) {
    SendStandardErrorResponse(this, Socket, RequestId, TEXT("ACTOR_NOT_FOUND"),
                              TEXT("Actor not found"), nullptr);
    return true;
  }

  FMcpOutputCapture OutputCapture;
  UExporter::ExportToOutputDevice(nullptr, Found, nullptr, OutputCapture,
                                  TEXT("T3D"), 0, 0, false);
  FString OutputString = FString::Join(OutputCapture.Consume(), TEXT("\n"));

  TSharedPtr<FJsonObject> Data = McpHandlerUtils::CreateResultObject();
  Data->SetStringField(TEXT("t3d"), OutputString);
  Data->SetStringField(TEXT("actorName"), Found->GetActorLabel());
  SendStandardSuccessResponse(this, Socket, RequestId, TEXT("Actor exported"),
                              Data);
  return true;
#else
  return false;
#endif
}


bool UMcpAutomationBridgeSubsystem::HandleControlActorCallFunction(
    const FString &RequestId, const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  // P24 Gate 2: enhanced from the actor-only version. Feature parity with
  // the d9fea10 env-side call_function:
  //   * objectPath fallback so any UObject (not just spawned actors) can be
  //     the target -- lets consumers call BP functions on Blueprint CDOs,
  //     asset UObjects, subsystems, etc. that were reachable pre-split.
  //   * arguments JSON object -> parameter buffer via ApplyJsonValueToProperty
  //     reflection helper (already used by the rest of the bridge for
  //     get/set_property). Covers int/float/double/bool/string/name/enum/
  //     struct/object refs via the shared applier.
  //   * return value read-back for functions with a return parm -- exports
  //     via ExportPropertyToJsonValue so callers see the result inline.
  FString ActorName, ObjectPath, FunctionName;
  Payload->TryGetStringField(TEXT("actorName"),    ActorName);
  Payload->TryGetStringField(TEXT("objectPath"),   ObjectPath);
  Payload->TryGetStringField(TEXT("functionName"), FunctionName);

  if (FunctionName.IsEmpty() || (ActorName.IsEmpty() && ObjectPath.IsEmpty())) {
    SendAutomationError(Socket, RequestId,
        TEXT("functionName and one of actorName/objectPath are required"),
        TEXT("MISSING_PARAM"));
    return true;
  }

  // Try actorName first (in-world spawned actor). If that fails or wasn't
  // provided, fall through to objectPath (any UObject). Inspect.cpp's alias
  // shim sometimes copies objectPath into actorName, so a failed actor lookup
  // is not the same as a failed request -- retry via StaticLoadObject.
  UObject* TargetObject = nullptr;
  if (!ActorName.IsEmpty()) {
    TargetObject = FindActorByName(ActorName);
  }
  if (!TargetObject && !ObjectPath.IsEmpty()) {
    TargetObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
  }
  if (!TargetObject && !ActorName.IsEmpty() && ActorName.StartsWith(TEXT("/"))) {
    // actorName was really an object path in disguise.
    TargetObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ActorName);
  }
  if (!TargetObject) {
    SendAutomationError(Socket, RequestId,
        FString::Printf(TEXT("Target not found (actorName='%s', objectPath='%s')"),
            *ActorName, *ObjectPath),
        TEXT("TARGET_NOT_FOUND"));
    return true;
  }

  UFunction* Function = TargetObject->FindFunction(FName(*FunctionName));
  if (!Function) {
    // Case-insensitive fallback so "SetHealth" reaches "setHealth" too.
    for (TFieldIterator<UFunction> It(TargetObject->GetClass()); It; ++It) {
      if (It->GetName().Equals(FunctionName, ESearchCase::IgnoreCase)) {
        Function = *It;
        break;
      }
    }
  }
  if (!Function) {
    SendAutomationError(Socket, RequestId,
        FString::Printf(TEXT("Function '%s' not found on %s (%s)"),
            *FunctionName, *TargetObject->GetName(),
            *TargetObject->GetClass()->GetName()),
        TEXT("FUNCTION_NOT_FOUND"));
    return true;
  }

  void* ParmsBuffer = nullptr;
  if (Function->ParmsSize > 0) {
    ParmsBuffer = FMemory::Malloc(Function->ParmsSize, 16);
    FMemory::Memzero(ParmsBuffer, Function->ParmsSize);

    // Populate parameters from the arguments JSON object.
    const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
    if (Payload->TryGetObjectField(TEXT("arguments"), ArgsPtr)
        && ArgsPtr && (*ArgsPtr).IsValid()) {
      for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt) {
        FProperty* Param = *ParamIt;
        if (!Param || !(Param->PropertyFlags & CPF_Parm)) continue;
        if (Param->PropertyFlags & CPF_ReturnParm) continue;

        const TSharedPtr<FJsonValue> ParamVal =
            (*ArgsPtr)->TryGetField(Param->GetName());
        if (!ParamVal.IsValid()) continue;

        FString ApplyErr;
        if (!ApplyJsonValueToProperty(ParmsBuffer, Param, ParamVal, ApplyErr)) {
          // A silently zeroed parameter is a debugging tarpit (a zero FGuid
          // "runs fine" and just finds nothing). Fail the call loudly.
          FMemory::Free(ParmsBuffer);
          SendAutomationError(Socket, RequestId,
              FString::Printf(TEXT("Parameter '%s' (%s) failed conversion: %s"),
                  *Param->GetName(), *Param->GetClass()->GetName(),
                  ApplyErr.IsEmpty() ? TEXT("unsupported type") : *ApplyErr),
              TEXT("PARAM_CONVERSION_FAILED"));
          return true;
        }
      }
    }
  }

  TargetObject->ProcessEvent(Function, ParmsBuffer);

  // Optional targeted save: BlueprintCallable mutators (AddNode/AddConnection
  // on graph assets, etc.) dirty the package but never write it, and
  // control_editor save_all deterministically wedges the game thread on some
  // project states (TacticalBattler forge box, 7k+ uncontrolled assets). Save
  // the ONE affected package via the reliable SavePackagesForObjects path.
  bool bSaveRequested = false;
  Payload->TryGetBoolField(TEXT("save"), bSaveRequested);
  bool bSaved = false;
  if (bSaveRequested) {
    bSaved = McpSafeOperations::McpSafeAssetSave(TargetObject);
  }

  TSharedPtr<FJsonObject> Data = McpHandlerUtils::CreateResultObject();
  Data->SetStringField(TEXT("objectPath"),   TargetObject->GetPathName());
  Data->SetStringField(TEXT("functionName"), Function->GetName());
  if (bSaveRequested) {
    Data->SetBoolField(TEXT("saved"), bSaved);
  }
  if (!ActorName.IsEmpty()) {
    Data->SetStringField(TEXT("actorName"), ActorName);
  }

  // Read back the return parm if the function has one.
  if (ParmsBuffer) {
    for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt) {
      FProperty* Param = *ParamIt;
      if (!Param || !(Param->PropertyFlags & CPF_ReturnParm)) continue;
      if (TSharedPtr<FJsonValue> Ret =
              ExportPropertyToJsonValue(ParmsBuffer, Param)) {
        Data->SetField(TEXT("returnValue"), Ret);
      }
      break;
    }
    FMemory::Free(ParmsBuffer);
  }

  SendStandardSuccessResponse(this, Socket, RequestId, TEXT("Function called"), Data);
  return true;
#else
  return false;
#endif
}

// P24 Gate 2: TMap property editing on any UObject. Handles 4 sub-actions:
//   list_map_entries -> dump every (key,value) pair as JSON
//   add_map_entry / set_map_entry -> insert or update by key
//   remove_map_entry -> delete by key
// All resolved via objectPath (any UObject) with actorName fallback for the
// spawned-actor case. Uses ApplyJsonValueToProperty / ExportPropertyToJsonValue
// so key + value support the full property-type set the rest of the bridge
// already handles (int/float/bool/string/name/enum/struct/object refs).
bool UMcpAutomationBridgeSubsystem::HandleControlActorMapEntries(
    const FString &RequestId, const FString &SubAction,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  const FString LowerSub = SubAction.ToLower();

  FString ActorName, ObjectPath, PropName;
  Payload->TryGetStringField(TEXT("actorName"),    ActorName);
  Payload->TryGetStringField(TEXT("objectPath"),   ObjectPath);
  Payload->TryGetStringField(TEXT("propertyName"), PropName);
  if (PropName.IsEmpty()) {
    Payload->TryGetStringField(TEXT("propertyPath"), PropName);
  }
  if (PropName.IsEmpty()) {
    SendAutomationError(Socket, RequestId,
        TEXT("propertyName is required"), TEXT("INVALID_ARGUMENT"));
    return true;
  }
  if (ActorName.IsEmpty() && ObjectPath.IsEmpty()) {
    SendAutomationError(Socket, RequestId,
        TEXT("actorName or objectPath is required"), TEXT("MISSING_PARAM"));
    return true;
  }

  // Same fallback shape as HandleControlActorCallFunction -- see comment there.
  UObject* TargetObject = nullptr;
  if (!ActorName.IsEmpty()) {
    TargetObject = FindActorByName(ActorName);
  }
  if (!TargetObject && !ObjectPath.IsEmpty()) {
    TargetObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
  }
  if (!TargetObject && !ActorName.IsEmpty() && ActorName.StartsWith(TEXT("/"))) {
    TargetObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ActorName);
  }
  if (!TargetObject) {
    SendAutomationError(Socket, RequestId,
        FString::Printf(TEXT("Target not found (actorName='%s', objectPath='%s')"),
            *ActorName, *ObjectPath),
        TEXT("TARGET_NOT_FOUND"));
    return true;
  }

  FProperty* Prop = TargetObject->GetClass()->FindPropertyByName(FName(*PropName));
  if (!Prop) {
    SendAutomationError(Socket, RequestId,
        FString::Printf(TEXT("Property '%s' not found on %s"),
            *PropName, *TargetObject->GetClass()->GetName()),
        TEXT("UNKNOWN_PROPERTY"));
    return true;
  }
  FMapProperty* MP = CastField<FMapProperty>(Prop);
  if (!MP) {
    SendAutomationError(Socket, RequestId,
        FString::Printf(TEXT("Property '%s' is not a TMap (got %s)"),
            *PropName, *Prop->GetClass()->GetName()),
        TEXT("NOT_A_MAP"));
    return true;
  }

  void* MapAddr = MP->ContainerPtrToValuePtr<void>(TargetObject);
  FScriptMapHelper MapHelper(MP, MapAddr);

  // list_map_entries: dump the whole map, no key required.
  if (LowerSub.Equals(TEXT("list_map_entries"))) {
    TArray<TSharedPtr<FJsonValue>> Entries;
    for (int32 i = 0; i < MapHelper.GetMaxIndex(); ++i) {
      if (!MapHelper.IsValidIndex(i)) continue;
      TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
      Entry->SetField(TEXT("key"),
          ExportPropertyToJsonValue(MapHelper.GetKeyPtr(i),   MP->KeyProp));
      Entry->SetField(TEXT("value"),
          ExportPropertyToJsonValue(MapHelper.GetValuePtr(i), MP->ValueProp));
      Entries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    TSharedPtr<FJsonObject> Resp = McpHandlerUtils::CreateResultObject();
    Resp->SetStringField(TEXT("objectPath"),   TargetObject->GetPathName());
    Resp->SetStringField(TEXT("propertyName"), PropName);
    Resp->SetArrayField(TEXT("entries"),       Entries);
    Resp->SetNumberField(TEXT("count"),        MapHelper.Num());
    Resp->SetStringField(TEXT("keyType"),      MP->KeyProp->GetClass()->GetName());
    Resp->SetStringField(TEXT("valueType"),    MP->ValueProp->GetClass()->GetName());
    SendStandardSuccessResponse(this, Socket, RequestId,
        TEXT("Map entries listed"), Resp);
    return true;
  }

  // add / set / remove all need a key. Parse it into a scratch buffer.
  TSharedPtr<FJsonValue> KeyJson = Payload->TryGetField(TEXT("key"));
  if (!KeyJson.IsValid()) {
    SendAutomationError(Socket, RequestId,
        TEXT("'key' is required"), TEXT("INVALID_ARGUMENT"));
    return true;
  }

  const int32 KeySize  = MP->KeyProp->GetElementSize();
  const int32 KeyAlign = MP->KeyProp->GetMinAlignment();
  void* KeyBuf = FMemory::Malloc(KeySize, KeyAlign);
  MP->KeyProp->InitializeValue(KeyBuf);

  FString ApplyErr;
  if (!ApplyJsonValueToProperty(KeyBuf, MP->KeyProp, KeyJson, ApplyErr)) {
    MP->KeyProp->DestroyValue(KeyBuf);
    FMemory::Free(KeyBuf);
    SendAutomationError(Socket, RequestId,
        FString::Printf(TEXT("Failed to parse key: %s"), *ApplyErr),
        TEXT("INVALID_KEY"));
    return true;
  }

  // Locate existing entry (for update / remove).
  int32 FoundIdx = INDEX_NONE;
  for (int32 i = 0; i < MapHelper.GetMaxIndex(); ++i) {
    if (!MapHelper.IsValidIndex(i)) continue;
    if (MP->KeyProp->Identical(KeyBuf, MapHelper.GetKeyPtr(i))) {
      FoundIdx = i;
      break;
    }
  }

  if (LowerSub.Equals(TEXT("remove_map_entry"))) {
    const bool bRemoved = (FoundIdx != INDEX_NONE);
    if (bRemoved) {
      MapHelper.RemoveAt(FoundIdx);
      TargetObject->MarkPackageDirty();
    }
    MP->KeyProp->DestroyValue(KeyBuf);
    FMemory::Free(KeyBuf);
    TSharedPtr<FJsonObject> Resp = McpHandlerUtils::CreateResultObject();
    Resp->SetStringField(TEXT("objectPath"),   TargetObject->GetPathName());
    Resp->SetStringField(TEXT("propertyName"), PropName);
    Resp->SetBoolField(TEXT("removed"),        bRemoved);
    Resp->SetNumberField(TEXT("mapSize"),      MapHelper.Num());
    SendStandardSuccessResponse(this, Socket, RequestId,
        bRemoved ? TEXT("Map entry removed") : TEXT("Map entry not found (no-op)"),
        Resp);
    return true;
  }

  // add_map_entry / set_map_entry -- need a value.
  TSharedPtr<FJsonValue> ValJson = Payload->TryGetField(TEXT("value"));
  if (!ValJson.IsValid()) {
    MP->KeyProp->DestroyValue(KeyBuf);
    FMemory::Free(KeyBuf);
    SendAutomationError(Socket, RequestId,
        TEXT("'value' is required"), TEXT("INVALID_ARGUMENT"));
    return true;
  }

  const int32 ValSize  = MP->ValueProp->GetElementSize();
  const int32 ValAlign = MP->ValueProp->GetMinAlignment();
  void* ValBuf = FMemory::Malloc(ValSize, ValAlign);
  MP->ValueProp->InitializeValue(ValBuf);

  if (!ApplyJsonValueToProperty(ValBuf, MP->ValueProp, ValJson, ApplyErr)) {
    MP->KeyProp->DestroyValue(KeyBuf);   FMemory::Free(KeyBuf);
    MP->ValueProp->DestroyValue(ValBuf); FMemory::Free(ValBuf);
    SendAutomationError(Socket, RequestId,
        FString::Printf(TEXT("Failed to parse value: %s"), *ApplyErr),
        TEXT("INVALID_VALUE"));
    return true;
  }

  bool bUpdate = false;
  if (FoundIdx != INDEX_NONE) {
    MP->ValueProp->CopyCompleteValue(MapHelper.GetValuePtr(FoundIdx), ValBuf);
    bUpdate = true;
  } else {
    const int32 NewIdx = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
    MP->KeyProp  ->CopyCompleteValue(MapHelper.GetKeyPtr(NewIdx),   KeyBuf);
    MP->ValueProp->CopyCompleteValue(MapHelper.GetValuePtr(NewIdx), ValBuf);
    MapHelper.Rehash();
  }

  MP->KeyProp->DestroyValue(KeyBuf);   FMemory::Free(KeyBuf);
  MP->ValueProp->DestroyValue(ValBuf); FMemory::Free(ValBuf);
  TargetObject->MarkPackageDirty();

  TSharedPtr<FJsonObject> Resp = McpHandlerUtils::CreateResultObject();
  Resp->SetStringField(TEXT("objectPath"),   TargetObject->GetPathName());
  Resp->SetStringField(TEXT("propertyName"), PropName);
  Resp->SetBoolField(TEXT("updated"),        bUpdate);
  Resp->SetStringField(TEXT("operation"),    bUpdate ? TEXT("update") : TEXT("add"));
  Resp->SetNumberField(TEXT("mapSize"),      MapHelper.Num());
  SendStandardSuccessResponse(this, Socket, RequestId,
      bUpdate ? TEXT("Map entry updated") : TEXT("Map entry added"), Resp);
  return true;
#else
  return false;
#endif
}
