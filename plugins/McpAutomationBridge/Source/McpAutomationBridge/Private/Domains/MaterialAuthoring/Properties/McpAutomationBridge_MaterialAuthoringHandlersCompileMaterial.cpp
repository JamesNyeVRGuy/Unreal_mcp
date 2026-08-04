#include "Domains/MaterialAuthoring/McpAutomationBridge_MaterialAuthoringHandlersPrivate.h"

#include "MaterialShared.h"
#include "ShaderCompiler.h"

#if WITH_EDITOR
namespace McpMaterialAuthoringHandlers
{
bool HandleCompileMaterial(UMcpAutomationBridgeSubsystem* Bridge, const FString& RequestId, const FString& SubAction, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FMcpBridgeWebSocket> Socket)
{
  if (SubAction == TEXT("compile_material")) {
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("assetPath"), AssetPath) ||
        AssetPath.IsEmpty()) {
      Bridge->SendAutomationError(Socket, RequestId, TEXT("Missing 'assetPath'."),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    // Validate path security BEFORE loading asset
    FString ValidatedPath = SanitizeProjectRelativePath(AssetPath);
    if (ValidatedPath.IsEmpty()) {
      Bridge->SendAutomationError(Socket, RequestId,
                          FString::Printf(TEXT("Invalid path '%s': contains traversal sequences or invalid root"), *AssetPath),
                          TEXT("INVALID_PATH"));
      return true;
    }
    AssetPath = ValidatedPath;

    UMaterial *Material = nullptr;
    UMaterialFunction *Function = nullptr;
    LoadMaterialOrFunction(AssetPath, Material, Function);
    if (!Material && !Function) {
      Bridge->SendAutomationError(Socket, RequestId,
                          TEXT("Could not load Material or Material Function."),
                          TEXT("ASSET_NOT_FOUND"));
      return true;
    }

    // Force recompile / update
    UObject *Host = Material ? static_cast<UObject*>(Material) : static_cast<UObject*>(Function);
    Host->PreEditChange(nullptr);
    Host->PostEditChange();
    Host->MarkPackageDirty();

    // PostEditChange alone may compile NOTHING for an unrendered material (shader caching
    // is demand-driven), so force the recompile, block on it, and surface the compiler's
    // verdict -- otherwise a broken material reads as green and the failure only appears
    // at runtime as the default material.
    TArray<FString> CompileErrors;
    bool bShaderMapValid = true;
    if (Material) {
      Material->ForceRecompileForRendering();
      if (GShaderCompilingManager) {
        GShaderCompilingManager->FinishAllCompilation();
      }
      if (FMaterialResource *Resource = Material->GetMaterialResource(GMaxRHIFeatureLevel)) {
        CompileErrors = Resource->GetCompileErrors();
        // A failed compile can leave the errors list empty on this resource instance but
        // never produces a usable shader map -- treat a missing map as failure too.
        bShaderMapValid = Resource->GetGameThreadShaderMap() != nullptr;
      }
    }
    const bool bShadersCompiled = bShaderMapValid && CompileErrors.Num() == 0;

    bool bSave = true;
    Payload->TryGetBoolField(TEXT("save"), bSave);
    if (bSave) {
      if (Material) {
        SaveMaterialAsset(Material);
      } else {
        SaveMaterialFunctionAsset(Function);
      }
    }

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("assetType"),
                           Material ? TEXT("Material") : TEXT("MaterialFunction"));
    if (!bShadersCompiled) {
      // Error surface so clients see the compiler's verdict inline (a success-shaped
      // response with success:false renders as a bare 'Operation failed').
      FString ErrorMessage = TEXT("Material shader compile FAILED");
      if (CompileErrors.Num() == 0) {
        ErrorMessage += TEXT(": shader map missing (no reported errors)");
      }
      constexpr int32 kMaxInlineErrors = 3;
      for (int32 ErrorIndex = 0; ErrorIndex < FMath::Min(CompileErrors.Num(), kMaxInlineErrors); ++ErrorIndex) {
        ErrorMessage += FString::Printf(TEXT("\n%s"), *CompileErrors[ErrorIndex]);
      }
      if (CompileErrors.Num() > kMaxInlineErrors) {
        ErrorMessage += FString::Printf(TEXT("\n(+%d more)"), CompileErrors.Num() - kMaxInlineErrors);
      }
      Bridge->SendAutomationError(Socket, RequestId, ErrorMessage, TEXT("SHADER_COMPILE_FAILED"));
      return true;
    }

    Result->SetBoolField(TEXT("compiled"), true);
    Result->SetBoolField(TEXT("saved"), bSave);
    Bridge->SendAutomationResponse(Socket, RequestId, true,
                           Material ? TEXT("Material compiled.") : TEXT("Material function updated."),
                           Result);
    return true;
  }

  // --------------------------------------------------------------------------
  // get_material_info (supports UMaterial and UMaterialFunction)
  // --------------------------------------------------------------------------
  return false;
}
}
#endif
