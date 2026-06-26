#include "McpAutomationBridgeGlobals.h"
#include "Dom/JsonObject.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Public/Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/MonitoredProcess.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Exporters/Exporter.h"
#include "Misc/FileHelper.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

bool UMcpAutomationBridgeSubsystem::HandleSystemControlAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  // The sub-action is in the payload's "action" field
  FString SubAction;
  if (Payload.IsValid()) {
    Payload->TryGetStringField(TEXT("action"), SubAction);
  }
  
  const FString Lower = SubAction.ToLower();
  
  // Check if this handler should process this sub-action
  if (!Lower.StartsWith(TEXT("run_ubt")) &&
      !Lower.StartsWith(TEXT("run_tests")) &&
      !Lower.StartsWith(TEXT("test_progress")) &&
      !Lower.StartsWith(TEXT("test_stale")) &&
      Lower != TEXT("export_asset") &&
      Lower != TEXT("show_notification") &&
      Lower != TEXT("read_log") &&
      Lower != TEXT("generate_project_files") &&
      Lower != TEXT("regenerate_project_files") &&
      Lower != TEXT("cook") &&
      Lower != TEXT("cook_content") &&
      Lower != TEXT("live_coding") &&
      Lower != TEXT("hot_reload") &&
      Lower != TEXT("recompile")) {
    return false; // Not handled by this function
  }

#if WITH_EDITOR
  if (!Payload.IsValid()) {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("System control payload missing"),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  if (Lower == TEXT("run_ubt")) {
    // Extract optional parameters
    FString Target;
    Payload->TryGetStringField(TEXT("target"), Target);
    
    FString Platform;
    Payload->TryGetStringField(TEXT("platform"), Platform);
    
    FString Configuration;
    Payload->TryGetStringField(TEXT("configuration"), Configuration);
    
    FString AdditionalArgs;
    Payload->TryGetStringField(TEXT("additionalArgs"), AdditionalArgs);

    // Build UBT path
    FString EngineDir = FPaths::EngineDir();
    FString UBTPath;
    
#if PLATFORM_WINDOWS
    UBTPath = FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.bat"));
#else
    UBTPath = FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.sh"));
#endif

    if (!FPaths::FileExists(UBTPath)) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("UBT not found at: %s"), *UBTPath),
                          TEXT("UBT_NOT_FOUND"));
      return true;
    }

    // Build command line arguments
    FString Arguments;
    
    // Target (project or engine target)
    if (!Target.IsEmpty()) {
      Arguments += Target + TEXT(" ");
    } else {
      // Default to current project
      FString ProjectPath = FPaths::GetProjectFilePath();
      if (!ProjectPath.IsEmpty()) {
        Arguments += FString::Printf(TEXT("-project=\"%s\" "), *ProjectPath);
      }
    }
    
    // Platform
    if (!Platform.IsEmpty()) {
      Arguments += Platform + TEXT(" ");
    } else {
#if PLATFORM_WINDOWS
      Arguments += TEXT("Win64 ");
#elif PLATFORM_MAC
      Arguments += TEXT("Mac ");
#else
      Arguments += TEXT("Linux ");
#endif
    }
    
    // Configuration
    if (!Configuration.IsEmpty()) {
      Arguments += Configuration + TEXT(" ");
    } else {
      Arguments += TEXT("Development ");
    }
    
    // Additional args
    if (!AdditionalArgs.IsEmpty()) {
      Arguments += AdditionalArgs;
    }

    // Use FMonitoredProcess for non-blocking execution with output capture
    // For simplicity, we'll use a synchronous approach with timeout
    int32 ReturnCode = -1;
    FString StdOut;
    FString StdErr;
    
    // Note: FPlatformProcess::ExecProcess is simpler but blocks
    // Using CreateProc with pipes for better control
    void* ReadPipe = nullptr;
    void* WritePipe = nullptr;
    FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
    
    FProcHandle ProcessHandle = FPlatformProcess::CreateProc(
        *UBTPath,
        *Arguments,
        false,  // bLaunchDetached
        true,   // bLaunchHidden
        true,   // bLaunchReallyHidden
        nullptr, // OutProcessID
        0,      // PriorityModifier
        nullptr, // OptionalWorkingDirectory
        WritePipe // PipeWriteChild
    );

    if (!ProcessHandle.IsValid()) {
      FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Failed to launch UBT process"),
                          TEXT("PROCESS_LAUNCH_FAILED"));
      return true;
    }

    // Read output with timeout (30 seconds max wait, but check periodically)
    const double TimeoutSeconds = 300.0; // 5 minute timeout for builds
    const double StartTime = FPlatformTime::Seconds();
    
    while (FPlatformProcess::IsProcRunning(ProcessHandle)) {
      // Read available output
      FString NewOutput = FPlatformProcess::ReadPipe(ReadPipe);
      if (!NewOutput.IsEmpty()) {
        StdOut += NewOutput;
      }
      
      // Check timeout
      if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds) {
        FPlatformProcess::TerminateProc(ProcessHandle, true);
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
        
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("output"), StdOut);
        Result->SetBoolField(TEXT("timedOut"), true);
        
        SendAutomationResponse(RequestingSocket, RequestId, false,
                               TEXT("UBT process timed out"), Result,
                               TEXT("TIMEOUT"));
        return true;
      }
      
      // Small sleep to avoid busy waiting
      FPlatformProcess::Sleep(0.1f);
    }

    // Read any remaining output
    FString FinalOutput = FPlatformProcess::ReadPipe(ReadPipe);
    if (!FinalOutput.IsEmpty()) {
      StdOut += FinalOutput;
    }

    // Get return code
    FPlatformProcess::GetProcReturnCode(ProcessHandle, &ReturnCode);
    FPlatformProcess::CloseProc(ProcessHandle);
    FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("output"), StdOut);
    Result->SetNumberField(TEXT("returnCode"), ReturnCode);
    Result->SetStringField(TEXT("ubtPath"), UBTPath);
    Result->SetStringField(TEXT("arguments"), Arguments);

    if (ReturnCode == 0) {
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("UBT completed successfully"), Result);
    } else {
      SendAutomationResponse(RequestingSocket, RequestId, false,
                             FString::Printf(TEXT("UBT failed with code %d"), ReturnCode),
                             Result, TEXT("UBT_FAILED"));
    }
    return true;
  } else if (Lower == TEXT("run_tests")) {
    // Extract test filter
    FString Filter;
    Payload->TryGetStringField(TEXT("filter"), Filter);
    
    FString TestName;
    Payload->TryGetStringField(TEXT("test"), TestName);
    
    // If specific test name provided, use it as filter
    if (!TestName.IsEmpty() && Filter.IsEmpty()) {
      Filter = TestName;
    }
    
    // Build automation test command
    FString TestCommand;
    if (Filter.IsEmpty()) {
      // Run all tests
      TestCommand = TEXT("automation RunAll");
    } else {
      // Run filtered tests
      TestCommand = FString::Printf(TEXT("automation RunTests %s"), *Filter);
    }

    // Execute the automation command
    if (GEngine && GEditor && GEditor->GetEditorWorldContext().World()) {
      GEngine->Exec(GEditor->GetEditorWorldContext().World(), *TestCommand);
      
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      Result->SetStringField(TEXT("command"), TestCommand);
      Result->SetStringField(TEXT("filter"), Filter);
      
      // Note: Automation tests run asynchronously in UE.
      // The command starts the tests, but results come later via automation framework.
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             TEXT("Automation tests started. Check Output Log for results."),
                             Result);
    } else {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("Editor world not available for running tests"),
                          TEXT("EDITOR_NOT_AVAILABLE"));
    }
    return true;
  } else   if (Lower == TEXT("test_progress_protocol")) {
    // Test action for heartbeat/progress protocol
    // Simulates a long-running operation with progress updates
    UE_LOG(LogMcpAutomationBridgeSubsystem, Log,
           TEXT("test_progress_protocol: Handler called successfully"));
    int32 Steps = 5;
    Payload->TryGetNumberField(TEXT("steps"), Steps);
    Steps = FMath::Clamp(Steps, 1, 20);
    
    float StepDurationMs = 500.0f;
    Payload->TryGetNumberField(TEXT("stepDurationMs"), StepDurationMs);
    StepDurationMs = FMath::Clamp(StepDurationMs, 100.0f, 5000.0f);
    
    bool bSendProgress = true;
    if (Payload->HasField(TEXT("sendProgress"))) {
      bSendProgress = Payload->GetBoolField(TEXT("sendProgress"));
    }
    
    UE_LOG(LogMcpAutomationBridgeSubsystem, Log,
           TEXT("test_progress_protocol: Starting %d steps, %.0fms each, progress=%s"),
           Steps, StepDurationMs, bSendProgress ? TEXT("true") : TEXT("false"));
    
    for (int32 i = 1; i <= Steps; i++) {
      // Send progress update before each step
      if (bSendProgress) {
        float Percent = (static_cast<float>(i) / static_cast<float>(Steps)) * 100.0f;
        FString StatusMsg = FString::Printf(TEXT("Step %d/%d"), i, Steps);
        SendProgressUpdate(RequestId, Percent, StatusMsg, true);
      }
      
      // Simulate work by sleeping
      FPlatformProcess::Sleep(StepDurationMs / 1000.0f);
    }
    
    // Send final progress indicating completion
    if (bSendProgress) {
      SendProgressUpdate(RequestId, 100.0f, TEXT("Complete"), false);
    }
    
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("steps"), Steps);
    Result->SetNumberField(TEXT("stepDurationMs"), StepDurationMs);
    Result->SetBoolField(TEXT("progressSent"), bSendProgress);
    Result->SetStringField(TEXT("message"), TEXT("Progress protocol test completed"));
    
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Progress protocol test completed successfully"), Result);
    return true;
  } else if (Lower == TEXT("test_stale_progress")) {
    // Test action for stale progress detection
    // Sends the same progress percent multiple times to trigger stale detection
    int32 StaleCount = 5;
    Payload->TryGetNumberField(TEXT("staleCount"), StaleCount);
    StaleCount = FMath::Clamp(StaleCount, 1, 10);
    
    UE_LOG(LogMcpAutomationBridgeSubsystem, Log,
           TEXT("test_stale_progress: Sending %d stale updates"), StaleCount);
    
    // Send same progress repeatedly to trigger stale detection
    for (int32 i = 0; i < StaleCount; i++) {
      FString StatusMsg = FString::Printf(TEXT("Stale update %d/%d"), i + 1, StaleCount);
      SendProgressUpdate(RequestId, 50.0f, StatusMsg, true);  // Always 50%
      FPlatformProcess::Sleep(0.1f);  // 100ms between updates
    }
    
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("staleUpdatesSent"), StaleCount);
    Result->SetBoolField(TEXT("staleDetectionExpected"), true);
    
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Stale progress test completed"), Result);
    return true;
  } else if (Lower == TEXT("export_asset")) {
    // Export asset to FBX/OBJ/other format
    FString AssetPath;
    Payload->TryGetStringField(TEXT("assetPath"), AssetPath);
    
    FString ExportPath;
    Payload->TryGetStringField(TEXT("exportPath"), ExportPath);
    
    if (AssetPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("assetPath is required for export"),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }
    
    if (ExportPath.IsEmpty()) {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("exportPath is required for export"),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }
    
    // Check if asset exists
    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath)) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Asset not found: %s"), *AssetPath),
                          TEXT("ASSET_NOT_FOUND"));
      return true;
    }
    
    // Ensure export directory exists
    FString ExportDir = FPaths::GetPath(ExportPath);
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ExportDir)) {
      PlatformFile.CreateDirectoryTree(*ExportDir);
    }
    
    // Load the asset
    UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
    if (!Asset) {
      SendAutomationError(RequestingSocket, RequestId,
                          FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath),
                          TEXT("LOAD_FAILED"));
      return true;
    }
    
    // Determine export format from file extension
    FString Extension = FPaths::GetExtension(ExportPath).ToLower();
    
    // Try generic asset export via AssetTools
    bool bExportSuccess = false;
    FString ExportError;
    
    // CRITICAL FIX: Use AssetTools ExportAssets with explicit export path
    // This performs automated export without showing modal dialogs
    // The bPromptForIndividualFilenames=false suppresses file dialogs
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();
    
    // Use ExportAssets with explicit path - this suppresses dialogs for automated export
    // The asset will be exported with its original name to the specified directory
    TArray<UObject*> AssetsToExport;
    AssetsToExport.Add(Asset);
    
    // ExportAssets exports to the specified directory with the asset's name
    // For custom filename, we need to rename temporarily or use UExporter directly
    AssetTools.ExportAssets(AssetsToExport, ExportDir);
    
    // Check if file was created
    FString ExpectedExportPath = ExportDir / FPaths::GetBaseFilename(AssetPath) + TEXT(".") + Extension;
    if (FPaths::FileExists(ExpectedExportPath))
    {
      bExportSuccess = true;
    }
    else
    {
      // Try with the actual requested filename
      bExportSuccess = FPaths::FileExists(ExportPath);
    }
    
    if (!bExportSuccess)
    {
      // Fallback: Use UExporter::ExportToFile directly with Prompt=false
      UExporter* Exporter = nullptr;
      
      // Find appropriate exporter for the asset type and extension
      for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt) {
        UClass* CurrentClass = *ClassIt;
        if (CurrentClass->IsChildOf(UExporter::StaticClass()) && !CurrentClass->HasAnyClassFlags(CLASS_Abstract)) {
          UExporter* DefaultExporter = Cast<UExporter>(CurrentClass->GetDefaultObject());
          if (DefaultExporter && DefaultExporter->SupportedClass) {
            if (Asset->GetClass()->IsChildOf(DefaultExporter->SupportedClass)) {
              if (DefaultExporter->PreferredFormatIndex < DefaultExporter->FormatExtension.Num()) {
                FString PreferredExt = DefaultExporter->FormatExtension[DefaultExporter->PreferredFormatIndex].ToLower();
                if (PreferredExt == Extension || PreferredExt.Contains(Extension)) {
                  Exporter = DefaultExporter;
                  break;
                }
              }
              if (!Exporter) {
                Exporter = DefaultExporter;
              }
            }
          }
        }
      }
      
      if (Exporter) {
        // ExportToFile signature: (Object, Exporter, Filename, InSelectedOnly, NoReplaceIdentical, Prompt)
        // The last parameter (Prompt=false) should suppress dialogs for most exporters
        int32 ExportResult = UExporter::ExportToFile(Asset, Exporter, *ExportPath, false, false, false);
        bExportSuccess = (ExportResult != 0);
      }
      
      if (!bExportSuccess) {
        ExportError = FString::Printf(TEXT("Export failed for asset type '%s' and format '%s'"),
                                       *Asset->GetClass()->GetName(), *Extension);
      }
    }
    
    if (bExportSuccess) {
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      AddAssetVerification(Result, Asset);
      Result->SetStringField(TEXT("assetPath"), AssetPath);
      Result->SetStringField(TEXT("exportPath"), ExportPath);
      Result->SetStringField(TEXT("format"), Extension);
      Result->SetBoolField(TEXT("success"), true);
      
      SendAutomationResponse(RequestingSocket, RequestId, true,
                             FString::Printf(TEXT("Asset exported to: %s"), *ExportPath),
                             Result);
    } else {
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      Result->SetStringField(TEXT("assetPath"), AssetPath);
      Result->SetStringField(TEXT("exportPath"), ExportPath);
      Result->SetStringField(TEXT("format"), Extension);
      Result->SetStringField(TEXT("error"), ExportError);
      
      SendAutomationResponse(RequestingSocket, RequestId, false,
                             FString::Printf(TEXT("Export failed: %s"), *ExportError),
                             Result, TEXT("EXPORT_FAILED"));
    }
    return true;
  } else if (Lower == TEXT("show_notification")) {
    FString Message;
    Payload->TryGetStringField(TEXT("message"), Message);
    if (Message.IsEmpty())
    {
      SendAutomationError(RequestingSocket, RequestId,
                          TEXT("message is required"), TEXT("INVALID_ARGUMENT"));
      return true;
    }

    FString TypeStr;
    Payload->TryGetStringField(TEXT("type"), TypeStr);
    double Duration = 5.0;
    Payload->TryGetNumberField(TEXT("duration"), Duration);

    SNotificationItem::ECompletionState CompletionState = SNotificationItem::CS_None;
    if (TypeStr.Equals(TEXT("success"), ESearchCase::IgnoreCase))
      CompletionState = SNotificationItem::CS_Success;
    else if (TypeStr.Equals(TEXT("fail"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("error"), ESearchCase::IgnoreCase))
      CompletionState = SNotificationItem::CS_Fail;
    else if (TypeStr.Equals(TEXT("pending"), ESearchCase::IgnoreCase))
      CompletionState = SNotificationItem::CS_Pending;

    FNotificationInfo Info(FText::FromString(Message));
    Info.bFireAndForget = true;
    Info.ExpireDuration = static_cast<float>(Duration);
    Info.bUseThrobber = (CompletionState == SNotificationItem::CS_Pending);

    TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
    if (NotificationItem.IsValid() && CompletionState != SNotificationItem::CS_None)
    {
      NotificationItem->SetCompletionState(CompletionState);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("message"), Message);
    Result->SetStringField(TEXT("type"), TypeStr.IsEmpty() ? TEXT("info") : TypeStr);
    Result->SetNumberField(TEXT("duration"), Duration);
    SendAutomationResponse(RequestingSocket, RequestId, true,
                           TEXT("Notification shown"), Result);
    return true;
  } else if (Lower == TEXT("read_log")) {
    // Delegate to the log handler
    TSharedPtr<FJsonObject> LogPayload = MakeShared<FJsonObject>();
    LogPayload->SetStringField(TEXT("subAction"), TEXT("read"));
    if (Payload->HasField(TEXT("count")))
      LogPayload->SetField(TEXT("count"), Payload->TryGetField(TEXT("count")));
    if (Payload->HasField(TEXT("category")))
      LogPayload->SetField(TEXT("category"), Payload->TryGetField(TEXT("category")));
    if (Payload->HasField(TEXT("verbosity")))
      LogPayload->SetField(TEXT("verbosity"), Payload->TryGetField(TEXT("verbosity")));
    return HandleLogAction(RequestId, TEXT("manage_logs"), LogPayload, RequestingSocket);
  }

  // --- Generate project files ---
  if (Lower == TEXT("generate_project_files") || Lower == TEXT("regenerate_project_files"))
  {
    FString UProjectPath = FPaths::GetProjectFilePath();
    FString UBTPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Build/BatchFiles/Build.bat"));

    FString EnginePath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
    FString FullProjectPath = FPaths::ConvertRelativePathToFull(UProjectPath);

    // Try GenerateProjectFiles script first (source builds)
    FString GenerateScript;
#if PLATFORM_WINDOWS
    GenerateScript = FPaths::Combine(EnginePath, TEXT("Build/BatchFiles/GenerateProjectFiles.bat"));
#else
    GenerateScript = FPaths::Combine(EnginePath, TEXT("Build/BatchFiles/Linux/GenerateProjectFiles.sh"));
#endif

    FString ExePath;
    FString Args;

    if (FPaths::FileExists(GenerateScript))
    {
      ExePath = GenerateScript;
      Args = FString::Printf(TEXT("-projectfiles -project=\"%s\" -game -engine"), *FullProjectPath);
    }
    else
    {
      // Fallback for installed engine builds: use UnrealBuildTool directly
      FString UBTExe;
#if PLATFORM_WINDOWS
      UBTExe = FPaths::Combine(EnginePath, TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe"));
#else
      UBTExe = FPaths::Combine(EnginePath, TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool"));
#endif

      if (!FPaths::FileExists(UBTExe))
      {
        SendAutomationResponse(RequestingSocket, RequestId, false,
                               FString::Printf(TEXT("Neither GenerateProjectFiles script (%s) nor UBT (%s) found"), *GenerateScript, *UBTExe),
                               nullptr, TEXT("SCRIPT_NOT_FOUND"));
        return true;
      }

      ExePath = UBTExe;
      Args = FString::Printf(TEXT("-projectfiles -project=\"%s\" -game -engine -progress"), *FullProjectPath);
    }

    FProcHandle Proc = FPlatformProcess::CreateProc(*ExePath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);
    bool bLaunched = Proc.IsValid();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("launched"), bLaunched);
    Result->SetStringField(TEXT("script"), GenerateScript);
    Result->SetStringField(TEXT("projectPath"), UProjectPath);
    SendAutomationResponse(RequestingSocket, RequestId, bLaunched,
                           bLaunched ? TEXT("Project file generation started") : TEXT("Failed to launch GenerateProjectFiles"),
                           Result);
    return true;
  }

  // --- Cook content ---
  if (Lower == TEXT("cook") || Lower == TEXT("cook_content"))
  {
    FString Platform;
    Payload->TryGetStringField(TEXT("platform"), Platform);
    if (Platform.IsEmpty())
      Platform = TEXT("WindowsEditor");

    FString UProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    FString EditorExe = FPaths::ConvertRelativePathToFull(FPlatformProcess::ExecutablePath());

    // Build cook command line
    FString CookArgs = FString::Printf(
        TEXT("\"%s\" -run=cook -targetplatform=%s -iterate -unversioned"),
        *UProjectPath, *Platform);

    FProcHandle Proc = FPlatformProcess::CreateProc(*EditorExe, *CookArgs, true, false, false, nullptr, 0, nullptr, nullptr);
    bool bLaunched = Proc.IsValid();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("launched"), bLaunched);
    Result->SetStringField(TEXT("platform"), Platform);
    Result->SetStringField(TEXT("projectPath"), UProjectPath);
    SendAutomationResponse(RequestingSocket, RequestId, bLaunched,
                           bLaunched ? FString::Printf(TEXT("Content cook started for %s"), *Platform) : TEXT("Failed to launch cook"),
                           Result);
    return true;
  }

  // --- Live coding / hot reload ---
  if (Lower == TEXT("live_coding") || Lower == TEXT("hot_reload") || Lower == TEXT("recompile"))
  {
    bool bSuccess = false;
    FString Message;

    // Trigger Live Coding via console command (avoids module dependency)
    if (GEditor && GEditor->GetEditorWorldContext().World())
    {
      GEditor->GetEditorWorldContext().World()->Exec(
          GEditor->GetEditorWorldContext().World(), TEXT("LiveCoding.Compile"));
      bSuccess = true;
      Message = TEXT("Live Coding compile triggered via console command");
    }
    else
    {
      Message = TEXT("No editor world available for Live Coding");
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("triggered"), bSuccess);
    SendAutomationResponse(RequestingSocket, RequestId, bSuccess, Message, Result);
    return true;
  }

  return false;
#else
  SendAutomationResponse(RequestingSocket, RequestId, false,
                         TEXT("System control actions require editor build"),
                         nullptr, TEXT("NOT_IMPLEMENTED"));
  return true;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleBatchAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket) {
  const TArray<TSharedPtr<FJsonValue>>* RequestsArray = nullptr;
  if (!Payload || !Payload->TryGetArrayField(TEXT("requests"), RequestsArray) || !RequestsArray)
  {
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("requests array is required"), TEXT("INVALID_ARGUMENT"));
    return true;
  }

  TArray<TSharedPtr<FJsonValue>> Results;
  int32 SuccessCount = 0;
  int32 FailCount = 0;

  for (int32 i = 0; i < RequestsArray->Num(); ++i)
  {
    const TSharedPtr<FJsonValue>& ReqVal = (*RequestsArray)[i];
    if (ReqVal->Type != EJson::Object)
    {
      TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
      ErrObj->SetNumberField(TEXT("index"), i);
      ErrObj->SetBoolField(TEXT("success"), false);
      ErrObj->SetStringField(TEXT("error"), TEXT("Expected object in requests array"));
      Results.Add(MakeShared<FJsonValueObject>(ErrObj));
      ++FailCount;
      continue;
    }

    TSharedPtr<FJsonObject> ReqObj = ReqVal->AsObject();
    FString SubAction;
    ReqObj->TryGetStringField(TEXT("action"), SubAction);
    if (SubAction.IsEmpty())
    {
      TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
      ErrObj->SetNumberField(TEXT("index"), i);
      ErrObj->SetBoolField(TEXT("success"), false);
      ErrObj->SetStringField(TEXT("error"), TEXT("action field required in each request"));
      Results.Add(MakeShared<FJsonValueObject>(ErrObj));
      ++FailCount;
      continue;
    }

    // Generate a sub-request ID
    FString SubRequestId = FString::Printf(TEXT("%s_batch_%d"), *RequestId, i);

    // Capture the response by processing synchronously
    // We create the payload from the request object itself
    TSharedPtr<FJsonObject> SubPayload = ReqObj;

    // Process the sub-request through the normal handler chain
    // We track the result via a simple success check
    bool bSubHandled = false;

    // Try the handler registry first (O(1) lookup)
    if (const FAutomationHandler* Handler = AutomationHandlers.Find(SubAction))
    {
      bSubHandled = (*Handler)(SubRequestId, SubAction, SubPayload, RequestingSocket);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetNumberField(TEXT("index"), i);
    ResultObj->SetStringField(TEXT("action"), SubAction);
    ResultObj->SetBoolField(TEXT("dispatched"), bSubHandled);
    if (bSubHandled)
      ++SuccessCount;
    else
      ++FailCount;
    Results.Add(MakeShared<FJsonValueObject>(ResultObj));
  }

  TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
  Result->SetNumberField(TEXT("totalRequests"), RequestsArray->Num());
  Result->SetNumberField(TEXT("dispatched"), SuccessCount);
  Result->SetNumberField(TEXT("failed"), FailCount);
  Result->SetArrayField(TEXT("results"), Results);
  SendAutomationResponse(RequestingSocket, RequestId, SuccessCount > 0,
                         FString::Printf(TEXT("Batch: %d dispatched, %d failed of %d"),
                                         SuccessCount, FailCount, RequestsArray->Num()),
                         Result);
  return true;
}
