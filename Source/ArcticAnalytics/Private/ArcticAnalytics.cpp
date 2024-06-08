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

FAnalyticsProviderArcticAnalytics::FAnalyticsProviderArcticAnalytics() : bHasSessionStarted(false), bHasWrittenFirstEvent(false), Age(0), FileWriter(nullptr)
{
	AnalyticsFilePath = FPaths::ProjectSavedDir() / TEXT("Analytics");
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
	const FString FilePath = AnalyticsFilePath / (SessionId + TEXT(".analytics"));
	// Close the old file and open a new one
	FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*FilePath, FILEWRITE_EvenIfReadOnly));
	if (FileWriter)
	{
		FileWriter->Logf(TEXT("{"));
		FileWriter->Logf(TEXT("\t\"sessionId\" : \"%s\","), *SessionId);
		FileWriter->Logf(TEXT("\t\"userId\" : \"%s\","), *UserId);
		if (BuildInfo.Len() > 0)
		{
			FileWriter->Logf(TEXT("\t\"buildInfo\" : \"%s\","), *BuildInfo);
		}
		if (Age != 0)
		{
			FileWriter->Logf(TEXT("\t\"age\" : %d,"), Age);
		}
		if (Gender.Len() > 0)
		{
			FileWriter->Logf(TEXT("\t\"gender\" : \"%s\","), *Gender);
		}
		if (Location.Len() > 0)
		{
			FileWriter->Logf(TEXT("\t\"location\" : \"%s\","), *Location);
		}
		FileWriter->Logf(TEXT("\t\"events\" : ["));
		bHasSessionStarted = true;
		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Session created file (%s) for user (%s)"), *FilePath, *UserId);
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::StartSession failed to create file to log analytics events to"));
	}
	return bHasSessionStarted;
}

void FAnalyticsProviderArcticAnalytics::EndSession()
{
	if (FileWriter)
	{
		FileWriter->Logf(TEXT("\t]"));
		FileWriter->Logf(TEXT("}"));
		FileWriter->Flush();
		FileWriter->Close();
		SendDataToServer();
		FileWriter = nullptr;
		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Session ended for user (%s) and session id (%s)"), *UserId, *SessionId);
	}
	bHasWrittenFirstEvent = false;
	bHasSessionStarted = false;
}

void FAnalyticsProviderArcticAnalytics::FlushEvents()
{
	if (FileWriter)
	{
		FileWriter->Flush();
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
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	// Set analytics content
	FString AnalyticsPath = AnalyticsFilePath / (SessionId + TEXT(".analytics"));
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

void FAnalyticsProviderArcticAnalytics::SetDefaultEventAttributes(TArray<FAnalyticsEventAttribute>&& Attributes)
{
	DefaultEventAttributes = Attributes;
}

TArray<FAnalyticsEventAttribute> FAnalyticsProviderArcticAnalytics::GetDefaultEventAttributesSafe() const
{
	return DefaultEventAttributes;
}

int32 FAnalyticsProviderArcticAnalytics::GetDefaultEventAttributeCount() const
{
	return DefaultEventAttributes.Num();
}

FAnalyticsEventAttribute FAnalyticsProviderArcticAnalytics::GetDefaultEventAttribute(int AttributeIndex) const
{
	return DefaultEventAttributes[AttributeIndex];
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

void FAnalyticsProviderArcticAnalytics::SetDefaultEventAttributes(TArray<FAnalyticsEventAttribute>&& Attributes)
{
	DefaultEventAttributes = Attributes;
}

TArray<FAnalyticsEventAttribute> FAnalyticsProviderArcticAnalytics::GetDefaultEventAttributesSafe() const
{
	return DefaultEventAttributes;
}

int32 FAnalyticsProviderArcticAnalytics::GetDefaultEventAttributeCount() const
{
	return DefaultEventAttributes.Num();
}

FAnalyticsEventAttribute FAnalyticsProviderArcticAnalytics::GetDefaultEventAttribute(int AttributeIndex) const
{
	return DefaultEventAttributes[AttributeIndex];
}

void FAnalyticsProviderArcticAnalytics::RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	static uint32 RecordId(0);

	if (bHasSessionStarted)
	{
		if (FileWriter)
		{
			TStringBuilder<1024> Builder;
			if (bHasWrittenFirstEvent)
			{
				Builder.Appendf(TEXT(","));
			}
			bHasWrittenFirstEvent = true;

			// Log event as JSON
			Builder.Appendf(TEXT("\t\t{\n"));
			Builder.Appendf(TEXT("\t\t\t\"EventName\": \"%s\""), *EventName);
			
			// Add the event timestamp field
			Builder.Appendf(TEXT(",\n\t\t\t\"TimestampUTC\": \"%s\""), FDateTime::UtcNow().ToUnixTimestampDecimal());
			
			// Add the record Id and increment it
			Builder.Appendf(TEXT(",\n\t\t\t\"RecordId\": \"%s\""), RecordId++);

			// Accumulate all the attributes together. We could have had two loops but this seems cleaner
			TArray<FAnalyticsEventAttribute> EventAttributes(DefaultEventAttributes);
			EventAttributes.Append(Attributes);

			// Add all the attributes
			for (const FAnalyticsEventAttribute& Attribute : EventAttributes)
			{
				// This should be almost nearly true, but we should check and JSON'ify as needed
				if (Attribute.IsJsonFragment())
				{
					Builder.Appendf(TEXT(",\n\t\t\t\"%s\":%s"), *Attribute.GetName(), *Attribute.GetValue());
				}
				else
				{
					Builder.Appendf(TEXT(",\n\t\t\t\"%s\":\"%s\""), *Attribute.GetName(), *Attribute.GetValue());
				}
			}

			Builder.Appendf(TEXT("\n\t\t}"));

			FileWriter->Logf(TEXT("%s"), Builder.ToString());

			UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Analytics event (%s) written with (%d) attributes"), *EventName, Attributes.Num());
		}
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
		check(FileWriter);

		if (bHasWrittenFirstEvent)
		{
			FileWriter->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileWriter->Logf(TEXT("\t\t{"));
		FileWriter->Logf(TEXT("\t\t\t\"eventName\" : \"recordItemPurchase\","));

		FileWriter->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileWriter->Logf(TEXT("\t\t\t["));

		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"itemId\", \t\"value\" : \"%s\" },"), *ItemId);
		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"currency\", \t\"value\" : \"%s\" },"), *Currency);
		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"perItemCost\", \t\"value\" : \"%d\" },"), PerItemCost);
		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"itemQuantity\", \t\"value\" : \"%d\" }"), ItemQuantity);

		FileWriter->Logf(TEXT("\t\t\t]"));

		FileWriter->Logf(TEXT("\t\t}"));

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
		check(FileWriter);

		if (bHasWrittenFirstEvent)
		{
			FileWriter->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileWriter->Logf(TEXT("\t\t{"));
		FileWriter->Logf(TEXT("\t\t\t\"eventName\" : \"recordCurrencyPurchase\","));

		FileWriter->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileWriter->Logf(TEXT("\t\t\t["));

		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"gameCurrencyType\", \t\"value\" : \"%s\" },"), *GameCurrencyType);
		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"gameCurrencyAmount\", \t\"value\" : \"%d\" },"), GameCurrencyAmount);
		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"realCurrencyType\", \t\"value\" : \"%s\" },"), *RealCurrencyType);
		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"realMoneyCost\", \t\"value\" : \"%f\" },"), RealMoneyCost);
		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"paymentProvider\", \t\"value\" : \"%s\" }"), *PaymentProvider);

		FileWriter->Logf(TEXT("\t\t\t]"));

		FileWriter->Logf(TEXT("\t\t}"));

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
		check(FileWriter);

		if (bHasWrittenFirstEvent)
		{
			FileWriter->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileWriter->Logf(TEXT("\t\t{"));
		FileWriter->Logf(TEXT("\t\t\t\"eventName\" : \"recordCurrencyGiven\","));

		FileWriter->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileWriter->Logf(TEXT("\t\t\t["));

		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"gameCurrencyType\", \t\"value\" : \"%s\" },"), *GameCurrencyType);
		FileWriter->Logf(TEXT("\t\t\t\t{ \"name\" : \"gameCurrencyAmount\", \t\"value\" : \"%d\" }"), GameCurrencyAmount);

		FileWriter->Logf(TEXT("\t\t\t]"));

		FileWriter->Logf(TEXT("\t\t}"));

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
		check(FileWriter);

		if (bHasWrittenFirstEvent)
		{
			FileWriter->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileWriter->Logf(TEXT("\t\t{"));
		FileWriter->Logf(TEXT("\t\t\t\"error\" : \"%s\","), *Error);

		FileWriter->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileWriter->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileWriter->Logf(TEXT("\t\t\t,"));
			}
			FileWriter->Logf(TEXT("\t\t\t{"));
			FileWriter->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileWriter->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileWriter->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileWriter->Logf(TEXT("\t\t\t]"));

		FileWriter->Logf(TEXT("\t\t}"));

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
		check(FileWriter);

		if (bHasWrittenFirstEvent)
		{
			FileWriter->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileWriter->Logf(TEXT("\t\t{"));
		FileWriter->Logf(TEXT("\t\t\t\"eventType\" : \"Progress\","));
		FileWriter->Logf(TEXT("\t\t\t\"progressType\" : \"%s\","), *ProgressType);
		FileWriter->Logf(TEXT("\t\t\t\"progressName\" : \"%s\","), *ProgressName);

		FileWriter->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileWriter->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileWriter->Logf(TEXT("\t\t\t,"));
			}
			FileWriter->Logf(TEXT("\t\t\t{"));
			FileWriter->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileWriter->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileWriter->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileWriter->Logf(TEXT("\t\t\t]"));

		FileWriter->Logf(TEXT("\t\t}"));

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
		check(FileWriter);

		if (bHasWrittenFirstEvent)
		{
			FileWriter->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileWriter->Logf(TEXT("\t\t{"));
		FileWriter->Logf(TEXT("\t\t\t\"eventType\" : \"ItemPurchase\","));
		FileWriter->Logf(TEXT("\t\t\t\"itemId\" : \"%s\","), *ItemId);
		FileWriter->Logf(TEXT("\t\t\t\"itemQuantity\" : %d,"), ItemQuantity);

		FileWriter->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileWriter->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileWriter->Logf(TEXT("\t\t\t,"));
			}
			FileWriter->Logf(TEXT("\t\t\t{"));
			FileWriter->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileWriter->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileWriter->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileWriter->Logf(TEXT("\t\t\t]"));

		FileWriter->Logf(TEXT("\t\t}"));

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
		check(FileWriter);

		if (bHasWrittenFirstEvent)
		{
			FileWriter->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileWriter->Logf(TEXT("\t\t{"));
		FileWriter->Logf(TEXT("\t\t\t\"eventType\" : \"CurrencyPurchase\","));
		FileWriter->Logf(TEXT("\t\t\t\"gameCurrencyType\" : \"%s\","), *GameCurrencyType);
		FileWriter->Logf(TEXT("\t\t\t\"gameCurrencyAmount\" : %d,"), GameCurrencyAmount);

		FileWriter->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileWriter->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileWriter->Logf(TEXT("\t\t\t,"));
			}
			FileWriter->Logf(TEXT("\t\t\t{"));
			FileWriter->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileWriter->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileWriter->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileWriter->Logf(TEXT("\t\t\t]"));

		FileWriter->Logf(TEXT("\t\t}"));

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
		check(FileWriter);

		if (bHasWrittenFirstEvent)
		{
			FileWriter->Logf(TEXT("\t\t,"));
		}
		bHasWrittenFirstEvent = true;

		FileWriter->Logf(TEXT("\t\t{"));
		FileWriter->Logf(TEXT("\t\t\t\"eventType\" : \"CurrencyGiven\","));
		FileWriter->Logf(TEXT("\t\t\t\"gameCurrencyType\" : \"%s\","), *GameCurrencyType);
		FileWriter->Logf(TEXT("\t\t\t\"gameCurrencyAmount\" : %d,"), GameCurrencyAmount);

		FileWriter->Logf(TEXT("\t\t\t\"attributes\" :"));
		FileWriter->Logf(TEXT("\t\t\t["));
		bool bHasWrittenFirstAttr = false;
		// Write out the list of attributes as an array of attribute objects
		for (auto Attr : Attributes)
		{
			if (bHasWrittenFirstAttr)
			{
				FileWriter->Logf(TEXT("\t\t\t,"));
			}
			FileWriter->Logf(TEXT("\t\t\t{"));
			FileWriter->Logf(TEXT("\t\t\t\t\"name\" : \"%s\","), *Attr.GetName());
			FileWriter->Logf(TEXT("\t\t\t\t\"value\" : \"%s\""), *Attr.GetValue());
			FileWriter->Logf(TEXT("\t\t\t}"));
			bHasWrittenFirstAttr = true;
		}
		FileWriter->Logf(TEXT("\t\t\t]"));

		FileWriter->Logf(TEXT("\t\t}"));

		UE_LOG(LogArcticAnalyticsAnalytics, Display, TEXT("Currency given type (%s), quantity (%d), number of attributes is (%d)"), *GameCurrencyType,
			   GameCurrencyAmount, Attributes.Num());
	}
	else
	{
		UE_LOG(LogArcticAnalyticsAnalytics, Warning, TEXT("FAnalyticsProviderArcticAnalytics::RecordCurrencyGiven called before StartSession. Ignoring."));
	}
}
