#include "McpAutomationBridgeGlobals.h"
#include "Dom/JsonObject.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeSubsystem.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "MaterialExpressionIO.h"
#include "Engine/Texture.h"

// Material API compatibility macros are defined in McpAutomationBridgeHelpers.h

namespace {

// Lowercase + strip whitespace. Lets callers pass "Final Color", "final color",
// or "FinalColor" interchangeably.
static FString NormalizePinKey(const FString& InName)
{
    FString Out = InName.ToLower();
    Out.ReplaceInline(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
    Out.ReplaceInline(TEXT("\t"), TEXT(""), ESearchCase::CaseSensitive);
    return Out;
}

// Resolve a material-root input by friendly name. Returns nullptr on miss.
// UI domain note: the "Final Color" pin in the UI Material editor is wired to
// the EmissiveColor field internally, and Opacity stays Opacity. Map both.
static FExpressionInput* ResolveMainMaterialInput(UMaterial* Material, const FString& InName)
{
    if (!Material) return nullptr;
    const FString Key = NormalizePinKey(InName);
    if (Key.IsEmpty()) return nullptr;
#if WITH_EDITORONLY_DATA
    if (Key == TEXT("basecolor"))                                    return &MCP_GET_MATERIAL_INPUT(Material, BaseColor);
    if (Key == TEXT("emissivecolor") || Key == TEXT("finalcolor"))   return &MCP_GET_MATERIAL_INPUT(Material, EmissiveColor);
    if (Key == TEXT("roughness"))                                    return &MCP_GET_MATERIAL_INPUT(Material, Roughness);
    if (Key == TEXT("metallic"))                                     return &MCP_GET_MATERIAL_INPUT(Material, Metallic);
    if (Key == TEXT("specular"))                                     return &MCP_GET_MATERIAL_INPUT(Material, Specular);
    if (Key == TEXT("normal"))                                       return &MCP_GET_MATERIAL_INPUT(Material, Normal);
    if (Key == TEXT("opacity"))                                      return &MCP_GET_MATERIAL_INPUT(Material, Opacity);
    if (Key == TEXT("opacitymask"))                                  return &MCP_GET_MATERIAL_INPUT(Material, OpacityMask);
    if (Key == TEXT("ambientocclusion") || Key == TEXT("ao"))        return &MCP_GET_MATERIAL_INPUT(Material, AmbientOcclusion);
    if (Key == TEXT("subsurfacecolor"))                              return &MCP_GET_MATERIAL_INPUT(Material, SubsurfaceColor);
    if (Key == TEXT("worldpositionoffset"))                          return &MCP_GET_MATERIAL_INPUT(Material, WorldPositionOffset);
    if (Key == TEXT("refraction"))                                   return &MCP_GET_MATERIAL_INPUT(Material, Refraction);
    if (Key == TEXT("pixeldepthoffset"))                             return &MCP_GET_MATERIAL_INPUT(Material, PixelDepthOffset);
#endif
    return nullptr;
}

// Friendly aliases for the main-material inputs, for /docs purposes.
static const TCHAR* GetMainInputDisplayName(const FString& Key)
{
    const FString K = NormalizePinKey(Key);
    if (K == TEXT("finalcolor"))     return TEXT("Final Color (alias of EmissiveColor)");
    if (K == TEXT("emissivecolor"))  return TEXT("Emissive Color");
    if (K == TEXT("basecolor"))      return TEXT("Base Color");
    if (K == TEXT("opacitymask"))    return TEXT("Opacity Mask");
    return *Key;
}

// Resolve a named input on a material expression to a writable FExpressionInput*.
// Handles three cases:
//   1. UPROPERTY field whose struct type derives from FExpressionInput
//      (covers FExpressionInput, FColorMaterialInput, FScalarMaterialInput,
//      FVectorMaterialInput, FMaterialAttributesInput).
//   2. UMaterialExpressionCustom: look up by InputName in the Inputs array.
//   3. Case-insensitive whitespace-tolerant match against the property name.
// Returns nullptr if no input matches.
static FExpressionInput* FindNamedInputOnExpression(UMaterialExpression* Expr, const FString& PinName)
{
    if (!Expr || PinName.IsEmpty()) return nullptr;
    const FString Key = NormalizePinKey(PinName);

    // Custom expression: named inputs live in a TArray<FCustomInput>.
    if (UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr))
    {
        for (FCustomInput& Custom : CustomExpr->Inputs)
        {
            if (NormalizePinKey(Custom.InputName.ToString()) == Key)
            {
                return &Custom.Input;
            }
        }
        return nullptr;
    }

    // Standard expressions: scan UPROPERTY fields for any FStructProperty whose
    // Struct is FExpressionInput or one of its derived input structs. FExpressionInput
    // is declared USTRUCT(noexport) so it has no compile-time StaticStruct accessor;
    // we recognize it (and known derivatives) by struct name instead.
    auto IsExpressionInputStruct = [](UScriptStruct* S) {
        if (!S) return false;
        const FName N = S->GetFName();
        // Walk the reflection parent chain so any custom derivative is caught too.
        for (UScriptStruct* Cur = S; Cur; Cur = Cast<UScriptStruct>(Cur->GetSuperStruct()))
        {
            const FName CurName = Cur->GetFName();
            if (CurName == TEXT("ExpressionInput")) return true;
            if (CurName == TEXT("ColorMaterialInput")) return true;
            if (CurName == TEXT("ScalarMaterialInput")) return true;
            if (CurName == TEXT("VectorMaterialInput")) return true;
            if (CurName == TEXT("Vector2MaterialInput")) return true;
            if (CurName == TEXT("Vector4MaterialInput")) return true;
            if (CurName == TEXT("MaterialAttributesInput")) return true;
            if (CurName == TEXT("ShadingModelMaterialInput")) return true;
            if (CurName == TEXT("SubstrateMaterialInput")) return true;
        }
        (void)N;
        return false;
    };

    for (TFieldIterator<FProperty> It(Expr->GetClass()); It; ++It)
    {
        FStructProperty* StructProp = CastField<FStructProperty>(*It);
        if (!StructProp || !StructProp->Struct) continue;
        if (!IsExpressionInputStruct(StructProp->Struct)) continue;

        if (NormalizePinKey(StructProp->GetName()) == Key)
        {
            return StructProp->ContainerPtrToValuePtr<FExpressionInput>(Expr);
        }
    }
    return nullptr;
}

// Emit per-node input descriptors as a JSON array.
static TArray<TSharedPtr<FJsonValue>> EnumerateExpressionInputs(UMaterialExpression* Expr)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    if (!Expr) return Out;

    if (UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr))
    {
        for (const FCustomInput& Custom : CustomExpr->Inputs)
        {
            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            Obj->SetStringField(TEXT("name"), Custom.InputName.ToString());
            Obj->SetStringField(TEXT("type"), TEXT("FCustomInput"));
            Obj->SetBoolField(TEXT("connected"), Custom.Input.Expression != nullptr);
            Out.Add(MakeShared<FJsonValueObject>(Obj));
        }
        return Out;
    }

    auto IsExpressionInputStruct = [](UScriptStruct* S) {
        if (!S) return false;
        for (UScriptStruct* Cur = S; Cur; Cur = Cast<UScriptStruct>(Cur->GetSuperStruct()))
        {
            const FName CurName = Cur->GetFName();
            if (CurName == TEXT("ExpressionInput")) return true;
            if (CurName == TEXT("ColorMaterialInput")) return true;
            if (CurName == TEXT("ScalarMaterialInput")) return true;
            if (CurName == TEXT("VectorMaterialInput")) return true;
            if (CurName == TEXT("Vector2MaterialInput")) return true;
            if (CurName == TEXT("Vector4MaterialInput")) return true;
            if (CurName == TEXT("MaterialAttributesInput")) return true;
            if (CurName == TEXT("ShadingModelMaterialInput")) return true;
            if (CurName == TEXT("SubstrateMaterialInput")) return true;
        }
        return false;
    };
    for (TFieldIterator<FProperty> It(Expr->GetClass()); It; ++It)
    {
        FStructProperty* StructProp = CastField<FStructProperty>(*It);
        if (!StructProp || !StructProp->Struct) continue;
        if (!IsExpressionInputStruct(StructProp->Struct)) continue;
        const FExpressionInput* Input = StructProp->ContainerPtrToValuePtr<FExpressionInput>(Expr);
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), StructProp->GetName());
        Obj->SetStringField(TEXT("type"), StructProp->Struct->GetName());
        Obj->SetBoolField(TEXT("connected"), Input && Input->Expression != nullptr);
        Out.Add(MakeShared<FJsonValueObject>(Obj));
    }
    return Out;
}

// Emit per-node output descriptors. Each UMaterialExpression has a TArray<FExpressionOutput>
// `Outputs`; the primary output is at index 0 and usually has an empty OutputName.
// Custom expressions additionally expose AdditionalOutputs by name.
static TArray<TSharedPtr<FJsonValue>> EnumerateExpressionOutputs(UMaterialExpression* Expr)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    if (!Expr) return Out;

    const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
    for (int32 i = 0; i < Outputs.Num(); ++i)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        const FString Name = Outputs[i].OutputName.IsNone() ? FString::Printf(TEXT("Output%d"), i)
                                                            : Outputs[i].OutputName.ToString();
        Obj->SetStringField(TEXT("name"), Name);
        Obj->SetNumberField(TEXT("index"), i);
        Out.Add(MakeShared<FJsonValueObject>(Obj));
    }
    return Out;
}

} // namespace

bool UMcpAutomationBridgeSubsystem::HandleMaterialGraphAction(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
  if (Action != TEXT("manage_material_graph")) {
    return false;
  }

#if WITH_EDITOR
  if (!Payload.IsValid()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing payload."),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  FString AssetPath;
  if (!Payload->TryGetStringField(TEXT("assetPath"), AssetPath) ||
      AssetPath.IsEmpty()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing 'assetPath'."),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  UMaterial *Material = LoadObject<UMaterial>(nullptr, *AssetPath);
  if (!Material) {
    SendAutomationError(Socket, RequestId, TEXT("Could not load Material."),
                        TEXT("ASSET_NOT_FOUND"));
    return true;
  }

  FString SubAction;
  if (!Payload->TryGetStringField(TEXT("subAction"), SubAction) ||
      SubAction.IsEmpty()) {
    SendAutomationError(Socket, RequestId,
                        TEXT("Missing 'subAction' for manage_material_graph"),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  // Helper function to find expression by ID, name, or index
  // Supports: GUID string, node name, object path, parameter name, or numeric index
  auto FindExpressionByIdOrNameOrIndex =
      [&](const FString &IdOrName, int32 Index = -1) -> UMaterialExpression * {
    // First, try index-based lookup if Index is valid
    if (Index >= 0) {
      const TArray<UMaterialExpression*> &Expressions = MCP_GET_MATERIAL_EXPRESSIONS(Material);
      if (Index < Expressions.Num()) {
        return Expressions[Index];
      }
      return nullptr; // Index out of bounds
    }
    
    // If IdOrName is empty, return nullptr
    if (IdOrName.IsEmpty()) {
      return nullptr;
    }

    // Try to parse as numeric index first (e.g., "0", "1", "2")
    int32 ParsedIndex = -1;
    if (IdOrName.IsNumeric()) {
      ParsedIndex = FCString::Atoi(*IdOrName);
      const TArray<UMaterialExpression*> &Expressions = MCP_GET_MATERIAL_EXPRESSIONS(Material);
      if (ParsedIndex >= 0 && ParsedIndex < Expressions.Num()) {
        return Expressions[ParsedIndex];
      }
    }

    const FString Needle = IdOrName.TrimStartAndEnd();
    for (UMaterialExpression *Expr : MCP_GET_MATERIAL_EXPRESSIONS(Material)) {
      if (!Expr) {
        continue;
      }
      if (Expr->MaterialExpressionGuid.ToString() == Needle) {
        return Expr;
      }
      if (Expr->GetName() == Needle) {
        return Expr;
      }
      // Some callers may pass a full object path.
      if (Expr->GetPathName() == Needle) {
        return Expr;
      }

      // Also check ParameterName if it's a parameter node
      if (UMaterialExpressionParameter *ParamExpr =
              Cast<UMaterialExpressionParameter>(Expr)) {
        if (ParamExpr->ParameterName.ToString() == Needle) {
          return Expr;
        }
      }
    }
    return nullptr;
  };

  // Wrapper that accepts both string ID and numeric index
  auto FindExpressionByPayload = [&](const FString &IdField, const FString &IndexField) -> UMaterialExpression* {
    // Check for numeric index first (e.g., fromExpression: 0)
    int32 Index = -1;
    if (Payload->TryGetNumberField(*IndexField, Index) && Index >= 0) {
      return FindExpressionByIdOrNameOrIndex(FString(), Index);
    }
    
    // Then check for string ID
    FString IdOrName;
    if (Payload->TryGetStringField(*IdField, IdOrName) && !IdOrName.IsEmpty()) {
      return FindExpressionByIdOrNameOrIndex(IdOrName);
    }
    
    // Also check if IdField contains a numeric string
    if (Payload->TryGetStringField(*IdField, IdOrName) && IdOrName.IsNumeric()) {
      return FindExpressionByIdOrNameOrIndex(IdOrName);
    }
    
    return nullptr;
  };

  if (SubAction == TEXT("add_node")) {
    FString NodeType;
    Payload->TryGetStringField(TEXT("nodeType"), NodeType);
    float X = 0.0f;
    float Y = 0.0f;
    Payload->TryGetNumberField(TEXT("x"), X);
    Payload->TryGetNumberField(TEXT("y"), Y);

    UClass *ExpressionClass = nullptr;
    if (NodeType == TEXT("TextureSample"))
      ExpressionClass = UMaterialExpressionTextureSample::StaticClass();
    else if (NodeType == TEXT("VectorParameter") ||
             NodeType == TEXT("ConstantVectorParameter"))
      ExpressionClass = UMaterialExpressionVectorParameter::StaticClass();
    else if (NodeType == TEXT("ScalarParameter") ||
             NodeType == TEXT("ConstantScalarParameter"))
      ExpressionClass = UMaterialExpressionScalarParameter::StaticClass();
    else if (NodeType == TEXT("Add"))
      ExpressionClass = UMaterialExpressionAdd::StaticClass();
    else if (NodeType == TEXT("Multiply"))
      ExpressionClass = UMaterialExpressionMultiply::StaticClass();
    else if (NodeType == TEXT("Constant") || NodeType == TEXT("Float") ||
             NodeType == TEXT("Scalar"))
      ExpressionClass = UMaterialExpressionConstant::StaticClass();
    else if (NodeType == TEXT("Constant3Vector") ||
             NodeType == TEXT("ConstantVector") || NodeType == TEXT("Color") ||
             NodeType == TEXT("Vector3"))
      ExpressionClass = UMaterialExpressionConstant3Vector::StaticClass();
    else {
      // Try resolve class by full path or partial name
      ExpressionClass = ResolveClassByName(NodeType);
      // Also try with MaterialExpression prefix
      if (!ExpressionClass ||
          !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
        FString PrefixedName =
            FString::Printf(TEXT("MaterialExpression%s"), *NodeType);
        ExpressionClass = ResolveClassByName(PrefixedName);
      }
      if (!ExpressionClass ||
          !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
        // Provide helpful error with available types
        SendAutomationError(
            Socket, RequestId,
            FString::Printf(
                TEXT("Unknown node type: %s. Available types: TextureSample, "
                     "VectorParameter, ScalarParameter, Add, Multiply, "
                     "Constant, Constant3Vector, "
                     "Color, ConstantVectorParameter. Or use full class name "
                     "like 'MaterialExpressionLerp'."),
                *NodeType),
            TEXT("UNKNOWN_TYPE"));
        return true;
      }
    }

    UMaterialExpression *NewExpr = NewObject<UMaterialExpression>(
        Material, ExpressionClass, NAME_None, RF_Transactional);
    if (NewExpr) {
      NewExpr->MaterialExpressionEditorX = (int32)X;
      NewExpr->MaterialExpressionEditorY = (int32)Y;
#if WITH_EDITORONLY_DATA
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
      if (Material->GetEditorOnlyData()) {
        MCP_GET_MATERIAL_EXPRESSIONS(Material).Add(NewExpr);
      }
#else
      // UE 5.0: Direct access
      Material->Expressions.Add(NewExpr);
#endif
#endif

      // If parameter, set name
      FString ParamName;
      if (Payload->TryGetStringField(TEXT("name"), ParamName)) {
        if (UMaterialExpressionParameter *ParamExpr =
                Cast<UMaterialExpressionParameter>(NewExpr)) {
          ParamExpr->ParameterName = FName(*ParamName);
        }
      }

      Material->PostEditChange();
      Material->MarkPackageDirty();

      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      AddAssetVerification(Result, Material);
      Result->SetStringField(TEXT("nodeId"),
                             NewExpr->MaterialExpressionGuid.ToString());
      Result->SetStringField(TEXT("nodeType"), ExpressionClass->GetName());
      SendAutomationResponse(Socket, RequestId, true, TEXT("Node added."),
                             Result);
    } else {
      SendAutomationError(Socket, RequestId,
                          TEXT("Failed to create expression."),
                          TEXT("CREATE_FAILED"));
    }
    return true;
  } else if (SubAction == TEXT("remove_node")) {
    FString NodeId;
    Payload->TryGetStringField(TEXT("nodeId"), NodeId);
    
    // Also check for expressionIndex (numeric index)
    int32 ExpressionIndex = -1;
    Payload->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex);

    if (NodeId.IsEmpty() && ExpressionIndex < 0) {
      SendAutomationError(Socket, RequestId, TEXT("Missing 'nodeId' or 'expressionIndex'."),
                          TEXT("INVALID_ARGUMENT"));
      return true;
    }

    UMaterialExpression *TargetExpr = FindExpressionByIdOrNameOrIndex(NodeId, ExpressionIndex);

    if (TargetExpr) {
      FString RemovedNodeId = TargetExpr->MaterialExpressionGuid.ToString();
#if WITH_EDITORONLY_DATA
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
      if (Material->GetEditorOnlyData()) {
        MCP_GET_MATERIAL_EXPRESSIONS(Material).Remove(TargetExpr);
      }
#else
      // UE 5.0: Direct access
      Material->Expressions.Remove(TargetExpr);
#endif
#endif
      Material->PostEditChange();
      Material->MarkPackageDirty();
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      AddAssetVerification(Result, Material);
      Result->SetStringField(TEXT("nodeId"), RemovedNodeId);
      Result->SetBoolField(TEXT("removed"), true);
      SendAutomationResponse(Socket, RequestId, true, TEXT("Node removed."), Result);
    } else {
      SendAutomationError(Socket, RequestId, TEXT("Node not found."),
                          TEXT("NODE_NOT_FOUND"));
    }
    return true;
  } else if (SubAction == TEXT("connect_nodes") ||
             SubAction == TEXT("connect_pins")) {
    // Material graph connections are complex because inputs are structs on the
    // expression, not EdGraph pins We need to find the target expression and
    // set its input
    
    // Support both string IDs and numeric indices for source/target
    // fromExpression/sourceNodeId: string ID or numeric index
    // toExpression/targetNodeId: string ID or numeric index (or empty for main material)
    FString SourceNodeId, TargetNodeId, InputName;
    Payload->TryGetStringField(TEXT("sourceNodeId"), SourceNodeId);
    Payload->TryGetStringField(TEXT("targetNodeId"), TargetNodeId);
    Payload->TryGetStringField(TEXT("inputName"), InputName);
    
    // Also check for newer parameter names
    if (SourceNodeId.IsEmpty()) {
      Payload->TryGetStringField(TEXT("fromExpression"), SourceNodeId);
    }
    if (TargetNodeId.IsEmpty()) {
      Payload->TryGetStringField(TEXT("toExpression"), TargetNodeId);
    }
    
    // Try numeric indices first
    int32 SourceIndex = -1, TargetIndex = -1;
    Payload->TryGetNumberField(TEXT("fromExpression"), SourceIndex);
    Payload->TryGetNumberField(TEXT("toExpression"), TargetIndex);

    UMaterialExpression *SourceExpr = (SourceIndex >= 0) 
        ? FindExpressionByIdOrNameOrIndex(FString(), SourceIndex)
        : FindExpressionByIdOrNameOrIndex(SourceNodeId);

    if (!SourceExpr) {
      SendAutomationError(Socket, RequestId, TEXT("Source node not found."),
                          TEXT("NODE_NOT_FOUND"));
      return true;
    }

    // Optional: source output index (for nodes with multiple outputs, e.g. TexCoord).
    int32 SourceOutputIndex = 0;
    Payload->TryGetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
    if (SourceOutputIndex < 0) SourceOutputIndex = 0;

    // Target could be another expression OR the main material node (if
    // TargetNodeId is empty or "Main" or no target specified).
    if ((TargetNodeId.IsEmpty() || TargetNodeId == TEXT("Main")) && TargetIndex < 0) {
      FExpressionInput* MainInput = ResolveMainMaterialInput(Material, InputName);
      if (!MainInput) {
        SendAutomationError(
            Socket, RequestId,
            FString::Printf(TEXT("Unknown input on main node: '%s'. Valid (UI): Final Color, Opacity. (Surface): Base Color, Emissive Color, Roughness, Metallic, Specular, Normal, Opacity, Opacity Mask, Ambient Occlusion, Subsurface Color, World Position Offset, Refraction, Pixel Depth Offset."),
                            *InputName),
            TEXT("INVALID_PIN"));
        return true;
      }
      MainInput->Expression = SourceExpr;
      MainInput->OutputIndex = SourceOutputIndex;
      Material->PostEditChange();
      Material->MarkPackageDirty();
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      AddAssetVerification(Result, Material);
      Result->SetStringField(TEXT("inputName"), InputName);
      Result->SetStringField(TEXT("resolvedInput"), GetMainInputDisplayName(InputName));
      Result->SetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
      SendAutomationResponse(Socket, RequestId, true,
                             TEXT("Connected to main material node."), Result);
      return true;
    } else {
      UMaterialExpression *TargetExpr = (TargetIndex >= 0)
          ? FindExpressionByIdOrNameOrIndex(FString(), TargetIndex)
          : FindExpressionByIdOrNameOrIndex(TargetNodeId);

      if (!TargetExpr) {
        SendAutomationError(Socket, RequestId, TEXT("Target node not found."),
                            TEXT("NODE_NOT_FOUND"));
        return true;
      }

      FExpressionInput* InputPtr = FindNamedInputOnExpression(TargetExpr, InputName);
      if (!InputPtr) {
        // Build a helpful error listing the available input names so the
        // caller can correct without round-tripping through get_node_details.
        TArray<TSharedPtr<FJsonValue>> Available = EnumerateExpressionInputs(TargetExpr);
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("availableInputs"), Available);
        Result->SetStringField(TEXT("nodeType"), TargetExpr->GetClass()->GetName());
        Result->SetStringField(TEXT("requestedInput"), InputName);
        SendAutomationResponse(
            Socket, RequestId, false,
            FString::Printf(TEXT("Input pin '%s' not found on %s. See availableInputs."),
                            *InputName, *TargetExpr->GetClass()->GetName()),
            Result, TEXT("PIN_NOT_FOUND"));
        return true;
      }

      InputPtr->Expression = SourceExpr;
      InputPtr->OutputIndex = SourceOutputIndex;
      Material->PostEditChange();
      Material->MarkPackageDirty();
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      AddAssetVerification(Result, Material);
      Result->SetStringField(TEXT("inputName"), InputName);
      Result->SetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
      SendAutomationResponse(Socket, RequestId, true,
                             TEXT("Nodes connected."), Result);
      return true;
    }
  } else if (SubAction == TEXT("break_connections")) {
    FString NodeId;
    Payload->TryGetStringField(TEXT("nodeId"), NodeId);
    FString
        PinName; // If provided, break specific pin. If empty, break all inputs?
    Payload->TryGetStringField(TEXT("pinName"), PinName);

    // Check if main node
    if (NodeId.IsEmpty() || NodeId == TEXT("Main")) {
      // Disconnect from main material node
      if (!PinName.IsEmpty()) {
        bool bFound = false;
#if WITH_EDITORONLY_DATA
        if (PinName == TEXT("BaseColor")) {
          MCP_GET_MATERIAL_INPUT(Material, BaseColor).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("EmissiveColor")) {
          MCP_GET_MATERIAL_INPUT(Material, EmissiveColor).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("Roughness")) {
          MCP_GET_MATERIAL_INPUT(Material, Roughness).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("Metallic")) {
          MCP_GET_MATERIAL_INPUT(Material, Metallic).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("Specular")) {
          MCP_GET_MATERIAL_INPUT(Material, Specular).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("Normal")) {
          MCP_GET_MATERIAL_INPUT(Material, Normal).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("Opacity")) {
          MCP_GET_MATERIAL_INPUT(Material, Opacity).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("OpacityMask")) {
          MCP_GET_MATERIAL_INPUT(Material, OpacityMask).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("AmbientOcclusion")) {
          MCP_GET_MATERIAL_INPUT(Material, AmbientOcclusion).Expression = nullptr;
          bFound = true;
        } else if (PinName == TEXT("SubsurfaceColor")) {
          MCP_GET_MATERIAL_INPUT(Material, SubsurfaceColor).Expression = nullptr;
          bFound = true;
        }
#endif

        if (bFound) {
          Material->PostEditChange();
          Material->MarkPackageDirty();
          TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
          AddAssetVerification(Result, Material);
          Result->SetStringField(TEXT("pinName"), PinName);
          SendAutomationResponse(Socket, RequestId, true,
                                 TEXT("Disconnected from main material pin."), Result);
          return true;
        }
      }
    }

    // Also check for expressionIndex
    int32 ExpressionIndex = -1;
    Payload->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex);
    
    UMaterialExpression *TargetExpr = (ExpressionIndex >= 0)
        ? FindExpressionByIdOrNameOrIndex(FString(), ExpressionIndex)
        : FindExpressionByIdOrNameOrIndex(NodeId);

    if (TargetExpr) {
      // Disconnect all inputs of this node if no specific pin name
      // Since GetInputs() is not available, we skip generic breaking for now.
      // We can implement breaking for specific pin if needed via property
      // reflection.

      // For now, just acknowledge but warn.
      Material->PostEditChange();
      Material->MarkPackageDirty();
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      AddAssetVerification(Result, Material);
      SendAutomationResponse(
          Socket, RequestId, true,
          TEXT("Node disconnection partial (generic inputs not cleared)."), Result);
      return true;
    }

    SendAutomationError(Socket, RequestId, TEXT("Node not found."),
                        TEXT("NODE_NOT_FOUND"));
    return true;
  } else if (SubAction == TEXT("get_node_details")) {
    FString NodeId;
    Payload->TryGetStringField(TEXT("nodeId"), NodeId);
    
    // Also check for expressionIndex
    int32 ExpressionIndex = -1;
    Payload->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex);

    UMaterialExpression *TargetExpr = nullptr;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
    auto AllExpressions = Material->GetExpressions();
#else
    // UE 5.0: Direct access to Expressions array
    auto& AllExpressions = Material->Expressions;
#endif

    // If nodeId or index provided, try to find that specific node
    if (ExpressionIndex >= 0) {
      TargetExpr = FindExpressionByIdOrNameOrIndex(FString(), ExpressionIndex);
    } else if (!NodeId.IsEmpty()) {
      TargetExpr = FindExpressionByIdOrNameOrIndex(NodeId);
    }

    if (TargetExpr) {
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      AddAssetVerification(Result, Material);
      Result->SetStringField(TEXT("nodeId"), TargetExpr->MaterialExpressionGuid.ToString());
      Result->SetStringField(TEXT("nodeType"),
                             TargetExpr->GetClass()->GetName());
      Result->SetStringField(TEXT("desc"), TargetExpr->Desc);
      Result->SetNumberField(TEXT("x"), TargetExpr->MaterialExpressionEditorX);
      Result->SetNumberField(TEXT("y"), TargetExpr->MaterialExpressionEditorY);
      Result->SetArrayField(TEXT("inputs"), EnumerateExpressionInputs(TargetExpr));
      Result->SetArrayField(TEXT("outputs"), EnumerateExpressionOutputs(TargetExpr));

      SendAutomationResponse(Socket, RequestId, true,
                             TEXT("Node details retrieved."), Result);
    } else {
      // List all available nodes to help the user
      TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
      TArray<TSharedPtr<FJsonValue>> NodeList;

      for (int32 i = 0; i < AllExpressions.Num(); ++i) {
        UMaterialExpression *Expr = AllExpressions[i];
        TSharedPtr<FJsonObject> NodeInfo = MakeShared<FJsonObject>();
        NodeInfo->SetStringField(TEXT("nodeId"),
                                 Expr->MaterialExpressionGuid.ToString());
        NodeInfo->SetStringField(TEXT("nodeType"), Expr->GetClass()->GetName());
        NodeInfo->SetNumberField(TEXT("index"), i);
        if (!Expr->Desc.IsEmpty()) {
          NodeInfo->SetStringField(TEXT("desc"), Expr->Desc);
        }
        NodeList.Add(MakeShared<FJsonValueObject>(NodeInfo));
      }

      Result->SetArrayField(TEXT("availableNodes"), NodeList);
      Result->SetNumberField(TEXT("nodeCount"), AllExpressions.Num());

      // If no nodeId was provided, this is a successful "list all nodes" operation
      // If nodeId was provided but not found, return error
      if (NodeId.IsEmpty() && ExpressionIndex < 0) {
        FString Message = FString::Printf(
            TEXT("Material has %d nodes. Available nodes listed."),
            AllExpressions.Num());
        SendAutomationResponse(Socket, RequestId, true, Message, Result);
      } else {
        FString Message = FString::Printf(
            TEXT("Node '%s' not found. Material has %d nodes."),
            NodeId.IsEmpty() ? *FString::FromInt(ExpressionIndex) : *NodeId, 
            AllExpressions.Num());
        SendAutomationResponse(Socket, RequestId, false, Message, Result,
                               TEXT("NODE_NOT_FOUND"));
      }
    }
    return true;
  }

  SendAutomationError(
      Socket, RequestId,
      FString::Printf(TEXT("Unknown subAction: %s"), *SubAction),
      TEXT("INVALID_SUBACTION"));
  return true;
#else
  SendAutomationError(Socket, RequestId, TEXT("Editor only."),
                      TEXT("EDITOR_ONLY"));
  return true;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleAddMaterialTextureSample(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  if (!Payload.IsValid()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing payload."),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  FString MaterialPath;
  if (!Payload->TryGetStringField(TEXT("materialPath"), MaterialPath) ||
      MaterialPath.IsEmpty()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing 'materialPath'."),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  FString TexturePath;
  if (!Payload->TryGetStringField(TEXT("texturePath"), TexturePath) ||
      TexturePath.IsEmpty()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing 'texturePath'."),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  UMaterial *Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
  if (!Material) {
    SendAutomationError(Socket, RequestId,
                        FString::Printf(TEXT("Could not load Material: %s"),
                                        *MaterialPath),
                        TEXT("ASSET_NOT_FOUND"));
    return true;
  }

  UTexture *Texture = LoadObject<UTexture>(nullptr, *TexturePath);
  if (!Texture) {
    SendAutomationError(Socket, RequestId,
                        FString::Printf(TEXT("Could not load Texture: %s"),
                                        *TexturePath),
                        TEXT("ASSET_NOT_FOUND"));
    return true;
  }

  // Get optional coordinate index
  int32 CoordinateIndex = 0;
  Payload->TryGetNumberField(TEXT("coordinateIndex"), CoordinateIndex);

  // Get optional position
  float X = 0.0f, Y = 0.0f;
  Payload->TryGetNumberField(TEXT("x"), X);
  Payload->TryGetNumberField(TEXT("y"), Y);

  // Create the texture sample expression
  UMaterialExpressionTextureSample *TexSample =
      NewObject<UMaterialExpressionTextureSample>(
          Material, UMaterialExpressionTextureSample::StaticClass(), NAME_None,
          RF_Transactional);

  if (!TexSample) {
    SendAutomationError(Socket, RequestId,
                        TEXT("Failed to create TextureSample expression."),
                        TEXT("CREATE_FAILED"));
    return true;
  }

  TexSample->Texture = Texture;
  TexSample->ConstCoordinate = CoordinateIndex;
  TexSample->MaterialExpressionEditorX = (int32)X;
  TexSample->MaterialExpressionEditorY = (int32)Y;

#if WITH_EDITORONLY_DATA
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
  if (Material->GetEditorOnlyData()) {
    MCP_GET_MATERIAL_EXPRESSIONS(Material).Add(TexSample);
  }
#else
  // UE 5.0: Direct access
  Material->Expressions.Add(TexSample);
#endif
#endif

  Material->PreEditChange(nullptr);
  Material->PostEditChange();
  McpSafeAssetSave(Material);

  TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
  AddAssetVerification(Result, Material);
  Result->SetStringField(TEXT("nodeId"),
                         TexSample->MaterialExpressionGuid.ToString());
  Result->SetStringField(TEXT("texturePath"), Texture->GetPathName());

  SendAutomationResponse(Socket, RequestId, true,
                         TEXT("TextureSample expression added to material."),
                         Result);
  return true;
#else
  SendAutomationError(Socket, RequestId, TEXT("Editor only."),
                      TEXT("EDITOR_ONLY"));
  return true;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleAddMaterialExpression(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  if (!Payload.IsValid()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing payload."),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  FString MaterialPath;
  if (!Payload->TryGetStringField(TEXT("materialPath"), MaterialPath) ||
      MaterialPath.IsEmpty()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing 'materialPath'."),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  FString ExpressionClassName;
  if (!Payload->TryGetStringField(TEXT("expressionClass"), ExpressionClassName) ||
      ExpressionClassName.IsEmpty()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing 'expressionClass'."),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  UMaterial *Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
  if (!Material) {
    SendAutomationError(Socket, RequestId,
                        FString::Printf(TEXT("Could not load Material: %s"),
                                        *MaterialPath),
                        TEXT("ASSET_NOT_FOUND"));
    return true;
  }

  // Get optional position
  float X = 0.0f, Y = 0.0f;
  Payload->TryGetNumberField(TEXT("x"), X);
  Payload->TryGetNumberField(TEXT("y"), Y);

  // Try to find the expression class by name
  UClass *ExpressionClass = nullptr;

  // Try with full path first
  ExpressionClass = FindObject<UClass>(
      nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ExpressionClassName));

  // If not found, try with MaterialExpression prefix
  if (!ExpressionClass ||
      !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
    FString PrefixedName =
        FString::Printf(TEXT("MaterialExpression%s"), *ExpressionClassName);
    ExpressionClass = FindObject<UClass>(
        nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *PrefixedName));
  }

  // Try ResolveClassByName helper as fallback
  if (!ExpressionClass ||
      !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
    ExpressionClass = ResolveClassByName(ExpressionClassName);
    if (ExpressionClass &&
        !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
      ExpressionClass = nullptr;
    }
  }

  // Final fallback with MaterialExpression prefix
  if (!ExpressionClass) {
    FString PrefixedName =
        FString::Printf(TEXT("MaterialExpression%s"), *ExpressionClassName);
    ExpressionClass = ResolveClassByName(PrefixedName);
    if (ExpressionClass &&
        !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
      ExpressionClass = nullptr;
    }
  }

  if (!ExpressionClass) {
    SendAutomationError(
        Socket, RequestId,
        FString::Printf(
            TEXT("Unknown expression class: %s. Try using the full class "
                 "name like 'MaterialExpressionAdd' or 'Add'."),
            *ExpressionClassName),
        TEXT("CLASS_NOT_FOUND"));
    return true;
  }

  UMaterialExpression *NewExpr = NewObject<UMaterialExpression>(
      Material, ExpressionClass, NAME_None, RF_Transactional);

  if (!NewExpr) {
    SendAutomationError(Socket, RequestId,
                        TEXT("Failed to create expression."),
                        TEXT("CREATE_FAILED"));
    return true;
  }

  NewExpr->MaterialExpressionEditorX = (int32)X;
  NewExpr->MaterialExpressionEditorY = (int32)Y;

#if WITH_EDITORONLY_DATA
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
  if (Material->GetEditorOnlyData()) {
    MCP_GET_MATERIAL_EXPRESSIONS(Material).Add(NewExpr);
  }
#else
  // UE 5.0: Direct access
  Material->Expressions.Add(NewExpr);
#endif
#endif

  Material->PreEditChange(nullptr);
  Material->PostEditChange();
  McpSafeAssetSave(Material);

  TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
  AddAssetVerification(Result, Material);
  Result->SetStringField(TEXT("nodeId"),
                         NewExpr->MaterialExpressionGuid.ToString());
  Result->SetStringField(TEXT("expressionClass"),
                         ExpressionClass->GetName());

  SendAutomationResponse(
      Socket, RequestId, true,
      FString::Printf(TEXT("Expression '%s' added to material."),
                      *ExpressionClass->GetName()),
      Result);
  return true;
#else
  SendAutomationError(Socket, RequestId, TEXT("Editor only."),
                      TEXT("EDITOR_ONLY"));
  return true;
#endif
}

bool UMcpAutomationBridgeSubsystem::HandleCreateMaterialNodes(
    const FString &RequestId, const FString &Action,
    const TSharedPtr<FJsonObject> &Payload,
    TSharedPtr<FMcpBridgeWebSocket> Socket) {
#if WITH_EDITOR
  if (!Payload.IsValid()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing payload."),
                        TEXT("INVALID_PAYLOAD"));
    return true;
  }

  FString MaterialPath;
  if (!Payload->TryGetStringField(TEXT("materialPath"), MaterialPath) ||
      MaterialPath.IsEmpty()) {
    SendAutomationError(Socket, RequestId, TEXT("Missing 'materialPath'."),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  UMaterial *Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
  if (!Material) {
    SendAutomationError(Socket, RequestId,
                        FString::Printf(TEXT("Could not load Material: %s"),
                                        *MaterialPath),
                        TEXT("ASSET_NOT_FOUND"));
    return true;
  }

  const TArray<TSharedPtr<FJsonValue>> *NodesArray;
  if (!Payload->TryGetArrayField(TEXT("nodes"), NodesArray) || !NodesArray) {
    SendAutomationError(Socket, RequestId,
                        TEXT("Missing 'nodes' array."),
                        TEXT("INVALID_ARGUMENT"));
    return true;
  }

  TArray<TSharedPtr<FJsonValue>> CreatedNodes;
  int32 SuccessCount = 0;
  int32 FailCount = 0;

  for (const auto &NodeVal : *NodesArray) {
    TSharedPtr<FJsonObject> NodeObj = NodeVal->AsObject();
    if (!NodeObj.IsValid()) {
      FailCount++;
      continue;
    }

    FString NodeType;
    if (!NodeObj->TryGetStringField(TEXT("type"), NodeType) ||
        NodeType.IsEmpty()) {
      FailCount++;
      continue;
    }

    float X = 0.0f, Y = 0.0f;
    NodeObj->TryGetNumberField(TEXT("x"), X);
    NodeObj->TryGetNumberField(TEXT("y"), Y);

    // Resolve expression class
    UClass *ExpressionClass = nullptr;

    // Try common shorthand types first
    if (NodeType == TEXT("TextureSample"))
      ExpressionClass = UMaterialExpressionTextureSample::StaticClass();
    else if (NodeType == TEXT("VectorParameter"))
      ExpressionClass = UMaterialExpressionVectorParameter::StaticClass();
    else if (NodeType == TEXT("ScalarParameter"))
      ExpressionClass = UMaterialExpressionScalarParameter::StaticClass();
    else if (NodeType == TEXT("Add"))
      ExpressionClass = UMaterialExpressionAdd::StaticClass();
    else if (NodeType == TEXT("Multiply"))
      ExpressionClass = UMaterialExpressionMultiply::StaticClass();
    else if (NodeType == TEXT("Constant"))
      ExpressionClass = UMaterialExpressionConstant::StaticClass();
    else if (NodeType == TEXT("Constant3Vector") || NodeType == TEXT("Color"))
      ExpressionClass = UMaterialExpressionConstant3Vector::StaticClass();
    else {
      // Try to resolve by name
      ExpressionClass = FindObject<UClass>(
          nullptr,
          *FString::Printf(TEXT("/Script/Engine.%s"), *NodeType));
      if (!ExpressionClass ||
          !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
        FString PrefixedName =
            FString::Printf(TEXT("MaterialExpression%s"), *NodeType);
        ExpressionClass = FindObject<UClass>(
            nullptr,
            *FString::Printf(TEXT("/Script/Engine.%s"), *PrefixedName));
      }
      if (!ExpressionClass ||
          !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
        ExpressionClass = ResolveClassByName(NodeType);
        if (ExpressionClass &&
            !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())) {
          ExpressionClass = nullptr;
        }
      }
    }

    if (!ExpressionClass) {
      FailCount++;
      continue;
    }

    UMaterialExpression *NewExpr = NewObject<UMaterialExpression>(
        Material, ExpressionClass, NAME_None, RF_Transactional);

    if (!NewExpr) {
      FailCount++;
      continue;
    }

    NewExpr->MaterialExpressionEditorX = (int32)X;
    NewExpr->MaterialExpressionEditorY = (int32)Y;

    // Handle parameter name if applicable
    FString ParamName;
    if (NodeObj->TryGetStringField(TEXT("name"), ParamName)) {
      if (UMaterialExpressionParameter *ParamExpr =
              Cast<UMaterialExpressionParameter>(NewExpr)) {
        ParamExpr->ParameterName = FName(*ParamName);
      }
    }

    // Handle texture path for texture samples
    FString TexturePath;
    if (NodeObj->TryGetStringField(TEXT("texturePath"), TexturePath)) {
      if (UMaterialExpressionTextureSample *TexSample =
              Cast<UMaterialExpressionTextureSample>(NewExpr)) {
        UTexture *Texture = LoadObject<UTexture>(nullptr, *TexturePath);
        if (Texture) {
          TexSample->Texture = Texture;
        }
      }
    }

    // Handle default value for constant expressions
    double DefaultValue = 0.0;
    if (NodeObj->TryGetNumberField(TEXT("value"), DefaultValue)) {
      if (UMaterialExpressionConstant *ConstExpr =
              Cast<UMaterialExpressionConstant>(NewExpr)) {
        ConstExpr->R = (float)DefaultValue;
      }
    }

#if WITH_EDITORONLY_DATA
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
    if (Material->GetEditorOnlyData()) {
      MCP_GET_MATERIAL_EXPRESSIONS(Material).Add(NewExpr);
    }
#else
    // UE 5.0: Direct access
    Material->Expressions.Add(NewExpr);
#endif
#endif

    // Record created node info
    TSharedPtr<FJsonObject> NodeInfo = MakeShared<FJsonObject>();
    NodeInfo->SetStringField(TEXT("nodeId"),
                             NewExpr->MaterialExpressionGuid.ToString());
    NodeInfo->SetStringField(TEXT("type"), ExpressionClass->GetName());
    CreatedNodes.Add(MakeShared<FJsonValueObject>(NodeInfo));

    SuccessCount++;
  }

  Material->PreEditChange(nullptr);
  Material->PostEditChange();
  McpSafeAssetSave(Material);

  TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
  AddAssetVerification(Result, Material);
  Result->SetArrayField(TEXT("createdNodes"), CreatedNodes);
  Result->SetNumberField(TEXT("successCount"), SuccessCount);
  Result->SetNumberField(TEXT("failCount"), FailCount);

  SendAutomationResponse(
      Socket, RequestId, true,
      FString::Printf(TEXT("Created %d nodes (%d failed)."), SuccessCount,
                      FailCount),
      Result);
  return true;
#else
  SendAutomationError(Socket, RequestId, TEXT("Editor only."),
                      TEXT("EDITOR_ONLY"));
  return true;
#endif // WITH_EDITOR
}


#endif // WITH_EDITOR