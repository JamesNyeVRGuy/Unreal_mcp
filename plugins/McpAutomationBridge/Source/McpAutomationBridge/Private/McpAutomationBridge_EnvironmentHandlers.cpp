#include "Dom/JsonObject.h"
#include "McpAutomationBridgeGlobals.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Slate/SceneViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Selection.h"

#if __has_include("Subsystems/EditorActorSubsystem.h")
#include "Subsystems/EditorActorSubsystem.h"
#elif __has_include("EditorActorSubsystem.h")
#include "EditorActorSubsystem.h"
#endif
#if __has_include("Subsystems/UnrealEditorSubsystem.h")
#include "Subsystems/UnrealEditorSubsystem.h"
#elif __has_include("UnrealEditorSubsystem.h")
#include "UnrealEditorSubsystem.h"
#endif
#if __has_include("Subsystems/LevelEditorSubsystem.h")
#include "Subsystems/LevelEditorSubsystem.h"
#elif __has_include("LevelEditorSubsystem.h")
#include "LevelEditorSubsystem.h"
#endif
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "EditorValidatorSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GeneralProjectSettings.h"
#include "KismetProceduralMeshLibrary.h"
#include "Misc/FileHelper.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"

// Landscape includes
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeGrassType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "Kismet2/BlueprintEditorUtils.h"

#endif

bool UMcpAutomationBridgeSubsystem::HandleBuildEnvironmentAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  const FString Lower = Action.ToLower();
  if (!Lower.Equals(TEXT("build_environment"), ESearchCase::IgnoreCase) &&
      !Lower.StartsWith(TEXT("build_environment")))
    return false;

  if (!Payload.IsValid()) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("build_environment payload missing."),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  FString SubAction;
  Payload->TryGetStringField(TEXT("action"), SubAction);
  const FString LowerSub = SubAction.ToLower();

  // Fast-path foliage sub-actions to dedicated native handlers to avoid double
  // responses
  if (LowerSub == TEXT("add_foliage_instances")) {
    // Transform from build_environment schema to foliage handler schema
    FString FoliageTypePath;
    Payload->TryGetStringField(TEXT("foliageType"), FoliageTypePath);
    const TArray<TSharedPtr<FJsonValue>> *Transforms = nullptr;
    Payload->TryGetArrayField(TEXT("transforms"), Transforms);
    TSharedPtr<FJsonObject> FoliagePayload = MakeShared<FJsonObject>();
    if (!FoliageTypePath.IsEmpty()) {
      FoliagePayload->SetStringField(TEXT("foliageTypePath"), FoliageTypePath);
    }
    TArray<TSharedPtr<FJsonValue>> Locations;
    if (Transforms) {
      for (const TSharedPtr<FJsonValue> &V : *Transforms) {
        if (!V.IsValid() || V->Type != EJson::Object)
          continue;
        const TSharedPtr<FJsonObject> *TObj = nullptr;
        if (!V->TryGetObject(TObj) || !TObj)
          continue;
        const TSharedPtr<FJsonObject> *LocObj = nullptr;
        if (!(*TObj)->TryGetObjectField(TEXT("location"), LocObj) || !LocObj)
          continue;
        double X = 0, Y = 0, Z = 0;
        (*LocObj)->TryGetNumberField(TEXT("x"), X);
        (*LocObj)->TryGetNumberField(TEXT("y"), Y);
        (*LocObj)->TryGetNumberField(TEXT("z"), Z);
        TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
        L->SetNumberField(TEXT("x"), X);
        L->SetNumberField(TEXT("y"), Y);
        L->SetNumberField(TEXT("z"), Z);
        Locations.Add(MakeShared<FJsonValueObject>(L));
      }
    }
    FoliagePayload->SetArrayField(TEXT("locations"), Locations);
    return HandlePaintFoliage(RequestId, TEXT("paint_foliage"), FoliagePayload,
                              RequestingSocket);
  } else if (LowerSub == TEXT("get_foliage_instances")) {
    FString FoliageTypePath;
    Payload->TryGetStringField(TEXT("foliageType"), FoliageTypePath);
    TSharedPtr<FJsonObject> FoliagePayload = MakeShared<FJsonObject>();
    if (!FoliageTypePath.IsEmpty()) {
      FoliagePayload->SetStringField(TEXT("foliageTypePath"), FoliageTypePath);
    }
    return HandleGetFoliageInstances(RequestId, TEXT("get_foliage_instances"),
                                     FoliagePayload, RequestingSocket);
  } else if (LowerSub == TEXT("remove_foliage")) {
    FString FoliageTypePath;
    Payload->TryGetStringField(TEXT("foliageType"), FoliageTypePath);
    bool bRemoveAll = false;
    Payload->TryGetBoolField(TEXT("removeAll"), bRemoveAll);
    TSharedPtr<FJsonObject> FoliagePayload = MakeShared<FJsonObject>();
    if (!FoliageTypePath.IsEmpty()) {
      FoliagePayload->SetStringField(TEXT("foliageTypePath"), FoliageTypePath);
    }
    FoliagePayload->SetBoolField(TEXT("removeAll"), bRemoveAll);
    return HandleRemoveFoliage(RequestId, TEXT("remove_foliage"),
                               FoliagePayload, RequestingSocket);
  } else if (LowerSub == TEXT("paint_foliage")) {
    // Direct dispatch to foliage handler (payload already in correct format)
    return HandlePaintFoliage(RequestId, TEXT("paint_foliage"), Payload,
                              RequestingSocket);
  } else if (LowerSub == TEXT("create_procedural_foliage")) {
    // Dispatch to procedural foliage handler
    return HandleCreateProceduralFoliage(RequestId,
                                         TEXT("create_procedural_foliage"),
                                         Payload, RequestingSocket);
  } else if (LowerSub == TEXT("create_procedural_terrain")) {
    // Dispatch to procedural terrain handler
    return HandleCreateProceduralTerrain(RequestId,
                                         TEXT("create_procedural_terrain"),
                                         Payload, RequestingSocket);
  } else if (LowerSub == TEXT("add_foliage_type") || LowerSub == TEXT("add_foliage")) {
    // Dispatch to foliage type handler
    return HandleAddFoliageType(RequestId, TEXT("add_foliage_type"),
                                Payload, RequestingSocket);
  } else if (LowerSub == TEXT("create_landscape")) {
    // Dispatch to landscape creation handler
    return HandleCreateLandscape(RequestId, TEXT("create_landscape"),
                                 Payload, RequestingSocket);
  }
  // Dispatch landscape operations
  else if (LowerSub == TEXT("paint_landscape") ||
           LowerSub == TEXT("paint_landscape_layer")) {
    return HandlePaintLandscapeLayer(RequestId, TEXT("paint_landscape_layer"),
                                     Payload, RequestingSocket);
  } else if (LowerSub == TEXT("sculpt_landscape") || LowerSub == TEXT("sculpt")) {
    return HandleSculptLandscape(RequestId, TEXT("sculpt_landscape"), Payload,
                                 RequestingSocket);
  } else if (LowerSub == TEXT("modify_heightmap")) {
    return HandleModifyHeightmap(RequestId, TEXT("modify_heightmap"), Payload,
                                 RequestingSocket);
  } else if (LowerSub == TEXT("set_landscape_material")) {
    return HandleSetLandscapeMaterial(RequestId, TEXT("set_landscape_material"),
                                      Payload, RequestingSocket);
  } else if (LowerSub == TEXT("create_landscape_grass_type")) {
    return HandleCreateLandscapeGrassType(RequestId,
                                          TEXT("create_landscape_grass_type"),
                                          Payload, RequestingSocket);
  } else if (LowerSub == TEXT("generate_lods")) {
    return HandleGenerateLODs(RequestId, TEXT("generate_lods"), Payload,
                              RequestingSocket);
  } else if (LowerSub == TEXT("bake_lightmap")) {
    return HandleBakeLightmap(RequestId, TEXT("bake_lightmap"), Payload,
                              RequestingSocket);
  }

#if WITH_EDITOR
  TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
  Resp->SetStringField(TEXT("action"), LowerSub);
  bool bSuccess = true;
  FString Message =
      FString::Printf(TEXT("Environment action '%s' completed"), *LowerSub);
  FString ErrorCode;

  if (LowerSub == TEXT("export_snapshot")) {
    FString Path;
    Payload->TryGetStringField(TEXT("path"), Path);
    if (Path.IsEmpty()) {
      bSuccess = false;
      Message = TEXT("path required for export_snapshot");
      ErrorCode = TEXT("INVALID_ARGUMENT");
      Resp->SetStringField(TEXT("error"), Message);
    } else {
      // SECURITY: Validate file path to prevent directory traversal and arbitrary file access
      // Use SanitizeProjectFilePath for file operations (accepts /Temp, /Saved, etc.)
      FString SafePath = SanitizeProjectFilePath(Path);
      if (SafePath.IsEmpty()) {
        bSuccess = false;
        Message = FString::Printf(TEXT("Invalid or unsafe path: %s. Path must be relative to project (e.g., /Temp/snapshot.json)"), *Path);
        ErrorCode = TEXT("SECURITY_VIOLATION");
        Resp->SetStringField(TEXT("error"), Message);
      } else {
        // Convert project-relative path to absolute file path
        FString AbsolutePath = FPaths::ProjectDir() / SafePath;
        FPaths::MakeStandardFilename(AbsolutePath);
        
        TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
        Snapshot->SetStringField(TEXT("timestamp"),
                                 FDateTime::UtcNow().ToString());
        Snapshot->SetStringField(TEXT("type"), TEXT("environment_snapshot"));

        FString JsonString;
        TSharedRef<TJsonWriter<>> Writer =
            TJsonWriterFactory<>::Create(&JsonString);
        if (FJsonSerializer::Serialize(Snapshot.ToSharedRef(), Writer)) {
          if (FFileHelper::SaveStringToFile(JsonString, *AbsolutePath)) {
            Resp->SetStringField(TEXT("exportPath"), SafePath);
            Resp->SetStringField(TEXT("message"), TEXT("Snapshot exported"));
          } else {
            bSuccess = false;
            Message = TEXT("Failed to write snapshot file");
            ErrorCode = TEXT("WRITE_FAILED");
            Resp->SetStringField(TEXT("error"), Message);
          }
        } else {
          bSuccess = false;
          Message = TEXT("Failed to serialize snapshot");
          ErrorCode = TEXT("SERIALIZE_FAILED");
          Resp->SetStringField(TEXT("error"), Message);
        }
      }
    }
  } else if (LowerSub == TEXT("import_snapshot")) {
    FString Path;
    Payload->TryGetStringField(TEXT("path"), Path);
    if (Path.IsEmpty()) {
      bSuccess = false;
      Message = TEXT("path required for import_snapshot");
      ErrorCode = TEXT("INVALID_ARGUMENT");
      Resp->SetStringField(TEXT("error"), Message);
    } else {
      // SECURITY: Validate file path to prevent directory traversal and arbitrary file access
      // Use SanitizeProjectFilePath for file operations (accepts /Temp, /Saved, etc.)
      FString SafePath = SanitizeProjectFilePath(Path);
      if (SafePath.IsEmpty()) {
        bSuccess = false;
        Message = FString::Printf(TEXT("Invalid or unsafe path: %s. Path must be relative to project (e.g., /Temp/snapshot.json)"), *Path);
        ErrorCode = TEXT("SECURITY_VIOLATION");
        Resp->SetStringField(TEXT("error"), Message);
      } else {
        // Convert project-relative path to absolute file path
        FString AbsolutePath = FPaths::ProjectDir() / SafePath;
        FPaths::MakeStandardFilename(AbsolutePath);
        
        FString JsonString;
        if (!FFileHelper::LoadFileToString(JsonString, *AbsolutePath)) {
          bSuccess = false;
          Message = TEXT("Failed to read snapshot file");
          ErrorCode = TEXT("LOAD_FAILED");
          Resp->SetStringField(TEXT("error"), Message);
        } else {
          TSharedPtr<FJsonObject> SnapshotObj;
          TSharedRef<TJsonReader<>> Reader =
              TJsonReaderFactory<>::Create(JsonString);
          if (!FJsonSerializer::Deserialize(Reader, SnapshotObj) ||
              !SnapshotObj.IsValid()) {
            bSuccess = false;
            Message = TEXT("Failed to parse snapshot");
            ErrorCode = TEXT("PARSE_FAILED");
            Resp->SetStringField(TEXT("error"), Message);
          } else {
            Resp->SetObjectField(TEXT("snapshot"), SnapshotObj.ToSharedRef());
            Resp->SetStringField(TEXT("message"), TEXT("Snapshot imported"));
          }
        }
      }
    }
  } else if (LowerSub == TEXT("delete")) {
    const TArray<TSharedPtr<FJsonValue>> *NamesArray = nullptr;
    if (!Payload->TryGetArrayField(TEXT("names"), NamesArray) || !NamesArray) {
      bSuccess = false;
      Message = TEXT("names array required for delete");
      ErrorCode = TEXT("INVALID_ARGUMENT");
      Resp->SetStringField(TEXT("error"), Message);
    } else if (!GEditor) {
      bSuccess = false;
      Message = TEXT("Editor not available");
      ErrorCode = TEXT("EDITOR_NOT_AVAILABLE");
      Resp->SetStringField(TEXT("error"), Message);
    } else {
      UEditorActorSubsystem *ActorSS =
          GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
      if (!ActorSS) {
        bSuccess = false;
        Message = TEXT("EditorActorSubsystem not available");
        ErrorCode = TEXT("EDITOR_ACTOR_SUBSYSTEM_MISSING");
        Resp->SetStringField(TEXT("error"), Message);
      } else {
        TArray<FString> Deleted;
        TArray<FString> Missing;
        for (const TSharedPtr<FJsonValue> &Val : *NamesArray) {
          if (Val.IsValid() && Val->Type == EJson::String) {
            FString Name = Val->AsString();
            TArray<AActor *> AllActors = ActorSS->GetAllLevelActors();
            bool bRemoved = false;
            for (AActor *A : AllActors) {
              if (A &&
                  A->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase)) {
                if (ActorSS->DestroyActor(A)) {
                  Deleted.Add(Name);
                  bRemoved = true;
                }
                break;
              }
            }
            if (!bRemoved) {
              Missing.Add(Name);
            }
          }
        }

        TArray<TSharedPtr<FJsonValue>> DeletedArray;
        for (const FString &Name : Deleted) {
          DeletedArray.Add(MakeShared<FJsonValueString>(Name));
        }
        Resp->SetArrayField(TEXT("deleted"), DeletedArray);
        Resp->SetNumberField(TEXT("deletedCount"), Deleted.Num());

        if (Missing.Num() > 0) {
          TArray<TSharedPtr<FJsonValue>> MissingArray;
          for (const FString &Name : Missing) {
            MissingArray.Add(MakeShared<FJsonValueString>(Name));
          }
          Resp->SetArrayField(TEXT("missing"), MissingArray);
          bSuccess = false;
          Message = TEXT("Some environment actors could not be removed");
          ErrorCode = TEXT("DELETE_PARTIAL");
          Resp->SetStringField(TEXT("error"), Message);
        } else {
          Message = TEXT("Environment actors deleted");
        }
      }
    }
  } else if (LowerSub == TEXT("create_sky_sphere")) {
    if (GEditor) {
      UClass *SkySphereClass = LoadClass<AActor>(
          nullptr, TEXT("/Script/Engine.Blueprint'/Engine/Maps/Templates/"
                        "SkySphere.SkySphere_C'"));
      if (SkySphereClass) {
        AActor *SkySphere = SpawnActorInActiveWorld<AActor>(
            SkySphereClass, FVector::ZeroVector, FRotator::ZeroRotator,
            TEXT("SkySphere"));
        if (SkySphere) {
          bSuccess = true;
          Message = TEXT("Sky sphere created");
          Resp->SetStringField(TEXT("actorName"), SkySphere->GetActorLabel());
        }
      }
    }
    if (!bSuccess) {
      bSuccess = false;
      Message = TEXT("Failed to create sky sphere");
      ErrorCode = TEXT("CREATION_FAILED");
    }
  } else if (LowerSub == TEXT("set_time_of_day")) {
    float TimeOfDay = 12.0f;
    Payload->TryGetNumberField(TEXT("time"), TimeOfDay);

    if (GEditor) {
      UEditorActorSubsystem *ActorSS =
          GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
      if (ActorSS) {
        for (AActor *Actor : ActorSS->GetAllLevelActors()) {
          if (Actor->GetClass()->GetName().Contains(TEXT("SkySphere"))) {
            UFunction *SetTimeFunction =
                Actor->FindFunction(TEXT("SetTimeOfDay"));
            if (SetTimeFunction) {
              float TimeParam = TimeOfDay;
              Actor->ProcessEvent(SetTimeFunction, &TimeParam);
              bSuccess = true;
              Message =
                  FString::Printf(TEXT("Time of day set to %.2f"), TimeOfDay);
              break;
            }
          }
        }
      }
    }
    if (!bSuccess) {
      bSuccess = false;
      Message = TEXT("Sky sphere not found or time function not available");
      ErrorCode = TEXT("SET_TIME_FAILED");
    }
  } else if (LowerSub == TEXT("create_fog_volume")) {
    FVector Location(0, 0, 0);
    Payload->TryGetNumberField(TEXT("x"), Location.X);
    Payload->TryGetNumberField(TEXT("y"), Location.Y);
    Payload->TryGetNumberField(TEXT("z"), Location.Z);

    if (GEditor) {
      UClass *FogClass = LoadClass<AActor>(
          nullptr, TEXT("/Script/Engine.ExponentialHeightFog"));
      if (FogClass) {
        AActor *FogVolume = SpawnActorInActiveWorld<AActor>(
            FogClass, Location, FRotator::ZeroRotator, TEXT("FogVolume"));
        if (FogVolume) {
          bSuccess = true;
          Message = TEXT("Fog volume created");
          Resp->SetStringField(TEXT("actorName"), FogVolume->GetActorLabel());
        }
      }
    }
    if (!bSuccess) {
      bSuccess = false;
      Message = TEXT("Failed to create fog volume");
      ErrorCode = TEXT("CREATION_FAILED");
    }
  } else {
    bSuccess = false;
    Message = FString::Printf(TEXT("Environment action '%s' not implemented"),
                              *LowerSub);
    ErrorCode = TEXT("NOT_IMPLEMENTED");
    Resp->SetStringField(TEXT("error"), Message);
  }

  Resp->SetBoolField(TEXT("success"), bSuccess);
  SendAutomationResponse(RequestingSocket, RequestId, bSuccess, Message, Resp,
                         ErrorCode);
  return true;
#else
  SendAutomationResponse(
      RequestingSocket, RequestId, false,
      TEXT("Environment building actions require editor build."), nullptr,
      TEXT("NOT_IMPLEMENTED"));
  return true;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleControlEnvironmentAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  const FString Lower = Action.ToLower();
  if (!Lower.Equals(TEXT("control_environment"), ESearchCase::IgnoreCase) &&
      !Lower.StartsWith(TEXT("control_environment"))) {
    return false;
  }

  if (!Payload.IsValid()) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("control_environment payload missing."),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  FString SubAction;
  Payload->TryGetStringField(TEXT("action"), SubAction);
  const FString LowerSub = SubAction.ToLower();

#if WITH_EDITOR
  auto SendResult = [&](bool bSuccess, const TCHAR *Message,
                        const FString &ErrorCode,
                        const TSharedPtr<FJsonObject> &Result) {
    if (bSuccess) {
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             Message ? Message
                                     : TEXT("Environment control succeeded."),
                             Result, FString());
    } else {
      SendAutomationResponse(RequestingSocket, RequestId, false,
                             Message ? Message
                                     : TEXT("Environment control failed."),
                             Result, ErrorCode);
    }
  };

  UWorld *World = nullptr;
  if (GEditor) {
    World = GEditor->GetEditorWorldContext().World();
  }

  if (!World) {
    SendResult(false, TEXT("Editor world is unavailable"),
               TEXT("WORLD_NOT_AVAILABLE"), nullptr);
    return true;
  }

  auto FindFirstDirectionalLight = [&]() -> ADirectionalLight * {
    for (TActorIterator<ADirectionalLight> It(World); It; ++It) {
      if (ADirectionalLight *Light = *It) {
        if (IsValid(Light)) {
          return Light;
        }
      }
    }
    return nullptr;
  };

  auto FindFirstSkyLight = [&]() -> ASkyLight * {
    for (TActorIterator<ASkyLight> It(World); It; ++It) {
      if (ASkyLight *Sky = *It) {
        if (IsValid(Sky)) {
          return Sky;
        }
      }
    }
    return nullptr;
  };

  if (LowerSub == TEXT("set_time_of_day")) {
    double Hour = 0.0;
    const bool bHasHour = Payload->TryGetNumberField(TEXT("hour"), Hour);
    if (!bHasHour) {
      SendResult(false, TEXT("Missing hour parameter"),
                 TEXT("INVALID_ARGUMENT"), nullptr);
      return true;
    }

    ADirectionalLight *SunLight = FindFirstDirectionalLight();
    if (!SunLight) {
      SendResult(false, TEXT("No directional light found"),
                 TEXT("SUN_NOT_FOUND"), nullptr);
      return true;
    }

    const float ClampedHour =
        FMath::Clamp(static_cast<float>(Hour), 0.0f, 24.0f);
    const float SolarPitch = (ClampedHour / 24.0f) * 360.0f - 90.0f;

    SunLight->Modify();
    FRotator NewRotation = SunLight->GetActorRotation();
    NewRotation.Pitch = SolarPitch;
    SunLight->SetActorRotation(NewRotation);

    if (UDirectionalLightComponent *LightComp =
            Cast<UDirectionalLightComponent>(SunLight->GetLightComponent())) {
      LightComp->MarkRenderStateDirty();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("hour"), ClampedHour);
    Result->SetNumberField(TEXT("pitch"), SolarPitch);
    Result->SetStringField(TEXT("actor"), SunLight->GetPathName());
    
    // Add verification data
    AddActorVerification(Result, SunLight);
    
    SendResult(true, TEXT("Time of day updated"), FString(), Result);
    return true;
  }

  if (LowerSub == TEXT("set_sun_intensity")) {
    double Intensity = 0.0;
    if (!Payload->TryGetNumberField(TEXT("intensity"), Intensity)) {
      SendResult(false, TEXT("Missing intensity parameter"),
                 TEXT("INVALID_ARGUMENT"), nullptr);
      return true;
    }

    ADirectionalLight *SunLight = FindFirstDirectionalLight();
    if (!SunLight) {
      SendResult(false, TEXT("No directional light found"),
                 TEXT("SUN_NOT_FOUND"), nullptr);
      return true;
    }

    if (UDirectionalLightComponent *LightComp =
            Cast<UDirectionalLightComponent>(SunLight->GetLightComponent())) {
      LightComp->SetIntensity(static_cast<float>(Intensity));
      LightComp->MarkRenderStateDirty();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("intensity"), Intensity);
    Result->SetStringField(TEXT("actor"), SunLight->GetPathName());
    SendResult(true, TEXT("Sun intensity updated"), FString(), Result);
    return true;
  }

  if (LowerSub == TEXT("set_skylight_intensity")) {
    double Intensity = 0.0;
    if (!Payload->TryGetNumberField(TEXT("intensity"), Intensity)) {
      SendResult(false, TEXT("Missing intensity parameter"),
                 TEXT("INVALID_ARGUMENT"), nullptr);
      return true;
    }

    ASkyLight *SkyActor = FindFirstSkyLight();
    if (!SkyActor) {
      SendResult(false, TEXT("No skylight found"), TEXT("SKYLIGHT_NOT_FOUND"),
                 nullptr);
      return true;
    }

    if (USkyLightComponent *SkyComp = SkyActor->GetLightComponent()) {
      SkyComp->SetIntensity(static_cast<float>(Intensity));
      SkyComp->MarkRenderStateDirty();
      SkyActor->MarkComponentsRenderStateDirty();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("intensity"), Intensity);
    Result->SetStringField(TEXT("actor"), SkyActor->GetPathName());
    SendResult(true, TEXT("Skylight intensity updated"), FString(), Result);
    return true;
  }

  TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
  Result->SetStringField(TEXT("action"), LowerSub);
  SendResult(false, TEXT("Unsupported environment control action"),
             TEXT("UNSUPPORTED_ACTION"), Result);
  return true;
#else
  SendAutomationResponse(RequestingSocket, RequestId, false,
                         TEXT("Environment control requires editor build"),
                         nullptr, TEXT("NOT_IMPLEMENTED"));
  return true;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleConsoleCommandAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  // Handle both direct "console_command" action and "system_control" with action="console_command" in payload
  const FString LowerAction = Action.ToLower();
  const bool bIsDirectConsoleCommand = LowerAction.Equals(TEXT("console_command"), ESearchCase::IgnoreCase);
  const bool bIsSystemControl = LowerAction.Equals(TEXT("system_control"), ESearchCase::IgnoreCase);
  
  if (!bIsDirectConsoleCommand && !bIsSystemControl) {
    return false;
  }

#if WITH_EDITOR
  // For system_control, check if the sub-action is console_command
  if (bIsSystemControl && Payload.IsValid()) {
    FString SubAction;
    Payload->TryGetStringField(TEXT("action"), SubAction);
    if (!SubAction.ToLower().Equals(TEXT("console_command"))) {
      return false; // Not a console_command, let other handlers try
    }
  }

  if (!Payload.IsValid()) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("console_command payload missing"),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  FString Command;
  if (!Payload->TryGetStringField(TEXT("command"), Command) ||
      Command.IsEmpty()) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("command field required"), TEXT("INVALID_ARGUMENT"));
    return true;
  }

  // Security: Block dangerous commands
  FString LowerCommand = Command.ToLower();
  
  // Whitelist safe commands that should bypass token filtering
  // "Log" is a read-only command that prints to console - always safe
  bool bIsWhitelistedCommand = LowerCommand.StartsWith(TEXT("log "));
  if (bIsWhitelistedCommand) {
    // Safe to execute - skip all security checks below
    GEngine->Exec(nullptr, *Command);
    
    TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
    Resp->SetStringField(TEXT("command"), Command);
    Resp->SetBoolField(TEXT("success"), true);
    
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Console command executed"), Resp, FString());
    return true;
  }
  
  // Block explicit dangerous commands
  TArray<FString> BlockedCommands = {
    TEXT("quit"), TEXT("exit"), TEXT("crash"), TEXT("shutdown"),
    TEXT("restart"), TEXT("reboot"), TEXT("debug exec"), TEXT("suicide"),
    TEXT("disconnect"), TEXT("reconnect")
  };
  
  for (const FString& Blocked : BlockedCommands) {
    if (LowerCommand.StartsWith(Blocked)) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Command '%s' is blocked for security"), *Blocked),
                          TEXT("COMMAND_BLOCKED"));
      return true;
    }
  }
  
  // Block destructive file operations
  // Note: These tokens have trailing spaces to avoid matching
  // valid MCP action names like "remove_volume" or "delete_actor"
  TArray<FString> BlockedTokens = {
    TEXT("rm "), TEXT("del "), TEXT("format"), TEXT("rmdir"), TEXT("rd "),
    TEXT("delete "), TEXT("remove "), TEXT("erase ")
  };
  
  for (const FString& Token : BlockedTokens) {
    if (LowerCommand.Contains(Token)) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Command contains blocked token '%s'"), *Token.TrimEnd()),
                          TEXT("COMMAND_BLOCKED"));
      return true;
    }
  }
  
  // Block command chaining and injection attempts
  if (LowerCommand.Contains(TEXT("&&")) || LowerCommand.Contains(TEXT("||")) ||
      LowerCommand.Contains(TEXT(";") ) || LowerCommand.Contains(TEXT("|`")) ||
      LowerCommand.Contains(TEXT("\n")) || LowerCommand.Contains(TEXT("\r"))) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("Command chaining and special characters are not allowed"),
                        TEXT("COMMAND_BLOCKED"));
    return true;
  }

  // Execute the console command
  GEngine->Exec(nullptr, *Command);

  TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
  Resp->SetStringField(TEXT("command"), Command);
  Resp->SetBoolField(TEXT("success"), true);
  Resp->SetBoolField(TEXT("executed"), true);

  SendAutomationResponse(RequestingSocket, RequestId, true,
                         TEXT("Console command executed"), Resp, FString());
  return true;
#else
  SendAutomationResponse(RequestingSocket, RequestId, false,
                         TEXT("console_command requires editor build"), nullptr,
                         TEXT("NOT_IMPLEMENTED"));
  return true;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleBakeLightmap(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  const FString Lower = Action.ToLower();
  if (!Lower.Equals(TEXT("bake_lightmap"), ESearchCase::IgnoreCase)) {
    return false;
  }

#if WITH_EDITOR
  FString QualityStr = TEXT("Preview");
  if (Payload.IsValid())
    Payload->TryGetStringField(TEXT("quality"), QualityStr);

  // Reuse HandleExecuteEditorFunction logic
  TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
  P->SetStringField(TEXT("functionName"), TEXT("BUILD_LIGHTING"));
  P->SetStringField(TEXT("quality"), QualityStr);

  return HandleExecuteEditorFunction(RequestId, TEXT("execute_editor_function"),
                                     P, RequestingSocket);

#else
  SendAutomationResponse(RequestingSocket, RequestId, false,
                         TEXT("Requires editor"), nullptr,
                         TEXT("NOT_IMPLEMENTED"));
  return true;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleCreateProceduralTerrain(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  const FString Lower = Action.ToLower();
  if (!Lower.Equals(TEXT("create_procedural_terrain"), ESearchCase::IgnoreCase)) {
    return false;
  }

#if WITH_EDITOR
  if (!GEditor) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("Editor not available"),
                        TEXT("EDITOR_NOT_AVAILABLE"));
    return true;
  }

  if (!Payload.IsValid()) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("create_procedural_terrain payload missing"),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  // Get terrain parameters
  int32 SizeX = 100;
  int32 SizeY = 100;
  double Spacing = 100.0;
  double HeightScale = 500.0;
  int32 Subdivisions = 50;
  FString ActorName = TEXT("ProceduralTerrain");
  
  Payload->TryGetNumberField(TEXT("sizeX"), SizeX);
  Payload->TryGetNumberField(TEXT("sizeY"), SizeY);
  Payload->TryGetNumberField(TEXT("spacing"), Spacing);
  Payload->TryGetNumberField(TEXT("heightScale"), HeightScale);
  Payload->TryGetNumberField(TEXT("subdivisions"), Subdivisions);
  Payload->TryGetStringField(TEXT("actorName"), ActorName);
  
  // Strict validation: reject empty actorName
  if (ActorName.IsEmpty()) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("actorName parameter is required for create_procedural_terrain"),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  // Validate actorName format (reject invalid characters)
  if (ActorName.Contains(TEXT("/")) || ActorName.Contains(TEXT("\\")) ||
      ActorName.Contains(TEXT(":")) || ActorName.Contains(TEXT("*")) ||
      ActorName.Contains(TEXT("?")) || ActorName.Contains(TEXT("\"")) ||
      ActorName.Contains(TEXT("<")) || ActorName.Contains(TEXT(">")) ||
      ActorName.Contains(TEXT("|"))) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("actorName contains invalid characters (/, \\, :, *, ?, \", <, >, |)"),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  // Validate actorName length
  if (ActorName.Len() > 128) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("actorName exceeds maximum length of 128 characters"),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }
  
  // Clamp values to reasonable limits
  SizeX = FMath::Clamp(SizeX, 2, 1000);
  SizeY = FMath::Clamp(SizeY, 2, 1000);
  Subdivisions = FMath::Clamp(Subdivisions, 2, 200);
  Spacing = FMath::Max(Spacing, 1.0);
  HeightScale = FMath::Max(HeightScale, 0.0);

  UWorld *World = GEditor->GetEditorWorldContext().World();
  if (!World) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("World not available"),
                        TEXT("WORLD_NOT_AVAILABLE"));
    return true;
  }

  // Spawn the actor
  FActorSpawnParameters SpawnParams;
  SpawnParams.Name = FName(*ActorName);
  SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
  
  FVector Location(0, 0, 0);
  const TSharedPtr<FJsonObject> *LocObj = nullptr;
  if (Payload->TryGetObjectField(TEXT("location"), LocObj) && LocObj) {
    double X = 0, Y = 0, Z = 0;
    (*LocObj)->TryGetNumberField(TEXT("x"), X);
    (*LocObj)->TryGetNumberField(TEXT("y"), Y);
    (*LocObj)->TryGetNumberField(TEXT("z"), Z);
    Location = FVector(X, Y, Z);
  }
  
  FRotator Rotation(0, 0, 0);
  const TSharedPtr<FJsonObject> *RotObj = nullptr;
  if (Payload->TryGetObjectField(TEXT("rotation"), RotObj) && RotObj) {
    double Pitch = 0, Yaw = 0, Roll = 0;
    (*RotObj)->TryGetNumberField(TEXT("pitch"), Pitch);
    (*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
    (*RotObj)->TryGetNumberField(TEXT("roll"), Roll);
    Rotation = FRotator(Pitch, Yaw, Roll);
  }

  AActor *TerrainActor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation, SpawnParams);
  if (!TerrainActor) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("Failed to spawn terrain actor"),
                        TEXT("SPAWN_FAILED"));
    return true;
  }

  // Add procedural mesh component
  UProceduralMeshComponent *ProcMesh = NewObject<UProceduralMeshComponent>(TerrainActor);
  if (!ProcMesh) {
    TerrainActor->Destroy();
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("Failed to create procedural mesh component"),
                        TEXT("COMPONENT_CREATION_FAILED"));
    return true;
  }
  
  ProcMesh->RegisterComponent();
  TerrainActor->AddInstanceComponent(ProcMesh);
  TerrainActor->SetRootComponent(ProcMesh);

  // Generate terrain mesh using KismetProceduralMeshLibrary
  TArray<FVector> Vertices;
  TArray<int32> Triangles;
  TArray<FVector> Normals;
  TArray<FVector2D> UVs;
  TArray<FProcMeshTangent> Tangents;

  // Create grid of vertices
  for (int32 Y = 0; Y <= Subdivisions; ++Y) {
    for (int32 X = 0; X <= Subdivisions; ++X) {
      // Calculate normalized position (0 to 1)
      double NormX = static_cast<double>(X) / Subdivisions;
      double NormY = static_cast<double>(Y) / Subdivisions;
      
      // Calculate world position with spacing
      double WorldX = (NormX - 0.5) * SizeX * Spacing;
      double WorldY = (NormY - 0.5) * SizeY * Spacing;
      
      // Generate height using simple noise/sine combination
      double WorldZ = FMath::Sin(NormX * 4.0 * PI) * FMath::Cos(NormY * 4.0 * PI) * HeightScale * 0.3 +
                      FMath::Sin(NormX * 8.0 * PI) * FMath::Cos(NormY * 8.0 * PI) * HeightScale * 0.15 +
                      FMath::Sin(NormX * 2.0 * PI + NormY * 3.0 * PI) * HeightScale * 0.25;
      
      Vertices.Add(FVector(WorldX, WorldY, WorldZ));
      UVs.Add(FVector2D(NormX, NormY));
    }
  }

  // Generate triangles
  for (int32 Y = 0; Y < Subdivisions; ++Y) {
    for (int32 X = 0; X < Subdivisions; ++X) {
      int32 Current = Y * (Subdivisions + 1) + X;
      int32 Next = Current + Subdivisions + 1;
      
      // First triangle
      Triangles.Add(Current);
      Triangles.Add(Next);
      Triangles.Add(Current + 1);
      
      // Second triangle
      Triangles.Add(Current + 1);
      Triangles.Add(Next);
      Triangles.Add(Next + 1);
    }
  }

  // Calculate normals and tangents
  UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UVs, Normals, Tangents);

  // Create the mesh section
  ProcMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, true);

  // Apply material if specified
  FString MaterialPath;
  if (Payload->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty()) {
    UMaterialInterface *Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (Material) {
      ProcMesh->SetMaterial(0, Material);
    }
  }

  // Mark the actor as modified
  TerrainActor->MarkPackageDirty();

  // Build response
  TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
  Resp->SetStringField(TEXT("actorName"), TerrainActor->GetName());
  Resp->SetStringField(TEXT("actorPath"), TerrainActor->GetPathName());
  Resp->SetNumberField(TEXT("vertices"), Vertices.Num());
  Resp->SetNumberField(TEXT("triangles"), Triangles.Num() / 3);
  Resp->SetNumberField(TEXT("sizeX"), SizeX);
  Resp->SetNumberField(TEXT("sizeY"), SizeY);
  Resp->SetNumberField(TEXT("subdivisions"), Subdivisions);
  
  // Add verification data
  AddActorVerification(Resp, TerrainActor);

  SendAutomationResponse(RequestingSocket, RequestId, true,
                         TEXT("Procedural terrain created successfully"), Resp, FString());
  return true;
#else
  SendAutomationResponse(RequestingSocket, RequestId, false,
                         TEXT("create_procedural_terrain requires editor build"), nullptr,
                         TEXT("NOT_IMPLEMENTED"));
  return true;
#endif
}

namespace {

// Write a JSON value into the memory of a single FProperty instance. Handles the
// common types that appear as TMap keys/values: structs (FGameplayTag via "Tag.Name"
// string OR full JSON object), class references (TSubclassOf accepts paths with or
// without "_C" suffix, and Blueprint asset paths get resolved to their generated
// class), object references, soft refs, names, strings, bools, numerics. Returns
// false and writes a description to OutError on failure.
static bool WriteJsonToPropertyMemory(FProperty *Prop, void *RawMem,
                                      const TSharedPtr<FJsonValue> &Value,
                                      FString &OutError) {
  if (!Prop || !RawMem || !Value.IsValid()) {
    OutError = TEXT("Invalid property/memory/value");
    return false;
  }

  if (FStructProperty *SP = CastField<FStructProperty>(Prop)) {
    if (!SP->Struct) {
      OutError = TEXT("Struct property has no struct type");
      return false;
    }
    if (Value->Type == EJson::Object) {
      const TSharedPtr<FJsonObject> &Obj = Value->AsObject();
      if (Obj.IsValid() && FJsonObjectConverter::JsonObjectToUStruct(
                              Obj.ToSharedRef(), SP->Struct, RawMem, 0, 0)) {
        return true;
      }
      OutError = FString::Printf(TEXT("Failed to convert JSON object to %s"),
                                 *SP->Struct->GetName());
      return false;
    }
    if (Value->Type == EJson::String) {
      const FString StrVal = Value->AsString();
      // Fast path for FGameplayTag: write the TagName field directly.
      if (SP->Struct->GetFName() == TEXT("GameplayTag")) {
        if (FNameProperty *NP = CastField<FNameProperty>(
                SP->Struct->FindPropertyByName(TEXT("TagName")))) {
          NP->SetPropertyValue_InContainer(RawMem, FName(*StrVal));
          return true;
        }
      }
      FString Wrapped = StrVal;
      if (!Wrapped.StartsWith(TEXT("(")))
        Wrapped = TEXT("(") + Wrapped + TEXT(")");
      const TCHAR *Buf = *Wrapped;
      if (SP->Struct->ImportText(Buf, RawMem, nullptr, PPF_None, GLog,
                                 SP->Struct->GetName())) {
        return true;
      }
      OutError = FString::Printf(TEXT("Failed to import string into %s"),
                                 *SP->Struct->GetName());
      return false;
    }
    OutError = TEXT("Expected string or object for struct property");
    return false;
  }

  if (FClassProperty *CP = CastField<FClassProperty>(Prop)) {
    if (Value->Type != EJson::String) {
      OutError = TEXT("Expected string class path for class property");
      return false;
    }
    const FString Path = Value->AsString();
    if (Path.IsEmpty()) {
      CP->SetObjectPropertyValue(RawMem, nullptr);
      return true;
    }
    UClass *Loaded = LoadObject<UClass>(nullptr, *Path);
    if (!Loaded) {
      const FString WithC =
          Path.EndsWith(TEXT("_C")) ? Path : (Path + TEXT("_C"));
      Loaded = LoadObject<UClass>(nullptr, *WithC);
    }
    if (!Loaded) {
      FString WithoutC = Path;
      if (WithoutC.EndsWith(TEXT("_C")))
        WithoutC.LeftChopInline(2);
      if (UObject *Obj = LoadObject<UObject>(nullptr, *WithoutC)) {
        if (UBlueprint *BP = Cast<UBlueprint>(Obj)) {
          Loaded = BP->GeneratedClass;
        }
      }
    }
    if (!Loaded) {
      OutError = FString::Printf(TEXT("Class not found: %s"), *Path);
      return false;
    }
    if (CP->MetaClass && !Loaded->IsChildOf(CP->MetaClass)) {
      OutError = FString::Printf(TEXT("%s is not a subclass of %s"),
                                 *Loaded->GetName(), *CP->MetaClass->GetName());
      return false;
    }
    CP->SetObjectPropertyValue(RawMem, Loaded);
    return true;
  }

  if (FSoftClassProperty *SCP = CastField<FSoftClassProperty>(Prop)) {
    if (Value->Type != EJson::String) {
      OutError = TEXT("Expected string path for soft class property");
      return false;
    }
    FSoftObjectPath SoftPath(Value->AsString());
    *static_cast<FSoftObjectPtr *>(RawMem) = FSoftObjectPtr(SoftPath);
    return true;
  }

  if (FObjectProperty *OP = CastField<FObjectProperty>(Prop)) {
    if (Value->Type != EJson::String) {
      OutError = TEXT("Expected string path for object property");
      return false;
    }
    const FString Path = Value->AsString();
    UObject *Loaded = Path.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Path);
    OP->SetObjectPropertyValue(RawMem, Loaded);
    return true;
  }

  FString ValueStr;
  if (Value->Type == EJson::String) {
    ValueStr = Value->AsString();
  } else if (Value->Type == EJson::Number) {
    ValueStr = FString::SanitizeFloat(Value->AsNumber());
  } else if (Value->Type == EJson::Boolean) {
    ValueStr = Value->AsBool() ? TEXT("True") : TEXT("False");
  } else {
    OutError = TEXT("Unsupported JSON type for primitive map element");
    return false;
  }
  const TCHAR *ImportResult =
      Prop->ImportText_Direct(*ValueStr, RawMem, nullptr, PPF_None);
  if (!ImportResult) {
    OutError = FString::Printf(TEXT("Failed to import '%s' into %s"),
                               *ValueStr, *Prop->GetClass()->GetName());
    return false;
  }
  return true;
}

// Export a property's memory at RawMem to a JSON value, mirroring the
// types accepted by WriteJsonToPropertyMemory.
static TSharedPtr<FJsonValue> ReadPropertyMemoryToJson(FProperty *Prop,
                                                       const void *RawMem) {
  if (!Prop || !RawMem)
    return MakeShared<FJsonValueNull>();

  if (FStructProperty *SP = CastField<FStructProperty>(Prop)) {
    if (SP->Struct && SP->Struct->GetFName() == TEXT("GameplayTag")) {
      if (FNameProperty *NP = CastField<FNameProperty>(
              SP->Struct->FindPropertyByName(TEXT("TagName")))) {
        return MakeShared<FJsonValueString>(
            NP->GetPropertyValue_InContainer(RawMem).ToString());
      }
    }
    if (SP->Struct) {
      TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
      if (FJsonObjectConverter::UStructToJsonObject(SP->Struct, RawMem, Out, 0, 0)) {
        return MakeShared<FJsonValueObject>(Out);
      }
    }
    return MakeShared<FJsonValueNull>();
  }
  if (FClassProperty *CP = CastField<FClassProperty>(Prop)) {
    UObject *Obj = CP->GetObjectPropertyValue(RawMem);
    return Obj ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueString>(Obj->GetPathName()))
               : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>());
  }
  if (FObjectProperty *OP = CastField<FObjectProperty>(Prop)) {
    UObject *Obj = OP->GetObjectPropertyValue(RawMem);
    return Obj ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueString>(Obj->GetPathName()))
               : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>());
  }
  FString Str;
  Prop->ExportTextItem_Direct(Str, RawMem, nullptr, nullptr, PPF_None);
  return MakeShared<FJsonValueString>(Str);
}

// If TargetObject is a Class Default Object, mark the owning Blueprint as
// modified so the editor saves the new defaults with the Blueprint asset.
static void MarkCDOOwnerBlueprintModified(UObject *TargetObject) {
  if (!TargetObject || !TargetObject->HasAllFlags(RF_ClassDefaultObject))
    return;
  if (UClass *Cls = TargetObject->GetClass()) {
    if (UBlueprint *BP = Cast<UBlueprint>(Cls->ClassGeneratedBy)) {
      FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    }
  }
}

} // namespace

bool UMcpAutomationBridgeSubsystem::HandleInspectAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  const FString Lower = Action.ToLower();
  if (!Lower.Equals(TEXT("inspect"), ESearchCase::IgnoreCase)) {
    return false;
  }

#if WITH_EDITOR
  if (!Payload.IsValid()) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("inspect payload missing"),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  // Get the sub-action to determine if objectPath is required
  FString SubAction;
  Payload->TryGetStringField(TEXT("action"), SubAction);
  const FString LowerSubAction = SubAction.ToLower();
  
  // List of global actions that don't require objectPath
  const bool bIsGlobalAction = 
    LowerSubAction.Equals(TEXT("get_project_settings")) ||
    LowerSubAction.Equals(TEXT("get_editor_settings")) ||
    LowerSubAction.Equals(TEXT("get_world_settings")) ||
    LowerSubAction.Equals(TEXT("get_viewport_info")) ||
    LowerSubAction.Equals(TEXT("get_selected_actors")) ||
    LowerSubAction.Equals(TEXT("get_scene_stats")) ||
    LowerSubAction.Equals(TEXT("get_performance_stats")) ||
    LowerSubAction.Equals(TEXT("get_memory_stats")) ||
    LowerSubAction.Equals(TEXT("list_objects")) ||
    LowerSubAction.Equals(TEXT("find_by_class")) ||
    LowerSubAction.Equals(TEXT("find_by_tag")) ||
    LowerSubAction.Equals(TEXT("inspect_class"));

  // Actions that require actorName instead of objectPath (actor-only operations)
  const bool bIsActorAction =
    LowerSubAction.Equals(TEXT("get_components")) ||
    LowerSubAction.Equals(TEXT("get_component_property")) ||
    LowerSubAction.Equals(TEXT("set_component_property")) ||
    LowerSubAction.Equals(TEXT("get_metadata")) ||
    LowerSubAction.Equals(TEXT("add_tag")) ||
    LowerSubAction.Equals(TEXT("create_snapshot")) ||
    LowerSubAction.Equals(TEXT("restore_snapshot")) ||
    LowerSubAction.Equals(TEXT("delete_object")) ||
    LowerSubAction.Equals(TEXT("get_bounding_box"));

  // Actions that work on any UObject via objectPath (not just actors)
  const bool bIsObjectPropertyAction =
    LowerSubAction.Equals(TEXT("get_property")) ||
    LowerSubAction.Equals(TEXT("set_property")) ||
    LowerSubAction.Equals(TEXT("export")) ||
    LowerSubAction.Equals(TEXT("call_function")) ||
    LowerSubAction.Equals(TEXT("set_map_entry")) ||
    LowerSubAction.Equals(TEXT("add_map_entry")) ||
    LowerSubAction.Equals(TEXT("remove_map_entry")) ||
    LowerSubAction.Equals(TEXT("list_map_entries"));

  // Delegate actor-related actions to the control_actor handler
  if (bIsActorAction) {
    // These actions are handled by HandleControlActorAction - delegate directly
    return HandleControlActorAction(RequestId, TEXT("control_actor"), Payload, RequestingSocket);
  }

  // Only require objectPath for non-global actions
  FString ObjectPath;
  if (!bIsGlobalAction) {
    if (!Payload->TryGetStringField(TEXT("objectPath"), ObjectPath) ||
        ObjectPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("objectPath required"),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }
  }

  // Handle global actions that don't require objectPath
  if (bIsGlobalAction) {
    TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
    
    if (LowerSubAction.Equals(TEXT("get_project_settings"))) {
      // Return project settings info
      // Set action to "inspect" (the tool name) for proper action matching in TS message-handler
      Resp->SetStringField(TEXT("action"), TEXT("inspect"));
      Resp->SetStringField(TEXT("subAction"), SubAction);
      Resp->SetStringField(TEXT("message"), TEXT("Project settings retrieved"));
      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Project settings retrieved"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("get_editor_settings"))) {
      // Set action to "inspect" (the tool name) for proper action matching in TS message-handler
      Resp->SetStringField(TEXT("action"), TEXT("inspect"));
      Resp->SetStringField(TEXT("subAction"), SubAction);
      Resp->SetStringField(TEXT("message"), TEXT("Editor settings retrieved"));
      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Editor settings retrieved"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("get_world_settings"))) {
      if (GEditor && GEditor->GetEditorWorldContext().World()) {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        Resp->SetStringField(TEXT("worldName"), World->GetName());
        Resp->SetStringField(TEXT("levelName"), World->GetCurrentLevel()->GetName());
        Resp->SetBoolField(TEXT("success"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true,
                               TEXT("World settings retrieved"), Resp, FString());
      } else {
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("No world available"),
                            TEXT("WORLD_NOT_FOUND"));
      }
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("get_viewport_info"))) {
      if (GEditor && GEditor->GetActiveViewport()) {
        FViewport* Viewport = GEditor->GetActiveViewport();
        Resp->SetNumberField(TEXT("width"), Viewport->GetSizeXY().X);
        Resp->SetNumberField(TEXT("height"), Viewport->GetSizeXY().Y);
        Resp->SetBoolField(TEXT("success"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true,
                               TEXT("Viewport info retrieved"), Resp, FString());
      } else {
        Resp->SetBoolField(TEXT("success"), true);
        Resp->SetStringField(TEXT("message"), TEXT("Viewport info not available in this context"));
        SendAutomationResponse(RequestingSocket, RequestId, true,
                               TEXT("Viewport info retrieved"), Resp, FString());
      }
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("get_selected_actors"))) {
      TArray<TSharedPtr<FJsonValue>> ActorsArray;
      if (GEditor) {
        TArray<AActor*> SelectedActors;
        GEditor->GetSelectedActors()->GetSelectedObjects(SelectedActors);
        for (AActor* Actor : SelectedActors) {
          if (Actor) {
            TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
            ActorObj->SetStringField(TEXT("name"), Actor->GetName());
            ActorObj->SetStringField(TEXT("path"), Actor->GetPathName());
            ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
            ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
          }
        }
      }
      Resp->SetArrayField(TEXT("actors"), ActorsArray);
      Resp->SetNumberField(TEXT("count"), ActorsArray.Num());
      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Selected actors retrieved"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("get_scene_stats"))) {
      int32 ActorCount = 0;
      if (GEditor && GEditor->GetEditorWorldContext().World()) {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        for (TActorIterator<AActor> It(World); It; ++It) {
          ActorCount++;
        }
      }
      Resp->SetNumberField(TEXT("actorCount"), ActorCount);
      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Scene stats retrieved"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("get_performance_stats"))) {
      Resp->SetBoolField(TEXT("success"), true);
      Resp->SetStringField(TEXT("message"), TEXT("Performance stats placeholder - implement with actual metrics"));
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Performance stats retrieved"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("get_memory_stats"))) {
      Resp->SetBoolField(TEXT("success"), true);
      Resp->SetStringField(TEXT("message"), TEXT("Memory stats placeholder - implement with actual metrics"));
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Memory stats retrieved"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("list_objects"))) {
      TArray<TSharedPtr<FJsonValue>> ObjectsArray;
      if (GEditor && GEditor->GetEditorWorldContext().World()) {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        for (TActorIterator<AActor> It(World); It; ++It) {
          AActor* Actor = *It;
          TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
          Obj->SetStringField(TEXT("name"), Actor->GetName());
          Obj->SetStringField(TEXT("path"), Actor->GetPathName());
          Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
          ObjectsArray.Add(MakeShared<FJsonValueObject>(Obj));
        }
      }
      Resp->SetArrayField(TEXT("objects"), ObjectsArray);
      Resp->SetNumberField(TEXT("count"), ObjectsArray.Num());
      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Objects listed"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("find_by_class"))) {
      FString ClassName;
      Payload->TryGetStringField(TEXT("className"), ClassName);
      TArray<TSharedPtr<FJsonValue>> ObjectsArray;
      if (GEditor && GEditor->GetEditorWorldContext().World() && !ClassName.IsEmpty()) {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        for (TActorIterator<AActor> It(World); It; ++It) {
          AActor* Actor = *It;
          if (Actor->GetClass()->GetName().Equals(ClassName, ESearchCase::IgnoreCase) ||
              Actor->GetClass()->GetPathName().Contains(ClassName)) {
            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            Obj->SetStringField(TEXT("name"), Actor->GetName());
            Obj->SetStringField(TEXT("path"), Actor->GetPathName());
            Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
            ObjectsArray.Add(MakeShared<FJsonValueObject>(Obj));
          }
        }
      }
      Resp->SetArrayField(TEXT("objects"), ObjectsArray);
      Resp->SetNumberField(TEXT("count"), ObjectsArray.Num());
      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Objects found by class"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("find_by_tag"))) {
      FString Tag;
      Payload->TryGetStringField(TEXT("tag"), Tag);
      TArray<TSharedPtr<FJsonValue>> ObjectsArray;
      if (GEditor && GEditor->GetEditorWorldContext().World() && !Tag.IsEmpty()) {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        for (TActorIterator<AActor> It(World); It; ++It) {
          AActor* Actor = *It;
          if (Actor->ActorHasTag(FName(*Tag))) {
            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            Obj->SetStringField(TEXT("name"), Actor->GetName());
            Obj->SetStringField(TEXT("path"), Actor->GetPathName());
            Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
            ObjectsArray.Add(MakeShared<FJsonValueObject>(Obj));
          }
        }
      }
      Resp->SetArrayField(TEXT("objects"), ObjectsArray);
      Resp->SetNumberField(TEXT("count"), ObjectsArray.Num());
      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Objects found by tag"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("inspect_class"))) {
      FString ClassName;
      Payload->TryGetStringField(TEXT("className"), ClassName);
      if (!ClassName.IsEmpty()) {
        // Try to find the class
        UClass* TargetClass = FindObject<UClass>(nullptr, *ClassName);
        if (!TargetClass && !ClassName.Contains(TEXT("."))) {
          // Try with /Script/Engine prefix for common classes
          TargetClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
        }
        if (TargetClass) {
          Resp->SetStringField(TEXT("className"), TargetClass->GetName());
          Resp->SetStringField(TEXT("classPath"), TargetClass->GetPathName());
          Resp->SetStringField(TEXT("parentClass"), TargetClass->GetSuperClass() ? TargetClass->GetSuperClass()->GetName() : TEXT("None"));
          Resp->SetBoolField(TEXT("success"), true);
          SendAutomationResponse(RequestingSocket, RequestId, true,
                                 TEXT("Class inspected"), Resp, FString());
        } else {
          SendAutomationError(RequestingSocket, RequestId,
                              FString::Printf(TEXT("Class not found: %s"), *ClassName),
                              TEXT("CLASS_NOT_FOUND"));
        }
      } else {
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("className is required for inspect_class"),
                            TEXT("INVALID_ARGUMENT"));
      }
      return true;
    }
    
    // Fallback for unimplemented global actions
    Resp->SetBoolField(TEXT("success"), true);
    Resp->SetStringField(TEXT("message"), FString::Printf(TEXT("Action %s acknowledged (placeholder implementation)"), *SubAction));
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Action processed"), Resp, FString());
    return true;
  }

  // Find the object (for non-global actions that require objectPath)
  UObject *TargetObject = nullptr;
  
  // CRITICAL FIX: Handle component paths in "ActorName.ComponentName" format
  // This resolves paths like "TestActor.StaticMeshComponent0" to the actual component object
  if (ObjectPath.Contains(TEXT(".")) && !ObjectPath.StartsWith(TEXT("/")))
  {
    FString ActorName = ObjectPath.Left(ObjectPath.Find(TEXT(".")));
    FString ComponentName = ObjectPath.Right(ObjectPath.Len() - ActorName.Len() - 1);
    
    if (!ActorName.IsEmpty() && !ComponentName.IsEmpty())
    {
      // Try to find the actor first
      if (AActor *Actor = FindActorByName(ActorName))
      {
        // Find the component on the actor using fuzzy name matching
        if (UActorComponent *Comp = FindComponentByName(Actor, ComponentName))
        {
          TargetObject = Comp;
          // Normalize the path for downstream error messages
          ObjectPath = Comp->GetPathName();
        }
      }
    }
  }
  
  // Try to find by path first (if not already found as component)
  if (!TargetObject) {
    TargetObject = FindObject<UObject>(nullptr, *ObjectPath);
  }
  
  // If not found, try to find actor by name/label in editor world
  if (!TargetObject && GEditor) {
    if (AActor *FoundActor = FindActorByName(ObjectPath)) {
      TargetObject = FoundActor;
      ObjectPath = FoundActor->GetPathName();
    } else {
      UWorld *World = GEditor->GetEditorWorldContext().World();
      if (World) {
        for (TActorIterator<AActor> It(World); It; ++It) {
          AActor *Actor = *It;
          if (Actor && (Actor->GetActorLabel().Equals(ObjectPath, ESearchCase::IgnoreCase) ||
                        Actor->GetName().Equals(ObjectPath, ESearchCase::IgnoreCase))) {
            TargetObject = Actor;
            break;
          }
        }
      }
    }
  }

  // If not found in editor world, try PIE world (runtime actors during Play-In-Editor)
  bool bFoundInPIE = false;
  if (!TargetObject && GEditor) {
    // Check if PIE is active and search PIE worlds
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
      if (Context.WorldType == EWorldType::PIE && Context.World())
      {
        UWorld* PIEWorld = Context.World();
        for (TActorIterator<AActor> It(PIEWorld); It; ++It)
        {
          AActor *Actor = *It;
          if (Actor && (Actor->GetActorLabel().Equals(ObjectPath, ESearchCase::IgnoreCase) ||
                        Actor->GetName().Equals(ObjectPath, ESearchCase::IgnoreCase)))
          {
            TargetObject = Actor;
            bFoundInPIE = true;
            break;
          }
        }
        if (TargetObject) break;

        // Also try FindObject within PIE world
        UObject* PIEObj = FindObject<UObject>(PIEWorld, *ObjectPath);
        if (PIEObj)
        {
          TargetObject = PIEObj;
          bFoundInPIE = true;
          break;
        }
      }
    }
  }

  // If still not found, try loading as an asset package (any mount point)
  if (!TargetObject && ObjectPath.StartsWith(TEXT("/"))) {
    TargetObject = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
    if (!TargetObject) {
      FString PackagePath = ObjectPath;
      if (PackagePath.Contains(TEXT("."))) {
        PackagePath = PackagePath.Left(PackagePath.Find(TEXT(".")));
      }
      UPackage* LoadedPackage = LoadPackage(nullptr, *PackagePath, LOAD_None);
      if (LoadedPackage) {
        TargetObject = FindObject<UObject>(LoadedPackage, *ObjectPath);
        if (!TargetObject) {
          FString AssetName = FPaths::GetBaseFilename(PackagePath);
          TargetObject = FindObject<UObject>(LoadedPackage, *AssetName);
        }
      }
    }
  }

  // Final fallback: query the AssetRegistry. Lets callers reach plugin-mount assets
  // when their path is missing a content-subfolder segment (e.g. /ALS/Character/AB
  // instead of /ALS/ALS/Character/AB), and also resolves bare asset names by suffix.
  FString PathSuggestionList;
  if (!TargetObject) {
    FAssetRegistryModule& AssetRegistryModule =
      FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Derive a probable asset name from the final path segment (strip any .AssetName suffix).
    FString ProbeName = ObjectPath;
    int32 LastDot = INDEX_NONE;
    if (ProbeName.FindLastChar(TEXT('.'), LastDot)) {
      ProbeName = ProbeName.Left(LastDot);
    }
    int32 LastSlash = INDEX_NONE;
    if (ProbeName.FindLastChar(TEXT('/'), LastSlash)) {
      ProbeName = ProbeName.Mid(LastSlash + 1);
    }

    if (!ProbeName.IsEmpty()) {
      FARFilter Filter;
      Filter.PackageNames.Empty();
      Filter.bRecursivePaths = true;
      // Search all mounted roots; the asset registry has them indexed.
      TArray<FAssetData> Candidates;
      AssetRegistry.GetAssetsByPackageName(FName(*ObjectPath), Candidates);
      if (Candidates.Num() == 0) {
        // Broader search: assets whose AssetName matches the probe (case-insensitive).
        AssetRegistry.GetAllAssets(Candidates, /*bIncludeOnlyOnDiskAssets=*/true);
        Candidates.RemoveAll([&ProbeName](const FAssetData& Data) {
          return !Data.AssetName.ToString().Equals(ProbeName, ESearchCase::IgnoreCase);
        });
      }

      if (Candidates.Num() == 1) {
        // Unambiguous match: load it.
        TargetObject = Candidates[0].GetAsset();
        if (TargetObject) {
          ObjectPath = TargetObject->GetPathName();
        }
      } else if (Candidates.Num() > 1) {
        // Multiple matches: build a suggestion list so the caller can pick.
        int32 ShownCount = 0;
        for (const FAssetData& Cand : Candidates) {
          if (ShownCount >= 8) {
            PathSuggestionList += FString::Printf(TEXT("\n  ... (%d more)"), Candidates.Num() - ShownCount);
            break;
          }
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
          PathSuggestionList += FString::Printf(TEXT("\n  %s"), *Cand.GetSoftObjectPath().ToString());
#else
          PathSuggestionList += FString::Printf(TEXT("\n  %s"), *Cand.ToSoftObjectPath().ToString());
#endif
          ShownCount++;
        }
      }
    }
  }

  if (!TargetObject) {
    const FString Message = PathSuggestionList.IsEmpty()
      ? FString::Printf(TEXT("Object not found: %s"), *ObjectPath)
      : FString::Printf(TEXT("Object not found at '%s'. Candidates with matching asset name:%s"), *ObjectPath, *PathSuggestionList);
    SendAutomationError(RequestingSocket, RequestId, Message, TEXT("OBJECT_NOT_FOUND"));
    return true;
  }

  // Handle get_property / set_property / export on any UObject
  if (bIsObjectPropertyAction)
  {
    if (LowerSubAction.Equals(TEXT("get_property")))
    {
      FString PropertyName;
      Payload->TryGetStringField(TEXT("propertyName"), PropertyName);
      if (PropertyName.IsEmpty())
      {
        Payload->TryGetStringField(TEXT("propertyPath"), PropertyName);
      }
      if (PropertyName.IsEmpty())
      {
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("propertyName is required"), TEXT("INVALID_ARGUMENT"));
        return true;
      }

      // Find the property by name
      FProperty* Prop = TargetObject->GetClass()->FindPropertyByName(FName(*PropertyName));
      if (!Prop)
      {
        // Try case-insensitive search
        for (TFieldIterator<FProperty> It(TargetObject->GetClass()); It; ++It)
        {
          if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
          {
            Prop = *It;
            break;
          }
        }
      }
      if (!Prop)
      {
        SendAutomationError(RequestingSocket, RequestId,
                            FString::Printf(TEXT("Property '%s' not found on %s (%s)"),
                                            *PropertyName, *TargetObject->GetName(),
                                            *TargetObject->GetClass()->GetName()),
                            TEXT("PROPERTY_NOT_FOUND"));
        return true;
      }

      TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
      Resp->SetStringField(TEXT("objectPath"), TargetObject->GetPathName());
      Resp->SetStringField(TEXT("propertyName"), Prop->GetName());
      Resp->SetStringField(TEXT("propertyType"), Prop->GetCPPType());

      TSharedPtr<FJsonValue> JsonVal = ExportPropertyToJsonValue(TargetObject, Prop);
      if (JsonVal.IsValid())
      {
        Resp->SetField(TEXT("value"), JsonVal);
      }
      else
      {
        FString ValueStr;
        MCP_PROPERTY_EXPORT_TEXT(Prop, ValueStr,
                                Prop->ContainerPtrToValuePtr<void>(TargetObject),
                                nullptr, nullptr, PPF_None);
        Resp->SetStringField(TEXT("value"), ValueStr);
      }

      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Property retrieved"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("set_property")))
    {
      // Delegate to HandleSetObjectProperty which supports any UObject (not just actors)
      // Construct a set_object_property payload from the inspect payload
      TSharedPtr<FJsonObject> SetPayload = MakeShared<FJsonObject>();
      SetPayload->SetStringField(TEXT("objectPath"), ObjectPath);
      FString PropName;
      Payload->TryGetStringField(TEXT("propertyName"), PropName);
      if (PropName.IsEmpty()) Payload->TryGetStringField(TEXT("propertyPath"), PropName);
      SetPayload->SetStringField(TEXT("propertyName"), PropName);
      if (Payload->HasField(TEXT("value")))
      {
        SetPayload->SetField(TEXT("value"), Payload->TryGetField(TEXT("value")));
      }
      return HandleSetObjectProperty(RequestId, TEXT("set_object_property"), SetPayload, RequestingSocket);
    }
    else if (LowerSubAction.Equals(TEXT("export")))
    {
      // Export all UPROPERTY values as a JSON object
      TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
      Resp->SetStringField(TEXT("objectPath"), TargetObject->GetPathName());
      Resp->SetStringField(TEXT("objectName"), TargetObject->GetName());
      Resp->SetStringField(TEXT("className"), TargetObject->GetClass()->GetName());

      TSharedPtr<FJsonObject> PropertiesObj = MakeShared<FJsonObject>();
      for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass()); PropIt; ++PropIt)
      {
        FProperty* Prop = *PropIt;
        if (!Prop) continue;
        if (!(Prop->PropertyFlags & (CPF_Edit | CPF_BlueprintVisible | CPF_Config | CPF_BlueprintReadOnly)))
          continue;

        TSharedPtr<FJsonValue> JsonVal = ExportPropertyToJsonValue(TargetObject, Prop);
        if (JsonVal.IsValid())
        {
          PropertiesObj->SetField(Prop->GetName(), JsonVal);
        }
        else
        {
          FString ValueStr;
          MCP_PROPERTY_EXPORT_TEXT(Prop, ValueStr,
                                  Prop->ContainerPtrToValuePtr<void>(TargetObject),
                                  nullptr, nullptr, PPF_None);
          if (!ValueStr.IsEmpty())
          {
            PropertiesObj->SetStringField(Prop->GetName(), ValueStr);
          }
        }
      }
      Resp->SetObjectField(TEXT("properties"), PropertiesObj);
      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Object exported"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("call_function")))
    {
      FString FunctionName;
      Payload->TryGetStringField(TEXT("functionName"), FunctionName);
      if (FunctionName.IsEmpty())
      {
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("functionName is required"), TEXT("INVALID_ARGUMENT"));
        return true;
      }

      UFunction* Function = TargetObject->FindFunction(FName(*FunctionName));
      if (!Function)
      {
        // Try case-insensitive search
        for (TFieldIterator<UFunction> It(TargetObject->GetClass()); It; ++It)
        {
          if (It->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
          {
            Function = *It;
            break;
          }
        }
      }
      if (!Function)
      {
        SendAutomationError(RequestingSocket, RequestId,
                            FString::Printf(TEXT("Function '%s' not found on %s (%s)"),
                                            *FunctionName, *TargetObject->GetName(),
                                            *TargetObject->GetClass()->GetName()),
                            TEXT("FUNCTION_NOT_FOUND"));
        return true;
      }

      // Build parameters buffer from JSON arguments
      TSharedPtr<FJsonObject> ArgsObj;
      const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
      if (Payload->TryGetObjectField(TEXT("arguments"), ArgsPtr) && ArgsPtr)
      {
        ArgsObj = *ArgsPtr;
      }

      void* ParmsBuffer = nullptr;
      if (Function->ParmsSize > 0)
      {
        ParmsBuffer = FMemory::Malloc(Function->ParmsSize, 16);
        FMemory::Memzero(ParmsBuffer, Function->ParmsSize);

        // Try to populate parameters from the arguments JSON
        if (ArgsObj.IsValid())
        {
          for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
          {
            FProperty* Param = *ParamIt;
            if (!Param || !(Param->PropertyFlags & CPF_Parm)) continue;
            if (Param->PropertyFlags & CPF_ReturnParm) continue;

            TSharedPtr<FJsonValue> ParamVal = ArgsObj->TryGetField(Param->GetName());
            if (!ParamVal.IsValid()) continue;

            void* ValuePtr = Param->ContainerPtrToValuePtr<void>(ParmsBuffer);
            if (FStrProperty* StrProp = CastField<FStrProperty>(Param))
            {
              StrProp->SetPropertyValue(ValuePtr, ParamVal->AsString());
            }
            else if (FIntProperty* IntProp = CastField<FIntProperty>(Param))
            {
              IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(ParamVal->AsNumber()));
            }
            else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Param))
            {
              FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(ParamVal->AsNumber()));
            }
            else if (FDoubleProperty* DblProp = CastField<FDoubleProperty>(Param))
            {
              DblProp->SetPropertyValue(ValuePtr, ParamVal->AsNumber());
            }
            else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Param))
            {
              BoolProp->SetPropertyValue(ValuePtr, ParamVal->AsBool());
            }
            else if (FNameProperty* NameProp = CastField<FNameProperty>(Param))
            {
              NameProp->SetPropertyValue(ValuePtr, FName(*ParamVal->AsString()));
            }
            else
            {
              // Fallback: try ImportText
              Param->ImportText_Direct(*ParamVal->AsString(), ValuePtr, nullptr, PPF_None);
            }
          }
        }
      }

      TargetObject->ProcessEvent(Function, ParmsBuffer);

      // Extract return value if any
      TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
      Resp->SetStringField(TEXT("objectPath"), TargetObject->GetPathName());
      Resp->SetStringField(TEXT("functionName"), Function->GetName());

      if (ParmsBuffer)
      {
        for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
        {
          FProperty* Param = *ParamIt;
          if (!Param || !(Param->PropertyFlags & CPF_ReturnParm)) continue;

          void* ValuePtr = Param->ContainerPtrToValuePtr<void>(ParmsBuffer);
          TSharedPtr<FJsonValue> RetVal = ExportPropertyToJsonValue(ParmsBuffer, Param);
          if (RetVal.IsValid())
          {
            Resp->SetField(TEXT("returnValue"), RetVal);
          }
          else
          {
            FString ValueStr;
            MCP_PROPERTY_EXPORT_TEXT(Param, ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
            Resp->SetStringField(TEXT("returnValue"), ValueStr);
          }
          break;
        }
        FMemory::Free(ParmsBuffer);
      }

      Resp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Function called"), Resp, FString());
      return true;
    }
    else if (LowerSubAction.Equals(TEXT("set_map_entry")) ||
             LowerSubAction.Equals(TEXT("add_map_entry")) ||
             LowerSubAction.Equals(TEXT("remove_map_entry")) ||
             LowerSubAction.Equals(TEXT("list_map_entries")))
    {
      FString PropName;
      Payload->TryGetStringField(TEXT("propertyName"), PropName);
      if (PropName.IsEmpty()) Payload->TryGetStringField(TEXT("propertyPath"), PropName);
      if (PropName.IsEmpty()) {
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("propertyName is required"), TEXT("INVALID_ARGUMENT"));
        return true;
      }

      FProperty *Prop = TargetObject->GetClass()->FindPropertyByName(FName(*PropName));
      if (!Prop) {
        SendAutomationError(RequestingSocket, RequestId,
                            FString::Printf(TEXT("Property '%s' not found on %s"),
                                            *PropName, *TargetObject->GetClass()->GetName()),
                            TEXT("UNKNOWN_PROPERTY"));
        return true;
      }
      FMapProperty *MP = CastField<FMapProperty>(Prop);
      if (!MP) {
        SendAutomationError(RequestingSocket, RequestId,
                            FString::Printf(TEXT("Property '%s' is not a TMap (got %s)"),
                                            *PropName, *Prop->GetClass()->GetName()),
                            TEXT("NOT_A_MAP"));
        return true;
      }

      void *MapAddr = MP->ContainerPtrToValuePtr<void>(TargetObject);
      FScriptMapHelper MapHelper(MP, MapAddr);

      // list_map_entries: dump every (key, value) pair as JSON.
      if (LowerSubAction.Equals(TEXT("list_map_entries"))) {
        TArray<TSharedPtr<FJsonValue>> EntriesArr;
        for (int32 i = 0; i < MapHelper.GetMaxIndex(); ++i) {
          if (!MapHelper.IsValidIndex(i)) continue;
          TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
          EntryObj->SetField(TEXT("key"), ReadPropertyMemoryToJson(MP->KeyProp, MapHelper.GetKeyPtr(i)));
          EntryObj->SetField(TEXT("value"), ReadPropertyMemoryToJson(MP->ValueProp, MapHelper.GetValuePtr(i)));
          EntriesArr.Add(MakeShared<FJsonValueObject>(EntryObj));
        }
        TSharedPtr<FJsonObject> ListResp = MakeShared<FJsonObject>();
        ListResp->SetStringField(TEXT("objectPath"), TargetObject->GetPathName());
        ListResp->SetStringField(TEXT("propertyName"), PropName);
        ListResp->SetArrayField(TEXT("entries"), EntriesArr);
        ListResp->SetNumberField(TEXT("count"), MapHelper.Num());
        ListResp->SetStringField(TEXT("keyType"), MP->KeyProp->GetClass()->GetName());
        ListResp->SetStringField(TEXT("valueType"), MP->ValueProp->GetClass()->GetName());
        ListResp->SetBoolField(TEXT("success"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true,
                               TEXT("Map entries listed"), ListResp, FString());
        return true;
      }

      // Parse key (required for add/set/remove).
      TSharedPtr<FJsonValue> KeyJson = Payload->TryGetField(TEXT("key"));
      if (!KeyJson.IsValid()) {
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("'key' is required"), TEXT("INVALID_ARGUMENT"));
        return true;
      }

      // Allocate scratch key memory.
      const int32 KeyElementSize = MP->KeyProp->GetElementSize();
      const int32 KeyMinAlign = MP->KeyProp->GetMinAlignment();
      void *KeyBuf = FMemory::Malloc(KeyElementSize, KeyMinAlign);
      MP->KeyProp->InitializeValue(KeyBuf);

      FString WriteErr;
      if (!WriteJsonToPropertyMemory(MP->KeyProp, KeyBuf, KeyJson, WriteErr)) {
        MP->KeyProp->DestroyValue(KeyBuf);
        FMemory::Free(KeyBuf);
        SendAutomationError(RequestingSocket, RequestId,
                            FString::Printf(TEXT("Failed to parse key: %s"), *WriteErr),
                            TEXT("INVALID_KEY"));
        return true;
      }

      // Find existing pair with matching key.
      int32 FoundIdx = INDEX_NONE;
      for (int32 i = 0; i < MapHelper.GetMaxIndex(); ++i) {
        if (!MapHelper.IsValidIndex(i)) continue;
        if (MP->KeyProp->Identical(KeyBuf, MapHelper.GetKeyPtr(i))) {
          FoundIdx = i;
          break;
        }
      }

      TargetObject->Modify();

      if (LowerSubAction.Equals(TEXT("remove_map_entry"))) {
        bool bRemoved = false;
        if (FoundIdx != INDEX_NONE) {
          MapHelper.RemoveAt(FoundIdx);
          bRemoved = true;
        }
        MP->KeyProp->DestroyValue(KeyBuf);
        FMemory::Free(KeyBuf);
        if (bRemoved) {
          TargetObject->MarkPackageDirty();
          MarkCDOOwnerBlueprintModified(TargetObject);
        }
        TSharedPtr<FJsonObject> RemoveResp = MakeShared<FJsonObject>();
        RemoveResp->SetStringField(TEXT("objectPath"), TargetObject->GetPathName());
        RemoveResp->SetStringField(TEXT("propertyName"), PropName);
        RemoveResp->SetBoolField(TEXT("removed"), bRemoved);
        RemoveResp->SetNumberField(TEXT("mapSize"), MapHelper.Num());
        RemoveResp->SetBoolField(TEXT("success"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true,
                               bRemoved ? TEXT("Map entry removed")
                                        : TEXT("Map entry not found (no-op)"),
                               RemoveResp, FString());
        return true;
      }

      // set_map_entry / add_map_entry: parse value and write.
      TSharedPtr<FJsonValue> ValJson = Payload->TryGetField(TEXT("value"));
      if (!ValJson.IsValid()) {
        MP->KeyProp->DestroyValue(KeyBuf);
        FMemory::Free(KeyBuf);
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("'value' is required"), TEXT("INVALID_ARGUMENT"));
        return true;
      }

      const int32 ValElementSize = MP->ValueProp->GetElementSize();
      const int32 ValMinAlign = MP->ValueProp->GetMinAlignment();
      void *ValBuf = FMemory::Malloc(ValElementSize, ValMinAlign);
      MP->ValueProp->InitializeValue(ValBuf);

      if (!WriteJsonToPropertyMemory(MP->ValueProp, ValBuf, ValJson, WriteErr)) {
        MP->KeyProp->DestroyValue(KeyBuf);
        FMemory::Free(KeyBuf);
        MP->ValueProp->DestroyValue(ValBuf);
        FMemory::Free(ValBuf);
        SendAutomationError(RequestingSocket, RequestId,
                            FString::Printf(TEXT("Failed to parse value: %s"), *WriteErr),
                            TEXT("INVALID_VALUE"));
        return true;
      }

      bool bWasUpdate = false;
      if (FoundIdx != INDEX_NONE) {
        MP->ValueProp->CopyCompleteValue(MapHelper.GetValuePtr(FoundIdx), ValBuf);
        bWasUpdate = true;
      } else {
        const int32 NewIdx = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
        MP->KeyProp->CopyCompleteValue(MapHelper.GetKeyPtr(NewIdx), KeyBuf);
        MP->ValueProp->CopyCompleteValue(MapHelper.GetValuePtr(NewIdx), ValBuf);
        MapHelper.Rehash();
      }

      MP->KeyProp->DestroyValue(KeyBuf);
      FMemory::Free(KeyBuf);
      MP->ValueProp->DestroyValue(ValBuf);
      FMemory::Free(ValBuf);

      TargetObject->MarkPackageDirty();
      MarkCDOOwnerBlueprintModified(TargetObject);

      TSharedPtr<FJsonObject> SetResp = MakeShared<FJsonObject>();
      SetResp->SetStringField(TEXT("objectPath"), TargetObject->GetPathName());
      SetResp->SetStringField(TEXT("propertyName"), PropName);
      SetResp->SetBoolField(TEXT("updated"), bWasUpdate);
      SetResp->SetStringField(TEXT("operation"), bWasUpdate ? TEXT("update") : TEXT("add"));
      SetResp->SetNumberField(TEXT("mapSize"), MapHelper.Num());
      SetResp->SetBoolField(TEXT("success"), true);
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             bWasUpdate ? TEXT("Map entry updated")
                                        : TEXT("Map entry added"),
                             SetResp, FString());
      return true;
    }
  }

  // Build inspection result
  TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
  
  // Basic object info
  Resp->SetStringField(TEXT("objectPath"), TargetObject->GetPathName());
  Resp->SetStringField(TEXT("objectName"), TargetObject->GetName());
  Resp->SetStringField(TEXT("className"), TargetObject->GetClass()->GetName());
  Resp->SetStringField(TEXT("classPath"), TargetObject->GetClass()->GetPathName());
  
  // If it's an actor, add actor-specific info
  if (AActor *Actor = Cast<AActor>(TargetObject)) {
    Resp->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
    Resp->SetBoolField(TEXT("isActor"), true);
    Resp->SetBoolField(TEXT("isHidden"), Actor->IsHidden());
    Resp->SetBoolField(TEXT("isSelected"), Actor->IsSelected());
    
    // Transform info
    TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();
    const FTransform &Transform = Actor->GetActorTransform();
    
    TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
    LocationObj->SetNumberField(TEXT("x"), Transform.GetLocation().X);
    LocationObj->SetNumberField(TEXT("y"), Transform.GetLocation().Y);
    LocationObj->SetNumberField(TEXT("z"), Transform.GetLocation().Z);
    TransformObj->SetObjectField(TEXT("location"), LocationObj);
    
    TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
    FRotator Rotator = Transform.GetRotation().Rotator();
    RotationObj->SetNumberField(TEXT("pitch"), Rotator.Pitch);
    RotationObj->SetNumberField(TEXT("yaw"), Rotator.Yaw);
    RotationObj->SetNumberField(TEXT("roll"), Rotator.Roll);
    TransformObj->SetObjectField(TEXT("rotation"), RotationObj);
    
    TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
    ScaleObj->SetNumberField(TEXT("x"), Transform.GetScale3D().X);
    ScaleObj->SetNumberField(TEXT("y"), Transform.GetScale3D().Y);
    ScaleObj->SetNumberField(TEXT("z"), Transform.GetScale3D().Z);
    TransformObj->SetObjectField(TEXT("scale"), ScaleObj);
    
    Resp->SetObjectField(TEXT("transform"), TransformObj);
    
    // Components info
    TArray<TSharedPtr<FJsonValue>> ComponentsArray;
    TInlineComponentArray<UActorComponent *> Components;
    Actor->GetComponents(Components);
    
    for (UActorComponent *Component : Components) {
      if (Component) {
        TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
        CompObj->SetStringField(TEXT("name"), Component->GetName());
        CompObj->SetStringField(TEXT("class"), Component->GetClass()->GetName());
        CompObj->SetBoolField(TEXT("isActive"), Component->IsActive());
        
        // Add specific info for common component types
        if (USceneComponent *SceneComp = Cast<USceneComponent>(Component)) {
          CompObj->SetBoolField(TEXT("isSceneComponent"), true);
          CompObj->SetBoolField(TEXT("isVisible"), SceneComp->IsVisible());
        }
        
        if (UStaticMeshComponent *MeshComp = Cast<UStaticMeshComponent>(Component)) {
          CompObj->SetBoolField(TEXT("isStaticMesh"), true);
          if (MeshComp->GetStaticMesh()) {
            CompObj->SetStringField(TEXT("staticMesh"), MeshComp->GetStaticMesh()->GetName());
          }
        }
        
        ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
      }
    }
    Resp->SetArrayField(TEXT("components"), ComponentsArray);
    Resp->SetNumberField(TEXT("componentCount"), ComponentsArray.Num());
  } else {
    Resp->SetBoolField(TEXT("isActor"), false);
  }
  
  // Serialize all reflected UPROPERTY values on the object
  {
    TSharedPtr<FJsonObject> PropertiesObj = MakeShared<FJsonObject>();
    UClass* IterClass = TargetObject->GetClass();
    // Find the first non-engine ancestor to skip base UObject/AActor/UDataAsset noise
    UClass* StopAtClass = UObject::StaticClass();
    if (AActor* AsActor = Cast<AActor>(TargetObject))
    {
      StopAtClass = AActor::StaticClass();
    }
    else if (TargetObject->IsA(UDataAsset::StaticClass()))
    {
      StopAtClass = UDataAsset::StaticClass();
    }

    for (TFieldIterator<FProperty> PropIt(IterClass); PropIt; ++PropIt)
    {
      FProperty* Prop = *PropIt;
      if (!Prop) continue;

      // Skip properties from base engine classes to reduce noise
      if (Prop->GetOwnerClass() && Prop->GetOwnerClass()->IsChildOf(StopAtClass) &&
          Prop->GetOwnerClass() != IterClass)
      {
        // Allow properties from the target's own class and its custom ancestors,
        // but skip properties defined on UObject/AActor/UDataAsset themselves
        if (Prop->GetOwnerClass() == StopAtClass)
          continue;
      }

      // Only include properties with UPROPERTY (CPF_Edit or CPF_BlueprintVisible or any reflection flag)
      if (!(Prop->PropertyFlags & (CPF_Edit | CPF_BlueprintVisible | CPF_Config | CPF_BlueprintReadOnly)))
        continue;

      void* Container = TargetObject;
      TSharedPtr<FJsonValue> JsonVal = ExportPropertyToJsonValue(Container, Prop);
      if (JsonVal.IsValid())
      {
        PropertiesObj->SetField(Prop->GetName(), JsonVal);
      }
      else
      {
        // For properties that ExportPropertyToJsonValue can't handle,
        // export as string via the property's ExportText
        FString ValueStr;
        MCP_PROPERTY_EXPORT_TEXT(Prop, ValueStr, Prop->ContainerPtrToValuePtr<void>(Container),
                                nullptr, nullptr, PPF_None);
        if (!ValueStr.IsEmpty())
        {
          PropertiesObj->SetStringField(Prop->GetName(), ValueStr);
        }
      }
    }
    Resp->SetObjectField(TEXT("properties"), PropertiesObj);
  }

  // Tags - only for Actor-derived classes to avoid assertion failure
  TArray<TSharedPtr<FJsonValue>> TagsArray;
  UClass* ObjClass = TargetObject->GetClass();
  // Check if the class is actually an Actor or Actor-derived class before calling GetDefaultObject<AActor>()
  // Using IsChildOf instead of Cast to avoid assertion in UE 5.7
  if (ObjClass && ObjClass->IsChildOf(AActor::StaticClass())) {
    if (AActor* DefaultActor = ObjClass->GetDefaultObject<AActor>()) {
      for (const FName &Tag : DefaultActor->Tags) {
        TagsArray.Add(MakeShared<FJsonValueString>(Tag.ToString()));
      }
    }
  }
  Resp->SetArrayField(TEXT("tags"), TagsArray);

  SendAutomationResponse(RequestingSocket, RequestId, true,
                         TEXT("Object inspection completed"), Resp, FString());
  return true;
#else
  SendAutomationResponse(RequestingSocket, RequestId, false,
                         TEXT("inspect requires editor build"), nullptr,
                         TEXT("NOT_IMPLEMENTED"));
  return true;
#endif
}
