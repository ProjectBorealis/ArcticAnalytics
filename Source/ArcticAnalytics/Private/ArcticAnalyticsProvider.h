// Copyright 2017-2018 Project Borealis. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "ArcticAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "Interfaces/IAnalyticsProvider.h"

class Error;

class FAnalyticsProviderArcticAnalytics :
	public IAnalyticsProvider
{
	/** Path where analytics files are saved out */
	FString AnalyticsFilePath;
	/** Tracks whether we need to start the session or restart it */
	bool bHasSessionStarted;
	/** Whether an event was written before or not */
	bool bHasWrittenFirstEvent;
	/** Id representing the user the analytics are recording for */
	FString UserId;
	/** Unique Id representing the session the analytics are recording for */
	FString SessionId;
	/** Holds the Age if set */
	int32 Age;
	/** Holds the Location of the user if set */
	FString Location;
	/** Holds the Gender of the user if set */
	FString Gender;
	/** Holds the build info if set */
	FString BuildInfo;
	/** The file archive used to write the data */
	FArchive* FileArchive;

public:
	DECLARE_DELEGATE_OneParam(FHMACSecretDelegate, FString&);

	FAnalyticsProviderArcticAnalytics();
	virtual ~FAnalyticsProviderArcticAnalytics();

	virtual bool StartSession(const TArray<FAnalyticsEventAttribute>& Attributes) override;
	virtual void EndSession() override;
	virtual void FlushEvents() override;

	virtual void SetUserID(const FString& InUserID) override;
	virtual FString GetUserID() const override;

	virtual FString GetSessionID() const override;
	virtual bool SetSessionID(const FString& InSessionID) override;

	virtual void RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes) override;

	virtual void RecordItemPurchase(const FString& ItemId, const FString& Currency, int PerItemCost, int ItemQuantity) override;

	virtual void RecordCurrencyPurchase(const FString& GameCurrencyType, int GameCurrencyAmount, const FString& RealCurrencyType, float RealMoneyCost, const FString& PaymentProvider) override;

	virtual void RecordCurrencyGiven(const FString& GameCurrencyType, int GameCurrencyAmount) override;

	virtual void SetBuildInfo(const FString& InBuildInfo) override;
	virtual void SetGender(const FString& InGender) override;
	virtual void SetLocation(const FString& InLocation) override;
	virtual void SetAge(const int32 InAge) override;

	virtual void RecordItemPurchase(const FString& ItemId, int ItemQuantity, const TArray<FAnalyticsEventAttribute>& EventAttrs) override;
	virtual void RecordCurrencyPurchase(const FString& GameCurrencyType, int GameCurrencyAmount, const TArray<FAnalyticsEventAttribute>& EventAttrs) override;
	virtual void RecordCurrencyGiven(const FString& GameCurrencyType, int GameCurrencyAmount, const TArray<FAnalyticsEventAttribute>& EventAttrs) override;
	virtual void RecordError(const FString& Error, const TArray<FAnalyticsEventAttribute>& EventAttrs) override;
	virtual void RecordProgress(const FString& ProgressType, const FString& ProgressHierarchy, const TArray<FAnalyticsEventAttribute>& EventAttrs) override;

	FHMACSecretDelegate& GetHMACSecretDelegate()
	{
		static FHMACSecretDelegate HMACSecretDelegate;
		return HMACSecretDelegate;
	}

	void SendDataToServer();
};

typedef void(*THMACSecretFunc)(FString&);
	
void RegisterHMACSecretCallback(THMACSecretFunc InCallback)
{
	FAnalyticsProviderArcticAnalytics::GetHMACSecretDelegate().BindLambda([InCallback](FString& Secret)
	{
		InCallback(Secret);
	});
}

DECLARE_DELEGATE_OneParam(FArcticAnalyticsConfigSectionDelegate, FString&);
DECLARE_DELEGATE_OneParam(FArcticAnalyticsConfigKeyDelegate, FString&);
DECLARE_DELEGATE_OneParam(FArcticAnalyticsConfigFileDelegate, FString&);

static FArcticAnalyticsConfigSectionDelegate ConfigSectionDelegate;
static FArcticAnalyticsConfigKeyDelegate ConfigKeyDelegate;
static FArcticAnalyticsConfigFileDelegate ConfigFileDelegate;

void RegisterArcticAnalyticsConfigSection()
{
	ConfigSectionDelegate.BindLambda([InCallback](FString& ConfigSection)
	{
		ConfigSection = TEXT("/Script/ArcticAnalytics.Settings");
	});
}

void RegisterArcticAnalyticsConfigKey()
{
	ConfigKeyDelegate.BindLambda([InCallback](FString& ConfigKey)
	{
		ConfigKey = TEXT("Secret");
	});
}

void RegisterArcticAnalyticsConfigFile()
{
	ConfigFileDelegate.BindLambda([InCallback](FString& ConfigFile)
	{
		ConfigFile = TEXT("%sDefaultEngine.ini");
	});
}

struct FArcticAnalyticsConfigRegistration
{
	FArcticAnalyticsConfigRegistration()
	{
		RegisterArcticAnalyticsConfigSection();
		RegisterArcticAnalyticsConfigKey();
		RegisterArcticAnalyticsConfigFile();
	}
} GArcticAnalyticsConfigRegistration;

struct FHMACKeyRegistration
{
	FHMACKeyRegistration()
	{
		RegisterHMACSecretCallback(&Callback);
	}
	
	static void Callback(FString& Secret)
	{
		FString Section;
		ConfigSectionDelegate.ExecuteIfBound(Section);
		FString Key;
		ConfigKeyDelegate.ExecuteIfBound(Key);
		FString File;
		ConfigFileDelegate.ExecuteIfBound(File);
		if (Section.IsEmpty() || Key.IsEmpty() || File.IsEmpty()
			!GConfig->GetString(Section, Key, Secret,
								FString::Printf(File, *FPaths::SourceConfigDir())))
		{
			return;
		}
	}
} GArcticAnalyticsHMACKeyRegistration;