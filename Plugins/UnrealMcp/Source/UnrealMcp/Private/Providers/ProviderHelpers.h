#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpSettings.h"

namespace UnrealMcp
{
	bool LoadJsonObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject);
	FString JsonObjectToString(const TSharedPtr<FJsonObject>& JsonObject);

	namespace Providers
	{
		inline FString ProviderIdForError(const FAiProviderConfig& Config)
		{
			const FString Id = Config.Id.TrimStartAndEnd();
			return Id.IsEmpty() ? TEXT("<unnamed>") : Id;
		}

		inline FString BytesToString(const TArray<uint8>& Bytes)
		{
			if (Bytes.IsEmpty()) { return FString(); }
			const FUTF8ToTCHAR Converter(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
			return FString(Converter.Length(), Converter.Get());
		}

		inline FString SerializeToolResult(const FUnrealMcpExecutionResult& ToolResult)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("text"), ToolResult.Text);
			Object->SetBoolField(TEXT("is_error"), ToolResult.bIsError);
			if (ToolResult.StructuredContent.IsValid()) { Object->SetObjectField(TEXT("structured_content"), ToolResult.StructuredContent); }
			return UnrealMcp::JsonObjectToString(Object);
		}
	}
}
