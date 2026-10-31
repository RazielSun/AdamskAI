// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Cook/ADSKAssetRenames.h"
#include "Cook/ADSKCookReferences.h"
#include "Reload/ADSKLiveReload.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FAdamskAIEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override { LiveReload.Start(); CookReferences.Start(); AssetRenames.Start(); }
	virtual void ShutdownModule() override { AssetRenames.Stop(); CookReferences.Stop(); LiveReload.Stop(); }
private:
	FADSKLiveReload LiveReload;
	FADSKCookReferences CookReferences;
	FADSKAssetRenames AssetRenames;
};

IMPLEMENT_MODULE(FAdamskAIEditorModule, AdamskAIEditor)
