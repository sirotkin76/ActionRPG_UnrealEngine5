// Copyright Epic Games, Inc. All Rights Reserved.

#include "RPGGameInstanceBase.h"

/** 访问ChunkDownloader，以及一些用于管理资产和委托的有用工具 */
#include "ChunkDownloader.h"
#include "Misc/CoreDelegates.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializerMacros.h"

#include "RPGAssetManager.h"
#include "RPGSaveGame.h"
#include "Items/RPGItem.h"
#include "Kismet/GameplayStatics.h"

URPGGameInstanceBase::URPGGameInstanceBase()
	: SaveSlot(TEXT("SaveGame"))
	, SaveUserIndex(0)
{}

void URPGGameInstanceBase::Init()
{
	Super::Init();

	const FString DeploymentName = "PatchingLive";
	const FString ContentBuildId = "PatchingKey";

	TSharedRef<FChunkDownloader> Downloader = FChunkDownloader::GetOrCreate();

	Downloader->Initialize("Windows", 8);

	Downloader->LoadCachedBuild(DeploymentName);

	TFunction<void(bool bSuccess)> UpdateCompleteCallback = [&](bool bSuccess) {
		bIsDownloadManifestUpToDate = true;
	};

	Downloader->UpdateBuild(DeploymentName, ContentBuildId, UpdateCompleteCallback);
}

void URPGGameInstanceBase::Shutdown()
{
	Super::Shutdown();

	FChunkDownloader::Shutdown();
}

void URPGGameInstanceBase::GetLoadingProgress(int32& BytesDownloaded, int32& TotalBytesToDownload, float& DownloadPercent, int32& ChunksMounted, int32& TotalChunksToMount, float& MountPercent) const
{

	TSharedRef<FChunkDownloader> Downloader = FChunkDownloader::GetChecked();


	FChunkDownloader::FStats LoadingStats = Downloader->GetLoadingStats();


	BytesDownloaded = LoadingStats.BytesDownloaded;
	TotalBytesToDownload = LoadingStats.TotalBytesToDownload;


	ChunksMounted = LoadingStats.ChunksMounted;
	TotalChunksToMount = LoadingStats.TotalChunksToMount;


	DownloadPercent = ((float)BytesDownloaded / (float)TotalBytesToDownload) * 100.0f;
	MountPercent = ((float)ChunksMounted / (float)TotalChunksToMount) * 100.0f;
}

bool URPGGameInstanceBase::PatchGame()
{

	if (bIsDownloadManifestUpToDate)
	{

		TSharedRef<FChunkDownloader> Downloader = FChunkDownloader::GetChecked();


		for (int32 ChunkID : ChunkDownloadList)
		{
			int32 ChunkStatus = static_cast<int32>(Downloader->GetChunkStatus(ChunkID));
			UE_LOG(LogTemp, Display, TEXT("Chunk %i status: %i"), ChunkID, ChunkStatus);
		}

		TFunction<void(bool bSuccess)> DownloadCompleteCallback = [&](bool bSuccess) {

			OnDownloadComplete(bSuccess);
		};

		Downloader->DownloadChunks(ChunkDownloadList, DownloadCompleteCallback, 1);

		TFunction<void(bool bSuccess)> LoadingModeCompleteCallback = [&](bool bSuccess) {

			OnLoadingModeComplete(bSuccess);
		};

		Downloader->BeginLoadingMode(LoadingModeCompleteCallback);
		
		return true;
	}

	UE_LOG(LogTemp, Display, TEXT("Manifest Update Failed. Can't patch the game"));

	return false;
}

void URPGGameInstanceBase::OnManifestUpdateComplete(bool bSuccess)
{
	bIsDownloadManifestUpToDate = bSuccess;
}

void URPGGameInstanceBase::OnDownloadComplete(bool bSuccess)
{
	if (bSuccess)
	{
		UE_LOG(LogTemp, Display, TEXT("Download Complete"));


		TSharedRef<FChunkDownloader> Downloader = FChunkDownloader::GetChecked();

		FJsonSerializableArrayInt DownloadedChunks;

		for (int32 ChunkID : ChunkDownloadList)
		{
			DownloadedChunks.Add(ChunkID);
		}

		TFunction<void(bool bSuccess)> MountCompleteCallback = [&](bool bSuccess) {
			OnMountComplete(bSuccess);
		};

		Downloader->MountChunks(DownloadedChunks, MountCompleteCallback);

		OnPatchComplete.Broadcast(true);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("Load process failed"));

		OnPatchComplete.Broadcast(false);
	}
}

void URPGGameInstanceBase::OnLoadingModeComplete(bool bSuccess)
{
	OnDownloadComplete(bSuccess);
}

void URPGGameInstanceBase::OnMountComplete(bool bSuccess)
{
	OnPatchComplete.Broadcast(bSuccess);
}

void URPGGameInstanceBase::AddDefaultInventory(URPGSaveGame* SaveGame, bool bRemoveExtra)
{
	// If we want to remove extra, clear out the existing inventory
	if (bRemoveExtra)
	{
		SaveGame->InventoryData.Reset();
	}

	// Now add the default inventory, this only adds if not already in hte inventory
	for (const TPair<FPrimaryAssetId, FRPGItemData>& Pair : DefaultInventory)
	{
		if (!SaveGame->InventoryData.Contains(Pair.Key))
		{
			SaveGame->InventoryData.Add(Pair.Key, Pair.Value);
		}
	}
}

bool URPGGameInstanceBase::IsValidItemSlot(FRPGItemSlot ItemSlot) const
{
	if (ItemSlot.IsValid())
	{
		const int32* FoundCount = ItemSlotsPerType.Find(ItemSlot.ItemType);

		if (FoundCount)
		{
			return ItemSlot.SlotNumber < *FoundCount;
		}
	}
	return false;
}

URPGSaveGame* URPGGameInstanceBase::GetCurrentSaveGame()
{
	return CurrentSaveGame;
}

void URPGGameInstanceBase::SetSavingEnabled(bool bEnabled)
{
	bSavingEnabled = bEnabled;
}

bool URPGGameInstanceBase::LoadOrCreateSaveGame()
{
	URPGSaveGame* LoadedSave = nullptr;

	if (UGameplayStatics::DoesSaveGameExist(SaveSlot, SaveUserIndex) && bSavingEnabled)
	{
		LoadedSave = Cast<URPGSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, SaveUserIndex));
	}

	return HandleSaveGameLoaded(LoadedSave);
}

bool URPGGameInstanceBase::HandleSaveGameLoaded(USaveGame* SaveGameObject)
{
	bool bLoaded = false;

	if (!bSavingEnabled)
	{
		// If saving is disabled, ignore passed in object
		SaveGameObject = nullptr;
	}

	// Replace current save, old object will GC out
	CurrentSaveGame = Cast<URPGSaveGame>(SaveGameObject);

	if (CurrentSaveGame)
	{
		// Make sure it has any newly added default inventory
		AddDefaultInventory(CurrentSaveGame, false);
		bLoaded = true;
	}
	else
	{
		// This creates it on demand
		CurrentSaveGame = Cast<URPGSaveGame>(UGameplayStatics::CreateSaveGameObject(URPGSaveGame::StaticClass()));

		AddDefaultInventory(CurrentSaveGame, true);
	}

	OnSaveGameLoaded.Broadcast(CurrentSaveGame);
	OnSaveGameLoadedNative.Broadcast(CurrentSaveGame);

	return bLoaded;
}

void URPGGameInstanceBase::GetSaveSlotInfo(FString& SlotName, int32& UserIndex) const
{
	SlotName = SaveSlot;
	UserIndex = SaveUserIndex;
}

bool URPGGameInstanceBase::WriteSaveGame()
{
	if (bSavingEnabled)
	{
		if (bCurrentlySaving)
		{
			// Schedule another save to happen after current one finishes. We only queue one save
			bPendingSaveRequested = true;
			return true;
		}

		// Indicate that we're currently doing an async save
		bCurrentlySaving = true;

		// This goes off in the background
		UGameplayStatics::AsyncSaveGameToSlot(GetCurrentSaveGame(), SaveSlot, SaveUserIndex, FAsyncSaveGameToSlotDelegate::CreateUObject(this, &URPGGameInstanceBase::HandleAsyncSave));
		return true;
	}
	return false;
}

void URPGGameInstanceBase::ResetSaveGame()
{
	// Call handle function with no loaded save, this will reset the data
	HandleSaveGameLoaded(nullptr);
}

void URPGGameInstanceBase::HandleAsyncSave(const FString& SlotName, const int32 UserIndex, bool bSuccess)
{
	ensure(bCurrentlySaving);
	bCurrentlySaving = false;

	if (bPendingSaveRequested)
	{
		// Start another save as we got a request while saving
		bPendingSaveRequested = false;
		WriteSaveGame();
	}
}
