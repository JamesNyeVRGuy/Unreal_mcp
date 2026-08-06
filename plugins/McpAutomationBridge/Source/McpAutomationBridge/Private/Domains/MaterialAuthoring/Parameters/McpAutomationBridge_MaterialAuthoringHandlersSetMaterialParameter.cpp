#include "Domains/MaterialAuthoring/McpAutomationBridge_MaterialAuthoringHandlersPrivate.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"

#if WITH_EDITOR
namespace McpMaterialAuthoringHandlers
{
namespace
{
// Accepts JSON number or numeric string ("0.7") -- MCP clients stringify
// values for union-typed schema fields, so both must resolve.
bool ReadNumericValue(const TSharedPtr<FJsonValue>& Value, double& OutNumber)
{
  if (!Value.IsValid()) return false;
  if (Value->Type == EJson::Number) {
    OutNumber = Value->AsNumber();
    return true;
  }
  if (Value->Type == EJson::String) {
    return LexTryParseString(OutNumber, *Value->AsString());
  }
  return false;
}

// Accepts {r,g,b,a} / {x,y,z,w} objects or a [r,g,b,a] array.
bool ReadColorValue(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
{
  if (!Value.IsValid()) return false;
  if (Value->Type == EJson::Object) {
    const TSharedPtr<FJsonObject> Obj = Value->AsObject();
    double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
    const bool bRgb = Obj->TryGetNumberField(TEXT("r"), R) | Obj->TryGetNumberField(TEXT("g"), G) | Obj->TryGetNumberField(TEXT("b"), B);
    const bool bXyz = Obj->TryGetNumberField(TEXT("x"), R) | Obj->TryGetNumberField(TEXT("y"), G) | Obj->TryGetNumberField(TEXT("z"), B);
    if (!bRgb && !bXyz) return false;
    if (!Obj->TryGetNumberField(TEXT("a"), A)) Obj->TryGetNumberField(TEXT("w"), A);
    OutColor = FLinearColor((float)R, (float)G, (float)B, (float)A);
    return true;
  }
  if (Value->Type == EJson::Array) {
    const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
    if (Arr.Num() < 3) return false;
    OutColor = FLinearColor(
        (float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber(), (float)Arr[2]->AsNumber(),
        Arr.Num() > 3 ? (float)Arr[3]->AsNumber() : 1.0f);
    return true;
  }
  return false;
}
} // namespace

bool HandleSetMaterialParameter(UMcpAutomationBridgeSubsystem* Bridge, const FString& RequestId, const FString& SubAction, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FMcpBridgeWebSocket> Socket)
{
  if (SubAction == TEXT("set_material_parameter")) {
    FString AssetPath, ParameterName;
    if (!Payload->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty()) {
      Bridge->SendAutomationError(Socket, RequestId, TEXT("Missing 'assetPath'."), TEXT("INVALID_ARGUMENT"));
      return true;
    }
    if (!Payload->TryGetStringField(TEXT("parameterName"), ParameterName) || ParameterName.IsEmpty()) {
      Bridge->SendAutomationError(Socket, RequestId, TEXT("Missing 'parameterName'."), TEXT("INVALID_ARGUMENT"));
      return true;
    }

    // SECURITY: Validate assetPath before use (accepts both Materials and Material Instances)
    FString ValidatedAssetPath = SanitizeProjectRelativePath(AssetPath);
    if (ValidatedAssetPath.IsEmpty()) {
      Bridge->SendAutomationError(Socket, RequestId,
                          FString::Printf(TEXT("Invalid assetPath '%s': contains traversal sequences or invalid root"), *AssetPath),
                          TEXT("INVALID_PATH"));
      return true;
    }
    AssetPath = ValidatedAssetPath;

    TSharedPtr<FJsonValue> Value = Payload->TryGetField(TEXT("value"));
    if (!Value.IsValid()) Value = Payload->TryGetField(TEXT("defaultValue"));
    if (!Value.IsValid()) {
      Bridge->SendAutomationError(Socket, RequestId,
                          TEXT("Missing 'value' (number for scalar, {r,g,b,a} for vector, bool for switch, texture path for texture)."),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    double NumberValue = 0.0;
    FLinearColor ColorValue = FLinearColor::White;

    // Material instance: route to the editor-only instance parameter API by value type.
    if (UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *AssetPath)) {
      bool bApplied = false;
      FString Applied;
      if (ReadColorValue(Value, ColorValue)) {
        Instance->SetVectorParameterValueEditorOnly(FName(*ParameterName), ColorValue);
        Applied = TEXT("vector");
        bApplied = true;
      } else if (Value->Type == EJson::Boolean) {
        Bridge->SendAutomationError(Socket, RequestId,
                            TEXT("Static switch overrides on instances are not supported by set_material_parameter; use set_static_switch_parameter_value."),
                            TEXT("INVALID_ARGUMENT"));
        return true;
      } else if (ReadNumericValue(Value, NumberValue)) {
        Instance->SetScalarParameterValueEditorOnly(FName(*ParameterName), (float)NumberValue);
        Applied = TEXT("scalar");
        bApplied = true;
      } else if (Value->Type == EJson::String) {
        const FString TexturePath = SanitizeProjectRelativePath(Value->AsString());
        UTexture* Texture = TexturePath.IsEmpty() ? nullptr : LoadObject<UTexture>(nullptr, *TexturePath);
        if (!Texture) {
          Bridge->SendAutomationError(Socket, RequestId,
                              FString::Printf(TEXT("Could not load texture '%s'."), *Value->AsString()),
                              TEXT("ASSET_NOT_FOUND"));
          return true;
        }
        Instance->SetTextureParameterValueEditorOnly(FName(*ParameterName), Texture);
        Applied = TEXT("texture");
        bApplied = true;
      }
      if (!bApplied) {
        Bridge->SendAutomationError(Socket, RequestId, TEXT("Unsupported 'value' type for a material instance parameter."), TEXT("INVALID_ARGUMENT"));
        return true;
      }
      Instance->PostEditChange();
      Instance->MarkPackageDirty();
      bool bSave = true;
      Payload->TryGetBoolField(TEXT("save"), bSave);
      if (bSave) SaveMaterialInstanceAsset(Instance);

      TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
      McpHandlerUtils::AddVerification(Result, Instance);
      Result->SetStringField(TEXT("parameterName"), ParameterName);
      Result->SetStringField(TEXT("parameterKind"), Applied);
      Bridge->SendAutomationResponse(Socket, RequestId, true,
          FString::Printf(TEXT("Instance %s parameter '%s' set."), *Applied, *ParameterName), Result);
      return true;
    }

    // Base material (or material function): set the parameter expression's default.
    UMaterial* Material = nullptr;
    UMaterialFunction* Function = nullptr;
    LoadMaterialOrFunction(AssetPath, Material, Function);
    if (!Material && !Function) {
      Bridge->SendAutomationError(Socket, RequestId, TEXT("Could not load Material, Material Function, or Material Instance."), TEXT("ASSET_NOT_FOUND"));
      return true;
    }

    UMaterialExpression* Expr = FIND_EXPR_IN_HOST(ParameterName);
    if (!Expr) {
      Bridge->SendAutomationError(Socket, RequestId,
                          FString::Printf(TEXT("No parameter expression named '%s' found."), *ParameterName),
                          TEXT("NODE_NOT_FOUND"));
      return true;
    }

    FString Applied;
    if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr)) {
      if (!ReadNumericValue(Value, NumberValue)) {
        Bridge->SendAutomationError(Socket, RequestId, TEXT("Scalar parameter requires a numeric 'value'."), TEXT("INVALID_ARGUMENT"));
        return true;
      }
      Scalar->DefaultValue = (float)NumberValue;
      Applied = TEXT("scalar");
    } else if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expr)) {
      if (!ReadColorValue(Value, ColorValue)) {
        Bridge->SendAutomationError(Socket, RequestId, TEXT("Vector parameter requires an {r,g,b,a} object or [r,g,b,a] array 'value'."), TEXT("INVALID_ARGUMENT"));
        return true;
      }
      Vector->DefaultValue = ColorValue;
      Applied = TEXT("vector");
    } else if (UMaterialExpressionStaticBoolParameter* Switch = Cast<UMaterialExpressionStaticBoolParameter>(Expr)) {
      if (Value->Type != EJson::Boolean) {
        Bridge->SendAutomationError(Socket, RequestId, TEXT("Static switch parameter requires a boolean 'value'."), TEXT("INVALID_ARGUMENT"));
        return true;
      }
      Switch->DefaultValue = Value->AsBool();
      Applied = TEXT("switch");
    } else if (UMaterialExpressionTextureSampleParameter* TextureParam = Cast<UMaterialExpressionTextureSampleParameter>(Expr)) {
      if (Value->Type != EJson::String) {
        Bridge->SendAutomationError(Socket, RequestId, TEXT("Texture parameter requires a texture asset path 'value'."), TEXT("INVALID_ARGUMENT"));
        return true;
      }
      const FString TexturePath = SanitizeProjectRelativePath(Value->AsString());
      UTexture* Texture = TexturePath.IsEmpty() ? nullptr : LoadObject<UTexture>(nullptr, *TexturePath);
      if (!Texture) {
        Bridge->SendAutomationError(Socket, RequestId,
                            FString::Printf(TEXT("Could not load texture '%s'."), *Value->AsString()),
                            TEXT("ASSET_NOT_FOUND"));
        return true;
      }
      TextureParam->Texture = Texture;
      Applied = TEXT("texture");
    } else {
      Bridge->SendAutomationError(Socket, RequestId,
                          FString::Printf(TEXT("Expression '%s' (%s) is not a settable parameter type."), *ParameterName, *Expr->GetClass()->GetName()),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    Expr->PostEditChange();
    FINALIZE_HOST();

    bool bSave = false;
    Payload->TryGetBoolField(TEXT("save"), bSave);
    if (bSave && Material) SaveMaterialAsset(Material);

    TSharedPtr<FJsonObject> Result = McpHandlerUtils::CreateResultObject();
    Result->SetStringField(TEXT("parameterName"), ParameterName);
    Result->SetStringField(TEXT("parameterKind"), Applied);
    Result->SetStringField(TEXT("nodeId"), MCP_NODE_ID(Expr));
    Bridge->SendAutomationResponse(Socket, RequestId, true,
        FString::Printf(TEXT("%s parameter '%s' default set."), *Applied, *ParameterName), Result);
    return true;
  }

  // --------------------------------------------------------------------------
  // get_material_node_details
  // --------------------------------------------------------------------------
  return false;
}
}
#endif
