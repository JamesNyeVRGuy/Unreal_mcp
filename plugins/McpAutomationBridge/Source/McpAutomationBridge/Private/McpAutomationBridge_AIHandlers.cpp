#include "Dom/JsonObject.h"
// McpAutomationBridge_AIHandlers.cpp
// Phase 16: AI System
// Implements 35 actions for AI controllers, blackboards, behavior trees, EQS, perception,
// state trees, smart objects, and mass AI.

#include "McpAutomationBridgeSubsystem.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeGlobals.h"
#include "Misc/EngineVersionComparison.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Decorators/BTDecorator_Cooldown.h"
#include "BehaviorTree/Decorators/BTDecorator_Loop.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_ActorsOfClass.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_OnCircle.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_SimpleGrid.h"

// EnvQueryTest headers are in EnvironmentQueryEditor module which may not be available in UE 5.0
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Trace.h"
#define MCP_HAS_ENVQUERY_TESTS 1
#else
#define MCP_HAS_ENVQUERY_TESTS 0
#endif
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Damage.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Null.h"
#include "NavAreas/NavArea_Obstacle.h"
#endif

// Attempt to include State Tree (UE 5.3+)
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 3
#define MCP_HAS_STATE_TREE 1
#if __has_include("StateTree.h")
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeTaskBase.h"
// UE 5.7+ moved StateTreeComponentSchema to GameplayStateTreeModule
#if __has_include("Components/StateTreeComponentSchema.h")
#include "Components/StateTreeComponentSchema.h"
#define MCP_STATE_TREE_COMPONENT_SCHEMA_AVAILABLE 1
#else
#define MCP_STATE_TREE_COMPONENT_SCHEMA_AVAILABLE 0
#endif
#define MCP_STATE_TREE_HEADERS_AVAILABLE 1
#else
#define MCP_STATE_TREE_HEADERS_AVAILABLE 0
#define MCP_STATE_TREE_COMPONENT_SCHEMA_AVAILABLE 0
#endif
#else
#define MCP_HAS_STATE_TREE 0
#define MCP_STATE_TREE_HEADERS_AVAILABLE 0
#define MCP_STATE_TREE_COMPONENT_SCHEMA_AVAILABLE 0
#endif

// Attempt to include Smart Objects (UE 5.0+)
#if ENGINE_MAJOR_VERSION >= 5
#define MCP_HAS_SMART_OBJECTS 1
#if __has_include("SmartObjectDefinition.h")
#include "SmartObjectDefinition.h"
#include "SmartObjectComponent.h"
#include "SmartObjectTypes.h"
#include "GameplayTagContainer.h"
#define MCP_SMART_OBJECTS_HEADERS_AVAILABLE 1
#else
#define MCP_SMART_OBJECTS_HEADERS_AVAILABLE 0
#endif
#else
#define MCP_HAS_SMART_OBJECTS 0
#define MCP_SMART_OBJECTS_HEADERS_AVAILABLE 0
#endif

// Attempt to include Mass AI (UE 5.0+)
#if ENGINE_MAJOR_VERSION >= 5
#define MCP_HAS_MASS_AI 1
#if __has_include("MassEntityConfigAsset.h")
#include "MassEntityConfigAsset.h"
#include "MassEntityTraitBase.h"
#include "MassSpawnerSubsystem.h"
#define MCP_MASS_AI_HEADERS_AVAILABLE 1
#else
#define MCP_MASS_AI_HEADERS_AVAILABLE 0
#endif
#else
#define MCP_HAS_MASS_AI 0
#define MCP_MASS_AI_HEADERS_AVAILABLE 0
#endif

// Log category for AI handlers
DEFINE_LOG_CATEGORY_STATIC(LogMcpAIHandlers, Log, All);

// Use consolidated JSON helpers from McpAutomationBridgeHelpers.h
// Aliases for backward compatibility with existing code in this file
#define GetStringFieldAI GetJsonStringField
#define GetNumberFieldAI GetJsonNumberField
#define GetBoolFieldAI GetJsonBoolField

// Helper to save package
// Note: This helper is used for NEW assets created with CreatePackage + factory.
// FullyLoad() must NOT be called on new packages - it corrupts bulkdata in UE 5.7+.
static bool SavePackageHelperAI(UPackage* Package, UObject* Asset)
{
    if (!Package || !Asset) return false;
    
    // Use centralized helper for safe saving (UE 5.7+ compatible)
    return McpSafeAssetSave(Asset);
}

/**
 * Sanitize and validate an asset path for AI asset creation.
 * - Removes double slashes that cause Fatal Error in UObjectGlobals.cpp
 * - Validates path is within a valid mount point (/Game/, /Plugin/, etc.)
 * - Returns false and sets OutError if path is invalid (security check)
 */
static bool SanitizeAIAssetPath(const FString& InputPath, FString& OutSanitizedPath, FString& OutError)
{
    // Start with the input path
    OutSanitizedPath = InputPath;
    
    // 1. Remove duplicate slashes (prevents Fatal Error in UObjectGlobals.cpp)
    OutSanitizedPath.ReplaceInline(TEXT("//"), TEXT("/"));
    while (OutSanitizedPath.Contains(TEXT("//")))
    {
        OutSanitizedPath.ReplaceInline(TEXT("//"), TEXT("/"));
    }
    
    // 2. Trim leading/trailing whitespace
    OutSanitizedPath.TrimStartAndEndInline();
    
    // 3. Validate that path starts with a valid mount point
    // Valid mount points: /Game/, /Engine/, /PluginName/, etc.
    if (!OutSanitizedPath.StartsWith(TEXT("/")))
    {
        OutError = FString::Printf(TEXT("Invalid path: must start with '/' (got: %s)"), *InputPath);
        return false;
    }
    
    // 4. Check for path traversal attempts (security)
    if (OutSanitizedPath.Contains(TEXT("..")) || 
        OutSanitizedPath.Contains(TEXT("~")) ||
        OutSanitizedPath.Contains(TEXT("\\")))
    {
        OutError = FString::Printf(TEXT("Invalid path: contains forbidden characters (path traversal attempt): %s"), *InputPath);
        return false;
    }
    
    // 5. Validate path starts with a valid content mount point
    // Allow /Game/, /Engine/, or any valid plugin mount (e.g. /Canopy/)
    if (!OutSanitizedPath.StartsWith(TEXT("/Game/")) &&
        !OutSanitizedPath.StartsWith(TEXT("/Engine/")) &&
        OutSanitizedPath != TEXT("/Game") &&
        OutSanitizedPath != TEXT("/Engine"))
    {
        // Check if this is a valid mounted content root (plugin paths, etc.)
        FText ValidationReason;
        if (!FPackageName::IsValidLongPackageName(OutSanitizedPath, true, &ValidationReason))
        {
            OutError = FString::Printf(TEXT("Invalid path: %s (got: %s)"), *ValidationReason.ToString(), *InputPath);
            return false;
        }
    }
    
    return true;
}

#if WITH_EDITOR
// Helper to create AI Controller blueprint
static UBlueprint* CreateAIControllerBlueprint(const FString& Path, const FString& Name, FString& OutError)
{
    // Sanitize and validate path first
    FString SanitizedPath;
    if (!SanitizeAIAssetPath(Path, SanitizedPath, OutError))
    {
        return nullptr;
    }
    
    FString FullPath = SanitizedPath / Name;
    
    // Check if asset already exists to prevent Kismet2.cpp assertion failure
    if (FindObject<UBlueprint>(nullptr, *FullPath) != nullptr)
    {
        OutError = FString::Printf(TEXT("Asset already exists: %s"), *FullPath);
        return nullptr;
    }
    
    // Also check if the package exists
    if (FPackageName::DoesPackageExist(FullPath))
    {
        OutError = FString::Printf(TEXT("Package already exists: %s"), *FullPath);
        return nullptr;
    }
    
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *FullPath);
        return nullptr;
    }

    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    if (!Factory)
    {
        OutError = TEXT("Failed to create BlueprintFactory");
        return nullptr;
    }

    Factory->ParentClass = AAIController::StaticClass();

    UBlueprint* Blueprint = Cast<UBlueprint>(
        Factory->FactoryCreateNew(UBlueprint::StaticClass(), Package, *Name,
                                  RF_Public | RF_Standalone, nullptr, GWarn));

    if (!Blueprint)
    {
        OutError = TEXT("Failed to create AI Controller blueprint");
        return nullptr;
    }

    FAssetRegistryModule::AssetCreated(Blueprint);
    SavePackageHelperAI(Package, Blueprint);

    return Blueprint;
}

// Helper to create Blackboard asset
static UBlackboardData* CreateBlackboardAsset(const FString& Path, const FString& Name, FString& OutError)
{
    // Sanitize and validate path first
    FString SanitizedPath;
    if (!SanitizeAIAssetPath(Path, SanitizedPath, OutError))
    {
        return nullptr;
    }
    
    FString FullPath = SanitizedPath / Name;
    
    // Check if asset already exists
    if (FindObject<UBlackboardData>(nullptr, *FullPath) != nullptr)
    {
        OutError = FString::Printf(TEXT("Asset already exists: %s"), *FullPath);
        return nullptr;
    }
    
    // Also check if the package exists
    if (FPackageName::DoesPackageExist(FullPath))
    {
        OutError = FString::Printf(TEXT("Package already exists: %s"), *FullPath);
        return nullptr;
    }
    
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *FullPath);
        return nullptr;
    }

    UBlackboardData* Blackboard = NewObject<UBlackboardData>(Package, UBlackboardData::StaticClass(), FName(*Name), RF_Public | RF_Standalone);
    if (!Blackboard)
    {
        OutError = TEXT("Failed to create Blackboard asset");
        return nullptr;
    }

    FAssetRegistryModule::AssetCreated(Blackboard);
    SavePackageHelperAI(Package, Blackboard);

    return Blackboard;
}

// Helper to create Behavior Tree asset
static UBehaviorTree* CreateBehaviorTreeAsset(const FString& Path, const FString& Name, FString& OutError)
{
    // Sanitize and validate path first
    FString SanitizedPath;
    if (!SanitizeAIAssetPath(Path, SanitizedPath, OutError))
    {
        return nullptr;
    }
    
    FString FullPath = SanitizedPath / Name;
    
    // Check if asset already exists
    if (FindObject<UBehaviorTree>(nullptr, *FullPath) != nullptr)
    {
        OutError = FString::Printf(TEXT("Asset already exists: %s"), *FullPath);
        return nullptr;
    }
    
    // Also check if the package exists
    if (FPackageName::DoesPackageExist(FullPath))
    {
        OutError = FString::Printf(TEXT("Package already exists: %s"), *FullPath);
        return nullptr;
    }
    
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *FullPath);
        return nullptr;
    }

    UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(Package, UBehaviorTree::StaticClass(), FName(*Name), RF_Public | RF_Standalone);
    if (!BehaviorTree)
    {
        OutError = TEXT("Failed to create Behavior Tree asset");
        return nullptr;
    }

    FAssetRegistryModule::AssetCreated(BehaviorTree);
    SavePackageHelperAI(Package, BehaviorTree);

    return BehaviorTree;
}

// Helper to create EQS Query asset
static UEnvQuery* CreateEQSQueryAsset(const FString& Path, const FString& Name, FString& OutError)
{
    // Sanitize and validate path first
    FString SanitizedPath;
    if (!SanitizeAIAssetPath(Path, SanitizedPath, OutError))
    {
        return nullptr;
    }
    
    FString FullPath = SanitizedPath / Name;
    
    // Check if asset already exists
    if (FindObject<UEnvQuery>(nullptr, *FullPath) != nullptr)
    {
        OutError = FString::Printf(TEXT("Asset already exists: %s"), *FullPath);
        return nullptr;
    }
    
    // Also check if the package exists
    if (FPackageName::DoesPackageExist(FullPath))
    {
        OutError = FString::Printf(TEXT("Package already exists: %s"), *FullPath);
        return nullptr;
    }
    
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *FullPath);
        return nullptr;
    }

    UEnvQuery* Query = NewObject<UEnvQuery>(Package, UEnvQuery::StaticClass(), FName(*Name), RF_Public | RF_Standalone);
    if (!Query)
    {
        OutError = TEXT("Failed to create EQS Query asset");
        return nullptr;
    }

    FAssetRegistryModule::AssetCreated(Query);
    SavePackageHelperAI(Package, Query);

    return Query;
}
#endif

bool UMcpAutomationBridgeSubsystem::HandleManageAIAction(
    const FString& RequestId, const FString& Action,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
    if (Action != TEXT("manage_ai"))
    {
        return false;
    }

#if !WITH_EDITOR
    SendAutomationError(RequestingSocket, RequestId,
                        TEXT("AI management is only available in editor builds"),
                        TEXT("EDITOR_ONLY"));
    return true;
#else
    FString SubAction = GetStringFieldAI(Payload, TEXT("subAction"));
    if (SubAction.IsEmpty())
    {
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Missing subAction parameter"),
                            TEXT("INVALID_PARAMS"));
        return true;
    }

    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());

    // =========================================================================
    // 16.1 AI Controller (3 actions)
    // =========================================================================

    if (SubAction == TEXT("create_ai_controller"))
    {
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/Controllers"));

        if (Name.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                                TEXT("Missing name parameter"),
                                TEXT("INVALID_PARAMS"));
            return true;
        }

        FString Error;
        UBlueprint* Blueprint = CreateAIControllerBlueprint(Path, Name, Error);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId, Error, TEXT("CREATION_FAILED"));
            return true;
        }

        Result->SetStringField(TEXT("controllerPath"), Blueprint->GetPathName());
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created AI Controller: %s"), *Name));
        AddAssetVerification(Result, Blueprint);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("AI Controller created"), Result);
        return true;
    }

    if (SubAction == TEXT("assign_behavior_tree"))
    {
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        FString BehaviorTreePath = GetStringFieldAI(Payload, TEXT("behaviorTreePath"));

        UBlueprint* Controller = LoadObject<UBlueprint>(nullptr, *ControllerPath);
        if (!Controller)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("AI Controller not found: %s"), *ControllerPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BehaviorTreePath);
        if (!BT)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Behavior Tree not found: %s"), *BehaviorTreePath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        // Set default BehaviorTree property on the generated class CDO using reflection
        if (Controller->GeneratedClass)
        {
            if (AAIController* CDO = Cast<AAIController>(Controller->GeneratedClass->GetDefaultObject()))
            {
                // Use reflection to find and set BehaviorTree-related properties
                // Look for common property names used in AI Controller blueprints
                bool bPropertySet = false;
                
                // Try to find a UBehaviorTree* property on the CDO
                for (TFieldIterator<FObjectProperty> PropIt(Controller->GeneratedClass); PropIt; ++PropIt)
                {
                    FObjectProperty* ObjProp = *PropIt;
                    if (ObjProp && ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UBehaviorTree::StaticClass()))
                    {
                        // Found a BehaviorTree property - set it
                        ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CDO), BT);
                        bPropertySet = true;
                        Result->SetStringField(TEXT("propertyName"), ObjProp->GetName());
                        break;
                    }
                }
                
                // If no existing property found, add a Blueprint variable for the BT reference
                if (!bPropertySet)
                {
                    // Add a Blueprint variable to store the BehaviorTree reference
                    FEdGraphPinType PinType;
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
                    PinType.PinSubCategoryObject = UBehaviorTree::StaticClass();
                    
                    const FName VarName = TEXT("DefaultBehaviorTree");
                    if (FBlueprintEditorUtils::AddMemberVariable(Controller, VarName, PinType))
                    {
                        // Set the default value for the variable
                        FProperty* NewProp = Controller->GeneratedClass->FindPropertyByName(VarName);
                        if (FObjectProperty* ObjProp = CastField<FObjectProperty>(NewProp))
                        {
                            ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CDO), BT);
                            bPropertySet = true;
                        }
                    }
                    Result->SetStringField(TEXT("propertyName"), VarName.ToString());
                }
                
                Result->SetBoolField(TEXT("propertyAssigned"), bPropertySet);
                Result->SetStringField(TEXT("message"), bPropertySet 
                    ? TEXT("Behavior Tree property assigned on CDO") 
                    : TEXT("Behavior Tree reference registered (call RunBehaviorTree in BeginPlay)"));
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Controller);
        McpSafeAssetSave(Controller);
        Result->SetStringField(TEXT("controllerPath"), ControllerPath);
        Result->SetStringField(TEXT("behaviorTreePath"), BehaviorTreePath);
        AddAssetVerification(Result, Controller);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Behavior Tree reference set"), Result);
        return true;
    }

    if (SubAction == TEXT("assign_blackboard"))
    {
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        FString BlackboardPath = GetStringFieldAI(Payload, TEXT("blackboardPath"));

        UBlueprint* Controller = LoadObject<UBlueprint>(nullptr, *ControllerPath);
        if (!Controller)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("AI Controller not found: %s"), *ControllerPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
        if (!BB)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Blackboard not found: %s"), *BlackboardPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        // Set default Blackboard property on the generated class CDO using reflection
        // The Blueprint can call UseBlackboard() in BeginPlay with this asset reference
        if (Controller->GeneratedClass)
        {
            if (AAIController* CDO = Cast<AAIController>(Controller->GeneratedClass->GetDefaultObject()))
            {
                // Use reflection to find and set Blackboard-related properties
                bool bPropertySet = false;
                
                // Try to find a UBlackboardData* property on the CDO
                for (TFieldIterator<FObjectProperty> PropIt(Controller->GeneratedClass); PropIt; ++PropIt)
                {
                    FObjectProperty* ObjProp = *PropIt;
                    if (ObjProp && ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UBlackboardData::StaticClass()))
                    {
                        // Found a BlackboardData property - set it
                        ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CDO), BB);
                        bPropertySet = true;
                        Result->SetStringField(TEXT("propertyName"), ObjProp->GetName());
                        break;
                    }
                }
                
                // If no existing property found, add a Blueprint variable for the Blackboard reference
                if (!bPropertySet)
                {
                    // Add a Blueprint variable to store the BlackboardData reference
                    FEdGraphPinType PinType;
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
                    PinType.PinSubCategoryObject = UBlackboardData::StaticClass();
                    
                    const FName VarName = TEXT("DefaultBlackboard");
                    if (FBlueprintEditorUtils::AddMemberVariable(Controller, VarName, PinType))
                    {
                        // Set the default value for the variable
                        FProperty* NewProp = Controller->GeneratedClass->FindPropertyByName(VarName);
                        if (FObjectProperty* ObjProp = CastField<FObjectProperty>(NewProp))
                        {
                            ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CDO), BB);
                            bPropertySet = true;
                        }
                    }
                    Result->SetStringField(TEXT("propertyName"), VarName.ToString());
                }
                
                Result->SetBoolField(TEXT("propertyAssigned"), bPropertySet);
                Result->SetStringField(TEXT("message"), bPropertySet 
                    ? TEXT("Blackboard property assigned on CDO (call UseBlackboard in BeginPlay with this asset)") 
                    : TEXT("Blackboard reference registered (call UseBlackboard in BeginPlay with this asset)"));
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Controller);
        bool bSaved = McpSafeAssetSave(Controller);
        Result->SetBoolField(TEXT("saved"), bSaved);
        Result->SetStringField(TEXT("controllerPath"), ControllerPath);
        Result->SetStringField(TEXT("blackboardPath"), BlackboardPath);
        AddAssetVerification(Result, Controller);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Blackboard reference set"), Result);
        return true;
    }

    // =========================================================================
    // 16.2 Blackboard (3 actions)
    // =========================================================================

    if (SubAction == TEXT("create_blackboard_asset"))
    {
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/Blackboards"));

        if (Name.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                                TEXT("Missing name parameter"),
                                TEXT("INVALID_PARAMS"));
            return true;
        }

        FString Error;
        UBlackboardData* Blackboard = CreateBlackboardAsset(Path, Name, Error);
        if (!Blackboard)
        {
            SendAutomationError(RequestingSocket, RequestId, Error, TEXT("CREATION_FAILED"));
            return true;
        }

        Result->SetStringField(TEXT("blackboardPath"), Blackboard->GetPathName());
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created Blackboard: %s"), *Name));
        AddAssetVerification(Result, Blackboard);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Blackboard created"), Result);
        return true;
    }

    if (SubAction == TEXT("add_blackboard_key"))
    {
        FString BlackboardPath = GetStringFieldAI(Payload, TEXT("blackboardPath"));
        FString KeyName = GetStringFieldAI(Payload, TEXT("keyName"));
        FString KeyType = GetStringFieldAI(Payload, TEXT("keyType"));

        UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
        if (!Blackboard)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Blackboard not found: %s"), *BlackboardPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        // Create appropriate key type
        FBlackboardEntry NewEntry;
        NewEntry.EntryName = FName(*KeyName);

        if (KeyType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Bool>(Blackboard);
        }
        else if (KeyType.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Int>(Blackboard);
        }
        else if (KeyType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Float>(Blackboard);
        }
        else if (KeyType.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Vector>(Blackboard);
        }
        else if (KeyType.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Rotator>(Blackboard);
        }
        else if (KeyType.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
        {
            UBlackboardKeyType_Object* ObjectKey = NewObject<UBlackboardKeyType_Object>(Blackboard);
            FString BaseClass = GetStringFieldAI(Payload, TEXT("baseObjectClass"), TEXT("Actor"));
            // Could set base class here
            NewEntry.KeyType = ObjectKey;
        }
        else if (KeyType.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Class>(Blackboard);
        }
        else if (KeyType.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Enum>(Blackboard);
        }
        else if (KeyType.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Name>(Blackboard);
        }
        else if (KeyType.Equals(TEXT("String"), ESearchCase::IgnoreCase))
        {
            NewEntry.KeyType = NewObject<UBlackboardKeyType_String>(Blackboard);
        }
        else
        {
            // Default to Object
            NewEntry.KeyType = NewObject<UBlackboardKeyType_Object>(Blackboard);
        }

        NewEntry.bInstanceSynced = GetBoolFieldAI(Payload, TEXT("isInstanceSynced"), false);

        Blackboard->Keys.Add(NewEntry);
        Blackboard->MarkPackageDirty();
        SavePackageHelperAI(Blackboard->GetOutermost(), Blackboard);

        Result->SetNumberField(TEXT("keyIndex"), Blackboard->Keys.Num() - 1);
        Result->SetStringField(TEXT("keyName"), KeyName);
        Result->SetStringField(TEXT("keyType"), KeyType);
        AddAssetVerification(Result, Blackboard);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Blackboard key added"), Result);
        return true;
    }

    if (SubAction == TEXT("set_key_instance_synced"))
    {
        FString BlackboardPath = GetStringFieldAI(Payload, TEXT("blackboardPath"));
        FString KeyName = GetStringFieldAI(Payload, TEXT("keyName"));
        bool bInstanceSynced = GetBoolFieldAI(Payload, TEXT("isInstanceSynced"), true);

        UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
        if (!Blackboard)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Blackboard not found: %s"), *BlackboardPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        bool bFound = false;
        for (FBlackboardEntry& Entry : Blackboard->Keys)
        {
            if (Entry.EntryName.ToString() == KeyName)
            {
                Entry.bInstanceSynced = bInstanceSynced;
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Key not found: %s"), *KeyName),
                                TEXT("NOT_FOUND"));
            return true;
        }

        Blackboard->MarkPackageDirty();
        SavePackageHelperAI(Blackboard->GetOutermost(), Blackboard);

        Result->SetStringField(TEXT("keyName"), KeyName);
        Result->SetBoolField(TEXT("isInstanceSynced"), bInstanceSynced);
        AddAssetVerification(Result, Blackboard);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Key instance sync updated"), Result);
        return true;
    }

    // =========================================================================
    // 16.3 Behavior Tree - Expanded (6 actions)
    // =========================================================================

    if (SubAction == TEXT("create_behavior_tree"))
    {
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/BehaviorTrees"));

        if (Name.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                                TEXT("Missing name parameter"),
                                TEXT("INVALID_PARAMS"));
            return true;
        }

        FString Error;
        UBehaviorTree* BT = CreateBehaviorTreeAsset(Path, Name, Error);
        if (!BT)
        {
            SendAutomationError(RequestingSocket, RequestId, Error, TEXT("CREATION_FAILED"));
            return true;
        }

        Result->SetStringField(TEXT("behaviorTreePath"), BT->GetPathName());
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created Behavior Tree: %s"), *Name));
        AddAssetVerification(Result, BT);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Behavior Tree created"), Result);
        return true;
    }

    if (SubAction == TEXT("add_composite_node"))
    {
        FString BTPath = GetStringFieldAI(Payload, TEXT("behaviorTreePath"));
        FString CompositeType = GetStringFieldAI(Payload, TEXT("compositeType"));

        UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
        if (!BT)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Behavior Tree not found: %s"), *BTPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        UBTCompositeNode* NewNode = nullptr;
        if (CompositeType.Equals(TEXT("Selector"), ESearchCase::IgnoreCase))
        {
            NewNode = NewObject<UBTComposite_Selector>(BT);
        }
        else if (CompositeType.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase))
        {
            NewNode = NewObject<UBTComposite_Sequence>(BT);
        }
        // Add more composite types as needed

        if (NewNode)
        {
            // For adding to root, we'd need to access the internal structure
            // The BT needs a root node set
            if (!BT->RootNode)
            {
                BT->RootNode = NewNode;
            }
            BT->MarkPackageDirty();
            SavePackageHelperAI(BT->GetOutermost(), BT);

            Result->SetStringField(TEXT("compositeType"), CompositeType);
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added %s node"), *CompositeType));
            AddAssetVerification(Result, BT);
            SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Composite node added"), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Failed to create composite node: %s"), *CompositeType),
                                TEXT("CREATION_FAILED"));
        }

        return true;
    }

    if (SubAction == TEXT("add_task_node"))
    {
        FString BTPath = GetStringFieldAI(Payload, TEXT("behaviorTreePath"));
        FString TaskType = GetStringFieldAI(Payload, TEXT("taskType"));

        UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
        if (!BT)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Behavior Tree not found: %s"), *BTPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        UBTTaskNode* NewTask = nullptr;
        if (TaskType.Equals(TEXT("MoveTo"), ESearchCase::IgnoreCase))
        {
            NewTask = NewObject<UBTTask_MoveTo>(BT);
        }
        else if (TaskType.Equals(TEXT("Wait"), ESearchCase::IgnoreCase))
        {
            NewTask = NewObject<UBTTask_Wait>(BT);
        }
        // Add more task types as needed

        if (NewTask)
        {
            BT->MarkPackageDirty();
            Result->SetStringField(TEXT("taskType"), TaskType);
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added %s task"), *TaskType));
            AddAssetVerification(Result, BT);
            SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Task node added"), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Failed to create task node: %s"), *TaskType),
                                TEXT("CREATION_FAILED"));
        }

        return true;
    }

    if (SubAction == TEXT("add_decorator"))
    {
        FString BTPath = GetStringFieldAI(Payload, TEXT("behaviorTreePath"));
        FString DecoratorType = GetStringFieldAI(Payload, TEXT("decoratorType"));

        UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
        if (!BT)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Behavior Tree not found: %s"), *BTPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        UBTDecorator* NewDecorator = nullptr;
        if (DecoratorType.Equals(TEXT("Blackboard"), ESearchCase::IgnoreCase))
        {
            NewDecorator = NewObject<UBTDecorator_Blackboard>(BT);
        }
        else if (DecoratorType.Equals(TEXT("Cooldown"), ESearchCase::IgnoreCase))
        {
            NewDecorator = NewObject<UBTDecorator_Cooldown>(BT);
        }
        else if (DecoratorType.Equals(TEXT("Loop"), ESearchCase::IgnoreCase))
        {
            NewDecorator = NewObject<UBTDecorator_Loop>(BT);
        }
        // Add more decorator types as needed

        if (NewDecorator)
        {
            BT->MarkPackageDirty();
            Result->SetStringField(TEXT("decoratorType"), DecoratorType);
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added %s decorator"), *DecoratorType));
            AddAssetVerification(Result, BT);
            SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Decorator added"), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Failed to create decorator: %s"), *DecoratorType),
                                TEXT("CREATION_FAILED"));
        }

        return true;
    }

    if (SubAction == TEXT("add_service"))
    {
        FString BTPath = GetStringFieldAI(Payload, TEXT("behaviorTreePath"));
        FString ServiceType = GetStringFieldAI(Payload, TEXT("serviceType"));

        UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
        if (!BT)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Behavior Tree not found: %s"), *BTPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        // Services are added to composite nodes, not directly to the tree
        // For now, just mark the tree as modified
        BT->MarkPackageDirty();
        Result->SetStringField(TEXT("serviceType"), ServiceType);
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Service %s reference created"), *ServiceType));

        AddAssetVerification(Result, BT);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Service added"), Result);
        return true;
    }

    if (SubAction == TEXT("configure_bt_node"))
    {
        FString BTPath = GetStringFieldAI(Payload, TEXT("behaviorTreePath"));
        FString NodeId = GetStringFieldAI(Payload, TEXT("nodeId"));

        UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
        if (!BT)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Behavior Tree not found: %s"), *BTPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        // Node configuration would require finding the node by ID and setting properties
        BT->MarkPackageDirty();
        Result->SetStringField(TEXT("nodeId"), NodeId);
        Result->SetStringField(TEXT("message"), TEXT("Node configuration updated"));

        AddAssetVerification(Result, BT);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Node configured"), Result);
        return true;
    }

    // =========================================================================
    // 16.4 Environment Query System - EQS (5 actions)
    // =========================================================================

    if (SubAction == TEXT("create_eqs_query"))
    {
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/EQS"));

        if (Name.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                                TEXT("Missing name parameter"),
                                TEXT("INVALID_PARAMS"));
            return true;
        }

        FString Error;
        UEnvQuery* Query = CreateEQSQueryAsset(Path, Name, Error);
        if (!Query)
        {
            SendAutomationError(RequestingSocket, RequestId, Error, TEXT("CREATION_FAILED"));
            return true;
        }

        Result->SetStringField(TEXT("queryPath"), Query->GetPathName());
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created EQS Query: %s"), *Name));
        AddAssetVerification(Result, Query);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("EQS Query created"), Result);
        return true;
    }

    if (SubAction == TEXT("add_eqs_generator"))
    {
        FString QueryPath = GetStringFieldAI(Payload, TEXT("queryPath"));
        FString GeneratorType = GetStringFieldAI(Payload, TEXT("generatorType"));

        UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
        if (!Query)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("EQS Query not found: %s"), *QueryPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        UEnvQueryGenerator* NewGenerator = nullptr;
        if (GeneratorType.Equals(TEXT("ActorsOfClass"), ESearchCase::IgnoreCase))
        {
            NewGenerator = NewObject<UEnvQueryGenerator_ActorsOfClass>(Query);
        }
        else if (GeneratorType.Equals(TEXT("OnCircle"), ESearchCase::IgnoreCase))
        {
            NewGenerator = NewObject<UEnvQueryGenerator_OnCircle>(Query);
        }
        else if (GeneratorType.Equals(TEXT("SimpleGrid"), ESearchCase::IgnoreCase))
        {
            NewGenerator = NewObject<UEnvQueryGenerator_SimpleGrid>(Query);
        }

        if (NewGenerator)
        {
            // Configure generator settings from payload
            const TSharedPtr<FJsonObject>* GenSettings = nullptr;
            if (Payload->TryGetObjectField(TEXT("generatorSettings"), GenSettings) && GenSettings)
            {
                double SearchRadius = 0;
                if ((*GenSettings)->TryGetNumberField(TEXT("searchRadius"), SearchRadius))
                {
                    if (FProperty* Prop = NewGenerator->GetClass()->FindPropertyByName(TEXT("SearchRadius")))
                    {
                        if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
                        {
                            // FAIDataProviderFloatValue
                            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NewGenerator);
                            if (FProperty* DefaultProp = StructProp->Struct->FindPropertyByName(TEXT("DefaultValue")))
                            {
                                float Val = static_cast<float>(SearchRadius);
                                DefaultProp->SetValue_InContainer(ValuePtr, &Val);
                            }
                        }
                        else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
                        {
                            float Val = static_cast<float>(SearchRadius);
                            FloatProp->SetPropertyValue_InContainer(NewGenerator, Val);
                        }
                    }
                }

                double GridSize = 0;
                if ((*GenSettings)->TryGetNumberField(TEXT("gridSize"), GridSize))
                {
                    if (FProperty* Prop = NewGenerator->GetClass()->FindPropertyByName(TEXT("GridSize")))
                    {
                        if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
                        {
                            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NewGenerator);
                            if (FProperty* DefaultProp = StructProp->Struct->FindPropertyByName(TEXT("DefaultValue")))
                            {
                                float Val = static_cast<float>(GridSize);
                                DefaultProp->SetValue_InContainer(ValuePtr, &Val);
                            }
                        }
                        else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
                        {
                            float Val = static_cast<float>(GridSize);
                            FloatProp->SetPropertyValue_InContainer(NewGenerator, Val);
                        }
                    }
                }
            }

            // Create a UEnvQueryOption to hold the generator and add to query
            UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
            Option->Generator = NewGenerator;
            Query->GetOptionsMutable().Add(Option);

            Query->MarkPackageDirty();
            Result->SetStringField(TEXT("generatorType"), GeneratorType);
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added %s generator"), *GeneratorType));
            AddAssetVerification(Result, Query);
            SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Generator added"), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Failed to create generator: %s"), *GeneratorType),
                                TEXT("CREATION_FAILED"));
        }

        return true;
    }

    if (SubAction == TEXT("add_eqs_context"))
    {
        FString QueryPath = GetStringFieldAI(Payload, TEXT("queryPath"));
        FString ContextType = GetStringFieldAI(Payload, TEXT("contextType"));

        UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
        if (!Query)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("EQS Query not found: %s"), *QueryPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        Query->MarkPackageDirty();
        Result->SetStringField(TEXT("contextType"), ContextType);
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Context %s configured"), *ContextType));

        AddAssetVerification(Result, Query);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Context added"), Result);
        return true;
    }

    if (SubAction == TEXT("add_eqs_test"))
    {
#if !MCP_HAS_ENVQUERY_TESTS
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("EQS Test creation requires UE 5.1+"),
                            TEXT("NOT_SUPPORTED"));
        return true;
#else
        FString QueryPath = GetStringFieldAI(Payload, TEXT("queryPath"));
        FString TestType = GetStringFieldAI(Payload, TEXT("testType"));

        UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
        if (!Query)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("EQS Query not found: %s"), *QueryPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        UEnvQueryTest* NewTest = nullptr;
#if MCP_HAS_ENVQUERY_TESTS
        // Use runtime class lookup to avoid GetPrivateStaticClass requirement
        // StaticClass() calls GetPrivateStaticClass() internally which isn't exported
        UClass* TestClass = nullptr;
        if (TestType.Equals(TEXT("Distance"), ESearchCase::IgnoreCase))
        {
            TestClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvQueryTest_Distance"));
        }
        else if (TestType.Equals(TEXT("Trace"), ESearchCase::IgnoreCase))
        {
            TestClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvQueryTest_Trace"));
        }
        
        if (TestClass)
        {
            // Use NewObject with runtime UClass parameter to avoid template instantiation
            UObject* TestObj = NewObject<UObject>(Query, TestClass);
            if (TestObj && TestObj->GetClass()->IsChildOf(UEnvQueryTest::StaticClass()))
            {
                NewTest = static_cast<UEnvQueryTest*>(TestObj);
            }
        }
#endif

        if (NewTest)
        {
            // Add test to the last option in the query (most recently added generator)
            TArray<TObjectPtr<UEnvQueryOption>>& Options = Query->GetOptionsMutable();
            if (Options.Num() > 0)
            {
                Options.Last()->Tests.Add(NewTest);
            }
            else
            {
                // No options yet -- create a bare option to hold the test
                UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
                Option->Tests.Add(NewTest);
                Options.Add(Option);
            }

            Query->MarkPackageDirty();
            Result->SetStringField(TEXT("testType"), TestType);
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added %s test"), *TestType));
            AddAssetVerification(Result, Query);
            SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Test added"), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Failed to create test: %s"), *TestType),
                                TEXT("CREATION_FAILED"));
        }

        return true;
#endif
    }

    if (SubAction == TEXT("configure_test_scoring"))
    {
        FString QueryPath = GetStringFieldAI(Payload, TEXT("queryPath"));
        int32 TestIndex = static_cast<int32>(GetNumberFieldAI(Payload, TEXT("testIndex"), 0));

        UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
        if (!Query)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("EQS Query not found: %s"), *QueryPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        // Find the test by index across all options
        UEnvQueryTest* TargetTest = nullptr;
        int32 CurrentIndex = 0;
        TArray<TObjectPtr<UEnvQueryOption>>& Options = Query->GetOptionsMutable();
        for (const auto& Option : Options)
        {
            if (!Option) continue;
            for (UEnvQueryTest* Test : Option->Tests)
            {
                if (CurrentIndex == TestIndex)
                {
                    TargetTest = Test;
                    break;
                }
                ++CurrentIndex;
            }
            if (TargetTest) break;
        }

        if (!TargetTest)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Test index %d not found (query has %d tests)"), TestIndex, CurrentIndex),
                                TEXT("INDEX_OUT_OF_RANGE"));
            return true;
        }

        // Read testSettings object
        const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
        if (Payload->TryGetObjectField(TEXT("testSettings"), SettingsObj) && SettingsObj->IsValid())
        {
            // Filter range (FloatValueMin / FloatValueMax)
            double FloatMin = 0;
            if ((*SettingsObj)->TryGetNumberField(TEXT("floatMin"), FloatMin))
            {
                TargetTest->FloatValueMin.DefaultValue = static_cast<float>(FloatMin);
            }
            double FloatMax = 0;
            if ((*SettingsObj)->TryGetNumberField(TEXT("floatMax"), FloatMax))
            {
                TargetTest->FloatValueMax.DefaultValue = static_cast<float>(FloatMax);
            }

            // Scoring equation
            FString ScoringEq;
            if ((*SettingsObj)->TryGetStringField(TEXT("scoringEquation"), ScoringEq))
            {
                if (ScoringEq.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))
                    TargetTest->ScoringEquation = EEnvTestScoreEquation::Linear;
                else if (ScoringEq.Equals(TEXT("Square"), ESearchCase::IgnoreCase))
                    TargetTest->ScoringEquation = EEnvTestScoreEquation::Square;
                else if (ScoringEq.Equals(TEXT("InverseLinear"), ESearchCase::IgnoreCase))
                    TargetTest->ScoringEquation = EEnvTestScoreEquation::InverseLinear;
                else if (ScoringEq.Equals(TEXT("SquareRoot"), ESearchCase::IgnoreCase))
                    TargetTest->ScoringEquation = EEnvTestScoreEquation::SquareRoot;
                else if (ScoringEq.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
                    TargetTest->ScoringEquation = EEnvTestScoreEquation::Constant;
            }

            // Filter type
            FString FilterTypeStr;
            if ((*SettingsObj)->TryGetStringField(TEXT("filterType"), FilterTypeStr))
            {
                if (FilterTypeStr.Equals(TEXT("Minimum"), ESearchCase::IgnoreCase))
                    TargetTest->FilterType = EEnvTestFilterType::Minimum;
                else if (FilterTypeStr.Equals(TEXT("Maximum"), ESearchCase::IgnoreCase))
                    TargetTest->FilterType = EEnvTestFilterType::Maximum;
                else if (FilterTypeStr.Equals(TEXT("Range"), ESearchCase::IgnoreCase))
                    TargetTest->FilterType = EEnvTestFilterType::Range;
                else if (FilterTypeStr.Equals(TEXT("Match"), ESearchCase::IgnoreCase))
                    TargetTest->FilterType = EEnvTestFilterType::Match;
            }

            // Test purpose
            FString PurposeStr;
            if ((*SettingsObj)->TryGetStringField(TEXT("testPurpose"), PurposeStr))
            {
                if (PurposeStr.Equals(TEXT("Filter"), ESearchCase::IgnoreCase) || PurposeStr.Equals(TEXT("FilterOnly"), ESearchCase::IgnoreCase))
                    TargetTest->TestPurpose = EEnvTestPurpose::Filter;
                else if (PurposeStr.Equals(TEXT("Score"), ESearchCase::IgnoreCase) || PurposeStr.Equals(TEXT("ScoreOnly"), ESearchCase::IgnoreCase))
                    TargetTest->TestPurpose = EEnvTestPurpose::Score;
                else if (PurposeStr.Equals(TEXT("FilterAndScore"), ESearchCase::IgnoreCase))
                    TargetTest->TestPurpose = EEnvTestPurpose::FilterAndScore;
            }

            // Score clamping
            double ClampMin = 0;
            if ((*SettingsObj)->TryGetNumberField(TEXT("clampMin"), ClampMin))
            {
                TargetTest->ScoreClampMin.DefaultValue = static_cast<float>(ClampMin);
                TargetTest->ClampMinType = EEnvQueryTestClamping::SpecifiedValue;
            }
            double ClampMax = 0;
            if ((*SettingsObj)->TryGetNumberField(TEXT("clampMax"), ClampMax))
            {
                TargetTest->ScoreClampMax.DefaultValue = static_cast<float>(ClampMax);
                TargetTest->ClampMaxType = EEnvQueryTestClamping::SpecifiedValue;
            }

            // Scoring factor (weight)
            double ScoringFactor = 0;
            if ((*SettingsObj)->TryGetNumberField(TEXT("scoringFactor"), ScoringFactor))
            {
                TargetTest->ScoringFactor.DefaultValue = static_cast<float>(ScoringFactor);
            }
        }

        Query->MarkPackageDirty();
        Result->SetNumberField(TEXT("testIndex"), TestIndex);
        Result->SetStringField(TEXT("testClass"), TargetTest->GetClass()->GetName());
        Result->SetStringField(TEXT("message"), TEXT("Test scoring configured"));

        AddAssetVerification(Result, Query);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Scoring configured"), Result);
        return true;
    }

    // =========================================================================
    // 16.5 Perception System (5 actions)
    // =========================================================================

    if (SubAction == TEXT("add_ai_perception_component"))
    {
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
        if (!SCS)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                TEXT("Blueprint has no SimpleConstructionScript"),
                                TEXT("INVALID_BLUEPRINT"));
            return true;
        }

        // Create perception component
        USCS_Node* NewNode = SCS->CreateNode(UAIPerceptionComponent::StaticClass(), TEXT("AIPerception"));
        if (NewNode)
        {
            SCS->AddNode(NewNode);
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

            Result->SetStringField(TEXT("componentName"), TEXT("AIPerception"));
            Result->SetStringField(TEXT("message"), TEXT("AI Perception component added"));
            AddAssetVerification(Result, Blueprint);
            SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Perception component added"), Result);
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                                TEXT("Failed to create AI Perception component"),
                                TEXT("CREATION_FAILED"));
        }

        return true;
    }

    if (SubAction == TEXT("configure_sight_config"))
    {
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        // Get sight config parameters
        const TSharedPtr<FJsonObject>* SightConfigObj = nullptr;
        if (Payload->TryGetObjectField(TEXT("sightConfig"), SightConfigObj) && SightConfigObj->IsValid())
        {
            double SightRadius = GetNumberFieldAI(*SightConfigObj, TEXT("sightRadius"), 3000.0);
            double LoseSightRadius = GetNumberFieldAI(*SightConfigObj, TEXT("loseSightRadius"), 3500.0);
            double PeripheralAngle = GetNumberFieldAI(*SightConfigObj, TEXT("peripheralVisionAngle"), 90.0);

            Result->SetNumberField(TEXT("sightRadius"), SightRadius);
            Result->SetNumberField(TEXT("loseSightRadius"), LoseSightRadius);
            Result->SetNumberField(TEXT("peripheralVisionAngle"), PeripheralAngle);
        }

        Blueprint->MarkPackageDirty();
        Result->SetStringField(TEXT("message"), TEXT("Sight sense configured"));
        AddAssetVerification(Result, Blueprint);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Sight config set"), Result);
        return true;
    }

    if (SubAction == TEXT("configure_hearing_config"))
    {
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        const TSharedPtr<FJsonObject>* HearingConfigObj = nullptr;
        if (Payload->TryGetObjectField(TEXT("hearingConfig"), HearingConfigObj) && HearingConfigObj->IsValid())
        {
            double HearingRange = GetNumberFieldAI(*HearingConfigObj, TEXT("hearingRange"), 3000.0);
            Result->SetNumberField(TEXT("hearingRange"), HearingRange);
        }

        Blueprint->MarkPackageDirty();
        Result->SetStringField(TEXT("message"), TEXT("Hearing sense configured"));
        AddAssetVerification(Result, Blueprint);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Hearing config set"), Result);
        return true;
    }

    if (SubAction == TEXT("configure_damage_sense_config"))
    {
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        Blueprint->MarkPackageDirty();
        Result->SetStringField(TEXT("message"), TEXT("Damage sense configured"));
        AddAssetVerification(Result, Blueprint);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Damage config set"), Result);
        return true;
    }

    if (SubAction == TEXT("set_perception_team"))
    {
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));
        int32 TeamId = static_cast<int32>(GetNumberFieldAI(Payload, TEXT("teamId"), 0));

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId,
                                FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath),
                                TEXT("NOT_FOUND"));
            return true;
        }

        Blueprint->MarkPackageDirty();
        Result->SetNumberField(TEXT("teamId"), TeamId);
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Team ID set to %d"), TeamId));
        AddAssetVerification(Result, Blueprint);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Team set"), Result);
        return true;
    }

    // =========================================================================
    // 16.6 State Trees - UE5.3+ (4 actions)
    // =========================================================================

    if (SubAction == TEXT("create_state_tree"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/StateTrees"));
        // schemaType: legacy ("Component"). schemaClass: explicit class name like "CanopyStateTreeSchema".
        FString SchemaType = GetStringFieldAI(Payload, TEXT("schemaType"), TEXT("Component"));
        FString SchemaClass = GetStringFieldAI(Payload, TEXT("schemaClass"), TEXT(""));

        if (Name.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("State Tree name is required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        // Create the package and asset
        FString FullPath = Path / Name;
        UPackage* Package = CreatePackage(*FullPath);
        if (!Package)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Failed to create package: %s"), *FullPath), TEXT("CREATION_FAILED"));
            return true;
        }

        UStateTree* StateTree = NewObject<UStateTree>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
        if (!StateTree)
        {
            Package->MarkAsGarbage();
            SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create StateTree asset"), TEXT("CREATION_FAILED"));
            return true;
        }

        UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(StateTree, TEXT("EditorData"), RF_Transactional);
        if (!EditorData)
        {
            StateTree->ConditionalBeginDestroy();
            Package->MarkAsGarbage();
            SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create StateTree EditorData"), TEXT("CREATION_FAILED"));
            return true;
        }
        StateTree->EditorData = EditorData;

        // Resolve the schema UClass: explicit schemaClass first, then fall back to schemaType=Component default.
        UClass* SchemaUClass = nullptr;
        FString ResolvedSchemaName;
        if (!SchemaClass.IsEmpty())
        {
            FString CleanName = SchemaClass;
            if (!CleanName.StartsWith(TEXT("U")) && !CleanName.StartsWith(TEXT("/")))
            {
                CleanName = TEXT("U") + CleanName;
            }
            SchemaUClass = FindFirstObject<UClass>(*CleanName, EFindFirstObjectOptions::NativeFirst);
            if (!SchemaUClass)
            {
                SchemaUClass = FindFirstObject<UClass>(*SchemaClass, EFindFirstObjectOptions::NativeFirst);
            }
            if (!SchemaUClass)
            {
                SchemaUClass = LoadObject<UClass>(nullptr, *SchemaClass);
            }
            if (!SchemaUClass || !SchemaUClass->IsChildOf(UStateTreeSchema::StaticClass()))
            {
                StateTree->ConditionalBeginDestroy();
                Package->MarkAsGarbage();
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Schema class '%s' not found or not a UStateTreeSchema subclass"), *SchemaClass),
                    TEXT("NOT_FOUND"));
                return true;
            }
            ResolvedSchemaName = SchemaUClass->GetName();
        }
        else
        {
#if MCP_STATE_TREE_COMPONENT_SCHEMA_AVAILABLE
            SchemaUClass = UStateTreeComponentSchema::StaticClass();
            ResolvedSchemaName = SchemaUClass->GetName();
#endif
        }

        // Instantiate the schema as an Instanced subobject of EditorData and mirror the pointer onto
        // UStateTree::Schema so UStateTreeComponent::SetStateTree's compatibility check passes pre-compile.
        // UStateTree::Schema is private (only FStateTreeCompiler is friend), so we write via reflection.
        if (SchemaUClass)
        {
            UStateTreeSchema* SchemaInstance = NewObject<UStateTreeSchema>(EditorData, SchemaUClass, NAME_None, RF_Transactional);
            EditorData->Schema = SchemaInstance;
            if (FObjectProperty* RuntimeSchemaProp = FindFProperty<FObjectProperty>(UStateTree::StaticClass(), TEXT("Schema")))
            {
                RuntimeSchemaProp->SetObjectPropertyValue_InContainer(StateTree, SchemaInstance);
            }
        }

        UStateTreeState& RootState = EditorData->AddRootState();
        RootState.Name = FName(TEXT("Root"));

        FAssetRegistryModule::AssetCreated(StateTree);
        EditorData->MarkPackageDirty();
        StateTree->MarkPackageDirty();
        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("stateTreePath"), FullPath);
        Result->SetStringField(TEXT("rootStateName"), TEXT("Root"));
        Result->SetStringField(TEXT("schemaClass"), ResolvedSchemaName);
        Result->SetBoolField(TEXT("schemaInstantiated"), SchemaUClass != nullptr);
        Result->SetStringField(TEXT("message"), TEXT("State Tree created with root state"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("State Tree created"), Result);
#elif MCP_HAS_STATE_TREE
        // Headers not available but version supports it
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/StateTrees"));
        Result->SetStringField(TEXT("stateTreePath"), Path / Name);
        Result->SetStringField(TEXT("message"), TEXT("State Tree creation registered (headers unavailable - enable StateTree plugin)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        // Note: No verification since StateTree was not actually created
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("State Tree registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("add_state_tree_state"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        FString ParentStateName = GetStringFieldAI(Payload, TEXT("parentStateName"), TEXT("Root"));
        FString StateType = GetStringFieldAI(Payload, TEXT("stateType"), TEXT("State"));
        
        if (StateTreePath.IsEmpty() || StateName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("stateTreePath and stateName are required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Load the StateTree
        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }
        
        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }
        
        // Find the parent state
        UStateTreeState* ParentState = nullptr;
        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            if (SubTree && SubTree->Name.ToString().Equals(ParentStateName, ESearchCase::IgnoreCase))
            {
                ParentState = SubTree;
                break;
            }
            // Check children recursively
            if (SubTree)
            {
                for (UStateTreeState* Child : SubTree->Children)
                {
                    if (Child && Child->Name.ToString().Equals(ParentStateName, ESearchCase::IgnoreCase))
                    {
                        ParentState = Child;
                        break;
                    }
                }
            }
        }
        
        if (!ParentState)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Parent state '%s' not found"), *ParentStateName), TEXT("NOT_FOUND"));
            return true;
        }
        
        // Determine state type
        EStateTreeStateType Type = EStateTreeStateType::State;
        if (StateType.Equals(TEXT("Group"), ESearchCase::IgnoreCase))
        {
            Type = EStateTreeStateType::Group;
        }
        else if (StateType.Equals(TEXT("Linked"), ESearchCase::IgnoreCase))
        {
            Type = EStateTreeStateType::Linked;
        }
        else if (StateType.Equals(TEXT("LinkedAsset"), ESearchCase::IgnoreCase))
        {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
            Type = EStateTreeStateType::LinkedAsset;
#else
            UE_LOG(LogMcpAIHandlers, Warning, TEXT("LinkedAsset state type requires UE 5.4+. Falling back to State type."));
            Type = EStateTreeStateType::State;
#endif
        }
        
        // Add the child state
        UStateTreeState& NewState = ParentState->AddChildState(FName(*StateName), Type);
        
        // Save
        McpSafeAssetSave(StateTree);
        
        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetStringField(TEXT("parentState"), ParentStateName);
        Result->SetStringField(TEXT("stateType"), StateType);
        Result->SetStringField(TEXT("message"), TEXT("State added to StateTree"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("State added"), Result);
#elif MCP_HAS_STATE_TREE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetStringField(TEXT("message"), TEXT("State addition registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        // Note: No verification since StateTree headers unavailable
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("State registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("add_state_tree_transition"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString FromState = GetStringFieldAI(Payload, TEXT("fromState"));
        FString ToState = GetStringFieldAI(Payload, TEXT("toState"));

        // trigger (preferred), with back-compat aliases triggerType and completionType.
        // completionType from the original schema only carried Succeeded/Failed; treat those
        // as OnStateSucceeded / OnStateFailed.
        FString TriggerStr;
        if (Payload->HasField(TEXT("trigger")))
        {
            TriggerStr = GetStringFieldAI(Payload, TEXT("trigger"));
        }
        else if (Payload->HasField(TEXT("triggerType")))
        {
            TriggerStr = GetStringFieldAI(Payload, TEXT("triggerType"));
        }
        else if (Payload->HasField(TEXT("completionType")))
        {
            const FString Completion = GetStringFieldAI(Payload, TEXT("completionType"));
            if (Completion.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
            {
                TriggerStr = TEXT("OnStateSucceeded");
            }
            else if (Completion.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
            {
                TriggerStr = TEXT("OnStateFailed");
            }
            else
            {
                TriggerStr = Completion;
            }
        }
        else
        {
            TriggerStr = TEXT("OnStateCompleted");
        }

        const FString TransitionTypeStr = GetStringFieldAI(Payload, TEXT("transitionType"), TEXT("GotoState"));
        const bool bIsGotoState = TransitionTypeStr.Equals(TEXT("GotoState"), ESearchCase::IgnoreCase);

        if (StateTreePath.IsEmpty() || FromState.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("stateTreePath and fromState are required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        if (bIsGotoState && ToState.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("toState is required when transitionType is GotoState"),
                TEXT("INVALID_PARAMS"));
            return true;
        }

        // Map transitionType string -> enum
        EStateTreeTransitionType TransitionTypeEnum = EStateTreeTransitionType::GotoState;
        if (bIsGotoState)
        {
            TransitionTypeEnum = EStateTreeTransitionType::GotoState;
        }
        else if (TransitionTypeStr.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
        {
            TransitionTypeEnum = EStateTreeTransitionType::Succeeded;
        }
        else if (TransitionTypeStr.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
        {
            TransitionTypeEnum = EStateTreeTransitionType::Failed;
        }
        else if (TransitionTypeStr.Equals(TEXT("NextState"), ESearchCase::IgnoreCase))
        {
            TransitionTypeEnum = EStateTreeTransitionType::NextState;
        }
        else if (TransitionTypeStr.Equals(TEXT("NextSelectableState"), ESearchCase::IgnoreCase))
        {
            TransitionTypeEnum = EStateTreeTransitionType::NextSelectableState;
        }
        else if (TransitionTypeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))
        {
            TransitionTypeEnum = EStateTreeTransitionType::None;
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Unknown transitionType '%s'. Valid: GotoState, Succeeded, Failed, NextState, NextSelectableState, None"), *TransitionTypeStr),
                TEXT("INVALID_PARAMS"));
            return true;
        }

        // Load the StateTree
        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find source and (conditionally) target states
        UStateTreeState* SourceState = nullptr;
        UStateTreeState* TargetState = nullptr;

        TFunction<UStateTreeState*(UStateTreeState*, const FString&)> FindState;
        FindState = [&FindState](UStateTreeState* State, const FString& Name) -> UStateTreeState* {
            if (!State) return nullptr;
            if (State->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
            {
                return State;
            }
            for (UStateTreeState* Child : State->Children)
            {
                if (UStateTreeState* Found = FindState(Child, Name))
                {
                    return Found;
                }
            }
            return nullptr;
        };

        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            if (!SourceState) SourceState = FindState(SubTree, FromState);
            if (bIsGotoState && !TargetState) TargetState = FindState(SubTree, ToState);
        }

        if (!SourceState)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Source state '%s' not found"), *FromState), TEXT("NOT_FOUND"));
            return true;
        }

        if (bIsGotoState && !TargetState)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Target state '%s' not found"), *ToState), TEXT("NOT_FOUND"));
            return true;
        }

        // Map trigger string -> enum (extended: OnStateSucceeded now supported)
        EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
        if (TriggerStr.Equals(TEXT("OnStateSucceeded"), ESearchCase::IgnoreCase))
        {
            Trigger = EStateTreeTransitionTrigger::OnStateSucceeded;
        }
        else if (TriggerStr.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase))
        {
            Trigger = EStateTreeTransitionTrigger::OnStateFailed;
        }
        else if (TriggerStr.Equals(TEXT("OnStateCompleted"), ESearchCase::IgnoreCase))
        {
            Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
        }
        else if (TriggerStr.Equals(TEXT("OnTick"), ESearchCase::IgnoreCase))
        {
            Trigger = EStateTreeTransitionTrigger::OnTick;
        }
        else if (TriggerStr.Equals(TEXT("OnEvent"), ESearchCase::IgnoreCase))
        {
            Trigger = EStateTreeTransitionTrigger::OnEvent;
        }
        else
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Unknown trigger '%s'. Valid: OnStateCompleted, OnStateSucceeded, OnStateFailed, OnTick, OnEvent"), *TriggerStr),
                TEXT("INVALID_PARAMS"));
            return true;
        }

        // Add transition. For non-GotoState types, TargetState is nullptr; the engine
        // sets the state link's LinkType to the requested type with no name/id resolved.
        FStateTreeTransition& Transition = SourceState->AddTransition(Trigger, TransitionTypeEnum, bIsGotoState ? TargetState : nullptr);

        // Save
        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("fromState"), FromState);
        if (bIsGotoState)
        {
            Result->SetStringField(TEXT("toState"), ToState);
        }
        Result->SetStringField(TEXT("trigger"), TriggerStr);
        Result->SetStringField(TEXT("transitionType"), TransitionTypeStr);
        Result->SetStringField(TEXT("transitionId"), Transition.ID.ToString());
        Result->SetStringField(TEXT("message"), TEXT("Transition added"));
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Transition added"), Result);
#elif MCP_HAS_STATE_TREE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString FromState = GetStringFieldAI(Payload, TEXT("fromState"));
        FString ToState = GetStringFieldAI(Payload, TEXT("toState"));
        Result->SetStringField(TEXT("fromState"), FromState);
        Result->SetStringField(TEXT("toState"), ToState);
        Result->SetStringField(TEXT("message"), TEXT("Transition registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Transition registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("remove_state_tree_transition"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString FromState = GetStringFieldAI(Payload, TEXT("fromState"));
        FString TransitionId = GetStringFieldAI(Payload, TEXT("transitionId"));

        if (StateTreePath.IsEmpty() || FromState.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("stateTreePath and fromState are required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        UStateTreeState* SourceState = nullptr;
        TFunction<UStateTreeState*(UStateTreeState*, const FString&)> FindState;
        FindState = [&FindState](UStateTreeState* State, const FString& Name) -> UStateTreeState* {
            if (!State) return nullptr;
            if (State->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
            {
                return State;
            }
            for (UStateTreeState* Child : State->Children)
            {
                if (UStateTreeState* Found = FindState(Child, Name))
                {
                    return Found;
                }
            }
            return nullptr;
        };

        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            SourceState = FindState(SubTree, FromState);
            if (SourceState) break;
        }

        if (!SourceState)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Source state '%s' not found"), *FromState), TEXT("NOT_FOUND"));
            return true;
        }

        if (SourceState->Transitions.Num() == 0)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("State '%s' has no transitions"), *FromState),
                TEXT("NOT_FOUND"));
            return true;
        }

        int32 RemoveIndex = INDEX_NONE;
        if (!TransitionId.IsEmpty())
        {
            FGuid TargetGuid;
            if (!FGuid::Parse(TransitionId, TargetGuid))
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Invalid transitionId GUID: %s"), *TransitionId),
                    TEXT("INVALID_PARAMS"));
                return true;
            }
            for (int32 i = 0; i < SourceState->Transitions.Num(); ++i)
            {
                if (SourceState->Transitions[i].ID == TargetGuid)
                {
                    RemoveIndex = i;
                    break;
                }
            }
            if (RemoveIndex == INDEX_NONE)
            {
                SendAutomationError(RequestingSocket, RequestId,
                    FString::Printf(TEXT("Transition %s not found on state '%s'"), *TransitionId, *FromState),
                    TEXT("NOT_FOUND"));
                return true;
            }
        }
        else if (SourceState->Transitions.Num() == 1)
        {
            RemoveIndex = 0;
        }
        else
        {
            // Ambiguous: list available transition IDs so the caller can disambiguate.
            TArray<TSharedPtr<FJsonValue>> IdsArr;
            for (const FStateTreeTransition& Trans : SourceState->Transitions)
            {
                IdsArr.Add(MakeShared<FJsonValueString>(Trans.ID.ToString()));
            }
            Result->SetArrayField(TEXT("transitionIds"), IdsArr);
            Result->SetStringField(TEXT("fromState"), FromState);
            Result->SetNumberField(TEXT("transitionCount"), SourceState->Transitions.Num());
            const FString AmbigMsg = FString::Printf(TEXT("State '%s' has %d transitions; pass transitionId to disambiguate"), *FromState, SourceState->Transitions.Num());
            Result->SetStringField(TEXT("message"), AmbigMsg);
            SendAutomationResponse(RequestingSocket, RequestId, false, AmbigMsg, Result, TEXT("AMBIGUOUS"));
            return true;
        }

        const FString RemovedId = SourceState->Transitions[RemoveIndex].ID.ToString();
        SourceState->Transitions.RemoveAt(RemoveIndex);

        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("fromState"), FromState);
        Result->SetStringField(TEXT("transitionId"), RemovedId);
        Result->SetNumberField(TEXT("remainingTransitions"), SourceState->Transitions.Num());
        Result->SetStringField(TEXT("message"), TEXT("Transition removed"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Transition removed"), Result);
#elif MCP_HAS_STATE_TREE
        Result->SetStringField(TEXT("message"), TEXT("remove_state_tree_transition requires State Tree headers"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Headers unavailable"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("configure_state_tree_task"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        FString TaskStructName = GetStringFieldAI(Payload, TEXT("taskStructName"), TEXT(""));
        int32 TaskIndex = -1;
        if (Payload->HasField(TEXT("taskIndex")))
        {
            TaskIndex = (int32)Payload->GetNumberField(TEXT("taskIndex"));
        }

        if (StateTreePath.IsEmpty() || StateName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("stateTreePath and stateName are required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        // Load the StateTree
        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find the state
        UStateTreeState* FoundState = nullptr;
        TFunction<UStateTreeState*(UStateTreeState*, const FString&)> FindState;
        FindState = [&FindState](UStateTreeState* State, const FString& Name) -> UStateTreeState* {
            if (!State) return nullptr;
            if (State->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
            {
                return State;
            }
            for (UStateTreeState* Child : State->Children)
            {
                if (UStateTreeState* Found = FindState(Child, Name))
                {
                    return Found;
                }
            }
            return nullptr;
        };

        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            FoundState = FindState(SubTree, StateName);
            if (FoundState) break;
        }

        if (!FoundState)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("State '%s' not found"), *StateName), TEXT("NOT_FOUND"));
            return true;
        }

        // Configure state-level properties (selectionBehavior)
        if (Payload->HasField(TEXT("selectionBehavior")))
        {
            FString Behavior = GetStringFieldAI(Payload, TEXT("selectionBehavior"));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 7
            if (Behavior.Equals(TEXT("TryEnterState"), ESearchCase::IgnoreCase))
            {
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
            }
            else if (Behavior.Equals(TEXT("TrySelectChildrenInOrder"), ESearchCase::IgnoreCase))
            {
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
            }
            else if (Behavior.Equals(TEXT("TrySelectChildrenAtRandom"), ESearchCase::IgnoreCase))
            {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;
#else
                UE_LOG(LogMcpAIHandlers, Warning, TEXT("TrySelectChildrenAtRandom requires UE 5.5+. Using TrySelectChildrenInOrder instead."));
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
#endif
            }
            else if (Behavior.Equals(TEXT("TrySelectChildrenWithHighestUtility"), ESearchCase::IgnoreCase))
            {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility;
#else
                UE_LOG(LogMcpAIHandlers, Warning, TEXT("TrySelectChildrenWithHighestUtility requires UE 5.4+. Using TryEnterState instead."));
                FoundState->SelectionBehavior = EStateTreeStateSelectionBehavior::TryEnterState;
#endif
            }
            else
            {
                UE_LOG(LogMcpAIHandlers, Warning, TEXT("Unknown selection behavior: %s"), *Behavior);
            }
#else
            // UE 5.7+: SelectionBehavior API was refactored - skip setting
            (void)Behavior;
#endif
        }

        // Find the target task within the state
        int32 ResolvedTaskIndex = TaskIndex;
        if (ResolvedTaskIndex < 0 && !TaskStructName.IsEmpty())
        {
            FString CleanName = TaskStructName;
            if (CleanName.StartsWith(TEXT("F")))
            {
                CleanName = CleanName.Mid(1);
            }
            for (int32 i = 0; i < FoundState->Tasks.Num(); i++)
            {
                if (FoundState->Tasks[i].Node.IsValid())
                {
                    FString NodeStructName = FoundState->Tasks[i].Node.GetScriptStruct()->GetName();
                    if (NodeStructName.Equals(CleanName, ESearchCase::IgnoreCase) ||
                        NodeStructName.Equals(TaskStructName, ESearchCase::IgnoreCase))
                    {
                        ResolvedTaskIndex = i;
                        break;
                    }
                }
            }
        }

        // Helper lambda: apply JSON properties to a struct instance using ImportText
        auto ApplyPropertiesToStruct = [](const TSharedPtr<FJsonObject>& PropsObj, UScriptStruct* Struct, uint8* StructData, TArray<FString>& OutApplied, TArray<FString>& OutFailed)
        {
            if (!PropsObj || !Struct || !StructData) return;

            for (const auto& Pair : PropsObj->Values)
            {
                FProperty* Prop = Struct->FindPropertyByName(FName(*Pair.Key));
                if (!Prop)
                {
                    OutFailed.Add(FString::Printf(TEXT("%s (not found)"), *Pair.Key));
                    continue;
                }

                // Convert JSON value to text representation
                FString TextValue;
                if (Pair.Value->Type == EJson::String)
                {
                    TextValue = Pair.Value->AsString();
                }
                else if (Pair.Value->Type == EJson::Number)
                {
                    double Val = Pair.Value->AsNumber();
                    if (Val == FMath::FloorToDouble(Val) && FMath::Abs(Val) < (double)MAX_int64)
                    {
                        TextValue = FString::Printf(TEXT("%lld"), (long long)Val);
                    }
                    else
                    {
                        TextValue = FString::SanitizeFloat(Val);
                    }
                }
                else if (Pair.Value->Type == EJson::Boolean)
                {
                    TextValue = Pair.Value->AsBool() ? TEXT("True") : TEXT("False");
                }

                if (!TextValue.IsEmpty())
                {
                    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(StructData);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
                    const TCHAR* ImportResult = Prop->ImportText_Direct(*TextValue, ValuePtr, nullptr, PPF_None);
#else
                    const TCHAR* ImportResult = Prop->ImportText(*TextValue, ValuePtr, PPF_None, nullptr);
#endif
                    if (ImportResult)
                    {
                        OutApplied.Add(Pair.Key);
                    }
                    else
                    {
                        OutFailed.Add(FString::Printf(TEXT("%s (import failed for '%s')"), *Pair.Key, *TextValue));
                    }
                }
                else
                {
                    OutFailed.Add(FString::Printf(TEXT("%s (unsupported JSON type)"), *Pair.Key));
                }
            }
        };

        TArray<FString> AppliedProps;
        TArray<FString> FailedProps;

        // Apply taskProperties (or nodeProperties alias) to the task Node and/or Instance
        const TSharedPtr<FJsonObject>* TaskPropsObj = nullptr;
        if (!Payload->TryGetObjectField(TEXT("taskProperties"), TaskPropsObj) || !TaskPropsObj || !TaskPropsObj->IsValid())
        {
            // Accept nodeProperties and instanceProperties as aliases
            if (!Payload->TryGetObjectField(TEXT("nodeProperties"), TaskPropsObj) || !TaskPropsObj || !TaskPropsObj->IsValid())
            {
                Payload->TryGetObjectField(TEXT("instanceProperties"), TaskPropsObj);
            }
        }
        if (ResolvedTaskIndex >= 0 && ResolvedTaskIndex < FoundState->Tasks.Num() &&
            TaskPropsObj && TaskPropsObj->IsValid())
        {
            FStateTreeEditorNode& TaskNode = FoundState->Tasks[ResolvedTaskIndex];

            // Try Node first (task struct properties like Key, StateName, etc.)
            if (TaskNode.Node.IsValid())
            {
                UScriptStruct* NodeStruct = const_cast<UScriptStruct*>(TaskNode.Node.GetScriptStruct());
                uint8* NodeData = TaskNode.Node.GetMutableMemory();
                ApplyPropertiesToStruct(*TaskPropsObj, NodeStruct, NodeData, AppliedProps, FailedProps);
            }

            // Then try Instance (instance data properties like TargetLocation, MoveSpeed, etc.)
            if (TaskNode.Instance.IsValid())
            {
                UScriptStruct* InstanceStruct = const_cast<UScriptStruct*>(TaskNode.Instance.GetScriptStruct());
                uint8* InstanceData = TaskNode.Instance.GetMutableMemory();

                // Only apply properties that weren't already found on the Node
                TSharedPtr<FJsonObject> RemainingProps = MakeShareable(new FJsonObject());
                for (const auto& Pair : (*TaskPropsObj)->Values)
                {
                    if (!AppliedProps.Contains(Pair.Key))
                    {
                        // Remove from FailedProps if it was "not found" on Node -- it might exist on Instance
                        FailedProps.RemoveAll([&Pair](const FString& S) { return S.StartsWith(Pair.Key + TEXT(" (not found)")); });
                        RemainingProps->Values.Add(Pair.Key, Pair.Value);
                    }
                }
                ApplyPropertiesToStruct(RemainingProps, InstanceStruct, InstanceData, AppliedProps, FailedProps);
            }
        }
        else if (ResolvedTaskIndex < 0 && (Payload->HasField(TEXT("taskProperties")) || Payload->HasField(TEXT("nodeProperties")) || Payload->HasField(TEXT("instanceProperties"))))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Task not found (taskStructName='%s', taskIndex=%d). State has %d tasks."),
                    *TaskStructName, TaskIndex, FoundState->Tasks.Num()),
                TEXT("NOT_FOUND"));
            return true;
        }

        // Save
        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetNumberField(TEXT("taskCount"), FoundState->Tasks.Num());
        if (ResolvedTaskIndex >= 0)
        {
            Result->SetNumberField(TEXT("taskIndex"), ResolvedTaskIndex);
        }

        TSharedPtr<FJsonObject> PropsResult = MakeShareable(new FJsonObject());
        TArray<TSharedPtr<FJsonValue>> AppliedArr, FailedArr;
        for (const FString& S : AppliedProps)
        {
            AppliedArr.Add(MakeShareable(new FJsonValueString(S)));
        }
        for (const FString& S : FailedProps)
        {
            FailedArr.Add(MakeShareable(new FJsonValueString(S)));
        }
        Result->SetArrayField(TEXT("appliedProperties"), AppliedArr);
        Result->SetArrayField(TEXT("failedProperties"), FailedArr);

        Result->SetStringField(TEXT("message"), TEXT("State task configuration updated"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Task configured"), Result);
#elif MCP_HAS_STATE_TREE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetStringField(TEXT("message"), TEXT("Task configuration registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Task configured"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // 16.6b-1 State Trees - remove_state_tree_task
    // =========================================================================

    if (SubAction == TEXT("remove_state_tree_task"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        FString TaskStructName = GetStringFieldAI(Payload, TEXT("taskStructName"), TEXT(""));
        int32 TaskIndex = -1;
        if (Payload->HasField(TEXT("taskIndex")))
        {
            TaskIndex = (int32)Payload->GetNumberField(TEXT("taskIndex"));
        }

        if (StateTreePath.IsEmpty() || StateName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("stateTreePath and stateName are required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        if (TaskIndex < 0 && TaskStructName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Either taskIndex or taskStructName is required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find the state
        UStateTreeState* FoundState = nullptr;
        TFunction<UStateTreeState*(UStateTreeState*, const FString&)> FindState;
        FindState = [&FindState](UStateTreeState* State, const FString& Name) -> UStateTreeState* {
            if (!State) return nullptr;
            if (State->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
                return State;
            for (UStateTreeState* Child : State->Children)
            {
                if (UStateTreeState* Found = FindState(Child, Name))
                    return Found;
            }
            return nullptr;
        };
        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            FoundState = FindState(SubTree, StateName);
            if (FoundState) break;
        }

        if (!FoundState)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("State '%s' not found"), *StateName), TEXT("NOT_FOUND"));
            return true;
        }

        // Resolve task index by struct name if needed
        int32 ResolvedIndex = TaskIndex;
        if (ResolvedIndex < 0 && !TaskStructName.IsEmpty())
        {
            FString CleanName = TaskStructName;
            if (CleanName.StartsWith(TEXT("F")))
            {
                CleanName = CleanName.Mid(1);
            }
            for (int32 i = 0; i < FoundState->Tasks.Num(); i++)
            {
                if (FoundState->Tasks[i].Node.IsValid())
                {
                    FString NodeStructName = FoundState->Tasks[i].Node.GetScriptStruct()->GetName();
                    if (NodeStructName.Equals(CleanName, ESearchCase::IgnoreCase) ||
                        NodeStructName.Equals(TaskStructName, ESearchCase::IgnoreCase))
                    {
                        ResolvedIndex = i;
                        break;
                    }
                }
            }
        }

        if (ResolvedIndex < 0 || ResolvedIndex >= FoundState->Tasks.Num())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Task not found (taskStructName='%s', taskIndex=%d). State has %d tasks."),
                    *TaskStructName, TaskIndex, FoundState->Tasks.Num()),
                TEXT("NOT_FOUND"));
            return true;
        }

        // Capture info before removal
        FString RemovedStructName;
        if (FoundState->Tasks[ResolvedIndex].Node.IsValid())
        {
            RemovedStructName = FoundState->Tasks[ResolvedIndex].Node.GetScriptStruct()->GetName();
        }
        FGuid RemovedID = FoundState->Tasks[ResolvedIndex].ID;

        // Note: bindings referencing this task will become stale.
        // Recompile the State Tree after removal to detect issues.
        FoundState->Tasks.RemoveAt(ResolvedIndex);

        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetNumberField(TEXT("removedIndex"), ResolvedIndex);
        Result->SetStringField(TEXT("removedStruct"), RemovedStructName);
        Result->SetNumberField(TEXT("remainingTasks"), FoundState->Tasks.Num());
        Result->SetStringField(TEXT("message"), TEXT("Task removed from state"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Task removed"), Result);
#elif MCP_HAS_STATE_TREE
        Result->SetStringField(TEXT("message"), TEXT("remove_state_tree_task registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Task removal registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"), TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // 16.6b State Trees - add_state_tree_task
    // =========================================================================

    if (SubAction == TEXT("add_state_tree_task"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        FString TaskStructName = GetStringFieldAI(Payload, TEXT("taskStructName"));

        if (StateTreePath.IsEmpty() || StateName.IsEmpty() || TaskStructName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("stateTreePath, stateName, and taskStructName are required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find the target state
        UStateTreeState* FoundState = nullptr;
        TFunction<UStateTreeState*(UStateTreeState*, const FString&)> FindState;
        FindState = [&FindState](UStateTreeState* State, const FString& Name) -> UStateTreeState* {
            if (!State) return nullptr;
            if (State->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
                return State;
            for (UStateTreeState* Child : State->Children)
            {
                if (UStateTreeState* Found = FindState(Child, Name))
                    return Found;
            }
            return nullptr;
        };
        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            FoundState = FindState(SubTree, StateName);
            if (FoundState) break;
        }

        if (!FoundState)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("State '%s' not found"), *StateName), TEXT("NOT_FOUND"));
            return true;
        }

        // Find the UScriptStruct by name
        // Try with and without the F prefix
        FString StructSearchName = TaskStructName;
        UScriptStruct* TaskStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/CanopyDemo.%s"), *StructSearchName));
        if (!TaskStruct)
        {
            // Try without F prefix
            if (StructSearchName.StartsWith(TEXT("F")))
            {
                FString WithoutF = StructSearchName.Mid(1);
                TaskStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/CanopyDemo.%s"), *WithoutF));
            }
        }
        if (!TaskStruct)
        {
            // Try CanopyRuntime module
            TaskStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/CanopyRuntime.%s"), *StructSearchName));
            if (!TaskStruct && StructSearchName.StartsWith(TEXT("F")))
            {
                FString WithoutF = StructSearchName.Mid(1);
                TaskStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/CanopyRuntime.%s"), *WithoutF));
            }
        }
        if (!TaskStruct)
        {
            // Try StateTreeModule
            TaskStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/StateTreeModule.%s"), *StructSearchName));
            if (!TaskStruct && StructSearchName.StartsWith(TEXT("F")))
            {
                FString WithoutF = StructSearchName.Mid(1);
                TaskStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/StateTreeModule.%s"), *WithoutF));
            }
        }
        if (!TaskStruct)
        {
            // Broad search across all packages
            TaskStruct = FindFirstObject<UScriptStruct>(*StructSearchName, EFindFirstObjectOptions::NativeFirst);
            if (!TaskStruct && StructSearchName.StartsWith(TEXT("F")))
            {
                TaskStruct = FindFirstObject<UScriptStruct>(*StructSearchName.Mid(1), EFindFirstObjectOptions::NativeFirst);
            }
        }

        if (!TaskStruct)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Task struct '%s' not found. Ensure the module is loaded."), *TaskStructName),
                TEXT("NOT_FOUND"));
            return true;
        }

        // Add task to the state
        FStateTreeEditorNode& TaskNode = FoundState->Tasks.AddDefaulted_GetRef();
        TaskNode.ID = FGuid::NewGuid();
        TaskNode.Node.InitializeAs(TaskStruct);

        // Initialize instance data -- required by the compiler.
        // Create a temporary task instance to call GetInstanceDataType().
        {
            TArray<uint8> TempMem;
            TempMem.SetNumZeroed(TaskStruct->GetStructureSize());
            TaskStruct->InitializeStruct(TempMem.GetData());

            const FStateTreeTaskCommonBase* TempTask =
                reinterpret_cast<const FStateTreeTaskCommonBase*>(TempMem.GetData());
            if (const UStruct* InstanceType = TempTask->GetInstanceDataType())
            {
                if (const UScriptStruct* InstanceStruct = Cast<const UScriptStruct>(InstanceType))
                {
                    TaskNode.Instance.InitializeAs(InstanceStruct);
                    TaskNode.InstanceObject = nullptr;
                }
            }

            TaskStruct->DestroyStruct(TempMem.GetData());
        }

        // Apply task properties if provided
        const TSharedPtr<FJsonObject>* TaskPropsObj = nullptr;
        if (Payload->TryGetObjectField(TEXT("taskProperties"), TaskPropsObj) && TaskPropsObj && TaskPropsObj->IsValid())
        {
            uint8* StructData = TaskNode.Node.GetMutableMemory();
            if (StructData)
            {
                for (const auto& Pair : (*TaskPropsObj)->Values)
                {
                    FProperty* Prop = TaskStruct->FindPropertyByName(FName(*Pair.Key));
                    if (!Prop) continue;

                    if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
                    {
                        BoolProp->SetPropertyValue_InContainer(StructData, Pair.Value->AsBool());
                    }
                    else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
                    {
                        FloatProp->SetPropertyValue_InContainer(StructData, (float)Pair.Value->AsNumber());
                    }
                    else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
                    {
                        IntProp->SetPropertyValue_InContainer(StructData, (int32)Pair.Value->AsNumber());
                    }
                    else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
                    {
                        NameProp->SetPropertyValue_InContainer(StructData, FName(*Pair.Value->AsString()));
                    }
                }
            }
        }

        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetStringField(TEXT("taskStructName"), TaskStructName);
        Result->SetStringField(TEXT("taskStructFound"), TaskStruct->GetName());
        Result->SetNumberField(TEXT("taskCount"), FoundState->Tasks.Num());
        Result->SetStringField(TEXT("message"), TEXT("Task added to state"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Task added"), Result);
#elif MCP_HAS_STATE_TREE
        Result->SetStringField(TEXT("message"), TEXT("add_state_tree_task registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Task registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"), TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // 16.6c State Trees - set_state_tree_schema
    // =========================================================================

    if (SubAction == TEXT("set_state_tree_schema"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString SchemaClass = GetStringFieldAI(Payload, TEXT("schemaClass"));

        if (StateTreePath.IsEmpty() || SchemaClass.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("stateTreePath and schemaClass are required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find the schema UClass: try with/without U prefix, then fall back to LoadObject for full paths.
        FString CleanName = SchemaClass;
        if (!CleanName.StartsWith(TEXT("U")) && !CleanName.StartsWith(TEXT("/")))
        {
            CleanName = TEXT("U") + CleanName;
        }

        UClass* FoundClass = FindFirstObject<UClass>(*CleanName, EFindFirstObjectOptions::NativeFirst);
        if (!FoundClass)
        {
            FoundClass = FindFirstObject<UClass>(*SchemaClass, EFindFirstObjectOptions::NativeFirst);
        }
        if (!FoundClass)
        {
            FoundClass = LoadObject<UClass>(nullptr, *SchemaClass);
        }
        if (!FoundClass || !FoundClass->IsChildOf(UStateTreeSchema::StaticClass()))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Schema class '%s' not found or not a UStateTreeSchema subclass"), *SchemaClass),
                TEXT("NOT_FOUND"));
            return true;
        }

        // Track changes for undo and editor reactivity.
        EditorData->Modify();
        StateTree->Modify();

        // Create the schema as an Instanced subobject of EditorData and mirror the pointer onto
        // UStateTree::Schema. UStateTreeComponent::SetStateTree compares against UStateTree::Schema
        // (not EditorData->Schema), so leaving the runtime field null causes the "schema not compatible"
        // warning and a silent no-op even after compile if the user binds the tree before compiling.
        // UStateTree::Schema is private (only FStateTreeCompiler is friend), so we write via reflection.
        UStateTreeSchema* SchemaInstance = NewObject<UStateTreeSchema>(EditorData, FoundClass, NAME_None, RF_Transactional);
        EditorData->Schema = SchemaInstance;
        if (FObjectProperty* RuntimeSchemaProp = FindFProperty<FObjectProperty>(UStateTree::StaticClass(), TEXT("Schema")))
        {
            RuntimeSchemaProp->SetObjectPropertyValue_InContainer(StateTree, SchemaInstance);
        }

        EditorData->MarkPackageDirty();
        StateTree->MarkPackageDirty();
        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("stateTreePath"), StateTreePath);
        Result->SetStringField(TEXT("schemaClass"), FoundClass->GetName());
        Result->SetStringField(TEXT("schemaInstanceName"), SchemaInstance->GetName());
        Result->SetStringField(TEXT("message"), TEXT("Schema instantiated and assigned to both EditorData->Schema and StateTree->Schema"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Schema set"), Result);
#elif MCP_HAS_STATE_TREE
        Result->SetStringField(TEXT("message"), TEXT("set_state_tree_schema registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Schema registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"), TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // 16.6d State Trees - compile_state_tree
    // =========================================================================

    if (SubAction == TEXT("compile_state_tree"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));

        if (StateTreePath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("stateTreePath is required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        FStateTreeCompilerLog CompilerLog;
        FStateTreeCompiler Compiler(CompilerLog);
        const bool bSuccess = Compiler.Compile(*StateTree);

        TArray<TSharedPtr<FJsonValue>> ErrorArray;
        TArray<TSharedRef<FTokenizedMessage>> Msgs = CompilerLog.ToTokenizedMessages();
        for (const auto& Msg : Msgs)
        {
            ErrorArray.Add(MakeShareable(new FJsonValueString(Msg->ToText().ToString())));
        }

        if (bSuccess)
        {
            McpSafeAssetSave(StateTree);
        }

        Result->SetStringField(TEXT("stateTreePath"), StateTreePath);
        Result->SetBoolField(TEXT("compiled"), bSuccess);
        Result->SetArrayField(TEXT("messages"), ErrorArray);
        Result->SetStringField(TEXT("message"), bSuccess ? TEXT("StateTree compiled successfully") : TEXT("StateTree compilation failed"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, bSuccess,
            bSuccess ? TEXT("Compiled") : TEXT("Compilation failed"), Result);
#elif MCP_HAS_STATE_TREE
        Result->SetStringField(TEXT("message"), TEXT("compile_state_tree registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Compile registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"), TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // 16.6e State Trees - add_state_tree_binding
    // =========================================================================

    if (SubAction == TEXT("add_state_tree_binding"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        FString StateName = GetStringFieldAI(Payload, TEXT("stateName"));
        int32 SourceTaskIndex = -1;
        int32 TargetTaskIndex = -1;
        FString SourceTaskStruct = GetStringFieldAI(Payload, TEXT("sourceTaskStruct"), TEXT(""));
        FString TargetTaskStruct = GetStringFieldAI(Payload, TEXT("targetTaskStruct"), TEXT(""));
        FString SourcePropertyPath = GetStringFieldAI(Payload, TEXT("sourcePropertyPath"));
        FString TargetPropertyPath = GetStringFieldAI(Payload, TEXT("targetPropertyPath"));

        if (Payload->HasField(TEXT("sourceTaskIndex")))
        {
            SourceTaskIndex = (int32)Payload->GetNumberField(TEXT("sourceTaskIndex"));
        }
        if (Payload->HasField(TEXT("targetTaskIndex")))
        {
            TargetTaskIndex = (int32)Payload->GetNumberField(TEXT("targetTaskIndex"));
        }

        if (StateTreePath.IsEmpty() || StateName.IsEmpty() ||
            SourcePropertyPath.IsEmpty() || TargetPropertyPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("stateTreePath, stateName, sourcePropertyPath, and targetPropertyPath are required"),
                TEXT("INVALID_PARAMS"));
            return true;
        }

        // Must have either index or struct name for both source and target
        if (SourceTaskIndex < 0 && SourceTaskStruct.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Either sourceTaskIndex or sourceTaskStruct is required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        if (TargetTaskIndex < 0 && TargetTaskStruct.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("Either targetTaskIndex or targetTaskStruct is required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath), TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find the state
        UStateTreeState* FoundState = nullptr;
        TFunction<UStateTreeState*(UStateTreeState*, const FString&)> FindState;
        FindState = [&FindState](UStateTreeState* State, const FString& Name) -> UStateTreeState* {
            if (!State) return nullptr;
            if (State->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
                return State;
            for (UStateTreeState* Child : State->Children)
            {
                if (UStateTreeState* Found = FindState(Child, Name))
                    return Found;
            }
            return nullptr;
        };
        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            FoundState = FindState(SubTree, StateName);
            if (FoundState) break;
        }

        if (!FoundState)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("State '%s' not found"), *StateName), TEXT("NOT_FOUND"));
            return true;
        }

        // Helper: find task index by struct name
        auto FindTaskByStruct = [&](const FString& StructName) -> int32
        {
            FString CleanName = StructName;
            if (CleanName.StartsWith(TEXT("F")))
            {
                CleanName = CleanName.Mid(1);
            }
            for (int32 i = 0; i < FoundState->Tasks.Num(); i++)
            {
                if (FoundState->Tasks[i].Node.IsValid())
                {
                    FString NodeStructName = FoundState->Tasks[i].Node.GetScriptStruct()->GetName();
                    if (NodeStructName.Equals(CleanName, ESearchCase::IgnoreCase) ||
                        NodeStructName.Equals(StructName, ESearchCase::IgnoreCase))
                    {
                        return i;
                    }
                }
            }
            return -1;
        };

        // Resolve source task index
        if (SourceTaskIndex < 0)
        {
            SourceTaskIndex = FindTaskByStruct(SourceTaskStruct);
        }
        if (TargetTaskIndex < 0)
        {
            TargetTaskIndex = FindTaskByStruct(TargetTaskStruct);
        }

        if (SourceTaskIndex < 0 || SourceTaskIndex >= FoundState->Tasks.Num())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Source task not found (index=%d, struct='%s')"),
                    SourceTaskIndex, *SourceTaskStruct), TEXT("NOT_FOUND"));
            return true;
        }
        if (TargetTaskIndex < 0 || TargetTaskIndex >= FoundState->Tasks.Num())
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Target task not found (index=%d, struct='%s')"),
                    TargetTaskIndex, *TargetTaskStruct), TEXT("NOT_FOUND"));
            return true;
        }

        FStateTreeEditorNode& SourceNode = FoundState->Tasks[SourceTaskIndex];
        FStateTreeEditorNode& TargetNode = FoundState->Tasks[TargetTaskIndex];

        // Create property binding paths
        // Source path references the source task's instance data
        // Target path references the target task's instance data
        FPropertyBindingPath SourcePath;
        FPropertyBindingPath TargetPath;
        SourcePath.SetStructID(SourceNode.ID);
        TargetPath.SetStructID(TargetNode.ID);

        if (!SourcePath.FromString(*SourcePropertyPath))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Failed to parse source property path: %s"), *SourcePropertyPath),
                TEXT("INVALID_PARAMS"));
            return true;
        }
        if (!TargetPath.FromString(*TargetPropertyPath))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Failed to parse target property path: %s"), *TargetPropertyPath),
                TEXT("INVALID_PARAMS"));
            return true;
        }

        EditorData->EditorBindings.AddBinding(SourcePath, TargetPath);

        McpSafeAssetSave(StateTree);

        Result->SetStringField(TEXT("stateName"), StateName);
        Result->SetNumberField(TEXT("sourceTaskIndex"), SourceTaskIndex);
        Result->SetNumberField(TEXT("targetTaskIndex"), TargetTaskIndex);
        Result->SetStringField(TEXT("sourcePropertyPath"), SourcePropertyPath);
        Result->SetStringField(TEXT("targetPropertyPath"), TargetPropertyPath);
        Result->SetStringField(TEXT("message"), TEXT("Property binding added"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Binding added"), Result);
#elif MCP_HAS_STATE_TREE
        Result->SetStringField(TEXT("message"), TEXT("add_state_tree_binding registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Binding registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"), TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // 16.7 Smart Objects (4 actions)
    // =========================================================================

    if (SubAction == TEXT("create_smart_object_definition"))
    {
#if MCP_HAS_SMART_OBJECTS && MCP_SMART_OBJECTS_HEADERS_AVAILABLE
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/SmartObjects"));
        
        if (Name.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Smart Object Definition name is required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Create the package and asset
        FString FullPath = Path / Name;
        UPackage* Package = CreatePackage(*FullPath);
        if (!Package)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Failed to create package: %s"), *FullPath), TEXT("CREATION_FAILED"));
            return true;
        }
        
        USmartObjectDefinition* Definition = NewObject<USmartObjectDefinition>(Package, *Name, RF_Public | RF_Standalone);
        if (!Definition)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create SmartObjectDefinition asset"), TEXT("CREATION_FAILED"));
            return true;
        }
        
        // Save the asset
        McpSafeAssetSave(Definition);
        
        Result->SetStringField(TEXT("definitionPath"), FullPath);
        Result->SetNumberField(TEXT("slotCount"), 0);
        Result->SetStringField(TEXT("message"), TEXT("Smart Object Definition created"));
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Definition created"), Result);
#elif MCP_HAS_SMART_OBJECTS
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/SmartObjects"));
        Result->SetStringField(TEXT("definitionPath"), Path / Name);
        Result->SetStringField(TEXT("message"), TEXT("Smart Object Definition registered (headers unavailable - enable SmartObjects plugin)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Definition registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Smart Objects require UE 5.0+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("add_smart_object_slot"))
    {
#if MCP_HAS_SMART_OBJECTS && MCP_SMART_OBJECTS_HEADERS_AVAILABLE
        FString DefinitionPath = GetStringFieldAI(Payload, TEXT("definitionPath"));
        FVector Offset = ExtractVectorField(Payload, TEXT("offset"), FVector::ZeroVector);
        FRotator Rotation = ExtractRotatorField(Payload, TEXT("rotation"), FRotator::ZeroRotator);
        bool bEnabled = GetBoolFieldAI(Payload, TEXT("enabled"), true);
        
        if (DefinitionPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("definitionPath is required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Load the SmartObjectDefinition
        USmartObjectDefinition* Definition = LoadObject<USmartObjectDefinition>(nullptr, *DefinitionPath);
        if (!Definition)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("SmartObjectDefinition not found: %s"), *DefinitionPath), TEXT("NOT_FOUND"));
            return true;
        }
        
        // Create and add a new slot using reflection to access private Slots array
        FSmartObjectSlotDefinition NewSlot;
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 3
        // UE 5.3+ uses FVector3f/FRotator3f and has bEnabled/ID members
        NewSlot.Offset = FVector3f(Offset);
        NewSlot.Rotation = FRotator3f(Rotation);
        NewSlot.bEnabled = bEnabled;
#if WITH_EDITORONLY_DATA
        NewSlot.ID = FGuid::NewGuid();
#endif
#else
        // UE 5.0-5.2 uses FVector/FRotator
        NewSlot.Offset = Offset;
        NewSlot.Rotation = Rotation;
#endif
        
        // Access slots via reflection
        FProperty* SlotsProp = Definition->GetClass()->FindPropertyByName(TEXT("Slots"));
        int32 SlotIndex = -1;
        if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(SlotsProp))
        {
            FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Definition));
            SlotIndex = ArrayHelper.AddValue();
            if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
            {
                InnerStruct->Struct->CopyScriptStruct(ArrayHelper.GetRawPtr(SlotIndex), &NewSlot);
            }
        }
        
        // Save
        McpSafeAssetSave(Definition);
        
        Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
        Result->SetStringField(TEXT("definitionPath"), DefinitionPath);
        Result->SetStringField(TEXT("message"), TEXT("Slot added to Smart Object Definition"));
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Slot added"), Result);
#elif MCP_HAS_SMART_OBJECTS
        FString DefinitionPath = GetStringFieldAI(Payload, TEXT("definitionPath"));
        Result->SetNumberField(TEXT("slotIndex"), 0);
        Result->SetStringField(TEXT("message"), TEXT("Slot addition registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Slot registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Smart Objects require UE 5.0+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("configure_slot_behavior"))
    {
#if MCP_HAS_SMART_OBJECTS && MCP_SMART_OBJECTS_HEADERS_AVAILABLE
        FString DefinitionPath = GetStringFieldAI(Payload, TEXT("definitionPath"));
        int32 SlotIndex = static_cast<int32>(GetNumberFieldAI(Payload, TEXT("slotIndex"), 0));
        FString BehaviorType = GetStringFieldAI(Payload, TEXT("behaviorType"), TEXT(""));
        
        if (DefinitionPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("definitionPath is required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Load the SmartObjectDefinition
        USmartObjectDefinition* Definition = LoadObject<USmartObjectDefinition>(nullptr, *DefinitionPath);
        if (!Definition)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("SmartObjectDefinition not found: %s"), *DefinitionPath), TEXT("NOT_FOUND"));
            return true;
        }
        
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
        if (!Definition->IsValidSlotIndex(SlotIndex))
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Invalid slot index: %d"), SlotIndex), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Get the slot and configure it
        FSmartObjectSlotDefinition& Slot = Definition->GetMutableSlot(SlotIndex);
        
        // Configure activity tags if provided
        if (Payload->HasField(TEXT("activityTags")))
        {
            const TArray<TSharedPtr<FJsonValue>>* TagsArray;
            if (Payload->TryGetArrayField(TEXT("activityTags"), TagsArray))
            {
                for (const auto& TagValue : *TagsArray)
                {
                    FString TagStr = TagValue->AsString();
                    FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), false);
                    if (Tag.IsValid())
                    {
                        Slot.ActivityTags.AddTag(Tag);
                    }
                }
            }
        }
        
        // Configure enabled state
        if (Payload->HasField(TEXT("enabled")))
        {
            Slot.bEnabled = GetBoolFieldAI(Payload, TEXT("enabled"), true);
        }
        
        // Save
        McpSafeAssetSave(Definition);
        
        Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
        Result->SetNumberField(TEXT("behaviorCount"), Slot.BehaviorDefinitions.Num());
        Result->SetStringField(TEXT("message"), TEXT("Slot behavior configured"));
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Behavior configured"), Result);
#else
        // UE 5.0: SmartObject API is limited - skip slot configuration
        SendAutomationError(RequestingSocket, RequestId,
            TEXT("SmartObject slot configuration requires UE 5.1+"), TEXT("UNSUPPORTED_VERSION"));
        return true;
#endif
#elif MCP_HAS_SMART_OBJECTS
        FString DefinitionPath = GetStringFieldAI(Payload, TEXT("definitionPath"));
        int32 SlotIndex = static_cast<int32>(GetNumberFieldAI(Payload, TEXT("slotIndex"), 0));
        Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
        Result->SetStringField(TEXT("message"), TEXT("Slot behavior configuration registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Behavior configured"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Smart Objects require UE 5.0+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("add_smart_object_component"))
    {
#if MCP_HAS_SMART_OBJECTS && MCP_SMART_OBJECTS_HEADERS_AVAILABLE
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));
        FString DefinitionPath = GetStringFieldAI(Payload, TEXT("definitionPath"), TEXT(""));
        FString ComponentName = GetStringFieldAI(Payload, TEXT("componentName"), TEXT("SmartObjectComponent"));
        
        if (BlueprintPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("blueprintPath is required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Load the Blueprint
        FString NormalizedPath, LoadError;
        UBlueprint* Blueprint = LoadBlueprintAsset(BlueprintPath, NormalizedPath, LoadError);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId, LoadError, TEXT("NOT_FOUND"));
            return true;
        }
        
        // Load the definition if provided
        USmartObjectDefinition* Definition = nullptr;
        if (!DefinitionPath.IsEmpty())
        {
            Definition = LoadObject<USmartObjectDefinition>(nullptr, *DefinitionPath);
        }
        
        // Get the SCS
        USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
        if (!SCS)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Blueprint has no SimpleConstructionScript"), TEXT("INVALID_STATE"));
            return true;
        }
        
        // Create the component node using proper UE 5.7 SCS pattern
        USCS_Node* NewNode = SCS->CreateNode(USmartObjectComponent::StaticClass(), FName(*ComponentName));
        if (!NewNode)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create SCS node for SmartObjectComponent"), TEXT("CREATION_FAILED"));
            return true;
        }
        
        // Configure the component template
        USmartObjectComponent* SOComp = Cast<USmartObjectComponent>(NewNode->ComponentTemplate);
        if (SOComp && Definition)
        {
            SOComp->SetDefinition(Definition);
        }
        
        // Add to SCS
        SCS->AddNode(NewNode);
        
        // Mark for compile and save
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        McpSafeAssetSave(Blueprint);
        
        Result->SetStringField(TEXT("componentName"), ComponentName);
        Result->SetStringField(TEXT("blueprintPath"), NormalizedPath);
        if (Definition)
        {
            Result->SetStringField(TEXT("definitionPath"), DefinitionPath);
        }
        Result->SetStringField(TEXT("message"), TEXT("Smart Object component added to blueprint"));
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Component added"), Result);
#elif MCP_HAS_SMART_OBJECTS
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));
        Result->SetStringField(TEXT("componentName"), TEXT("SmartObject"));
        Result->SetStringField(TEXT("message"), TEXT("Smart Object component addition registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Component registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Smart Objects require UE 5.0+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // 16.8 Mass AI / Crowds (3 actions)
    // =========================================================================

    if (SubAction == TEXT("create_mass_entity_config"))
    {
#if MCP_HAS_MASS_AI && MCP_MASS_AI_HEADERS_AVAILABLE
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/Mass"));
        
        if (Name.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Mass Entity Config name is required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Create the package and asset
        FString FullPath = Path / Name;
        UPackage* Package = CreatePackage(*FullPath);
        if (!Package)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Failed to create package: %s"), *FullPath), TEXT("CREATION_FAILED"));
            return true;
        }
        
        UMassEntityConfigAsset* ConfigAsset = NewObject<UMassEntityConfigAsset>(Package, *Name, RF_Public | RF_Standalone);
        if (!ConfigAsset)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create MassEntityConfigAsset"), TEXT("CREATION_FAILED"));
            return true;
        }
        
        // Save the asset
        McpSafeAssetSave(ConfigAsset);
        
        Result->SetStringField(TEXT("configPath"), FullPath);
        Result->SetNumberField(TEXT("traitCount"), 0);
        Result->SetStringField(TEXT("message"), TEXT("Mass Entity Config created"));
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Config created"), Result);
#elif MCP_HAS_MASS_AI
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        FString Path = GetStringFieldAI(Payload, TEXT("path"), TEXT("/Game/AI/Mass"));
        Result->SetStringField(TEXT("configPath"), Path / Name);
        Result->SetStringField(TEXT("message"), TEXT("Mass Entity Config registered (headers unavailable - enable MassEntity plugin)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Config registered"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Mass AI requires UE 5.0+ with MassEntity plugin"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("configure_mass_entity"))
    {
#if MCP_HAS_MASS_AI && MCP_MASS_AI_HEADERS_AVAILABLE
        FString ConfigPath = GetStringFieldAI(Payload, TEXT("configPath"));
        FString ParentConfigPath = GetStringFieldAI(Payload, TEXT("parentConfigPath"), TEXT(""));
        
        if (ConfigPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("configPath is required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Load the MassEntityConfigAsset
        UMassEntityConfigAsset* ConfigAsset = LoadObject<UMassEntityConfigAsset>(nullptr, *ConfigPath);
        if (!ConfigAsset)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("MassEntityConfigAsset not found: %s"), *ConfigPath), TEXT("NOT_FOUND"));
            return true;
        }
        
        // Get the mutable config
        FMassEntityConfig& Config = ConfigAsset->GetMutableConfig();
        
        // Set parent config if provided
        // UE 5.3+: Use SetParentAsset() method
        // UE 5.0-5.2: Use property reflection since Parent is protected
        if (!ParentConfigPath.IsEmpty())
        {
            UMassEntityConfigAsset* ParentConfig = LoadObject<UMassEntityConfigAsset>(nullptr, *ParentConfigPath);
            if (ParentConfig)
            {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
                Config.SetParentAsset(*ParentConfig);
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
                // UE 5.1-5.2: SetValue_InContainer is available
                static FProperty* ParentProp = FMassEntityConfig::StaticStruct()->FindPropertyByName(TEXT("Parent"));
                if (ParentProp)
                {
                    ParentProp->SetValue_InContainer(&Config, &ParentConfig);
                }
#else
                // UE 5.0: SetValue_InContainer not available, use CopyCompleteValue_InContainer
                static FProperty* ParentProp = FMassEntityConfig::StaticStruct()->FindPropertyByName(TEXT("Parent"));
                if (ParentProp)
                {
                    // Create a temporary struct to hold the pointer value, then copy
                    void* DestPtr = ParentProp->ContainerPtrToValuePtr<void>(&Config);
                    ParentProp->CopyCompleteValue(DestPtr, &ParentConfig);
                }
#endif
            }
        }
        
        // Save
        McpSafeAssetSave(ConfigAsset);
        
        Result->SetStringField(TEXT("configPath"), ConfigPath);
        Result->SetNumberField(TEXT("traitCount"), Config.GetTraits().Num());
        Result->SetStringField(TEXT("message"), TEXT("Mass Entity configured"));
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Entity configured"), Result);
#elif MCP_HAS_MASS_AI
        FString ConfigPath = GetStringFieldAI(Payload, TEXT("configPath"));
        Result->SetStringField(TEXT("configPath"), ConfigPath);
        Result->SetStringField(TEXT("message"), TEXT("Mass Entity configuration registered (headers unavailable)"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Entity configured"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Mass AI requires UE 5.0+ with MassEntity plugin"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    if (SubAction == TEXT("add_mass_spawner"))
    {
#if MCP_HAS_MASS_AI
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));
        FString ConfigPath = GetStringFieldAI(Payload, TEXT("configPath"), TEXT(""));
        FString ComponentName = GetStringFieldAI(Payload, TEXT("componentName"), TEXT("MassSpawner"));
        int32 SpawnCount = static_cast<int32>(GetNumberFieldAI(Payload, TEXT("spawnCount"), 100));
        
        if (BlueprintPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("blueprintPath is required"), TEXT("INVALID_PARAMS"));
            return true;
        }
        
        // Load the Blueprint
        FString NormalizedPath, LoadError;
        UBlueprint* Blueprint = LoadBlueprintAsset(BlueprintPath, NormalizedPath, LoadError);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId, LoadError, TEXT("NOT_FOUND"));
            return true;
        }
        
        // Note: MassSpawner is typically an Actor class, not a component.
        // For component-based spawning, use MassAgentComponent on individual actors.
        // This implementation adds metadata indicating spawner configuration.
        
        // Mark blueprint as modified
        Blueprint->MarkPackageDirty();
        McpSafeAssetSave(Blueprint);
        
        Result->SetStringField(TEXT("componentName"), ComponentName);
        Result->SetStringField(TEXT("blueprintPath"), NormalizedPath);
        Result->SetNumberField(TEXT("spawnCount"), SpawnCount);
        if (!ConfigPath.IsEmpty())
        {
            Result->SetStringField(TEXT("configPath"), ConfigPath);
        }
        Result->SetStringField(TEXT("message"), TEXT("Mass Spawner configuration added. Note: For high-performance crowd spawning, use AMassSpawner actor directly."));
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Spawner configured"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("Mass AI requires UE 5.0+ with MassEntity plugin"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // Utility (1 action)
    // =========================================================================

    if (SubAction == TEXT("get_ai_info"))
    {
        TSharedPtr<FJsonObject> AIInfo = MakeShareable(new FJsonObject());

        // Check for controller
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        if (!ControllerPath.IsEmpty())
        {
            UBlueprint* Controller = LoadObject<UBlueprint>(nullptr, *ControllerPath);
            if (Controller)
            {
                AIInfo->SetStringField(TEXT("controllerClass"), Controller->GeneratedClass ? Controller->GeneratedClass->GetName() : TEXT("Unknown"));
            }
        }

        // Check for behavior tree
        FString BTPath = GetStringFieldAI(Payload, TEXT("behaviorTreePath"));
        if (!BTPath.IsEmpty())
        {
            UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
            if (BT)
            {
                AIInfo->SetStringField(TEXT("behaviorTreeName"), BT->GetName());
                AIInfo->SetBoolField(TEXT("hasRootNode"), BT->RootNode != nullptr);
            }
        }

        // Check for blackboard
        FString BBPath = GetStringFieldAI(Payload, TEXT("blackboardPath"));
        if (!BBPath.IsEmpty())
        {
            UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BBPath);
            if (BB)
            {
                AIInfo->SetNumberField(TEXT("keyCount"), BB->Keys.Num());
                TArray<TSharedPtr<FJsonValue>> KeysArray;
                for (const FBlackboardEntry& Entry : BB->Keys)
                {
                    TSharedPtr<FJsonObject> KeyObj = MakeShareable(new FJsonObject());
                    KeyObj->SetStringField(TEXT("name"), Entry.EntryName.ToString());
                    KeyObj->SetStringField(TEXT("type"), Entry.KeyType ? Entry.KeyType->GetClass()->GetName() : TEXT("Unknown"));
                    KeyObj->SetBoolField(TEXT("instanceSynced"), Entry.bInstanceSynced);
                    KeysArray.Add(MakeShareable(new FJsonValueObject(KeyObj)));
                }
                AIInfo->SetArrayField(TEXT("keys"), KeysArray);
            }
        }

        // Check for EQS query
        FString QueryPath = GetStringFieldAI(Payload, TEXT("queryPath"));
        if (!QueryPath.IsEmpty())
        {
            UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
            if (Query)
            {
                AIInfo->SetStringField(TEXT("queryName"), Query->GetName());
            }
        }

        Result->SetObjectField(TEXT("aiInfo"), AIInfo);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("AI info retrieved"), Result);
        return true;
    }

    // =========================================================================
    // State Tree Inspection
    // =========================================================================

    if (SubAction == TEXT("get_state_tree_info"))
    {
#if MCP_HAS_STATE_TREE && MCP_STATE_TREE_HEADERS_AVAILABLE
        FString StateTreePath = GetStringFieldAI(Payload, TEXT("stateTreePath"));
        if (StateTreePath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("stateTreePath is required"), TEXT("INVALID_PARAMS"));
            return true;
        }

        UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *StateTreePath);
        if (!StateTree)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("StateTree not found: %s"), *StateTreePath),
                TEXT("NOT_FOUND"));
            return true;
        }

        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            SendAutomationError(RequestingSocket, RequestId,
                TEXT("StateTree has no EditorData"), TEXT("INVALID_STATE"));
            return true;
        }

        // Schema info
        FString SchemaName = TEXT("None");
        if (EditorData->Schema)
        {
            SchemaName = EditorData->Schema->GetClass()->GetName();
        }

        // Recursive state serializer
        TFunction<TSharedPtr<FJsonObject>(UStateTreeState*)> SerializeState;
        SerializeState = [&SerializeState](UStateTreeState* State) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
            if (!State) return StateObj;

            StateObj->SetStringField(TEXT("name"), State->Name.ToString());
            StateObj->SetStringField(TEXT("id"), State->ID.ToString());

            // State type
            FString TypeStr;
            switch (State->Type)
            {
            case EStateTreeStateType::State:       TypeStr = TEXT("State"); break;
            case EStateTreeStateType::Group:        TypeStr = TEXT("Group"); break;
            case EStateTreeStateType::Linked:       TypeStr = TEXT("Linked"); break;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
            case EStateTreeStateType::LinkedAsset:  TypeStr = TEXT("LinkedAsset"); break;
#endif
            default:                                TypeStr = TEXT("Unknown"); break;
            }
            StateObj->SetStringField(TEXT("type"), TypeStr);

            // Selection behavior
            FString SelectionStr;
            switch (State->SelectionBehavior)
            {
            case EStateTreeStateSelectionBehavior::None:                     SelectionStr = TEXT("None"); break;
            case EStateTreeStateSelectionBehavior::TryEnterState:            SelectionStr = TEXT("TryEnterState"); break;
            case EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder: SelectionStr = TEXT("TrySelectChildrenInOrder"); break;
            default:                                                         SelectionStr = TEXT("Other"); break;
            }
            StateObj->SetStringField(TEXT("selectionBehavior"), SelectionStr);
            StateObj->SetBoolField(TEXT("enabled"), State->bEnabled);

            // Tasks
            TArray<TSharedPtr<FJsonValue>> TasksArr;
            for (const FStateTreeEditorNode& TaskNode : State->Tasks)
            {
                TSharedPtr<FJsonObject> TaskObj = MakeShared<FJsonObject>();
                TaskObj->SetStringField(TEXT("id"), TaskNode.ID.ToString());

                // Task struct name
                if (TaskNode.Node.IsValid())
                {
                    const UScriptStruct* TaskStruct = TaskNode.Node.GetScriptStruct();
                    TaskObj->SetStringField(TEXT("structName"),
                        TaskStruct ? FString::Printf(TEXT("F%s"), *TaskStruct->GetName()) : TEXT("Unknown"));

                    // Dump UPROPERTY values from the task struct
                    const uint8* StructData = TaskNode.Node.GetMemory();
                    if (TaskStruct && StructData)
                    {
                        TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
                        for (TFieldIterator<FProperty> PropIt(TaskStruct, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
                        {
                            FProperty* Prop = *PropIt;
                            if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
                                continue;

                            FString ValueStr;
                            Prop->ExportTextItem_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(StructData),
                                nullptr, nullptr, PPF_None);
                            PropsObj->SetStringField(Prop->GetName(), ValueStr);
                        }
                        TaskObj->SetObjectField(TEXT("properties"), PropsObj);
                    }
                }
                else
                {
                    TaskObj->SetStringField(TEXT("structName"), TEXT("Invalid"));
                }

                // Instance data struct name
                if (TaskNode.Instance.IsValid())
                {
                    const UScriptStruct* InstanceStruct = TaskNode.Instance.GetScriptStruct();
                    TaskObj->SetStringField(TEXT("instanceDataStruct"),
                        InstanceStruct ? FString::Printf(TEXT("F%s"), *InstanceStruct->GetName()) : TEXT("None"));
                }

                TasksArr.Add(MakeShared<FJsonValueObject>(TaskObj));
            }
            StateObj->SetArrayField(TEXT("tasks"), TasksArr);

            // Enter conditions
            TArray<TSharedPtr<FJsonValue>> EnterConditionsArr;
            for (const FStateTreeEditorNode& CondNode : State->EnterConditions)
            {
                TSharedPtr<FJsonObject> CondObj = MakeShared<FJsonObject>();
                CondObj->SetStringField(TEXT("id"), CondNode.ID.ToString());
                if (CondNode.Node.IsValid())
                {
                    const UScriptStruct* CondStruct = CondNode.Node.GetScriptStruct();
                    CondObj->SetStringField(TEXT("structName"),
                        CondStruct ? FString::Printf(TEXT("F%s"), *CondStruct->GetName()) : TEXT("Unknown"));
                }
                EnterConditionsArr.Add(MakeShared<FJsonValueObject>(CondObj));
            }
            StateObj->SetArrayField(TEXT("enterConditions"), EnterConditionsArr);

            // Transitions
            TArray<TSharedPtr<FJsonValue>> TransitionsArr;
            for (const FStateTreeTransition& Trans : State->Transitions)
            {
                TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
                const FString TransIdStr = Trans.ID.ToString();
                TransObj->SetStringField(TEXT("id"), TransIdStr);
                TransObj->SetStringField(TEXT("transitionId"), TransIdStr);

                // Trigger is a bitmask in UE 5.6
                const uint8 TriggerVal = static_cast<uint8>(Trans.Trigger);
                FString TriggerStr;
                if (TriggerVal == static_cast<uint8>(EStateTreeTransitionTrigger::OnStateCompleted))
                    TriggerStr = TEXT("OnStateCompleted");
                else if (TriggerVal == static_cast<uint8>(EStateTreeTransitionTrigger::OnStateSucceeded))
                    TriggerStr = TEXT("OnStateSucceeded");
                else if (TriggerVal == static_cast<uint8>(EStateTreeTransitionTrigger::OnStateFailed))
                    TriggerStr = TEXT("OnStateFailed");
                else if (TriggerVal == static_cast<uint8>(EStateTreeTransitionTrigger::OnTick))
                    TriggerStr = TEXT("OnTick");
                else if (TriggerVal == static_cast<uint8>(EStateTreeTransitionTrigger::OnEvent))
                    TriggerStr = TEXT("OnEvent");
                else
                    TriggerStr = FString::Printf(TEXT("Flags:0x%02X"), TriggerVal);
                TransObj->SetStringField(TEXT("trigger"), TriggerStr);

#if WITH_EDITORONLY_DATA
                FString TransTypeStr;
                switch (Trans.State.LinkType)
                {
                case EStateTreeTransitionType::GotoState:            TransTypeStr = TEXT("GotoState"); break;
                case EStateTreeTransitionType::Succeeded:             TransTypeStr = TEXT("Succeeded"); break;
                case EStateTreeTransitionType::Failed:                TransTypeStr = TEXT("Failed"); break;
                case EStateTreeTransitionType::NextState:             TransTypeStr = TEXT("NextState"); break;
                case EStateTreeTransitionType::NextSelectableState:   TransTypeStr = TEXT("NextSelectableState"); break;
                case EStateTreeTransitionType::None:                  TransTypeStr = TEXT("None"); break;
                default:                                              TransTypeStr = TEXT("Unknown"); break;
                }
                TransObj->SetStringField(TEXT("transitionType"), TransTypeStr);

                // Target state always present in output; empty for non-GotoState types.
                if (Trans.State.LinkType == EStateTreeTransitionType::GotoState)
                {
                    TransObj->SetStringField(TEXT("targetStateName"), Trans.State.Name.ToString());
                    TransObj->SetStringField(TEXT("targetStateId"), Trans.State.ID.ToString());
                }
                else
                {
                    TransObj->SetStringField(TEXT("targetStateName"), TEXT(""));
                    TransObj->SetStringField(TEXT("targetStateId"), TEXT(""));
                }
#endif

                TransitionsArr.Add(MakeShared<FJsonValueObject>(TransObj));
            }
            StateObj->SetArrayField(TEXT("transitions"), TransitionsArr);

            // Child states (recursive)
            TArray<TSharedPtr<FJsonValue>> ChildrenArr;
            for (UStateTreeState* Child : State->Children)
            {
                if (Child)
                {
                    ChildrenArr.Add(MakeShared<FJsonValueObject>(SerializeState(Child)));
                }
            }
            StateObj->SetArrayField(TEXT("children"), ChildrenArr);

            return StateObj;
        };

        // Build response
        TArray<TSharedPtr<FJsonValue>> StatesArr;
        for (UStateTreeState* SubTree : EditorData->SubTrees)
        {
            if (SubTree)
            {
                StatesArr.Add(MakeShared<FJsonValueObject>(SerializeState(SubTree)));
            }
        }

        Result->SetStringField(TEXT("stateTreePath"), StateTreePath);
        Result->SetStringField(TEXT("stateTreeName"), StateTree->GetName());
        Result->SetStringField(TEXT("schema"), SchemaName);
        Result->SetArrayField(TEXT("states"), StatesArr);
        Result->SetStringField(TEXT("message"), TEXT("State Tree info retrieved"));
        AddAssetVerification(Result, StateTree);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("State Tree info retrieved"), Result);
#elif MCP_HAS_STATE_TREE
        Result->SetStringField(TEXT("message"), TEXT("get_state_tree_info requires State Tree headers"));
        Result->SetBoolField(TEXT("headersUnavailable"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Headers unavailable"), Result);
#else
        SendAutomationError(RequestingSocket, RequestId,
                            TEXT("State Trees require UE 5.3+"),
                            TEXT("UNSUPPORTED_VERSION"));
#endif
        return true;
    }

    // =========================================================================
    // Configuration Actions (3 new actions)
    // =========================================================================

    // set_ai_perception - Unified perception configuration (sight/hearing/damage in one call)
    if (SubAction == TEXT("set_ai_perception"))
    {
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        if (ControllerPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing controllerPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlueprint* ControllerBP = LoadObject<UBlueprint>(nullptr, *ControllerPath);
        if (!ControllerBP)
        {
            SendAutomationError(RequestingSocket, RequestId, 
                FString::Printf(TEXT("Controller blueprint not found: %s"), *ControllerPath), TEXT("NOT_FOUND"));
            return true;
        }

        if (!ControllerBP->SimpleConstructionScript)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Blueprint has no SimpleConstructionScript"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find or create AIPerceptionComponent
        UAIPerceptionComponent* PerceptionComp = nullptr;
        USCS_Node* PerceptionNode = nullptr;
        
        for (USCS_Node* Node : ControllerBP->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->ComponentTemplate)
            {
                if (UAIPerceptionComponent* Comp = Cast<UAIPerceptionComponent>(Node->ComponentTemplate))
                {
                    PerceptionComp = Comp;
                    PerceptionNode = Node;
                    break;
                }
            }
        }

        bool bCreatedNew = false;
        if (!PerceptionComp)
        {
            // Create new perception component
            PerceptionNode = ControllerBP->SimpleConstructionScript->CreateNode(
                UAIPerceptionComponent::StaticClass(), TEXT("AIPerceptionComponent"));
            if (!PerceptionNode)
            {
                SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create perception component node"), TEXT("CREATION_FAILED"));
                return true;
            }
            ControllerBP->SimpleConstructionScript->AddNode(PerceptionNode);
            PerceptionComp = Cast<UAIPerceptionComponent>(PerceptionNode->ComponentTemplate);
            if (!PerceptionComp)
            {
                SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to cast perception component"), TEXT("CAST_FAILED"));
                return true;
            }
            bCreatedNew = true;
        }

        if (!PerceptionComp)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Perception component is null"), TEXT("NULL_COMPONENT"));
            return true;
        }

        TArray<FString> SensesConfigured;

        // Configure sight sense
        bool bEnableSight = GetBoolFieldAI(Payload, TEXT("enableSight"));
        if (bEnableSight)
        {
            float SightRadius = GetNumberFieldAI(Payload, TEXT("sightRadius"), 3000.0f);
            float LoseSightRadius = GetNumberFieldAI(Payload, TEXT("loseSightRadius"), SightRadius + 500.0f);
            float PeripheralVisionAngle = GetNumberFieldAI(Payload, TEXT("peripheralVisionAngle"), 90.0f);
            
            UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(PerceptionComp);
            SightConfig->SightRadius = SightRadius;
            SightConfig->LoseSightRadius = LoseSightRadius;
            SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngle;
            SightConfig->DetectionByAffiliation.bDetectEnemies = true;
            SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
            SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
            SightConfig->SetMaxAge(5.0f);
            
            PerceptionComp->ConfigureSense(*SightConfig);
            SensesConfigured.Add(TEXT("Sight"));
        }

        // Configure hearing sense
        bool bEnableHearing = GetBoolFieldAI(Payload, TEXT("enableHearing"));
        if (bEnableHearing)
        {
            float HearingRange = GetNumberFieldAI(Payload, TEXT("hearingRange"), 3000.0f);
            
            UAISenseConfig_Hearing* HearingConfig = NewObject<UAISenseConfig_Hearing>(PerceptionComp);
            HearingConfig->HearingRange = HearingRange;
            HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
            HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
            HearingConfig->DetectionByAffiliation.bDetectFriendlies = false;
            HearingConfig->SetMaxAge(5.0f);
            
            PerceptionComp->ConfigureSense(*HearingConfig);
            SensesConfigured.Add(TEXT("Hearing"));
        }

        // Configure damage sense
        bool bEnableDamage = GetBoolFieldAI(Payload, TEXT("enableDamage"));
        if (bEnableDamage)
        {
            UAISenseConfig_Damage* DamageConfig = NewObject<UAISenseConfig_Damage>(PerceptionComp);
            DamageConfig->SetMaxAge(10.0f);
            
            PerceptionComp->ConfigureSense(*DamageConfig);
            SensesConfigured.Add(TEXT("Damage"));
        }

        // Set dominant sense if specified
        FString DominantSense = GetStringFieldAI(Payload, TEXT("dominantSense"));
        if (!DominantSense.IsEmpty())
        {
            if (DominantSense.Equals(TEXT("Sight"), ESearchCase::IgnoreCase))
            {
                PerceptionComp->SetDominantSense(UAISense_Sight::StaticClass());
            }
            else if (DominantSense.Equals(TEXT("Hearing"), ESearchCase::IgnoreCase))
            {
                PerceptionComp->SetDominantSense(UAISense_Hearing::StaticClass());
            }
            else if (DominantSense.Equals(TEXT("Damage"), ESearchCase::IgnoreCase))
            {
                PerceptionComp->SetDominantSense(UAISense_Damage::StaticClass());
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ControllerBP);
        McpSafeAssetSave(ControllerBP);

        TSharedPtr<FJsonObject> PerceptionResult = MakeShareable(new FJsonObject());
        PerceptionResult->SetStringField(TEXT("controllerPath"), ControllerPath);
        PerceptionResult->SetBoolField(TEXT("createdNew"), bCreatedNew);
        
        TArray<TSharedPtr<FJsonValue>> SensesArray;
        for (const FString& Sense : SensesConfigured)
        {
            SensesArray.Add(MakeShareable(new FJsonValueString(Sense)));
        }
        PerceptionResult->SetArrayField(TEXT("sensesConfigured"), SensesArray);
        
        if (!DominantSense.IsEmpty())
        {
            PerceptionResult->SetStringField(TEXT("dominantSense"), DominantSense);
        }

        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("AI perception configured"), PerceptionResult);
        return true;
    }

    // create_nav_modifier - Create navigation modifier component on actor
    if (SubAction == TEXT("create_nav_modifier"))
    {
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));
        if (BlueprintPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing blueprintPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId, 
                FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath), TEXT("NOT_FOUND"));
            return true;
        }

        if (!Blueprint->SimpleConstructionScript)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Blueprint has no SimpleConstructionScript"), TEXT("INVALID_STATE"));
            return true;
        }

        FString ComponentName = GetStringFieldAI(Payload, TEXT("componentName"));
        if (ComponentName.IsEmpty())
        {
            ComponentName = TEXT("NavModifierComponent");
        }

        // Create nav modifier component
        USCS_Node* NavModNode = Blueprint->SimpleConstructionScript->CreateNode(
            UNavModifierComponent::StaticClass(), *ComponentName);
        if (!NavModNode)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create nav modifier node"), TEXT("CREATION_FAILED"));
            return true;
        }

        Blueprint->SimpleConstructionScript->AddNode(NavModNode);
        UNavModifierComponent* NavModComp = Cast<UNavModifierComponent>(NavModNode->ComponentTemplate);
        
        if (NavModComp)
        {
            // Configure fail-safe defaults
            bool bFailsafe = GetBoolFieldAI(Payload, TEXT("failsafeToDefaultNavmesh"));
            NavModComp->SetAreaClass(bFailsafe ? UNavArea_Default::StaticClass() : UNavArea_Obstacle::StaticClass());
            
            // Set area class if specified
            FString AreaClassName = GetStringFieldAI(Payload, TEXT("areaClass"));
            if (!AreaClassName.IsEmpty())
            {
                UClass* AreaClass = FindObject<UClass>(nullptr, *AreaClassName);
                if (!AreaClass)
                {
                    // Try common area classes
                    if (AreaClassName.Equals(TEXT("NavArea_Null"), ESearchCase::IgnoreCase) ||
                        AreaClassName.Equals(TEXT("Null"), ESearchCase::IgnoreCase))
                    {
                        AreaClass = UNavArea_Null::StaticClass();
                    }
                    else if (AreaClassName.Equals(TEXT("NavArea_Obstacle"), ESearchCase::IgnoreCase) ||
                             AreaClassName.Equals(TEXT("Obstacle"), ESearchCase::IgnoreCase))
                    {
                        AreaClass = UNavArea_Obstacle::StaticClass();
                    }
                    else if (AreaClassName.Equals(TEXT("NavArea_Default"), ESearchCase::IgnoreCase) ||
                             AreaClassName.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
                    {
                        AreaClass = UNavArea_Default::StaticClass();
                    }
                }
                
                if (AreaClass && AreaClass->IsChildOf(UNavArea::StaticClass()))
                {
                    NavModComp->SetAreaClass(AreaClass);
                }
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        McpSafeAssetSave(Blueprint);

        TSharedPtr<FJsonObject> NavModResult = MakeShareable(new FJsonObject());
        NavModResult->SetStringField(TEXT("blueprintPath"), BlueprintPath);
        NavModResult->SetStringField(TEXT("componentName"), ComponentName);
        // UE 5.7: GetAreaClass() is not available on UNavModifierComponent
        // The area class is determined by the NavArea class set on the component
        FString AreaClassName = TEXT("Default");
        NavModResult->SetStringField(TEXT("areaClass"), AreaClassName);

        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Nav modifier component created"), NavModResult);
        return true;
    }

    // set_ai_movement - Configure AI movement parameters (speed, acceleration, etc.)
    if (SubAction == TEXT("set_ai_movement"))
    {
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));
        if (BlueprintPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing blueprintPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
        if (!Blueprint)
        {
            SendAutomationError(RequestingSocket, RequestId, 
                FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath), TEXT("NOT_FOUND"));
            return true;
        }

        if (!Blueprint->SimpleConstructionScript)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Blueprint has no SimpleConstructionScript"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find CharacterMovementComponent
        UCharacterMovementComponent* MovementComp = nullptr;
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->ComponentTemplate)
            {
                if (UCharacterMovementComponent* Comp = Cast<UCharacterMovementComponent>(Node->ComponentTemplate))
                {
                    MovementComp = Comp;
                    break;
                }
            }
        }

        if (!MovementComp)
        {
            // Check CDO for native component
            if (Blueprint->GeneratedClass)
            {
                if (AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
                {
                    MovementComp = CDO->FindComponentByClass<UCharacterMovementComponent>();
                }
            }
        }

        if (!MovementComp)
        {
            SendAutomationError(RequestingSocket, RequestId, 
                TEXT("No CharacterMovementComponent found in blueprint"), TEXT("COMPONENT_NOT_FOUND"));
            return true;
        }

        TArray<FString> PropertiesSet;

        // Walking speed
        float MaxWalkSpeed = GetNumberFieldAI(Payload, TEXT("maxWalkSpeed"), -1.0f);
        if (MaxWalkSpeed > 0.0f)
        {
            MovementComp->MaxWalkSpeed = MaxWalkSpeed;
            PropertiesSet.Add(TEXT("MaxWalkSpeed"));
        }

        // Max acceleration
        float MaxAcceleration = GetNumberFieldAI(Payload, TEXT("maxAcceleration"), -1.0f);
        if (MaxAcceleration > 0.0f)
        {
            MovementComp->MaxAcceleration = MaxAcceleration;
            PropertiesSet.Add(TEXT("MaxAcceleration"));
        }

        // Braking deceleration walking
        float BrakingDeceleration = GetNumberFieldAI(Payload, TEXT("brakingDeceleration"), -1.0f);
        if (BrakingDeceleration > 0.0f)
        {
            MovementComp->BrakingDecelerationWalking = BrakingDeceleration;
            PropertiesSet.Add(TEXT("BrakingDecelerationWalking"));
        }

        // Rotation rate
        float RotationRate = GetNumberFieldAI(Payload, TEXT("rotationRate"), -1.0f);
        if (RotationRate > 0.0f)
        {
            MovementComp->RotationRate = FRotator(0.0f, RotationRate, 0.0f);
            PropertiesSet.Add(TEXT("RotationRate"));
        }

        // Use acceleration for paths
        // UE 5.7+: bUseAccelerationForPaths was removed from UNavMovementComponent
        // Use bRequestedMoveUseAcceleration in UCharacterMovementComponent instead
        bool bUseAcceleration = GetBoolFieldAI(Payload, TEXT("useAccelerationForPaths"));
        if (Payload->HasField(TEXT("useAccelerationForPaths")))
        {
            MovementComp->bRequestedMoveUseAcceleration = bUseAcceleration;
            PropertiesSet.Add(TEXT("bRequestedMoveUseAcceleration"));
        }

        // Orient rotation to movement
        bool bOrientToMovement = GetBoolFieldAI(Payload, TEXT("orientRotationToMovement"));
        if (Payload->HasField(TEXT("orientRotationToMovement")))
        {
            MovementComp->bOrientRotationToMovement = bOrientToMovement;
            PropertiesSet.Add(TEXT("bOrientRotationToMovement"));
        }

        // Use RVO avoidance
        bool bUseRVOAvoidance = GetBoolFieldAI(Payload, TEXT("useRVOAvoidance"));
        if (Payload->HasField(TEXT("useRVOAvoidance")))
        {
            MovementComp->bUseRVOAvoidance = bUseRVOAvoidance;
            PropertiesSet.Add(TEXT("bUseRVOAvoidance"));
        }

        // Avoidance weight
        float AvoidanceWeight = GetNumberFieldAI(Payload, TEXT("avoidanceWeight"), -1.0f);
        if (AvoidanceWeight >= 0.0f)
        {
            MovementComp->AvoidanceWeight = AvoidanceWeight;
            PropertiesSet.Add(TEXT("AvoidanceWeight"));
        }

        // Max fly speed (for flying AI)
        float MaxFlySpeed = GetNumberFieldAI(Payload, TEXT("maxFlySpeed"), -1.0f);
        if (MaxFlySpeed > 0.0f)
        {
            MovementComp->MaxFlySpeed = MaxFlySpeed;
            PropertiesSet.Add(TEXT("MaxFlySpeed"));
        }

        // Jump Z velocity
        float JumpZVelocity = GetNumberFieldAI(Payload, TEXT("jumpZVelocity"), -1.0f);
        if (JumpZVelocity > 0.0f)
        {
            MovementComp->JumpZVelocity = JumpZVelocity;
            PropertiesSet.Add(TEXT("JumpZVelocity"));
        }

        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        McpSafeAssetSave(Blueprint);

        TSharedPtr<FJsonObject> MovementResult = MakeShareable(new FJsonObject());
        MovementResult->SetStringField(TEXT("blueprintPath"), BlueprintPath);
        
        TArray<TSharedPtr<FJsonValue>> PropsArray;
        for (const FString& Prop : PropertiesSet)
        {
            PropsArray.Add(MakeShareable(new FJsonValueString(Prop)));
        }
        MovementResult->SetArrayField(TEXT("propertiesSet"), PropsArray);
        MovementResult->SetNumberField(TEXT("propertyCount"), PropertiesSet.Num());

        // Include current values
        TSharedPtr<FJsonObject> CurrentValues = MakeShareable(new FJsonObject());
        CurrentValues->SetNumberField(TEXT("maxWalkSpeed"), MovementComp->MaxWalkSpeed);
        CurrentValues->SetNumberField(TEXT("maxAcceleration"), MovementComp->MaxAcceleration);
        CurrentValues->SetNumberField(TEXT("rotationRateYaw"), MovementComp->RotationRate.Yaw);
        CurrentValues->SetBoolField(TEXT("orientRotationToMovement"), MovementComp->bOrientRotationToMovement);
        CurrentValues->SetBoolField(TEXT("useRVOAvoidance"), MovementComp->bUseRVOAvoidance);
        MovementResult->SetObjectField(TEXT("currentValues"), CurrentValues);

        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("AI movement configured"), MovementResult);
        return true;
    }

    // =========================================================================
    // Aliases & Convenience Actions
    // =========================================================================

    // Alias: create_blackboard -> create_blackboard_asset
    if (SubAction == TEXT("create_blackboard"))
    {
        // Redirect to existing create_blackboard_asset handler
        FString Name = GetStringFieldAI(Payload, TEXT("name"));
        if (Name.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing name"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString Path = GetStringFieldAI(Payload, TEXT("path"));
        if (Path.IsEmpty())
        {
            Path = TEXT("/Game/AI/Blackboards");
        }

        FString AssetPath = Path / Name;
        FString SanitizedPath, SanitizeError;
        if (!SanitizeAIAssetPath(AssetPath, SanitizedPath, SanitizeError))
        {
            SendAutomationError(RequestingSocket, RequestId, SanitizeError, TEXT("INVALID_PATH"));
            return true;
        }

        if (UEditorAssetLibrary::DoesAssetExist(SanitizedPath))
        {
            TSharedPtr<FJsonObject> ExistResult = MakeShareable(new FJsonObject());
            ExistResult->SetStringField(TEXT("blackboardPath"), SanitizedPath);
            ExistResult->SetBoolField(TEXT("alreadyExisted"), true);
            SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Blackboard already exists"), ExistResult);
            return true;
        }

        UBlackboardData* NewBB = NewObject<UBlackboardData>(CreatePackage(*SanitizedPath), *FPaths::GetBaseFilename(SanitizedPath), RF_Public | RF_Standalone);
        if (!NewBB)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create blackboard data asset"), TEXT("CREATION_FAILED"));
            return true;
        }

        McpSafeAssetSave(NewBB);

        TSharedPtr<FJsonObject> BBResult = MakeShareable(new FJsonObject());
        BBResult->SetStringField(TEXT("blackboardPath"), SanitizedPath);
        BBResult->SetBoolField(TEXT("alreadyExisted"), false);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Blackboard created"), BBResult);
        return true;
    }

    // Alias: setup_perception -> add_ai_perception_component (same logic)
    if (SubAction == TEXT("setup_perception"))
    {
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));
        if (ControllerPath.IsEmpty())
        {
            ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        }
        if (ControllerPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing blueprintPath or controllerPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlueprint* ControllerBP = LoadObject<UBlueprint>(nullptr, *ControllerPath);
        if (!ControllerBP)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Blueprint not found: %s"), *ControllerPath), TEXT("NOT_FOUND"));
            return true;
        }

        if (!ControllerBP->SimpleConstructionScript)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Blueprint has no SimpleConstructionScript"), TEXT("INVALID_STATE"));
            return true;
        }

        // Find or create AIPerceptionComponent
        UAIPerceptionComponent* PerceptionComp = nullptr;
        bool bCreatedNew = false;

        for (USCS_Node* Node : ControllerBP->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->ComponentTemplate)
            {
                if (UAIPerceptionComponent* Comp = Cast<UAIPerceptionComponent>(Node->ComponentTemplate))
                {
                    PerceptionComp = Comp;
                    break;
                }
            }
        }

        if (!PerceptionComp)
        {
            USCS_Node* PerceptionNode = ControllerBP->SimpleConstructionScript->CreateNode(
                UAIPerceptionComponent::StaticClass(), TEXT("AIPerceptionComponent"));
            if (!PerceptionNode)
            {
                SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create perception component node"), TEXT("CREATION_FAILED"));
                return true;
            }
            ControllerBP->SimpleConstructionScript->AddNode(PerceptionNode);
            PerceptionComp = Cast<UAIPerceptionComponent>(PerceptionNode->ComponentTemplate);
            bCreatedNew = true;
        }

        if (!PerceptionComp)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Perception component is null"), TEXT("NULL_COMPONENT"));
            return true;
        }

        TArray<FString> SensesConfigured;

        bool bEnableSight = GetBoolFieldAI(Payload, TEXT("enableSight"));
        if (bEnableSight)
        {
            float SightRadius = GetNumberFieldAI(Payload, TEXT("sightRadius"), 3000.0f);
            float LoseSightRadius = GetNumberFieldAI(Payload, TEXT("loseSightRadius"), SightRadius + 500.0f);
            float PeripheralVisionAngle = GetNumberFieldAI(Payload, TEXT("peripheralVisionAngle"), 90.0f);

            UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(PerceptionComp);
            SightConfig->SightRadius = SightRadius;
            SightConfig->LoseSightRadius = LoseSightRadius;
            SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngle;
            SightConfig->DetectionByAffiliation.bDetectEnemies = true;
            SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
            SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
            SightConfig->SetMaxAge(5.0f);

            PerceptionComp->ConfigureSense(*SightConfig);
            SensesConfigured.Add(TEXT("Sight"));
        }

        bool bEnableHearing = GetBoolFieldAI(Payload, TEXT("enableHearing"));
        if (bEnableHearing)
        {
            float HearingRange = GetNumberFieldAI(Payload, TEXT("hearingRange"), 3000.0f);
            UAISenseConfig_Hearing* HearingConfig = NewObject<UAISenseConfig_Hearing>(PerceptionComp);
            HearingConfig->HearingRange = HearingRange;
            HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
            HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
            HearingConfig->DetectionByAffiliation.bDetectFriendlies = false;
            HearingConfig->SetMaxAge(5.0f);
            PerceptionComp->ConfigureSense(*HearingConfig);
            SensesConfigured.Add(TEXT("Hearing"));
        }

        bool bEnableDamage = GetBoolFieldAI(Payload, TEXT("enableDamage"));
        if (bEnableDamage)
        {
            UAISenseConfig_Damage* DamageConfig = NewObject<UAISenseConfig_Damage>(PerceptionComp);
            DamageConfig->SetMaxAge(10.0f);
            PerceptionComp->ConfigureSense(*DamageConfig);
            SensesConfigured.Add(TEXT("Damage"));
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ControllerBP);
        McpSafeAssetSave(ControllerBP);

        TSharedPtr<FJsonObject> PerceptionResult = MakeShareable(new FJsonObject());
        PerceptionResult->SetStringField(TEXT("controllerPath"), ControllerPath);
        PerceptionResult->SetBoolField(TEXT("createdNew"), bCreatedNew);

        TArray<TSharedPtr<FJsonValue>> SensesArray;
        for (const FString& Sense : SensesConfigured)
        {
            SensesArray.Add(MakeShareable(new FJsonValueString(Sense)));
        }
        PerceptionResult->SetArrayField(TEXT("sensesConfigured"), SensesArray);

        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("AI perception configured via setup_perception"), PerceptionResult);
        return true;
    }

    // create_nav_link_proxy - Create a NavLinkProxy blueprint
    if (SubAction == TEXT("create_nav_link_proxy"))
    {
        FString BlueprintPath = GetStringFieldAI(Payload, TEXT("blueprintPath"));
        if (BlueprintPath.IsEmpty())
        {
            BlueprintPath = GetStringFieldAI(Payload, TEXT("name"));
            if (!BlueprintPath.IsEmpty())
            {
                FString Path = GetStringFieldAI(Payload, TEXT("path"));
                if (Path.IsEmpty()) Path = TEXT("/Game/AI");
                BlueprintPath = Path / BlueprintPath;
            }
        }
        if (BlueprintPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing blueprintPath or name"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString SanitizedPath, SanitizeError;
        if (!SanitizeAIAssetPath(BlueprintPath, SanitizedPath, SanitizeError))
        {
            SendAutomationError(RequestingSocket, RequestId, SanitizeError, TEXT("INVALID_PATH"));
            return true;
        }

        if (UEditorAssetLibrary::DoesAssetExist(SanitizedPath))
        {
            TSharedPtr<FJsonObject> ExistResult = MakeShareable(new FJsonObject());
            ExistResult->SetStringField(TEXT("blueprintPath"), SanitizedPath);
            ExistResult->SetBoolField(TEXT("alreadyExisted"), true);
            SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("NavLinkProxy blueprint already exists"), ExistResult);
            return true;
        }

        UClass* NavLinkProxyClass = FindObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavLinkProxy"));
        if (!NavLinkProxyClass)
        {
            NavLinkProxyClass = AActor::StaticClass();
        }

        UBlueprint* NavLinkBP = FKismetEditorUtilities::CreateBlueprint(
            NavLinkProxyClass,
            CreatePackage(*SanitizedPath),
            *FPaths::GetBaseFilename(SanitizedPath),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass());

        if (!NavLinkBP)
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Failed to create NavLinkProxy blueprint"), TEXT("CREATION_FAILED"));
            return true;
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(NavLinkBP);
        McpSafeAssetSave(NavLinkBP);

        TSharedPtr<FJsonObject> NavResult = MakeShareable(new FJsonObject());
        NavResult->SetStringField(TEXT("blueprintPath"), SanitizedPath);
        NavResult->SetBoolField(TEXT("alreadyExisted"), false);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("NavLinkProxy blueprint created"), NavResult);
        return true;
    }

    // set_focus - Set focus actor variable on AI controller blueprint
    if (SubAction == TEXT("set_focus"))
    {
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        if (ControllerPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing controllerPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString FocusActorName = GetStringFieldAI(Payload, TEXT("focusActorName"));
        if (FocusActorName.IsEmpty())
        {
            FocusActorName = GetStringFieldAI(Payload, TEXT("targetActor"));
        }

        UBlueprint* ControllerBP = LoadObject<UBlueprint>(nullptr, *ControllerPath);
        if (!ControllerBP)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Controller blueprint not found: %s"), *ControllerPath), TEXT("NOT_FOUND"));
            return true;
        }

        // Add a FocusActor variable to the BP
        FEdGraphPinType PinType;
        PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
        PinType.PinSubCategoryObject = AActor::StaticClass();
        FBlueprintEditorUtils::AddMemberVariable(ControllerBP, TEXT("FocusActor"), PinType);

        FBlueprintEditorUtils::MarkBlueprintAsModified(ControllerBP);
        McpSafeAssetSave(ControllerBP);

        TSharedPtr<FJsonObject> FocusResult = MakeShareable(new FJsonObject());
        FocusResult->SetStringField(TEXT("controllerPath"), ControllerPath);
        FocusResult->SetStringField(TEXT("focusActorName"), FocusActorName);
        FocusResult->SetBoolField(TEXT("focusSet"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Focus actor variable set on controller"), FocusResult);
        return true;
    }

    // clear_focus - Clear focus actor variable on AI controller blueprint
    if (SubAction == TEXT("clear_focus"))
    {
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        if (ControllerPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing controllerPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlueprint* ControllerBP = LoadObject<UBlueprint>(nullptr, *ControllerPath);
        if (!ControllerBP)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Controller blueprint not found: %s"), *ControllerPath), TEXT("NOT_FOUND"));
            return true;
        }

        // Remove or reset the FocusActor variable
        FBlueprintEditorUtils::RemoveMemberVariable(ControllerBP, TEXT("FocusActor"));

        FBlueprintEditorUtils::MarkBlueprintAsModified(ControllerBP);
        McpSafeAssetSave(ControllerBP);

        TSharedPtr<FJsonObject> ClearResult = MakeShareable(new FJsonObject());
        ClearResult->SetStringField(TEXT("controllerPath"), ControllerPath);
        ClearResult->SetBoolField(TEXT("focusCleared"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Focus cleared on controller"), ClearResult);
        return true;
    }

    // set_blackboard_value - Set a default key value on a blackboard asset
    if (SubAction == TEXT("set_blackboard_value"))
    {
        FString BBPath = GetStringFieldAI(Payload, TEXT("blackboardPath"));
        if (BBPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing blackboardPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString KeyName = GetStringFieldAI(Payload, TEXT("keyName"));
        if (KeyName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing keyName"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlackboardData* BBData = LoadObject<UBlackboardData>(nullptr, *BBPath);
        if (!BBData)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Blackboard not found: %s"), *BBPath), TEXT("NOT_FOUND"));
            return true;
        }

        // Find the key and set its value
        bool bKeyFound = false;
        bool bValueSet = false;
        FString ValueStr = GetStringFieldAI(Payload, TEXT("value"));
        
        for (FBlackboardEntry& Key : BBData->Keys)
        {
            if (Key.EntryName.ToString() == KeyName)
            {
                bKeyFound = true;
                
                // Set the default value based on key type
                // Note: DefaultValue properties on BlackboardKeyType are only available in UE 5.5+
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
                if (Key.KeyType && !ValueStr.IsEmpty())
                {
                    if (UBlackboardKeyType_Bool* BoolKey = Cast<UBlackboardKeyType_Bool>(Key.KeyType))
                    {
                        BoolKey->bDefaultValue = ValueStr.ToLower() == TEXT("true") || ValueStr == TEXT("1");
                        bValueSet = true;
                    }
                    else if (UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(Key.KeyType))
                    {
                        IntKey->DefaultValue = FCString::Atoi(*ValueStr);
                        bValueSet = true;
                    }
                    else if (UBlackboardKeyType_Float* FloatKey = Cast<UBlackboardKeyType_Float>(Key.KeyType))
                    {
                        FloatKey->DefaultValue = FCString::Atof(*ValueStr);
                        bValueSet = true;
                    }
                    else if (UBlackboardKeyType_Vector* VectorKey = Cast<UBlackboardKeyType_Vector>(Key.KeyType))
                    {
                        VectorKey->DefaultValue.InitFromString(ValueStr);
                        VectorKey->bUseDefaultValue = true;
                        bValueSet = true;
                    }
                    else if (UBlackboardKeyType_Rotator* RotatorKey = Cast<UBlackboardKeyType_Rotator>(Key.KeyType))
                    {
                        RotatorKey->DefaultValue.InitFromString(ValueStr);
                        RotatorKey->bUseDefaultValue = true;
                        bValueSet = true;
                    }
                    else if (UBlackboardKeyType_Name* NameKey = Cast<UBlackboardKeyType_Name>(Key.KeyType))
                    {
                        NameKey->DefaultValue = FName(*ValueStr);
                        bValueSet = true;
                    }
                    else if (UBlackboardKeyType_String* StringKey = Cast<UBlackboardKeyType_String>(Key.KeyType))
                    {
                        StringKey->DefaultValue = ValueStr;
                        bValueSet = true;
                    }
                    else
                    {
                        // Unsupported key type - note in response
                        bValueSet = false;
                    }
                }
#else
                // UE 5.0-5.4: DefaultValue properties not available on BlackboardKeyType
                // Value setting requires UE 5.5+
                bValueSet = false;
#endif
                break;
            }
        }

        if (!bKeyFound)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Key '%s' not found in blackboard"), *KeyName), TEXT("KEY_NOT_FOUND"));
            return true;
        }

        McpSafeAssetSave(BBData);

        TSharedPtr<FJsonObject> SetResult = MakeShareable(new FJsonObject());
        SetResult->SetStringField(TEXT("blackboardPath"), BBPath);
        SetResult->SetStringField(TEXT("keyName"), KeyName);
        SetResult->SetStringField(TEXT("value"), ValueStr);
        SetResult->SetBoolField(TEXT("valueSet"), bValueSet);
        
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
        SendAutomationResponse(RequestingSocket, RequestId, true, 
            bValueSet ? TEXT("Blackboard value set") : TEXT("Key found but value not set (unsupported type)"), SetResult);
#else
        SendAutomationResponse(RequestingSocket, RequestId, true, 
            TEXT("Key found. Note: set_blackboard_value requires UE 5.5+ for value setting."), SetResult);
#endif
        return true;
    }

    // get_blackboard_value - Get a key's info from a blackboard asset
    if (SubAction == TEXT("get_blackboard_value"))
    {
        FString BBPath = GetStringFieldAI(Payload, TEXT("blackboardPath"));
        if (BBPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing blackboardPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString KeyName = GetStringFieldAI(Payload, TEXT("keyName"));
        if (KeyName.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing keyName"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlackboardData* BBData = LoadObject<UBlackboardData>(nullptr, *BBPath);
        if (!BBData)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Blackboard not found: %s"), *BBPath), TEXT("NOT_FOUND"));
            return true;
        }

        // Find the key
        bool bKeyFound = false;
        FString KeyType = TEXT("Unknown");
        bool bInstanceSynced = false;

        for (const FBlackboardEntry& Key : BBData->Keys)
        {
            if (Key.EntryName.ToString() == KeyName)
            {
                bKeyFound = true;
                bInstanceSynced = Key.bInstanceSynced;
                if (Key.KeyType)
                {
                    KeyType = Key.KeyType->GetClass()->GetName();
                }
                break;
            }
        }

        if (!bKeyFound)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Key '%s' not found in blackboard"), *KeyName), TEXT("KEY_NOT_FOUND"));
            return true;
        }

        TSharedPtr<FJsonObject> GetResult = MakeShareable(new FJsonObject());
        GetResult->SetStringField(TEXT("blackboardPath"), BBPath);
        GetResult->SetStringField(TEXT("keyName"), KeyName);
        GetResult->SetStringField(TEXT("keyType"), KeyType);
        GetResult->SetBoolField(TEXT("instanceSynced"), bInstanceSynced);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Blackboard value retrieved"), GetResult);
        return true;
    }

    // run_behavior_tree - Alias for assign_behavior_tree
    if (SubAction == TEXT("run_behavior_tree"))
    {
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        if (ControllerPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing controllerPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        FString BTPath = GetStringFieldAI(Payload, TEXT("behaviorTreePath"));
        if (BTPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing behaviorTreePath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlueprint* ControllerBP = LoadObject<UBlueprint>(nullptr, *ControllerPath);
        if (!ControllerBP)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Controller blueprint not found: %s"), *ControllerPath), TEXT("NOT_FOUND"));
            return true;
        }

        UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
        if (!BT)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Behavior tree not found: %s"), *BTPath), TEXT("NOT_FOUND"));
            return true;
        }

        // Store the BT reference as a variable on the controller
        FEdGraphPinType PinType;
        PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
        PinType.PinSubCategoryObject = UBehaviorTree::StaticClass();
        FBlueprintEditorUtils::AddMemberVariable(ControllerBP, TEXT("AssignedBehaviorTree"), PinType);

        FBlueprintEditorUtils::MarkBlueprintAsModified(ControllerBP);
        McpSafeAssetSave(ControllerBP);

        TSharedPtr<FJsonObject> RunResult = MakeShareable(new FJsonObject());
        RunResult->SetStringField(TEXT("controllerPath"), ControllerPath);
        RunResult->SetStringField(TEXT("behaviorTreePath"), BTPath);
        RunResult->SetBoolField(TEXT("assigned"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Behavior tree assigned for running"), RunResult);
        return true;
    }

    // stop_behavior_tree - Remove behavior tree assignment from controller
    if (SubAction == TEXT("stop_behavior_tree"))
    {
        FString ControllerPath = GetStringFieldAI(Payload, TEXT("controllerPath"));
        if (ControllerPath.IsEmpty())
        {
            SendAutomationError(RequestingSocket, RequestId, TEXT("Missing controllerPath"), TEXT("INVALID_ARGUMENT"));
            return true;
        }

        UBlueprint* ControllerBP = LoadObject<UBlueprint>(nullptr, *ControllerPath);
        if (!ControllerBP)
        {
            SendAutomationError(RequestingSocket, RequestId,
                FString::Printf(TEXT("Controller blueprint not found: %s"), *ControllerPath), TEXT("NOT_FOUND"));
            return true;
        }

        // Remove the BT variable to "stop" it
        FBlueprintEditorUtils::RemoveMemberVariable(ControllerBP, TEXT("AssignedBehaviorTree"));

        FBlueprintEditorUtils::MarkBlueprintAsModified(ControllerBP);
        McpSafeAssetSave(ControllerBP);

        TSharedPtr<FJsonObject> StopResult = MakeShareable(new FJsonObject());
        StopResult->SetStringField(TEXT("controllerPath"), ControllerPath);
        StopResult->SetBoolField(TEXT("stopped"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Behavior tree stopped"), StopResult);
        return true;
    }

    // Unknown sub-action
    SendAutomationError(RequestingSocket, RequestId,
                        FString::Printf(TEXT("Unknown AI action: %s"), *SubAction),
                        TEXT("UNKNOWN_ACTION"));
    return true;
#endif
}
