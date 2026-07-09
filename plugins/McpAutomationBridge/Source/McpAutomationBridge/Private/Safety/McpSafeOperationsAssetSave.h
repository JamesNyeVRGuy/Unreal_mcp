#pragma once

#include "Safety/McpSafeOperationsLog.h"
#include "Safety/McpSafeOperationsPackageTools.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/SavePackage.h"
#endif

namespace McpSafeOperations
{

#if WITH_EDITOR

inline bool McpSafeAssetSave(UObject* Asset)
{
    if (!Asset)
    {
        return false;
    }

    UObject* AssetToSave = Asset;
    UPackage* Package = Cast<UPackage>(Asset);
    if (Package)
    {
        AssetToSave = nullptr;
        ForEachObjectWithPackage(Package, [&AssetToSave](UObject* Object) -> bool
        {
            if (Object && !Object->IsA<UPackage>() && Object->HasAnyFlags(RF_Public | RF_Standalone))
            {
                AssetToSave = Object;
                return false;
            }
            return true;
        }, false);
    }
    else
    {
        Package = Asset->GetOutermost();
    }
    if (!Package)
    {
        return false;
    }

    const FString PackageName = Package->GetName();
    if (PackageName.StartsWith(TEXT("/Temp/")) ||
        PackageName.StartsWith(TEXT("/Transient/")) ||
        PackageName.StartsWith(TEXT("/Engine/Transient")) ||
        Package->HasAnyFlags(RF_Transient))
    {
        return false;
    }

    Package->SetDirtyFlag(true);
    if (AssetToSave && AssetToSave != Package)
    {
        AssetToSave->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(AssetToSave);
    }

    auto ScanSavedPackage = [&PackageName]()
    {
        TArray<FString> PathsToScan;
        PathsToScan.Add(FPaths::GetPath(PackageName));
        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, false);
    };

    auto PackageExistsOnDisk = [&PackageName]()
    {
        FString AssetFilename;
        FString MapFilename;
        const bool bHasAssetFilename = FPackageName::TryConvertLongPackageNameToFilename(
            PackageName, AssetFilename, FPackageName::GetAssetPackageExtension());
        const bool bHasMapFilename = FPackageName::TryConvertLongPackageNameToFilename(
            PackageName, MapFilename, FPackageName::GetMapPackageExtension());

        return
            (bHasAssetFilename && IFileManager::Get().FileExists(*FPaths::ConvertRelativePathToFull(AssetFilename))) ||
            (bHasMapFilename && IFileManager::Get().FileExists(*FPaths::ConvertRelativePathToFull(MapFilename)));
    };

#if MCP_HAS_PACKAGE_TOOLS
    // TACB-821 ROOT CAUSE: git commits re-mark committed .uasset/.umap files
    // read-only. A save can't overwrite a read-only file -- the raw Save
    // returns Error, and the editor save path (SavePackagesForObjects /
    // PromptForCheckoutAndSave) HANGS trying to handle it (presents as a
    // FlushAsyncLoading deadlock, wedging the whole editor). Newly-created
    // packages saved fine only because they weren't committed yet (writable).
    // Clear the read-only bit up front so the save just works. This is the fix.
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        const FString Extensions[] = {
            FPackageName::GetAssetPackageExtension(),
            FPackageName::GetMapPackageExtension()
        };
        for (const FString& Ext : Extensions)
        {
            FString OnDisk;
            if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, OnDisk, Ext))
            {
                const FString FullPath = FPaths::ConvertRelativePathToFull(OnDisk);
                if (PlatformFile.FileExists(*FullPath) && PlatformFile.IsReadOnly(*FullPath))
                {
                    PlatformFile.SetReadOnly(*FullPath, false);
                    UE_LOG(LogMcpSafeOperations, Log,
                        TEXT("McpSafeAssetSave: cleared read-only bit on %s (TACB-821)"), *FullPath);
                }
            }
        }
    }

    // TACB-821: LEAN low-level save FIRST. The editor save paths below
    // (SavePackagesForObjects / PromptForCheckoutAndSave / SavePackages)
    // FullyLoad the package and resolve source-control state before writing,
    // which queues an async package load that then DEADLOCKS SavePackage's
    // FlushAsyncLoading (log: "1 QueuedPackages, 0 AsyncPackages", game thread
    // frozen -> every save of an item/WBP wedges on both boxes). The asset is
    // already resident (we just edited it), so a raw UPackage::Save that skips
    // the pre-save load avoids queuing anything and completes. Falls through to
    // the editor paths only if this raw save fails.
    {
        FString RawFilename;
        if (FPackageName::TryConvertLongPackageNameToFilename(
                PackageName, RawFilename, FPackageName::GetAssetPackageExtension()))
        {
            FlushRenderingCommands();
            FSavePackageArgs RawSaveArgs;
            RawSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            RawSaveArgs.SaveFlags = SAVE_NoError;
            RawSaveArgs.Error = GError;
            const FSavePackageResultStruct RawResult =
                UPackage::Save(Package, AssetToSave, *RawFilename, RawSaveArgs);
            if (RawResult.IsSuccessful() && PackageExistsOnDisk())
            {
                UE_LOG(LogMcpSafeOperations, Log,
                    TEXT("McpSafeAssetSave: raw UPackage::Save succeeded for %s (lean path, TACB-821)"),
                    *PackageName);
                ScanSavedPackage();
                return true;
            }
            UE_LOG(LogMcpSafeOperations, Warning,
                TEXT("McpSafeAssetSave: raw UPackage::Save did not persist %s (result=%d); falling back to editor save path"),
                *PackageName, static_cast<int32>(RawResult.Result));
        }
    }

    if (AssetToSave && AssetToSave != Package)
    {
        TArray<UObject*> ObjectsToSave;
        ObjectsToSave.Add(AssetToSave);

        FlushRenderingCommands();

        const bool bSaved = UPackageTools::SavePackagesForObjects(ObjectsToSave);
        if (bSaved && PackageExistsOnDisk())
        {
            ScanSavedPackage();
            return true;
        }

        if (bSaved)
        {
            UE_LOG(LogMcpSafeOperations, Warning,
                TEXT("McpSafeAssetSave: SavePackagesForObjects reported success but no package file exists for %s; trying package save fallback"),
                *PackageName);
        }
    }

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    const FEditorFileUtils::EPromptReturnCode PromptSaveResult =
        FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);
    const bool bPromptSaveSucceeded =
        PromptSaveResult == FEditorFileUtils::PR_Success;
    const bool bEditorSaveSucceeded =
        !bPromptSaveSucceeded && UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);
    const bool bExistsOnDisk = PackageExistsOnDisk();

    if ((bPromptSaveSucceeded || bEditorSaveSucceeded) && bExistsOnDisk)
    {
        ScanSavedPackage();
        return true;
    }

    return false;
#else
    return false;
#endif
}

#endif

}
