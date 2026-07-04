#include "Domains/WidgetAuthoring/McpAutomationBridge_WidgetAuthoringActions.h"
#include "Domains/WidgetAuthoring/Support/McpAutomationBridge_WidgetAuthoringBlueprintLoading.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Foundation/BridgeHelpers/McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"
#include "Transport/WebSocket/McpBridgeWebSocket.h"
#include "WidgetBlueprint.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"

namespace WidgetAuthoringHandlers
{
using namespace WidgetAuthoringHelpers;

bool HandleWidgetAuthoringPreview(
    UMcpAutomationBridgeSubsystem& Subsystem,
    const FString& RequestId,
    const FString& SubAction,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket,
    TSharedPtr<FJsonObject> ResultJson)
{
    // =========================================================================
    // 19.8 Utility (continued)
    // =========================================================================

    if (SubAction.Equals(TEXT("preview_widget"), ESearchCase::IgnoreCase))
    {
        FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        if (WidgetPath.IsEmpty())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Missing required parameter: widgetPath"), TEXT("MISSING_PARAMETER"));
            return true;
        }

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId, TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }

        // Widget preview is typically done by opening in editor or compiling
        // We can trigger a compile which updates the preview
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

        ResultJson->SetBoolField(TEXT("success"), true);
        ResultJson->SetStringField(TEXT("message"), TEXT("Widget blueprint marked for recompilation. Open in Widget Blueprint Editor to see preview."));
        ResultJson->SetStringField(TEXT("widgetPath"), WidgetPath);

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Widget preview updated"), ResultJson);
        return true;
    }

    // screenshot_widget - Render a UMG widget blueprint to PNG at design time.
    // Restored from feat/custom-tooling@d9fea10; dropped in the WidgetAuthoring
    // split refactor. FWidgetRenderer draws the widget into a linear-gamma
    // UTextureRenderTarget2D at the caller-specified resolution. Design-time
    // flags (EWidgetDesignFlags::Designing) are set BEFORE TakeWidget so
    // NativePreConstruct runs with bDesignTime=true -- that's what makes
    // placeholder text ("Forager", "80/100", etc.) show up in the render.
    if (SubAction.Equals(TEXT("screenshot_widget"), ESearchCase::IgnoreCase))
    {
        FString WidgetPath = GetJsonStringField(Payload, TEXT("widgetPath"));
        if (WidgetPath.IsEmpty())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Missing required parameter: widgetPath"), TEXT("MISSING_PARAMETER"));
            return true;
        }

        UWidgetBlueprint* WidgetBP = LoadWidgetBlueprint(WidgetPath);
        if (!WidgetBP)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Widget blueprint not found"), TEXT("NOT_FOUND"));
            return true;
        }

        int32 Width  = Payload->HasField(TEXT("width"))
            ? static_cast<int32>(GetJsonNumberField(Payload, TEXT("width")))  : 1920;
        int32 Height = Payload->HasField(TEXT("height"))
            ? static_cast<int32>(GetJsonNumberField(Payload, TEXT("height"))) : 1080;
        Width  = FMath::Clamp(Width,  64, 3840);
        Height = FMath::Clamp(Height, 64, 2160);

        FString Filename = GetJsonStringField(Payload, TEXT("filename"));
        if (Filename.IsEmpty())
        {
            const FString AssetName = FPaths::GetBaseFilename(WidgetPath);
            Filename = FString::Printf(TEXT("%s_%s.png"), *AssetName,
                *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
        }
        if (!Filename.EndsWith(TEXT(".png")))
        {
            Filename += TEXT(".png");
        }

        // Use the existing GeneratedClass when we can. Compiling after MCP
        // tree mutations (remove/rename/add) can crash the compiler because
        // it walks stale renamed UObjects; skip that unless we truly have no
        // generated class yet.
        UClass* WidgetClass = WidgetBP->GeneratedClass;
        if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
        {
            FKismetEditorUtilities::CompileBlueprint(WidgetBP,
                EBlueprintCompileOptions::SkipGarbageCollection);
            WidgetClass = WidgetBP->GeneratedClass;
        }
        if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Failed to compile widget blueprint"), TEXT("COMPILE_FAILED"));
            return true;
        }

        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("No editor world available"), TEXT("NO_WORLD"));
            return true;
        }

        UUserWidget* WidgetInstance = CreateWidget<UUserWidget>(World, WidgetClass);
        if (!WidgetInstance)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Failed to create widget instance"), TEXT("CREATE_FAILED"));
            return true;
        }

        WidgetInstance->SetDesignerFlags(EWidgetDesignFlags::Designing);
        WidgetInstance->SetDesiredSizeInViewport(FVector2D(Width, Height));

        TSharedPtr<SWidget> SlateWidget = WidgetInstance->TakeWidget();
        if (!SlateWidget.IsValid())
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Failed to create Slate widget"), TEXT("SLATE_FAILED"));
            return true;
        }

        // bForceLinearGamma=true: FWidgetRenderer writes gamma-space pixels
        // directly, so the RT must be linear or we double-correct and the
        // shot ends up washed out compared to the editor.
        UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
        RenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, /*bInForceLinearGamma*/ true);
        RenderTarget->UpdateResourceImmediate(true);

        FWidgetRenderer* WidgetRenderer = new FWidgetRenderer(true);
        WidgetRenderer->DrawWidget(RenderTarget, SlateWidget.ToSharedRef(),
            FVector2D(Width, Height), /*DeltaTime*/ 0.0f);
        FlushRenderingCommands();

        FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
        if (!RTResource)
        {
            delete WidgetRenderer;
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                TEXT("Failed to get render target resource"), TEXT("RENDER_FAILED"));
            return true;
        }

        TArray<FColor> Pixels;
        Pixels.SetNum(Width * Height);
        const FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
        RTResource->ReadPixels(Pixels, ReadFlags);

        const FString ScreenshotDir =
            FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Widgets");
        IFileManager::Get().MakeDirectory(*ScreenshotDir, /*Tree*/ true);
        const FString FullPath = ScreenshotDir / Filename;

        TArray64<uint8> PngData;
        FImageUtils::PNGCompressImageArray(Width, Height, Pixels, PngData);
        const bool bSaved = FFileHelper::SaveArrayToFile(PngData, *FullPath);

        delete WidgetRenderer;
        WidgetInstance->RemoveFromParent();
        WidgetInstance->ConditionalBeginDestroy();

        if (!bSaved)
        {
            Subsystem.SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Failed to save screenshot to: %s"), *FullPath),
                TEXT("SAVE_FAILED"));
            return true;
        }

        // Optional base64 payload. Matches the full_editor_window convention
        // in HandleControlEditorScreenshot -- lets the MCP client show the
        // image inline instead of always requiring a file-path round-trip.
        bool bReturnBase64 = false;
        Payload->TryGetBoolField(TEXT("returnBase64"), bReturnBase64);

        ResultJson->SetBoolField(TEXT("success"),   true);
        ResultJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Widget screenshot saved: %s"), *FullPath));
        ResultJson->SetStringField(TEXT("filePath"),  FullPath);
        ResultJson->SetStringField(TEXT("path"),      FullPath);
        ResultJson->SetStringField(TEXT("filename"),  Filename);
        ResultJson->SetNumberField(TEXT("width"),     Width);
        ResultJson->SetNumberField(TEXT("height"),    Height);
        ResultJson->SetNumberField(TEXT("sizeBytes"), static_cast<double>(PngData.Num()));
        ResultJson->SetStringField(TEXT("mimeType"),  TEXT("image/png"));
        ResultJson->SetStringField(TEXT("widgetPath"), WidgetPath);
        if (bReturnBase64)
        {
            ResultJson->SetStringField(TEXT("imageBase64"),
                FBase64::Encode(PngData.GetData(), static_cast<uint32>(PngData.Num())));
        }

        Subsystem.SendAutomationResponse(RequestingSocket, RequestId, true,
            TEXT("Widget screenshot captured"), ResultJson);
        return true;
    }

    return false;
}
}
