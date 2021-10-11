// Copyright 2017-2018 Project Borealis. All rights reserved.

#include "ArcticAnalytics.h"
#include "AnalyticsEventAttribute.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Runtime/Online/HTTP/Public/Http.h"

#include "Data_SHA256.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcticAnalyticsAnalytics, Display, All);

IMPLEMENT_MODULE(FAnalyticsArcticAnalytics, ArcticAnalytics)

void FAnalyticsArcticAnalytics::StartupModule()
{
	ArcticAnalyticsProvider = MakeShareable(new FAnalyticsProviderArcticAnalytics());
}

void FAnalyticsArcticAnalytics::ShutdownModule()
{
	if (ArcticAnalyticsProvider.IsValid())
	{
		ArcticAnalyticsProvider->EndSession();
	}
}

TSharedPtr<IAnalyticsProvider> FAnalyticsArcticAnalytics::CreateAnalyticsProvider(const FAnalyticsProviderConfigurationDelegate& GetConfigValue) const
{
	return ArcticAnalyticsProvider;
}

// Provider

FAnalyticsProviderArcticAnalytics::FAnalyticsProviderArcticAnalytics() : bHasSessionStarted(false), bHasWrittenFirstEvent(false), Age(0), FileArchive(nullptr)
{
	FileArchive = nullptr;
	AnalyticsFilePath = FPaths::ProjectSavedDir() + TEXT("Analytics/");
	UserId = FGuid::NewGuid().ToString();
}

FAnalyticsProviderArcticAnalytics::~FAnalyticsProviderArcticAnalytics()
{
	if (bHasSessionStarted)
	{
		EndSession();
	}
}

bool FAnalyticsProviderArcticAnalytics::StartSession(const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasSessionStarted)
	{
		EndSession();
	}
	SessionId = UserId + TEXT("-") + FDateTime::UtcNow().ToString();
	const FString FileName = AnalyticsFilePath + SessionId + TEXT(".analytics");
	// Close the old file and open a new one
	FileArchive = IFileManager::Get().CreateFileWriter(*FileName);
	if (FileArchive != nullptr)
	{
		FileArchive->Logf(TEXT("{"));
		FileArchive->Logf(TEXT("\t\"sessionId\" : \"%s\","), *SessionId);
		FileArchive->Logf(TEXT("\t\"userId\" : \"%s\","), *UserId);
		if (BuildInfo.Len() > 0)
		{
			FileArchive->Logf(TEXT("\t\"buildInfo\" : \"%s\","), *BuildInfo);
		}
		if (Age != 0)
		{
			FileArchive->Logf(TEXT("\t\"age\" : %d,"), Age);
		}
		if (Gender.Len() > 0)
		{
			FileArchive->Logf(TEXT("\t\"gender\" : \"%s\","), *Gender);
		}
		if (Location.Len() > 0)
		{
			FileArchive->Logf(TEXT("\t\"location\" : \"%s\","), *Location);
		}
		FileArchive->Logf(TEXT("\t\"events\" : ["));
		bHasSessionStarted = true;
		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Session created file (%s) for user (%s)"), *FileName, *UserId);
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::StartSession failed to create file to log analytics events to"));
	}
	return bHasSessionStarted;
}

void FAnalyticsProviderArcticAnalytics::EndSession()
{
	if (FileArchive != nullptr)
	{
		FileArchive->Logf(TEXT("\t]"));
		FileArchive->Logf(TEXT("}"));
		FileArchive->Flush();
		FileArchive->Close();
		SendDataToServer();
		delete FileArchive;
		FileArchive = nullptr;
		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Session ended for user (%s) and session id (%s)"), *UserId, *SessionId);
	}
	bHasWrittenFirstEvent = false;
	bHasSessionStarted = false;
}

void FAnalyticsProviderArcticAnalytics::FlushEvents()
{
	if (FileArchive != nullptr)
	{
		FileArchive->Flush();
		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Analytics file flushed"));
	}
}

void FAnalyticsProviderArcticAnalytics::SendDataToServer()
{
	// Get configured server
	FString ConfigServer;
	if (!GConfig->GetString(TEXT("/Script/ArcticAnalytics.Settings"), TEXT("Server"), ConfigServer,
							FString::Printf(TEXT("%sDefaultEngine.ini"), *FPaths::SourceConfigDir())))
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Error, TEXT("Server not configured! Can't send data to server."));
		return;
	}
	// Get configured secret
	FString ConfigSecret;
	if (!GConfig->GetString(TEXT("/Script/ArcticAnalytics.Settings"), TEXT("Secret"), ConfigSecret,
							FString::Printf(TEXT("%sDefaultEngine.ini"), *FPaths::SourceConfigDir())))
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Error, TEXT("Secret not configured! Can't send data to server."));
		return;
	}
	// Create the request
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	// Set endpoint
	Request->SetURL(ConfigServer);
	// Set headers
	Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accepts"), TEXT("application/json"));
	// Set analytics content
	FString AnalyticsPath = AnalyticsFilePath + SessionId + TEXT(".analytics");
	FString AnalyticsJson;
	if (!FFileHelper::LoadFileToString(AnalyticsJson, *AnalyticsPath))
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Error, TEXT("Session could not be loaded! Can't send data to server."));
		return;
	}
	// HMAC for auth header
	SHA256Key Hash = HMAC_SHA256::Hash(ConfigSecret, AnalyticsJson);
	Request->SetHeader(TEXT("Authorization"), Hash.ToHexString());
	// POST request
	Request->SetVerb("POST");
	Request->SetContentAsString(AnalyticsJson);
	Request->ProcessRequest();
}

void FAnalyticsProviderArcticAnalytics::SetUserID(const FString& InUserID)
{
	if (!bHasSessionStarted)
	{
		UserId = InUserID;
		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("User is now (%s)"), *UserId);
	}
	else
	{
		// Log that we shouldn't switch users during a session
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::SetUserID called while a session is in progress. Ignoring."));
	}
}

FString FAnalyticsProviderArcticAnalytics::GetUserID() const
{
	return UserId;
}

FString FAnalyticsProviderArcticAnalytics::GetSessionID() const
{
	return SessionId;
}

bool FAnalyticsProviderArcticAnalytics::SetSessionID(const FString& InSessionID)
{
	if (!bHasSessionStarted)
	{
		SessionId = InSessionID;
		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Session is now (%s)"), *SessionId);
	}
	else
	{
		// Log that we shouldn't switch session ids during a session
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::SetSessionID called while a session is in progress. Ignoring."));
	}
	return !bHasSessionStarted;
}

void FAnalyticsProviderArcticAnalytics::RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT(","));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"eventName\" : \"%s\""), *EventName);
		if (Attributes.Num() > 0)
		{
			FileArchive->Logf(TEXT(",\t\t\t\"attributes\" : ["));
			bool bHasWrittenFirstAttr = false;
			// Write out the list of attributes as an array of attribute objects
			for (auto Attr : Attributes)
			{
				if (bHasWrittenFirstAttr)
				{
					FileArchive->Logf(TEXT("\t\t\t,"));
				}
				FileArchive->Logf(TEXT("\t\t\t{"));
				FileArchive->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
				FileArchive->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
				FileArchive->Logf(TEXT("\t\t\t}"));
				bHasWrittenFirstAttr = true;
			}
			FileArchive->Logf(TEXT("\t\t\t]"));
		}
		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Analytics event (%s) written with (%d) attributes"), *EventName, Attributes.Num());
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordEvent called before StartSession. Ignoring."));
	}
}

void FAnalyticsProviderArcticAnalytics::RecordItemPurchase(const FString& ItemId, const FString& Currency, int PerItemCost, int ItemQuantity)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"eventName\" : \"recordItemPurchase\","));

		FileArchive->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileArchive->Logf(TEXT("\t\t\t["));

		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"itemId\", \t\"value\" : \"%s\" },"), *ItemId);
		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"currency\", \t\"value\" : \"%s\" },"), *Currency);
		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"perItemCost\", \t\"value\" : \"%d\" },"), PerItemCost);
		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"itemQuantity\", \t\"value\" : \"%d\" }"), ItemQuantity);

		FileArchive->Logf(TEXT("\t\t\t]"));

		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("(%d) number of item (%s) purchased with (%s) at a cost of (%d) each"), ItemQuantity, *ItemId, *Currency, PerItemCost);
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordItemPurchase called before StartSession. Ignoring."));
	}
}

void FAnalyticsProviderArcticAnalytics::RecordCurrencyPurchase(const FString& GameCurrencyType, int GameCurrencyAmount, const FString& RealCurrencyType,
															   float RealMoneyCost, const FString& PaymentProvider)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"eventName\" : \"recordCurrencyPurchase\","));

		FileArchive->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileArchive->Logf(TEXT("\t\t\t["));

		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"gameCurrencyType\", \t\"value\" : \"%s\" },"), *GameCurrencyType);
		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"gameCurrencyAmount\", \t\"value\" : \"%d\" },"), GameCurrencyAmount);
		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"realCurrencyType\", \t\"value\" : \"%s\" },"), *RealCurrencyType);
		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"realMoneyCost\", \t\"value\" : \"%f\" },"), RealMoneyCost);
		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"paymentProvider\", \t\"value\" : \"%s\" }"), *PaymentProvider);

		FileArchive->Logf(TEXT("\t\t\t]"));

		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("(%d) amount of in game currency (%s) purchased with (%s) at a cost of (%f) each"),
			   GameCurrencyAmount, *GameCurrencyType, *RealCurrencyType, RealMoneyCost);
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordCurrencyPurchase called before StartSession. Ignoring."));
	}
}

void FAnalyticsProviderArcticAnalytics::RecordCurrencyGiven(const FString& GameCurrencyType, int GameCurrencyAmount)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"eventName\" : \"recordCurrencyGiven\","));

		FileArchive->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileArchive->Logf(TEXT("\t\t\t["));

		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"gameCurrencyType\", \t\"value\" : \"%s\" },"), *GameCurrencyType);
		FileArchive->Logf(TEXT("\t\t\t\t{ \"name\" : \"gameCurrencyAmount\", \t\"value\" : \"%d\" }"), GameCurrencyAmount);

		FileArchive->Logf(TEXT("\t\t\t]"));

		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("(%d) amount of in game currency (%s) given to user"), GameCurrencyAmount, *GameCurrencyType);
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordCurrencyGiven called before StartSession. Ignoring."));
	}
}

void FAnalyticsProviderArcticAnalytics::SetAge(int InAge)
{
	Age = InAge;
}

void FAnalyticsProviderArcticAnalytics::SetLocation(const FString& InLocation)
{
	Location = InLocation;
}

void FAnalyticsProviderArcticAnalytics::SetGender(const FString& InGender)
{
	Gender = InGender;
}

void FAnalyticsProviderArcticAnalytics::SetBuildInfo(const FString& InBuildInfo)
{
	BuildInfo = InBuildInfo;
}

void FAnalyticsProviderArcticAnalytics::RecordError(const FString& Error, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"error\" : \"%s\","), *Error);

		FileArchive->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileArchive->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileArchive->Logf(TEXT("\t\t\t,"));
			}
			FileArchive->Logf(TEXT("\t\t\t{"));
			FileArchive->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileArchive->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileArchive->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileArchive->Logf(TEXT("\t\t\t]"));

		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Error is (%s) number of attributes is (%d)"), *Error, Attributes.Num());
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordError called before StartSession. Ignoring."));
	}
}

void FAnalyticsProviderArcticAnalytics::RecordProgress(const FString& ProgressType, const FString& ProgressName, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"eventType\" : \"Progress\","));
		FileArchive->Logf(TEXT("\t\t\t\"progressType\" : \"%s\","), *ProgressType);
		FileArchive->Logf(TEXT("\t\t\t\"progressName\" : \"%s\","), *ProgressName);

		FileArchive->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileArchive->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileArchive->Logf(TEXT("\t\t\t,"));
			}
			FileArchive->Logf(TEXT("\t\t\t{"));
			FileArchive->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileArchive->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileArchive->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileArchive->Logf(TEXT("\t\t\t]"));

		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Progress event is type (%s), named (%s), number of attributes is (%d)"), *ProgressType,
			   *ProgressName, Attributes.Num());
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordProgress called before StartSession. Ignoring."));
	}
}

void FAnalyticsProviderArcticAnalytics::RecordItemPurchase(const FString& ItemId, int ItemQuantity, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"eventType\" : \"ItemPurchase\","));
		FileArchive->Logf(TEXT("\t\t\t\"itemId\" : \"%s\","), *ItemId);
		FileArchive->Logf(TEXT("\t\t\t\"itemQuantity\" : %d,"), ItemQuantity);

		FileArchive->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileArchive->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileArchive->Logf(TEXT("\t\t\t,"));
			}
			FileArchive->Logf(TEXT("\t\t\t{"));
			FileArchive->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileArchive->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileArchive->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileArchive->Logf(TEXT("\t\t\t]"));

		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Item purchase id (%s), quantity (%d), number of attributes is (%d)"), *ItemId, ItemQuantity,
			   Attributes.Num());
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordItemPurchase called before StartSession. Ignoring."));
	}
}

void FAnalyticsProviderArcticAnalytics::RecordCurrencyPurchase(const FString& GameCurrencyType, int GameCurrencyAmount, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"eventType\" : \"CurrencyPurchase\","));
		FileArchive->Logf(TEXT("\t\t\t\"gameCurrencyType\" : \"%s\","), *GameCurrencyType);
		FileArchive->Logf(TEXT("\t\t\t\"gameCurrencyAmount\" : %d,"), GameCurrencyAmount);

		FileArchive->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileArchive->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileArchive->Logf(TEXT("\t\t\t,"));
			}
			FileArchive->Logf(TEXT("\t\t\t{"));
			FileArchive->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileArchive->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileArchive->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileArchive->Logf(TEXT("\t\t\t]"));

		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Currency purchase type (%s), quantity (%d), number of attributes is (%d)"), *GameCurrencyType,
			   GameCurrencyAmount, Attributes.Num());
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordCurrencyPurchase called before StartSession. Ignoring."));
	}
}

void FAnalyticsProviderArcticAnalytics::RecordCurrencyGiven(const FString& GameCurrencyType, int GameCurrencyAmount, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasSessionStarted)
	{
		check(FileArchive != nullptr);

		if (bHasWrittenFirstEvent)
		{
			FileArchive->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileArchive->Logf(TEXT("\t\t{"));
		FileArchive->Logf(TEXT("\t\t\t\"eventType\" : \"CurrencyGiven\","));
		FileArchive->Logf(TEXT("\t\t\t\"gameCurrencyType\" : \"%s\","), *GameCurrencyType);
		FileArchive->Logf(TEXT("\t\t\t\"gameCurrencyAmount\" : %d,"), GameCurrencyAmount);

		FileArchive->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileArchive->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileArchive->Logf(TEXT("\t\t\t,"));
			}
			FileArchive->Logf(TEXT("\t\t\t{"));
			FileArchive->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileArchive->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileArchive->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileArchive->Logf(TEXT("\t\t\t]"));

		FileArchive->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Currency given type (%s), quantity (%d), number of attributes is (%d)"), *GameCurrencyType,
			   GameCurrencyAmount, Attributes.Num());
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordCurrencyGiven called before StartSession. Ignoring."));
	}
}
