#include "UnrealMcpModule.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "ContentBrowserModule.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "EditorScriptingHelpers.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Commands/UIAction.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformOutputDevices.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "HttpRequestHandler.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IPythonScriptPlugin.h"
#include "IContentBrowserSingleton.h"
#include "IHttpRouter.h"
#include "JsonObjectConverter.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompilerModule.h"
#include "Logging/MessageLog.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Crc.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"
#include "PlayInEditorDataTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Templates/Atomic.h"
#include "ToolMenus.h"
#include "UnrealMcpChatPanel.h"
#include "UnrealMcpAssistantRun.h"
#include "UnrealMcpActorTools.h"
#include "UnrealMcpBlueprintTools.h"
#include "UnrealMcpEditorTools.h"
#include "UnrealMcpMemoryTools.h"
#include "UnrealMcpScaffoldTools.h"
#include "UnrealMcpSelfExtensionTools.h"
#include "UnrealMcpSettings.h"
#include "UnrealMcpSkillTools.h"
#include "UnrealMcpToolExecutionGuard.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpWorkbenchPanel.h"
#include "UnrealMcpWidgetTools.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "UnrealMcp"

DEFINE_LOG_CATEGORY(LogUnrealMcp);

namespace UnrealMcp
{
	static const FName ChatTabName(TEXT("UnrealMcp.Chat"));
	static const FName WorkbenchTabName(TEXT("UnrealMcp.Workbench"));
	static const FString LatestProtocolVersion = TEXT("2025-06-18");
	static const FString LegacyProtocolVersion = TEXT("2025-03-26");
	static constexpr double GameThreadTimeoutSeconds = 30.0;
	static constexpr int32 DefaultListLimit = 200;

	void ApplyAiHttpTimeoutOverrides(const UUnrealMcpSettings& Settings)
	{
		if (!GConfig)
		{
			return;
		}

		const float DesiredTotalTimeout = FMath::Max(10.0f, Settings.AiRequestTimeoutSeconds);
		const float DesiredActivityTimeout = FMath::Max(10.0f, Settings.AiRequestActivityTimeoutSeconds);
		const float DesiredConnectionTimeout = FMath::Max(DesiredTotalTimeout, DesiredActivityTimeout);

		GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpTotalTimeout"), DesiredTotalTimeout, GEngineIni);
		GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpConnectionTimeout"), DesiredConnectionTimeout, GEngineIni);
		GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpActivityTimeout"), DesiredActivityTimeout, GEngineIni);
		GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpReceiveTimeout"), DesiredConnectionTimeout, GEngineIni);
		GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpSendTimeout"), DesiredConnectionTimeout, GEngineIni);
		FHttpModule::Get().UpdateConfigs();
	}

	bool CanBindLocalTcpPort(int32 Port)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SocketSubsystem)
		{
			return true;
		}

		bool bIsValidAddress = false;
		const TSharedRef<FInternetAddr> LocalhostAddress = SocketSubsystem->CreateInternetAddr();
		LocalhostAddress->SetIp(TEXT("127.0.0.1"), bIsValidAddress);
		LocalhostAddress->SetPort(Port);
		if (!bIsValidAddress)
		{
			return true;
		}

		FSocket* ProbeSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealMcpPortProbe"), false);
		if (!ProbeSocket)
		{
			return true;
		}

		ProbeSocket->SetReuseAddr(false);
		const bool bCanBind = ProbeSocket->Bind(*LocalhostAddress);
		SocketSubsystem->DestroySocket(ProbeSocket);
		return bCanBind;
	}

	FString GetFirstHeaderValue(const FHttpServerRequest& Request, const FString& HeaderName)
	{
		for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
		{
			if (Header.Key.Equals(HeaderName, ESearchCase::IgnoreCase) && Header.Value.Num() > 0)
			{
				return Header.Value[0];
			}
		}

		return FString();
	}

	bool IsSupportedProtocolVersion(const FString& ProtocolVersion)
	{
		return ProtocolVersion.Equals(LatestProtocolVersion, ESearchCase::CaseSensitive)
			|| ProtocolVersion.Equals(LegacyProtocolVersion, ESearchCase::CaseSensitive);
	}

	FString NormalizeEndpointPath(const FString& EndpointPath)
	{
		FString Normalized = EndpointPath.TrimStartAndEnd();
		if (Normalized.IsEmpty())
		{
			Normalized = TEXT("/mcp");
		}

		if (!Normalized.StartsWith(TEXT("/")))
		{
			Normalized = TEXT("/") + Normalized;
		}

		return Normalized;
	}

	FString RequestBodyToString(const FHttpServerRequest& Request)
	{
		if (Request.Body.IsEmpty())
		{
			return FString();
		}

		FUTF8ToTCHAR Converter(reinterpret_cast<const UTF8CHAR*>(Request.Body.GetData()), Request.Body.Num());
		return FString(Converter.Length(), Converter.Get());
	}

	TSharedPtr<FJsonValue> MakeIdOrNull(const TSharedPtr<FJsonObject>& Message)
	{
		if (Message.IsValid() && Message->HasField(TEXT("id")))
		{
			return Message->TryGetField(TEXT("id"));
		}

		return MakeShared<FJsonValueNull>();
	}

	TSharedPtr<FJsonObject> MakeTextContentObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> ContentObject = MakeShared<FJsonObject>();
		ContentObject->SetStringField(TEXT("type"), TEXT("text"));
		ContentObject->SetStringField(TEXT("text"), Text);
		return ContentObject;
	}

	FString JsonObjectToString(const TSharedPtr<FJsonObject>& JsonObject)
	{
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
		return JsonString;
	}

	TSharedPtr<FJsonObject> MakeEmptyObject()
	{
		return MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> MakeExecutionStructuredMessage(const FString& Text)
	{
		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("message"), Text);
		return StructuredContent;
	}

	FUnrealMcpExecutionResult MakeExecutionResult(
		const FString& Text,
		const TSharedPtr<FJsonObject>& StructuredContent = nullptr,
		bool bIsError = false)
	{
		FUnrealMcpExecutionResult Result;
		Result.Text = Text;
		Result.StructuredContent = StructuredContent;
		Result.bIsError = bIsError;
		return Result;
	}

	bool TryGetMethodAndId(const FHttpServerRequest& Request, FString& OutMethod, TSharedPtr<FJsonValue>& OutId)
	{
		OutMethod.Reset();
		OutId = MakeShared<FJsonValueNull>();

		if (Request.Verb != EHttpServerRequestVerbs::VERB_POST)
		{
			return false;
		}

		const FString BodyString = RequestBodyToString(Request);
		if (BodyString.IsEmpty())
		{
			return false;
		}

		TSharedPtr<FJsonObject> MessageObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		if (!FJsonSerializer::Deserialize(Reader, MessageObject) || !MessageObject.IsValid())
		{
			return false;
		}

		OutId = MakeIdOrNull(MessageObject);
		return MessageObject->TryGetStringField(TEXT("method"), OutMethod);
	}

	TSharedPtr<FJsonObject> MakeObjectSchema()
	{
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		InputSchema->SetBoolField(TEXT("additionalProperties"), false);
		return InputSchema;
	}

		TSharedPtr<FJsonValue> NormalizeOpenAiSchemaValue(const TSharedPtr<FJsonValue>& Value);
		bool IsOpenAiSchemaCompatibleValue(const TSharedPtr<FJsonValue>& Value, FString& OutReason);
		bool TryGetStringArrayField(const FJsonObject& Arguments, const FString& FieldName, TArray<FString>& OutValues);

		TSharedPtr<FJsonObject> NormalizeOpenAiSchemaObject(const TSharedPtr<FJsonObject>& InputObject)
		{
			if (!InputObject.IsValid())
			{
				return MakeShared<FJsonObject>();
		}

		TSharedPtr<FJsonObject> OutputObject = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : InputObject->Values)
		{
			OutputObject->SetField(Pair.Key, NormalizeOpenAiSchemaValue(Pair.Value));
		}

			FString TypeString;
			if (OutputObject->TryGetStringField(TEXT("type"), TypeString) && TypeString == TEXT("object"))
			{
				const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
				if (!OutputObject->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject || !(*PropertiesObject).IsValid())
				{
					OutputObject->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
				}
			}
			else if (TypeString == TEXT("array"))
			{
				const TSharedPtr<FJsonObject>* ItemsObject = nullptr;
				if (OutputObject->TryGetObjectField(TEXT("items"), ItemsObject) && ItemsObject && (*ItemsObject).IsValid())
			{
				OutputObject->SetObjectField(TEXT("items"), NormalizeOpenAiSchemaObject(*ItemsObject));
			}
		}

			return OutputObject;
		}

		TSharedPtr<FJsonValue> NormalizeOpenAiSchemaValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return MakeShared<FJsonValueNull>();
		}

		if (Value->Type == EJson::Object && Value->AsObject().IsValid())
		{
			return MakeShared<FJsonValueObject>(NormalizeOpenAiSchemaObject(Value->AsObject()));
		}

		if (Value->Type == EJson::Array)
		{
			TArray<TSharedPtr<FJsonValue>> NormalizedArray;
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				NormalizedArray.Add(NormalizeOpenAiSchemaValue(Item));
			}
			return MakeShared<FJsonValueArray>(NormalizedArray);
		}

			return Value;
		}

		bool IsOpenAiSchemaCompatibleObject(const TSharedPtr<FJsonObject>& InputObject, FString& OutReason)
		{
			OutReason.Reset();
			if (!InputObject.IsValid())
			{
				return true;
			}

			FString TypeString;
			if (InputObject->TryGetStringField(TEXT("type"), TypeString) && TypeString == TEXT("object"))
			{
				bool bAllowsAdditionalProperties = false;
				if (InputObject->TryGetBoolField(TEXT("additionalProperties"), bAllowsAdditionalProperties) && bAllowsAdditionalProperties)
				{
					OutReason = TEXT("object schemas with additionalProperties=true are not accepted by the AI function interface");
					return false;
				}

				const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
				if (InputObject->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && (*PropertiesObject).IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertiesObject)->Values)
					{
						FString NestedReason;
						if (!IsOpenAiSchemaCompatibleValue(Pair.Value, NestedReason))
						{
							OutReason = FString::Printf(TEXT("property `%s` is incompatible: %s"), *Pair.Key, *NestedReason);
							return false;
						}
					}
				}
			}
			else if (TypeString == TEXT("array"))
			{
				const TSharedPtr<FJsonObject>* ItemsObject = nullptr;
				if (InputObject->TryGetObjectField(TEXT("items"), ItemsObject) && ItemsObject && (*ItemsObject).IsValid())
				{
					FString NestedReason;
					if (!IsOpenAiSchemaCompatibleObject(*ItemsObject, NestedReason))
					{
						OutReason = FString::Printf(TEXT("array items are incompatible: %s"), *NestedReason);
						return false;
					}
				}
			}

			return true;
		}

		bool IsOpenAiSchemaCompatibleValue(const TSharedPtr<FJsonValue>& Value, FString& OutReason)
		{
			OutReason.Reset();
			if (!Value.IsValid())
			{
				return true;
			}

			if (Value->Type == EJson::Object)
			{
				return IsOpenAiSchemaCompatibleObject(Value->AsObject(), OutReason);
			}

			if (Value->Type == EJson::Array)
			{
				for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
				{
					FString NestedReason;
					if (!IsOpenAiSchemaCompatibleValue(Item, NestedReason))
					{
						OutReason = NestedReason;
						return false;
					}
				}
			}

			return true;
		}

		FString GetJsonStringAtPath(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> PathSegments)
		{
			if (!Object.IsValid())
			{
				return FString();
			}

			TSharedPtr<FJsonObject> CurrentObject = Object;
			for (auto It = PathSegments.begin(); It != PathSegments.end(); ++It)
			{
				const bool bIsLast = (It + 1) == PathSegments.end();
				if (bIsLast)
				{
					FString Value;
					if (CurrentObject.IsValid() && CurrentObject->TryGetStringField(*It, Value))
					{
						return Value;
					}

					return FString();
				}

				const TSharedPtr<FJsonObject>* NextObject = nullptr;
				if (!CurrentObject.IsValid()
					|| !CurrentObject->TryGetObjectField(*It, NextObject)
					|| !NextObject
					|| !(*NextObject).IsValid())
				{
					return FString();
				}

				CurrentObject = *NextObject;
			}

			return FString();
		}

		FString ExtractOpenAiResponseFailureDetails(const TSharedPtr<FJsonObject>& ResponseObject)
		{
			const FString ErrorMessage = GetJsonStringAtPath(ResponseObject, { TEXT("error"), TEXT("message") });
			if (!ErrorMessage.IsEmpty())
			{
				return ErrorMessage;
			}

			const FString NestedStatusErrorMessage = GetJsonStringAtPath(ResponseObject, { TEXT("status_details"), TEXT("error"), TEXT("message") });
			if (!NestedStatusErrorMessage.IsEmpty())
			{
				return NestedStatusErrorMessage;
			}

			const FString IncompleteReason = GetJsonStringAtPath(ResponseObject, { TEXT("incomplete_details"), TEXT("reason") });
			if (!IncompleteReason.IsEmpty())
			{
				return IncompleteReason;
			}

			const FString StatusReason = GetJsonStringAtPath(ResponseObject, { TEXT("status_details"), TEXT("reason") });
			if (!StatusReason.IsEmpty())
			{
				return StatusReason;
			}

			const FString StatusMessage = GetJsonStringAtPath(ResponseObject, { TEXT("status_details"), TEXT("message") });
			if (!StatusMessage.IsEmpty())
			{
				return StatusMessage;
			}

			return FString();
		}

	TSharedPtr<FJsonObject> MakeStringProperty(const FString& Description, const FString& DefaultValue = FString())
	{
		TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), TEXT("string"));
		Property->SetStringField(TEXT("description"), Description);
		if (!DefaultValue.IsEmpty())
		{
			Property->SetStringField(TEXT("default"), DefaultValue);
		}
		return Property;
	}

	TSharedPtr<FJsonObject> MakeBoolProperty(const FString& Description, bool bDefaultValue)
	{
		TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), TEXT("boolean"));
		Property->SetStringField(TEXT("description"), Description);
		Property->SetBoolField(TEXT("default"), bDefaultValue);
		return Property;
	}

		TSharedPtr<FJsonObject> MakeNumberProperty(const FString& Description, double DefaultValue)
		{
			TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
			Property->SetStringField(TEXT("type"), TEXT("number"));
			Property->SetStringField(TEXT("description"), Description);
			Property->SetNumberField(TEXT("default"), DefaultValue);
			return Property;
		}

		TSharedPtr<FJsonObject> MakeStringArrayProperty(const FString& Description)
		{
			TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
			Property->SetStringField(TEXT("type"), TEXT("array"));
			Property->SetStringField(TEXT("description"), Description);

			TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
			Items->SetStringField(TEXT("type"), TEXT("string"));
			Property->SetObjectField(TEXT("items"), Items);
			return Property;
		}

		TSharedPtr<FJsonObject> MakeFlexibleObjectProperty(const FString& Description)
		{
			TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
			Property->SetStringField(TEXT("type"), TEXT("object"));
			Property->SetStringField(TEXT("description"), Description);
			Property->SetBoolField(TEXT("additionalProperties"), true);
			return Property;
		}

		TSharedPtr<FJsonObject> MakeObjectArrayProperty(const FString& Description)
		{
			TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
			Property->SetStringField(TEXT("type"), TEXT("array"));
			Property->SetStringField(TEXT("description"), Description);

			TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
			Items->SetStringField(TEXT("type"), TEXT("object"));
			Items->SetBoolField(TEXT("additionalProperties"), true);
			Property->SetObjectField(TEXT("items"), Items);
			return Property;
		}

		void AddActorQuerySchemaFields(
			const TSharedPtr<FJsonObject>& PropertiesObject,
			bool bIncludeClassPath = true,
			bool bIncludePaths = true,
			bool bIncludeSelectedOnly = true)
		{
			PropertiesObject->SetObjectField(TEXT("filter"), MakeStringProperty(TEXT("Optional substring filter applied to actor labels, names, classes, and paths.")));
			if (bIncludeClassPath)
			{
				PropertiesObject->SetObjectField(TEXT("classPath"), MakeStringProperty(TEXT("Optional class path filter, for example /Script/Engine.PointLight.")));
			}
			if (bIncludePaths)
			{
				PropertiesObject->SetObjectField(TEXT("paths"), MakeStringArrayProperty(TEXT("Optional exact actor paths to target.")));
			}
			if (bIncludeSelectedOnly)
			{
				PropertiesObject->SetObjectField(TEXT("selectedOnly"), MakeBoolProperty(TEXT("Whether to target only the currently selected actors. If no selectors are provided, selected actors are used automatically."), false));
			}

			PropertiesObject->SetObjectField(TEXT("limit"), MakeNumberProperty(TEXT("Maximum number of actors to affect."), DefaultListLimit));
		}

		TSharedPtr<FJsonObject> MakeSpawnActorBasicProperties(bool bIncludeClassPath)
		{
			TSharedPtr<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();
			if (bIncludeClassPath)
			{
				PropertiesObject->SetObjectField(TEXT("classPath"), MakeStringProperty(TEXT("Native or Blueprint actor class path to spawn.")));
			}

			PropertiesObject->SetObjectField(TEXT("x"), MakeNumberProperty(TEXT("Spawn location X."), 0.0));
			PropertiesObject->SetObjectField(TEXT("y"), MakeNumberProperty(TEXT("Spawn location Y."), 0.0));
			PropertiesObject->SetObjectField(TEXT("z"), MakeNumberProperty(TEXT("Spawn location Z."), 0.0));
			PropertiesObject->SetObjectField(TEXT("pitch"), MakeNumberProperty(TEXT("Spawn rotation pitch."), 0.0));
			PropertiesObject->SetObjectField(TEXT("yaw"), MakeNumberProperty(TEXT("Spawn rotation yaw."), 0.0));
			PropertiesObject->SetObjectField(TEXT("roll"), MakeNumberProperty(TEXT("Spawn rotation roll."), 0.0));
			PropertiesObject->SetObjectField(TEXT("sx"), MakeNumberProperty(TEXT("Spawn scale X."), 1.0));
			PropertiesObject->SetObjectField(TEXT("sy"), MakeNumberProperty(TEXT("Spawn scale Y."), 1.0));
			PropertiesObject->SetObjectField(TEXT("sz"), MakeNumberProperty(TEXT("Spawn scale Z."), 1.0));
			PropertiesObject->SetObjectField(TEXT("label"), MakeStringProperty(TEXT("Optional actor label after spawning.")));
			return PropertiesObject;
		}

		void CopyStringFieldIfPresent(const FJsonObject& Source, const FString& FieldName, const TSharedPtr<FJsonObject>& Destination)
		{
			FString Value;
			if (Source.TryGetStringField(FieldName, Value))
			{
				Destination->SetStringField(FieldName, Value);
			}
		}

		void CopyBoolFieldIfPresent(const FJsonObject& Source, const FString& FieldName, const TSharedPtr<FJsonObject>& Destination)
		{
			bool Value = false;
			if (Source.TryGetBoolField(FieldName, Value))
			{
				Destination->SetBoolField(FieldName, Value);
			}
		}

		void CopyNumberFieldIfPresent(const FJsonObject& Source, const FString& FieldName, const TSharedPtr<FJsonObject>& Destination)
		{
			double Value = 0.0;
			if (Source.TryGetNumberField(FieldName, Value))
			{
				Destination->SetNumberField(FieldName, Value);
			}
		}

		void CopyStringArrayFieldIfPresent(const FJsonObject& Source, const FString& FieldName, const TSharedPtr<FJsonObject>& Destination)
		{
			TArray<FString> Values;
			if (!TryGetStringArrayField(Source, FieldName, Values) || Values.Num() == 0)
			{
				return;
			}

			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}

			Destination->SetArrayField(FieldName, JsonValues);
		}

		void CopyActorQueryArguments(const FJsonObject& Source, const TSharedPtr<FJsonObject>& Destination, bool bIncludeClassPath = true)
		{
			CopyStringFieldIfPresent(Source, TEXT("filter"), Destination);
			if (bIncludeClassPath)
			{
				CopyStringFieldIfPresent(Source, TEXT("classPath"), Destination);
			}
			CopyStringArrayFieldIfPresent(Source, TEXT("paths"), Destination);
			CopyBoolFieldIfPresent(Source, TEXT("selectedOnly"), Destination);
			CopyNumberFieldIfPresent(Source, TEXT("limit"), Destination);
		}

	void AddToolDefinition(
		TArray<TSharedPtr<FJsonValue>>& ToolsArray,
		const FString& Name,
		const FString& Title,
		const FString& Description,
		const TSharedPtr<FJsonObject>& InputSchema)
	{
		if (!ShouldExposeToolToAi(Name))
		{
			return;
		}

		TSharedPtr<FJsonObject> ToolObject = MakeShared<FJsonObject>();
		ToolObject->SetStringField(TEXT("name"), Name);
		ToolObject->SetStringField(TEXT("title"), Title);
		ToolObject->SetStringField(TEXT("description"), Description);
		if (const FToolRegistryEntry* RegistryEntry = FindToolRegistryEntry(Name))
		{
			ToolObject->SetStringField(TEXT("category"), RegistryEntry->Category);
			ToolObject->SetStringField(TEXT("handlerName"), RegistryEntry->HandlerName.IsEmpty() ? Name : RegistryEntry->HandlerName);
			ToolObject->SetStringField(TEXT("exposure"), RegistryEntry->Exposure == EToolExposure::Visible ? TEXT("visible") : TEXT("legacy_hidden"));
			ToolObject->SetBoolField(TEXT("explicitRegistryEntry"), RegistryEntry->bLoadedFromExplicitRegistry);
			ToolObject->SetStringField(TEXT("registryNotes"), RegistryEntry->Notes);
		}
		else
		{
			ToolObject->SetStringField(TEXT("category"), TEXT("unregistered"));
			ToolObject->SetStringField(TEXT("handlerName"), Name);
			ToolObject->SetStringField(TEXT("exposure"), TEXT("visible"));
			ToolObject->SetBoolField(TEXT("explicitRegistryEntry"), false);
		}
		ToolObject->SetObjectField(TEXT("inputSchema"), InputSchema);
		ToolObject->SetObjectField(TEXT("policy"), MakeToolPolicyObject(Name));
		ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObject));
	}

	int32 GetPositiveIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue)
	{
		double Value = static_cast<double>(DefaultValue);
		if (Arguments.TryGetNumberField(FieldName, Value))
		{
			return FMath::Max(1, static_cast<int32>(Value));
		}

		return DefaultValue;
	}

	bool IsEditorPlaying()
	{
		return GEditor
			&& (GEditor->PlayWorld != nullptr
				|| GEditor->bIsSimulatingInEditor
				|| GEditor->GetPlaySessionRequest().IsSet());
	}

	FString SanitizeMcpToolIdForPath(const FString& ToolName);
	bool ResolveProjectOutputDirectory(const FString& RequestedOutputRoot, FString& OutDirectory, FString& OutFailureReason);

	FUnrealMcpExecutionResult MakePieBlockedResult(const FString& ToolName)
	{
		return MakeExecutionResult(
			FString::Printf(TEXT("Tool '%s' is blocked while Play In Editor is active or starting."), *ToolName),
			nullptr,
			true);
	}

		bool LoadJsonObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		bool TryGetStringArrayField(const FJsonObject& Arguments, const FString& FieldName, TArray<FString>& OutValues)
		{
			const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
			if (!Arguments.TryGetArrayField(FieldName, JsonArray) || !JsonArray)
			{
				return false;
			}

			for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
			{
				FString StringValue;
				if (Value.IsValid() && Value->TryGetString(StringValue))
				{
					OutValues.Add(StringValue);
				}
			}

			return true;
		}

		TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		bool TryGetObjectArrayField(const FJsonObject& Arguments, const FString& FieldName, TArray<TSharedPtr<FJsonObject>>& OutValues)
		{
			const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
			if (!Arguments.TryGetArrayField(FieldName, JsonArray) || !JsonArray)
			{
				return false;
			}

			for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
			{
				if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject().IsValid())
				{
					return false;
				}

				OutValues.Add(Value->AsObject());
			}

			return true;
		}

		struct FActorQueryResult
		{
			TArray<AActor*> Actors;
			TArray<FString> RequestedPaths;
			FString FilterText;
			FString ClassPathFilter;
			int32 MatchCount = 0;
			int32 Limit = DefaultListLimit;
			bool bSelectedOnly = false;
			bool bTruncated = false;
		};

		bool MatchesActorFilters(
			AActor* Actor,
			const FString& FilterText,
			const FString& ClassPathFilter,
			const TSet<FString>& ExplicitPaths);

		FProperty* FindPropertyByNameLoose(const UStruct* Struct, const FString& PropertyName)
		{
			if (!Struct)
			{
				return nullptr;
			}

			if (FProperty* ExactProperty = Struct->FindPropertyByName(*PropertyName))
			{
				return ExactProperty;
			}

			for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)
					|| It->GetAuthoredName().Equals(PropertyName, ESearchCase::IgnoreCase))
				{
					return *It;
				}
			}

			return nullptr;
		}

		bool ResolveObjectPropertyPath(
			UObject* RootObject,
			const FString& PropertyPath,
			UObject*& OutOwnerObject,
			FProperty*& OutLeafProperty,
			FProperty*& OutNotifyProperty,
			void*& OutValuePtr,
			FString& OutFailureReason)
		{
			OutOwnerObject = nullptr;
			OutLeafProperty = nullptr;
			OutNotifyProperty = nullptr;
			OutValuePtr = nullptr;
			OutFailureReason.Reset();

			if (!RootObject)
			{
				OutFailureReason = TEXT("Cannot resolve a property path on a null object.");
				return false;
			}

			TArray<FString> RawSegments;
			PropertyPath.ParseIntoArray(RawSegments, TEXT("."), true);

			TArray<FString> Segments;
			for (const FString& RawSegment : RawSegments)
			{
				const FString Segment = RawSegment.TrimStartAndEnd();
				if (!Segment.IsEmpty())
				{
					Segments.Add(Segment);
				}
			}

			if (Segments.Num() == 0)
			{
				OutFailureReason = TEXT("The property path is empty.");
				return false;
			}

			UObject* CurrentOwnerObject = RootObject;
			const UStruct* CurrentStruct = RootObject->GetClass();
			void* CurrentContainer = RootObject;
			FProperty* CurrentNotifyProperty = nullptr;

			for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
			{
				const FString& Segment = Segments[SegmentIndex];
				FProperty* Property = FindPropertyByNameLoose(CurrentStruct, Segment);
				if (!Property)
				{
					OutFailureReason = FString::Printf(
						TEXT("Property segment '%s' was not found on '%s' while resolving '%s'."),
						*Segment,
						*CurrentStruct->GetPathName(),
						*PropertyPath);
					return false;
				}

				const bool bIsLastSegment = SegmentIndex == Segments.Num() - 1;
				if (bIsLastSegment)
				{
					OutOwnerObject = CurrentOwnerObject;
					OutLeafProperty = Property;
					OutNotifyProperty = CurrentNotifyProperty ? CurrentNotifyProperty : Property;
					OutValuePtr = Property->ContainerPtrToValuePtr<void>(CurrentContainer);
					return OutOwnerObject != nullptr && OutLeafProperty != nullptr && OutValuePtr != nullptr;
				}

				if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					if (!CurrentNotifyProperty)
					{
						CurrentNotifyProperty = Property;
					}

					CurrentContainer = StructProperty->ContainerPtrToValuePtr<void>(CurrentContainer);
					CurrentStruct = StructProperty->Struct;
					continue;
				}

				if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					UObject* NextObject = ObjectProperty->GetObjectPropertyValue_InContainer(CurrentContainer);
					if (!NextObject)
					{
						OutFailureReason = FString::Printf(
							TEXT("Property segment '%s' on '%s' resolved to null while resolving '%s'."),
							*Segment,
							*CurrentOwnerObject->GetPathName(),
							*PropertyPath);
						return false;
					}

					CurrentOwnerObject = NextObject;
					CurrentStruct = NextObject->GetClass();
					CurrentContainer = NextObject;
					CurrentNotifyProperty = nullptr;
					continue;
				}

				OutFailureReason = FString::Printf(
					TEXT("Property segment '%s' on '%s' is neither a struct nor object property, so '%s' cannot be resolved further."),
					*Segment,
					*CurrentStruct->GetPathName(),
					*PropertyPath);
				return false;
			}

			OutFailureReason = FString::Printf(TEXT("Unable to resolve property path '%s'."), *PropertyPath);
			return false;
		}

		TSharedPtr<FJsonValue> PropertyValueToJson(FProperty* Property, const void* ValuePtr)
		{
			if (!Property || !ValuePtr)
			{
				return MakeShared<FJsonValueNull>();
			}

			if (TSharedPtr<FJsonValue> JsonValue = FJsonObjectConverter::UPropertyToJsonValue(
				Property,
				ValuePtr,
				0,
				0,
				nullptr,
				nullptr,
				EJsonObjectConversionFlags::WriteTextAsComplexString))
			{
				return JsonValue;
			}

			FString ExportedText;
			Property->ExportTextItem_Direct(ExportedText, ValuePtr, nullptr, nullptr, PPF_None);
			return MakeShared<FJsonValueString>(ExportedText);
		}

		bool ApplyPropertyMapToActor(
			AActor* Actor,
			const FJsonObject& PropertyValues,
			TArray<TSharedPtr<FJsonValue>>& OutEditResults,
			int32& OutSuccessCount,
			int32& OutFailureCount)
		{
			OutSuccessCount = 0;
			OutFailureCount = 0;

			if (!Actor)
			{
				return false;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : PropertyValues.Values)
			{
				const FString PropertyPath = Entry.Key.TrimStartAndEnd();
				TSharedPtr<FJsonObject> EditObject = MakeShared<FJsonObject>();
				EditObject->SetStringField(TEXT("propertyPath"), PropertyPath);

				if (PropertyPath.IsEmpty())
				{
					EditObject->SetBoolField(TEXT("success"), false);
					EditObject->SetStringField(TEXT("error"), TEXT("Property paths cannot be empty."));
					OutEditResults.Add(MakeShared<FJsonValueObject>(EditObject));
					++OutFailureCount;
					continue;
				}

				UObject* OwnerObject = nullptr;
				FProperty* LeafProperty = nullptr;
				FProperty* NotifyProperty = nullptr;
				void* ValuePtr = nullptr;
				FString FailureReason;
				if (!ResolveObjectPropertyPath(Actor, PropertyPath, OwnerObject, LeafProperty, NotifyProperty, ValuePtr, FailureReason))
				{
					EditObject->SetBoolField(TEXT("success"), false);
					EditObject->SetStringField(TEXT("error"), FailureReason);
					OutEditResults.Add(MakeShared<FJsonValueObject>(EditObject));
					++OutFailureCount;
					continue;
					}

					EditObject->SetStringField(TEXT("ownerObjectPath"), OwnerObject->GetPathName());
					const TSharedPtr<FJsonValue> BeforeValue = UnrealMcp::PropertyValueToJson(LeafProperty, ValuePtr);
					if (BeforeValue.IsValid())
					{
						EditObject->SetField(TEXT("before"), BeforeValue);
				}

				OwnerObject->Modify();
				if (OwnerObject != Actor)
				{
					Actor->Modify();
				}

				OwnerObject->PreEditChange(NotifyProperty);

				FText ImportFailureReason;
				const bool bApplied = FJsonObjectConverter::JsonValueToUProperty(
					Entry.Value,
					LeafProperty,
					ValuePtr,
					0,
					0,
					false,
					&ImportFailureReason,
					nullptr);

				if (bApplied)
				{
					FPropertyChangedEvent PropertyChangedEvent(NotifyProperty, EPropertyChangeType::ValueSet);
					OwnerObject->PostEditChangeProperty(PropertyChangedEvent);
					OwnerObject->MarkPackageDirty();
					Actor->MarkPackageDirty();

					EditObject->SetBoolField(TEXT("success"), true);
					const TSharedPtr<FJsonValue> AfterValue = UnrealMcp::PropertyValueToJson(LeafProperty, ValuePtr);
					if (AfterValue.IsValid())
					{
						EditObject->SetField(TEXT("after"), AfterValue);
					}
					++OutSuccessCount;
				}
				else
				{
					OwnerObject->PostEditChange();
					EditObject->SetBoolField(TEXT("success"), false);
					EditObject->SetStringField(
						TEXT("error"),
						ImportFailureReason.IsEmpty()
							? FString::Printf(TEXT("Failed to apply property '%s'."), *PropertyPath)
							: ImportFailureReason.ToString());
					++OutFailureCount;
				}

				OutEditResults.Add(MakeShared<FJsonValueObject>(EditObject));
			}

			return OutFailureCount == 0;
		}

		bool ResolveActorsFromArguments(
			UEditorActorSubsystem* EditorActorSubsystem,
			const FJsonObject& Arguments,
			FActorQueryResult& OutQuery,
			FString& OutFailureReason)
		{
			OutQuery = FActorQueryResult();
			OutFailureReason.Reset();

			if (!EditorActorSubsystem)
			{
				OutFailureReason = TEXT("EditorActorSubsystem is unavailable.");
				return false;
			}

			Arguments.TryGetStringField(TEXT("filter"), OutQuery.FilterText);
			Arguments.TryGetStringField(TEXT("classPath"), OutQuery.ClassPathFilter);
			Arguments.TryGetBoolField(TEXT("selectedOnly"), OutQuery.bSelectedOnly);
			TryGetStringArrayField(Arguments, TEXT("paths"), OutQuery.RequestedPaths);
			OutQuery.Limit = FMath::Min(GetPositiveIntArgument(Arguments, TEXT("limit"), DefaultListLimit), 1000);

			TSet<FString> ExplicitPaths;
			for (const FString& RequestedPath : OutQuery.RequestedPaths)
			{
				const FString TrimmedPath = RequestedPath.TrimStartAndEnd();
				if (!TrimmedPath.IsEmpty())
				{
					ExplicitPaths.Add(TrimmedPath);
				}
			}

			const bool bHasSelectors = !OutQuery.FilterText.TrimStartAndEnd().IsEmpty()
				|| !OutQuery.ClassPathFilter.TrimStartAndEnd().IsEmpty()
				|| ExplicitPaths.Num() > 0;

			if (OutQuery.bSelectedOnly || !bHasSelectors)
			{
				OutQuery.Actors = EditorActorSubsystem->GetSelectedLevelActors();
				OutQuery.Actors.RemoveAll([](AActor* Actor) { return Actor == nullptr; });
				OutQuery.MatchCount = OutQuery.Actors.Num();

				if (OutQuery.Actors.Num() == 0)
				{
					OutFailureReason = OutQuery.bSelectedOnly
						? TEXT("No actors are currently selected.")
						: TEXT("No actor selectors were provided, and there are no selected actors to act on.");
					return false;
				}

				OutQuery.Actors.Sort([](const AActor& A, const AActor& B)
				{
					return A.GetActorLabel() < B.GetActorLabel();
				});

				if (OutQuery.Actors.Num() > OutQuery.Limit)
				{
					OutQuery.Actors.SetNum(OutQuery.Limit);
					OutQuery.bTruncated = true;
				}

				return true;
			}

			TArray<AActor*> AllActors = EditorActorSubsystem->GetAllLevelActors();
			AllActors.Sort([](const AActor& A, const AActor& B)
			{
				return A.GetActorLabel() < B.GetActorLabel();
			});

			for (AActor* Actor : AllActors)
			{
				if (!Actor || !MatchesActorFilters(Actor, OutQuery.FilterText, OutQuery.ClassPathFilter, ExplicitPaths))
				{
					continue;
				}

				++OutQuery.MatchCount;
				if (OutQuery.Actors.Num() >= OutQuery.Limit)
				{
					OutQuery.bTruncated = true;
					continue;
				}

				OutQuery.Actors.Add(Actor);
			}

			if (OutQuery.MatchCount == 0)
			{
				OutFailureReason = TEXT("No actors matched the provided criteria.");
				return false;
			}

			return true;
		}

		IPythonScriptPlugin* LoadPythonScriptPlugin()
		{
			static const FName PythonScriptPluginModuleName(TEXT("PythonScriptPlugin"));
			if (IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(PythonScriptPluginModuleName))
			{
				return PythonPlugin;
			}

			return FModuleManager::LoadModulePtr<IPythonScriptPlugin>(PythonScriptPluginModuleName);
		}

		bool TryParsePythonFileExecutionScope(const FString& ScopeString, EPythonFileExecutionScope& OutScope)
		{
			if (ScopeString.Equals(TEXT("private"), ESearchCase::IgnoreCase))
			{
				OutScope = EPythonFileExecutionScope::Private;
				return true;
			}

			if (ScopeString.Equals(TEXT("public"), ESearchCase::IgnoreCase))
			{
				OutScope = EPythonFileExecutionScope::Public;
				return true;
			}

			return false;
		}

		FString QuoteShellArgument(const FString& Value)
		{
			FString Escaped = Value;
			Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));

			const bool bNeedsQuotes = Escaped.Contains(TEXT(" "))
				|| Escaped.Contains(TEXT("\t"))
				|| Escaped.Contains(TEXT("\""));

			return bNeedsQuotes
				? FString::Printf(TEXT("\"%s\""), *Escaped)
				: Escaped;
		}

		bool ResolvePythonScriptPath(
			const FString& RequestedPath,
			bool bAllowOutsideProject,
			FString& OutResolvedPath,
			FString& OutFailureReason)
		{
			OutResolvedPath.Reset();
			OutFailureReason.Reset();

			const FString TrimmedPath = RequestedPath.TrimStartAndEnd();
			if (TrimmedPath.IsEmpty())
			{
				OutFailureReason = TEXT("The scriptPath argument is required.");
				return false;
			}

			FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			FPaths::NormalizeDirectoryName(ProjectDir);
			FPaths::CollapseRelativeDirectories(ProjectDir);

			FString ResolvedPath = FPaths::IsRelative(TrimmedPath)
				? FPaths::Combine(ProjectDir, TrimmedPath)
				: TrimmedPath;
			ResolvedPath = FPaths::ConvertRelativePathToFull(ResolvedPath);
			FPaths::NormalizeFilename(ResolvedPath);
			FPaths::CollapseRelativeDirectories(ResolvedPath);

			if (!ResolvedPath.EndsWith(TEXT(".py"), ESearchCase::IgnoreCase))
			{
				OutFailureReason = FString::Printf(TEXT("scriptPath must point to a .py file. Received '%s'."), *ResolvedPath);
				return false;
			}

			if (!FPaths::FileExists(ResolvedPath))
			{
				OutFailureReason = FString::Printf(TEXT("Python script file does not exist: %s"), *ResolvedPath);
				return false;
			}

			const FString ProjectDirPrefix = ProjectDir.EndsWith(TEXT("/")) ? ProjectDir : ProjectDir + TEXT("/");
			if (!bAllowOutsideProject && !ResolvedPath.StartsWith(ProjectDirPrefix, ESearchCase::IgnoreCase))
			{
				OutFailureReason = FString::Printf(
					TEXT("scriptPath '%s' is outside the project directory '%s'. Set allowOutsideProject=true to override."),
					*ResolvedPath,
					*ProjectDir);
				return false;
			}

			OutResolvedPath = ResolvedPath;
			return true;
		}

	TArray<FAssetData> GetSelectedAssets()
	{
		TArray<FAssetData> SelectedAssets;
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
		return SelectedAssets;
	}

	TSharedPtr<FJsonObject> MakeAssetObject(const FAssetData& Asset)
	{
		TSharedPtr<FJsonObject> AssetObject = MakeShared<FJsonObject>();
		AssetObject->SetStringField(TEXT("packageName"), Asset.PackageName.ToString());
		AssetObject->SetStringField(TEXT("assetName"), Asset.AssetName.ToString());
		AssetObject->SetStringField(TEXT("classPath"), Asset.AssetClassPath.ToString());
		AssetObject->SetStringField(TEXT("objectPath"), Asset.GetSoftObjectPath().ToString());
		return AssetObject;
	}

	TSharedPtr<FJsonObject> MakeVectorObject(const FVector& Vector)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Vector.X);
		Object->SetNumberField(TEXT("y"), Vector.Y);
		Object->SetNumberField(TEXT("z"), Vector.Z);
		return Object;
	}

	TSharedPtr<FJsonObject> MakeRotatorObject(const FRotator& Rotator)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("pitch"), Rotator.Pitch);
		Object->SetNumberField(TEXT("yaw"), Rotator.Yaw);
		Object->SetNumberField(TEXT("roll"), Rotator.Roll);
		return Object;
	}

	TSharedPtr<FJsonObject> MakeActorObject(AActor* Actor)
	{
		TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
		ActorObject->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObject->SetStringField(TEXT("name"), Actor->GetName());
		ActorObject->SetStringField(TEXT("classPath"), Actor->GetClass()->GetPathName());
		ActorObject->SetStringField(TEXT("path"), Actor->GetPathName());
		ActorObject->SetObjectField(TEXT("location"), MakeVectorObject(Actor->GetActorLocation()));
		ActorObject->SetObjectField(TEXT("rotation"), MakeRotatorObject(Actor->GetActorRotation()));
		return ActorObject;
	}

	FString DescribeAsset(const FAssetData& Asset)
	{
		return FString::Printf(TEXT("%s [%s]"), *Asset.GetSoftObjectPath().ToString(), *Asset.AssetClassPath.ToString());
	}

		FString DescribeActor(AActor* Actor)
		{
			const FVector Location = Actor->GetActorLocation();
			return FString::Printf(
				TEXT("%s (%s) @ [%.1f, %.1f, %.1f]"),
			*Actor->GetActorLabel(),
			*Actor->GetClass()->GetName(),
			Location.X,
				Location.Y,
				Location.Z);
		}

		bool MatchesActorFilters(
			AActor* Actor,
			const FString& FilterText,
			const FString& ClassPathFilter,
			const TSet<FString>& ExplicitPaths)
		{
			const FString ActorLabel = Actor->GetActorLabel();
			const FString ActorName = Actor->GetName();
			const FString ActorClassPath = Actor->GetClass()->GetPathName();
			const FString ActorPath = Actor->GetPathName();

			if (ExplicitPaths.Num() > 0 && !ExplicitPaths.Contains(ActorPath))
			{
				return false;
			}

			if (!FilterText.IsEmpty()
				&& !ActorLabel.Contains(FilterText, ESearchCase::IgnoreCase)
				&& !ActorName.Contains(FilterText, ESearchCase::IgnoreCase)
				&& !ActorClassPath.Contains(FilterText, ESearchCase::IgnoreCase)
				&& !ActorPath.Contains(FilterText, ESearchCase::IgnoreCase))
			{
				return false;
			}

			if (!ClassPathFilter.IsEmpty()
				&& !ActorClassPath.Equals(ClassPathFilter, ESearchCase::IgnoreCase)
				&& !ActorClassPath.Contains(ClassPathFilter, ESearchCase::IgnoreCase))
			{
				return false;
			}

			return true;
		}

		AActor* ResolveActorReference(
			UEditorActorSubsystem* EditorActorSubsystem,
			const FString& ActorPath,
			const FString& ActorLabel,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();

			if (!EditorActorSubsystem)
			{
				OutFailureReason = TEXT("EditorActorSubsystem is unavailable.");
				return nullptr;
			}

			const TArray<AActor*> AllActors = EditorActorSubsystem->GetAllLevelActors();

			if (!ActorPath.TrimStartAndEnd().IsEmpty())
			{
				for (AActor* Actor : AllActors)
				{
					if (Actor && Actor->GetPathName().Equals(ActorPath, ESearchCase::IgnoreCase))
					{
						return Actor;
					}
				}

				OutFailureReason = FString::Printf(TEXT("No actor matched actorPath '%s'."), *ActorPath);
				return nullptr;
			}

			if (!ActorLabel.TrimStartAndEnd().IsEmpty())
			{
				AActor* MatchedActor = nullptr;
				for (AActor* Actor : AllActors)
				{
					if (Actor && Actor->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
					{
						if (MatchedActor)
						{
							OutFailureReason = FString::Printf(TEXT("Multiple actors matched actorLabel '%s'. Use actorPath instead."), *ActorLabel);
							return nullptr;
						}

						MatchedActor = Actor;
					}
				}

				if (MatchedActor)
				{
					return MatchedActor;
				}

				OutFailureReason = FString::Printf(TEXT("No actor matched actorLabel '%s'."), *ActorLabel);
				return nullptr;
			}

			const TArray<AActor*> SelectedActors = EditorActorSubsystem->GetSelectedLevelActors();
			if (SelectedActors.Num() == 1 && SelectedActors[0])
			{
				return SelectedActors[0];
			}

			OutFailureReason = SelectedActors.Num() > 1
				? TEXT("Multiple actors are selected. Provide actorPath or actorLabel.")
				: TEXT("No actor reference was provided, and there is no single selected actor.");
			return nullptr;
		}

		UWorld* ResolveConsoleWorld(const FString& RequestedTarget, FString& OutResolvedTarget, FString& OutFailureReason)
		{
		OutResolvedTarget = TEXT("editor");
		OutFailureReason.Reset();

		if (!GEditor)
		{
			OutFailureReason = TEXT("GEditor is unavailable.");
			return nullptr;
		}

		const FString NormalizedTarget = RequestedTarget.TrimStartAndEnd().ToLower();
		const bool bIsAuto = NormalizedTarget.IsEmpty() || NormalizedTarget == TEXT("auto");
		const bool bWantsPie = NormalizedTarget == TEXT("pie");
		const bool bWantsEditor = NormalizedTarget == TEXT("editor");

		if (!bIsAuto && !bWantsPie && !bWantsEditor)
		{
			OutFailureReason = FString::Printf(TEXT("Unknown console target '%s'. Use auto, editor, or pie."), *RequestedTarget);
			return nullptr;
		}

		if ((bIsAuto || bWantsPie) && GEditor->PlayWorld != nullptr)
		{
			OutResolvedTarget = TEXT("pie");
			return GEditor->PlayWorld;
		}

		if (bWantsPie)
		{
			OutFailureReason = TEXT("No PIE world is currently running.");
			return nullptr;
		}

		if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
		{
			OutResolvedTarget = TEXT("editor");
			return EditorWorld;
		}

		OutFailureReason = TEXT("The editor world is unavailable.");
		return nullptr;
	}


		void AddAuditIssue(
			TArray<TSharedPtr<FJsonValue>>& Issues,
			const FString& Severity,
			const FString& Location,
			const FString& Message);
		bool AnalyzeOpenAiSchemaCompatibility(
			const TSharedPtr<FJsonObject>& InputSchema,
			TArray<TSharedPtr<FJsonValue>>& Issues,
			FString& OutReason,
			TSharedPtr<FJsonObject>& OutNormalizedSchema);
		TSharedPtr<FJsonObject> FindToolDefinitionByName(
			const TArray<TSharedPtr<FJsonValue>>& ToolsArray,
			const FString& ToolName);
		FString GetProjectMemoryFilePath();
		bool LoadProjectMemory(TSharedPtr<FJsonObject>& OutMemory, FString& OutFailureReason);
		bool SaveProjectMemory(const TSharedPtr<FJsonObject>& MemoryObject, FString& OutFailureReason);
		TSharedPtr<FJsonObject> MakeProjectMemorySummary(const TSharedPtr<FJsonObject>& EntryObject, bool bIncludeContent);
		FUnrealMcpExecutionResult ProjectMemoryWrite(const FJsonObject& Arguments);
		FUnrealMcpExecutionResult ProjectMemoryRead(const FJsonObject& Arguments);
		FUnrealMcpExecutionResult ProjectMemoryView(const FJsonObject& Arguments);
		FUnrealMcpExecutionResult ProjectMemoryEdit(const FJsonObject& Arguments);
		FUnrealMcpExecutionResult ProjectMemoryDelete(const FJsonObject& Arguments);


		FString HashTextForManifest(const FString& Text);
		FString GetMcpModuleSourcePath();
		FString GetMcpExtensionBackupRoot();
		FString GetUnrealMcpSavedRoot();
		FString GetMcpBuildLogRoot();
		FString GetLatestMcpExtensionManifestPath();
		FString GetMcpExtensionLockPath();
		FString GetMcpProjectStateBackupRoot();
		FString GetMcpModuleHeaderPath();
		FString GetProjectReadmePath();
		FString GetPluginReadmePath();
		bool LoadJsonObjectFromFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutObject, FString& OutFailureReason);
		bool SaveJsonObjectToFile(const TSharedPtr<FJsonObject>& Object, const FString& FilePath, FString& OutFailureReason);
		bool ResolveProjectPathInsideProject(const FString& RequestedPath, FString& OutPath, FString& OutFailureReason);
		bool ResolveMcpScaffoldDirectory(const FJsonObject& Arguments, FString& OutDirectory, FString& OutToolName, FString& OutFailureReason);

		bool LoadScaffoldSnippet(
			const FString& ScaffoldDirectory,
			const FString& FileName,
			bool bRequired,
			FString& OutSnippet,
			TArray<TSharedPtr<FJsonValue>>& Issues,
			FString& OutFailureReason);
		TSharedPtr<FJsonObject> ValidateCppSnippetText(
			const FString& SnippetText,
			const FString& SnippetName,
			const FString& ToolName);
		TSharedPtr<FJsonObject> MakeTextDiffObject(const FString& BeforeText, const FString& AfterText, int32 MaxPreviewLines);

		bool TryAcquireExtensionSessionLock(
			const FString& Owner,
			const FString& Reason,
			int32 TtlSeconds,
			bool bForce,
			FString& OutSessionId,
			TSharedPtr<FJsonObject>& OutLockObject,
			FString& OutFailureReason);
		bool ReleaseExtensionSessionLock(const FString& SessionId, bool bForce, FString& OutFailureReason);

		class FScopedMcpExtensionSessionLock
		{
		public:
			FScopedMcpExtensionSessionLock(const FString& ToolName, const FJsonObject& Arguments)
			{
				bool bSkipLock = false;
				bool bForceLock = false;
				double TtlSecondsDouble = 900.0;
				FString Owner = TEXT("Unreal MCP Chat");
				Arguments.TryGetBoolField(TEXT("skipLock"), bSkipLock);
				Arguments.TryGetBoolField(TEXT("forceLock"), bForceLock);
				Arguments.TryGetNumberField(TEXT("lockTtlSeconds"), TtlSecondsDouble);
				Arguments.TryGetStringField(TEXT("lockOwner"), Owner);

				if (bSkipLock)
				{
					bAcquired = true;
					bOwnsLock = false;
					return;
				}

				const int32 TtlSeconds = FMath::Clamp(static_cast<int32>(TtlSecondsDouble), 30, 86400);
				const FString Reason = FString::Printf(TEXT("Executing %s"), *ToolName);
				bAcquired = TryAcquireExtensionSessionLock(Owner, Reason, TtlSeconds, bForceLock, SessionId, LockObject, FailureReason);
				bOwnsLock = bAcquired;
			}

			~FScopedMcpExtensionSessionLock()
			{
				if (bOwnsLock && !SessionId.IsEmpty())
				{
					FString ReleaseFailure;
					ReleaseExtensionSessionLock(SessionId, false, ReleaseFailure);
				}
			}

			bool IsAcquired() const
			{
				return bAcquired;
			}

			FString GetFailureReason() const
			{
				return FailureReason;
			}

			TSharedPtr<FJsonObject> MakeStructuredContent(const FString& Action) const
			{
				TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
				StructuredContent->SetStringField(TEXT("action"), Action);
				StructuredContent->SetBoolField(TEXT("locked"), bAcquired);
				StructuredContent->SetStringField(TEXT("lockPath"), GetMcpExtensionLockPath());
				StructuredContent->SetStringField(TEXT("sessionId"), SessionId);
				if (LockObject.IsValid())
				{
					StructuredContent->SetObjectField(TEXT("lock"), LockObject);
				}
				return StructuredContent;
			}

		private:
			bool bAcquired = false;
			bool bOwnsLock = false;
			FString SessionId;
			FString FailureReason;
			TSharedPtr<FJsonObject> LockObject;
		};

		FString GetHostBuildPlatformName();
		FString GetUnrealBuildScriptPath();
		FString QuoteCommandLineArgument(const FString& Value);
		void ParseBuildLog(const FString& LogText, int32 ReturnCode, const TSharedPtr<FJsonObject>& StructuredContent);
		void WriteBuildTestMemory(
			const FString& MemoryKey,
			const FString& Summary,
			const FString& Status,
			const FString& NextStep,
			const TSharedPtr<FJsonObject>& ContentObject);

	FString GetCommandRemainder(const FString& Input, const FString& Command)
	{
		if (Input.Len() <= Command.Len())
		{
			return FString();
		}

		return Input.Mid(Command.Len()).TrimStartAndEnd();
	}

	bool MatchesCommand(const FString& Input, const FString& Command)
	{
		return Input.Equals(Command, ESearchCase::IgnoreCase)
			|| Input.StartsWith(Command + TEXT(" "), ESearchCase::IgnoreCase);
	}
}

void FUnrealMcpModule::StartupModule()
{
	StartServer();
	SkillActivityTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FUnrealMcpModule::TickSkillActivity), 60.0f);
	RegisterTabSpawner();
	UToolMenus::Get()->RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealMcpModule::RegisterMenus));
}

void FUnrealMcpModule::ShutdownModule()
{
	if (SkillActivityTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SkillActivityTickerHandle);
		SkillActivityTickerHandle.Reset();
	}
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	UnregisterTabSpawner();
	StopServer();
}

bool FUnrealMcpModule::TickSkillActivity(float DeltaTime)
{
	UnrealMcp::TickSkillActivityRecorder();
	return true;
}

void FUnrealMcpModule::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		UnrealMcp::ChatTabName,
		FOnSpawnTab::CreateRaw(this, &FUnrealMcpModule::SpawnChatTab))
		.SetDisplayName(LOCTEXT("ChatTabTitle", "Unreal MCP Chat"))
		.SetTooltipText(LOCTEXT("ChatTabTooltip", "Open the Unreal MCP command chat window."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		UnrealMcp::WorkbenchTabName,
		FOnSpawnTab::CreateRaw(this, &FUnrealMcpModule::SpawnWorkbenchTab))
		.SetDisplayName(LOCTEXT("WorkbenchTabTitle", "Unreal MCP Workbench"))
		.SetTooltipText(LOCTEXT("WorkbenchTabTooltip", "Open the Unreal MCP self-extension workbench."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FUnrealMcpModule::UnregisterTabSpawner()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealMcp::ChatTabName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealMcp::WorkbenchTabName);
	}
}

void FUnrealMcpModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window")))
	{
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("UnrealMcp"));
		Section.AddMenuEntry(
			TEXT("OpenUnrealMcpChat"),
			LOCTEXT("OpenChatMenuLabel", "Unreal MCP Chat"),
			LOCTEXT("OpenChatMenuTooltip", "Open the Unreal MCP command chat window."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FUnrealMcpModule::OpenChatTab)));
		Section.AddMenuEntry(
			TEXT("OpenUnrealMcpWorkbench"),
			LOCTEXT("OpenWorkbenchMenuLabel", "Unreal MCP Workbench"),
			LOCTEXT("OpenWorkbenchMenuTooltip", "Open the thin self-extension workbench console."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FUnrealMcpModule::OpenWorkbenchTab)));
	}
}

void FUnrealMcpModule::OpenChatTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UnrealMcp::ChatTabName);
}

void FUnrealMcpModule::OpenWorkbenchTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UnrealMcp::WorkbenchTabName);
}

TSharedRef<SDockTab> FUnrealMcpModule::SpawnChatTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> Tab =
		SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SUnrealMcpChatPanel, this)
		];

	return Tab;
}

TSharedRef<SDockTab> FUnrealMcpModule::SpawnWorkbenchTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> Tab =
		SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SUnrealMcpWorkbenchPanel, this)
		];

	return Tab;
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteToolFromEditorUI(const FString& ToolName, const FJsonObject& Arguments) const
{
	return ExecuteTool(ToolName, Arguments);
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteTool(const FString& ToolName, const FJsonObject& Arguments) const
{
	const FString RegisteredHandlerName = UnrealMcp::ResolveToolHandlerName(ToolName);
	const UnrealMcp::FToolPolicy ActivityPolicy = UnrealMcp::GetToolPolicy(ToolName);
	if (ActivityPolicy.RiskLevel != UnrealMcp::EToolRiskLevel::ReadOnly)
	{
		TArray<FString> ArgumentKeys;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments.Values)
		{
			ArgumentKeys.Add(Pair.Key);
		}
		ArgumentKeys.Sort();
		TSharedPtr<FJsonObject> ActivityDetails = MakeShared<FJsonObject>();
		ActivityDetails->SetStringField(TEXT("toolName"), ToolName);
		ActivityDetails->SetStringField(TEXT("handlerName"), RegisteredHandlerName);
		ActivityDetails->SetStringField(TEXT("riskLevel"), UnrealMcp::LexToString(ActivityPolicy.RiskLevel));
		ActivityDetails->SetArrayField(TEXT("argumentKeys"), UnrealMcp::MakeJsonStringArray(ArgumentKeys));
		UnrealMcp::RecordSkillActivityEvent(TEXT("mcp_tool_call"), FString::Printf(TEXT("Called MCP tool %s."), *ToolName), ActivityDetails);
	}

	FUnrealMcpExecutionResult Result = ExecuteToolInternal(RegisteredHandlerName, Arguments);
	UnrealMcp::AttachToolExecutionCheck(ToolName, Arguments, Result);
	return Result;
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteToolInternal(const FString& ToolName, const FJsonObject& Arguments) const
{
	FUnrealMcpExecutionResult EditorToolResult;
	if (UnrealMcp::TryExecuteEditorTool(ToolName, Arguments, EditorToolResult))
	{
		return EditorToolResult;
	}

	FUnrealMcpExecutionResult ActorToolResult;
	if (UnrealMcp::TryExecuteActorTool(ToolName, Arguments, ActorToolResult))
	{
		return ActorToolResult;
	}

	FUnrealMcpExecutionResult BlueprintToolResult;
	if (UnrealMcp::TryExecuteBlueprintTool(ToolName, Arguments, BlueprintToolResult))
	{
		return BlueprintToolResult;
	}

	FUnrealMcpExecutionResult WidgetToolResult;
	if (UnrealMcp::TryExecuteWidgetTool(ToolName, Arguments, WidgetToolResult))
	{
		return WidgetToolResult;
	}

	FUnrealMcpExecutionResult ScaffoldToolResult;
	if (UnrealMcp::TryExecuteScaffoldTool(ToolName, Arguments, ScaffoldToolResult))
	{
		return ScaffoldToolResult;
	}

	FUnrealMcpExecutionResult MemoryToolResult;
	if (UnrealMcp::TryExecuteMemoryTool(ToolName, Arguments, MemoryToolResult))
	{
		return MemoryToolResult;
	}

	FUnrealMcpExecutionResult SkillToolResult;
	if (UnrealMcp::TryExecuteSkillTool(
		ToolName,
		Arguments,
		[&ToolName](const FJsonObject& ToolArguments)
		{
			UnrealMcp::FScopedMcpExtensionSessionLock ScopedLock(ToolName, ToolArguments);
			if (!ScopedLock.IsAcquired())
			{
				return UnrealMcp::MakeExecutionResult(ScopedLock.GetFailureReason(), ScopedLock.MakeStructuredContent(TEXT("mcp_extension_lock_failed")), true);
			}
			return UnrealMcp::SkillPromoteDraft(ToolArguments);
		},
		SkillToolResult))
	{
		return SkillToolResult;
	}

	TArray<TSharedPtr<FJsonValue>> ToolDefinitions;
	AppendToolDefinitions(ToolDefinitions);
	FUnrealMcpExecutionResult SelfExtensionToolResult;
	if (UnrealMcp::TryExecuteSelfExtensionTool(
		ToolName,
		Arguments,
		ToolDefinitions,
		[this](const FJsonObject& ToolArguments) { return RunMcpToolTest(ToolArguments); },
		[this](const FJsonObject& ToolArguments) { return RunMcpTestSuite(ToolArguments); },
		[this](const FJsonObject& ToolArguments) { return RunMcpExtensionPipeline(ToolArguments); },
		SelfExtensionToolResult))
	{
		return SelfExtensionToolResult;
	}

	return UnrealMcp::MakeExecutionResult(FString::Printf(TEXT("Unknown tool '%s'."), *ToolName), nullptr, true);
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteChatCommand(const FString& Input) const
{
	const FString TrimmedInput = Input.TrimStartAndEnd();
	if (TrimmedInput.IsEmpty())
	{
		return UnrealMcp::MakeExecutionResult(TEXT("Enter a command. Try /help."), nullptr, true);
	}

	if (TrimmedInput.StartsWith(TEXT("/tool "), ESearchCase::IgnoreCase))
	{
		const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/tool"));
		if (Remainder.IsEmpty())
		{
			return UnrealMcp::MakeExecutionResult(TEXT("Usage: /tool <tool-name> <json-args>"), nullptr, true);
		}

		FString ToolName = Remainder;
		FString JsonText;
		int32 SeparatorIndex = INDEX_NONE;
		if (Remainder.FindChar(TEXT(' '), SeparatorIndex))
		{
			ToolName = Remainder.Left(SeparatorIndex).TrimStartAndEnd();
			JsonText = Remainder.Mid(SeparatorIndex + 1).TrimStartAndEnd();
		}

		TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
		if (!JsonText.IsEmpty() && !UnrealMcp::LoadJsonObject(JsonText, ArgumentsObject))
		{
			return UnrealMcp::MakeExecutionResult(TEXT("Failed to parse JSON arguments for /tool."), nullptr, true);
		}

		return ExecuteTool(ToolName, *ArgumentsObject);
	}

		if (TrimmedInput.Equals(TEXT("/help"), ESearchCase::IgnoreCase))
		{
				return UnrealMcp::MakeExecutionResult(
					TEXT("Commands:\n")
					TEXT("/help\n")
					TEXT("/ask <prompt>  (handled by the chat panel AI)\n")
					TEXT("/reset_ai  (handled by the chat panel AI)\n")
					TEXT("/stop_ai  (handled by the chat panel AI)\n")
					TEXT("Plain text in the chat panel is also treated as an AI ask.\n")
					TEXT("/status\n")
					TEXT("/pie [simulate]\n")
					TEXT("/stop_pie\n")
					TEXT("/console stat fps\n")
					TEXT("/log [lines]\n")
					TEXT("/map_check\n")
					TEXT("/maps\n")
					TEXT("/assets [/Game/Path]\n")
					TEXT("/selected assets\n")
					TEXT("/selected actors\n")
						TEXT("/select PlayerStart\n")
						TEXT("/move_selected 0 0 300 [pitch yaw roll]\n")
						TEXT("/set_props {\"selectedOnly\":true,\"properties\":{\"Tags\":[\"Encounter\"],\"RootComponent.RelativeScale3D\":{\"X\":1.25,\"Y\":1.25,\"Z\":1.25}}}\n")
						TEXT("/layout_selected 400 300 [columns] [startX startY startZ]\n")
						TEXT("/layout_circle 1200 [startAngle] [arcDegrees] [centerX centerY centerZ]\n")
						TEXT("/actors [filter]\n")
					TEXT("/open_map /Game/TopDown/Maps/Lvl_TopDown\n")
					TEXT("/open_asset /Game/Variant_TwinStick/Blueprints/BP_TwinStickCharacter\n")
						TEXT("/browse /Game/Variant_TwinStick\n")
						TEXT("/spawn /Script/Engine.PointLight 0 0 150 ChatLight\n")
						TEXT("/spawn_batch {\"classPath\":\"/Script/Engine.PointLight\",\"items\":[{\"x\":0,\"y\":0,\"z\":150,\"label\":\"Light_A\"},{\"x\":300,\"y\":0,\"z\":150,\"label\":\"Light_B\"}]}\n")
						TEXT("/py import unreal; print(unreal.EditorLevelLibrary.get_selected_level_actors())\n")
						TEXT("/py_eval unreal.EditorLevelLibrary.get_editor_world().get_name()\n")
						TEXT("/py_file Tools/mcp_test_script.py\n")
						TEXT("/compile_bp /Game/Blueprints/BP_Test\n")
						TEXT("/compile_bps /Game/TopDown\n")
						TEXT("/new_bp /Game/Blueprints/BP_Test /Script/Engine.Actor\n")
						TEXT("/delete_selected\n")
						TEXT("/save\n")
						TEXT("/tool unreal.spawn_actor {\"classPath\":\"/Script/Engine.PointLight\",\"x\":0,\"y\":0,\"z\":150,\"label\":\"ChatLight\"}\n")
						TEXT("/tool unreal.tail_log {\"lines\":80,\"contains\":\"Error\"}\n")
						TEXT("/tool unreal.select_actors {\"paths\":[\"/Game/TopDown/Lvl_TopDown.Lvl_TopDown:PersistentLevel.PlayerStart_0\"]}\n")
						TEXT("/tool unreal.execute_python {\"command\":\"import unreal\\nprint(unreal.EditorLevelLibrary.get_editor_world())\"}"),
						UnrealMcp::MakeExecutionStructuredMessage(TEXT("help")),
						false);
				}

		if (TrimmedInput.Equals(TEXT("/status"), ESearchCase::IgnoreCase))
		{
			return ExecuteTool(TEXT("unreal.editor_status"), *UnrealMcp::MakeEmptyObject());
		}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/pie")))
		{
			const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/pie"));
			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			ArgumentsObject->SetBoolField(TEXT("simulate"), Remainder.Equals(TEXT("simulate"), ESearchCase::IgnoreCase));
			return ExecuteTool(TEXT("unreal.start_pie"), *ArgumentsObject);
		}

		if (TrimmedInput.Equals(TEXT("/stop_pie"), ESearchCase::IgnoreCase) || TrimmedInput.Equals(TEXT("/stop pie"), ESearchCase::IgnoreCase))
		{
			return ExecuteTool(TEXT("unreal.stop_pie"), *UnrealMcp::MakeEmptyObject());
		}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/console")))
		{
			const FString Command = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/console"));
			if (Command.IsEmpty())
			{
				return UnrealMcp::MakeExecutionResult(TEXT("Usage: /console <command>"), nullptr, true);
			}

			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			ArgumentsObject->SetStringField(TEXT("command"), Command);
			ArgumentsObject->SetStringField(TEXT("target"), TEXT("auto"));
			return ExecuteTool(TEXT("unreal.execute_console_command"), *ArgumentsObject);
		}

			if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/log")))
			{
			const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/log"));
			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			if (!Remainder.IsEmpty())
			{
				TArray<FString> Tokens;
				Remainder.ParseIntoArrayWS(Tokens);
				if (Tokens.Num() > 0)
				{
					int32 ParsedLines = 0;
					if (LexTryParseString(ParsedLines, *Tokens[0]))
					{
						ArgumentsObject->SetNumberField(TEXT("lines"), FMath::Max(1, ParsedLines));
						if (Tokens.Num() > 1)
						{
							TArray<FString> FilterTokens;
							for (int32 Index = 1; Index < Tokens.Num(); ++Index)
							{
								FilterTokens.Add(Tokens[Index]);
							}
							ArgumentsObject->SetStringField(TEXT("contains"), FString::Join(FilterTokens, TEXT(" ")));
						}
					}
					else
					{
						ArgumentsObject->SetStringField(TEXT("contains"), Remainder);
					}
				}
			}

				return ExecuteTool(TEXT("unreal.tail_log"), *ArgumentsObject);
			}

			if (TrimmedInput.Equals(TEXT("/map_check"), ESearchCase::IgnoreCase) || TrimmedInput.Equals(TEXT("/map check"), ESearchCase::IgnoreCase))
			{
				return ExecuteTool(TEXT("unreal.map_check"), *UnrealMcp::MakeEmptyObject());
			}

			if (TrimmedInput.Equals(TEXT("/maps"), ESearchCase::IgnoreCase))
			{
				return ExecuteTool(TEXT("unreal.list_maps"), *UnrealMcp::MakeEmptyObject());
			}

	if (TrimmedInput.Equals(TEXT("/selected assets"), ESearchCase::IgnoreCase) || TrimmedInput.Equals(TEXT("/selected_assets"), ESearchCase::IgnoreCase))
	{
		return ExecuteTool(TEXT("unreal.list_selected_assets"), *UnrealMcp::MakeEmptyObject());
	}

	if (TrimmedInput.Equals(TEXT("/selected actors"), ESearchCase::IgnoreCase) || TrimmedInput.Equals(TEXT("/selected_actors"), ESearchCase::IgnoreCase))
	{
		return ExecuteTool(TEXT("unreal.list_selected_actors"), *UnrealMcp::MakeEmptyObject());
	}

	if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/assets")))
	{
		TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
		const FString Path = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/assets"));
		ArgumentsObject->SetStringField(TEXT("path"), Path.IsEmpty() ? TEXT("/Game") : Path);
		ArgumentsObject->SetBoolField(TEXT("recursive"), true);
		return ExecuteTool(TEXT("unreal.list_assets"), *ArgumentsObject);
	}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/actors")))
		{
			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			const FString Filter = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/actors"));
			if (!Filter.IsEmpty())
		{
			ArgumentsObject->SetStringField(TEXT("filter"), Filter);
			}
			return ExecuteTool(TEXT("unreal.list_level_actors"), *ArgumentsObject);
		}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/select")))
		{
			const FString Filter = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/select"));
			if (Filter.IsEmpty())
			{
				return UnrealMcp::MakeExecutionResult(TEXT("Usage: /select <filter>"), nullptr, true);
			}

			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			ArgumentsObject->SetStringField(TEXT("filter"), Filter);
			ArgumentsObject->SetBoolField(TEXT("clearSelection"), true);
			return ExecuteTool(TEXT("unreal.select_actors"), *ArgumentsObject);
		}

			if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/move_selected")))
			{
			const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/move_selected"));
			TArray<FString> Tokens;
			Remainder.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 3)
			{
				return UnrealMcp::MakeExecutionResult(TEXT("Usage: /move_selected <x> <y> <z> [pitch yaw roll]"), nullptr, true);
			}

			double NumericValues[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
			for (int32 Index = 0; Index < Tokens.Num() && Index < 6; ++Index)
			{
				if (!LexTryParseString(NumericValues[Index], *Tokens[Index]))
				{
					return UnrealMcp::MakeExecutionResult(TEXT("Usage: /move_selected <x> <y> <z> [pitch yaw roll]"), nullptr, true);
				}
			}

			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			ArgumentsObject->SetNumberField(TEXT("x"), NumericValues[0]);
			ArgumentsObject->SetNumberField(TEXT("y"), NumericValues[1]);
			ArgumentsObject->SetNumberField(TEXT("z"), NumericValues[2]);
			if (Tokens.Num() > 3)
			{
				ArgumentsObject->SetNumberField(TEXT("pitch"), NumericValues[3]);
			}
			if (Tokens.Num() > 4)
			{
				ArgumentsObject->SetNumberField(TEXT("yaw"), NumericValues[4]);
			}
			if (Tokens.Num() > 5)
			{
				ArgumentsObject->SetNumberField(TEXT("roll"), NumericValues[5]);
			}

				return ExecuteTool(TEXT("unreal.set_actor_transform"), *ArgumentsObject);
			}

			if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/set_props")) || UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/setprops")))
			{
				const FString JsonText = UnrealMcp::GetCommandRemainder(
					TrimmedInput,
					TrimmedInput.StartsWith(TEXT("/setprops"), ESearchCase::IgnoreCase) ? TEXT("/setprops") : TEXT("/set_props"));
				if (JsonText.IsEmpty())
				{
					return UnrealMcp::MakeExecutionResult(TEXT("Usage: /set_props {\"selectedOnly\":true,\"properties\":{\"Tags\":[\"Encounter\"]}}"), nullptr, true);
				}

				TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
				if (!UnrealMcp::LoadJsonObject(JsonText, ArgumentsObject))
				{
					return UnrealMcp::MakeExecutionResult(TEXT("Failed to parse JSON arguments for /set_props."), nullptr, true);
				}

				if (!ArgumentsObject->HasField(TEXT("selectedOnly"))
					&& !ArgumentsObject->HasField(TEXT("filter"))
					&& !ArgumentsObject->HasField(TEXT("classPath"))
					&& !ArgumentsObject->HasField(TEXT("paths")))
				{
					ArgumentsObject->SetBoolField(TEXT("selectedOnly"), true);
				}

				return ExecuteTool(TEXT("unreal.batch_set_actor_properties"), *ArgumentsObject);
			}

			if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/layout_selected")))
			{
				const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/layout_selected"));
				TArray<FString> Tokens;
				Remainder.ParseIntoArrayWS(Tokens);
				if (Tokens.Num() < 2)
				{
					return UnrealMcp::MakeExecutionResult(TEXT("Usage: /layout_selected <spacingX> <spacingY> [columns] [startX startY startZ]"), nullptr, true);
				}

				double SpacingX = 0.0;
				double SpacingY = 0.0;
				if (!LexTryParseString(SpacingX, *Tokens[0]) || !LexTryParseString(SpacingY, *Tokens[1]))
				{
					return UnrealMcp::MakeExecutionResult(TEXT("Usage: /layout_selected <spacingX> <spacingY> [columns] [startX startY startZ]"), nullptr, true);
				}

				int32 Columns = 5;
				int32 StartTokenIndex = 2;
				if (Tokens.Num() >= 3)
				{
					if (!LexTryParseString(Columns, *Tokens[2]))
					{
						return UnrealMcp::MakeExecutionResult(TEXT("The optional columns value must be an integer."), nullptr, true);
					}
					StartTokenIndex = 3;
				}

				TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
				ArgumentsObject->SetBoolField(TEXT("selectedOnly"), true);
				ArgumentsObject->SetNumberField(TEXT("spacingX"), SpacingX);
				ArgumentsObject->SetNumberField(TEXT("spacingY"), SpacingY);
				ArgumentsObject->SetNumberField(TEXT("columns"), FMath::Max(1, Columns));

				if (Tokens.Num() > StartTokenIndex)
				{
					if (Tokens.Num() - StartTokenIndex != 3)
					{
						return UnrealMcp::MakeExecutionResult(TEXT("If you provide a start position, include all three values: startX startY startZ."), nullptr, true);
					}

					double StartX = 0.0;
					double StartY = 0.0;
					double StartZ = 0.0;
					if (!LexTryParseString(StartX, *Tokens[StartTokenIndex])
						|| !LexTryParseString(StartY, *Tokens[StartTokenIndex + 1])
						|| !LexTryParseString(StartZ, *Tokens[StartTokenIndex + 2]))
					{
						return UnrealMcp::MakeExecutionResult(TEXT("The optional start position values must be numeric."), nullptr, true);
					}

					ArgumentsObject->SetNumberField(TEXT("startX"), StartX);
					ArgumentsObject->SetNumberField(TEXT("startY"), StartY);
					ArgumentsObject->SetNumberField(TEXT("startZ"), StartZ);
				}

				return ExecuteTool(TEXT("unreal.layout_actors_grid"), *ArgumentsObject);
			}

			if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/layout_circle")))
			{
				const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/layout_circle"));
				TArray<FString> Tokens;
				Remainder.ParseIntoArrayWS(Tokens);
				if (Tokens.Num() < 1)
				{
					return UnrealMcp::MakeExecutionResult(TEXT("Usage: /layout_circle <radius> [startAngle] [arcDegrees] [centerX centerY centerZ]"), nullptr, true);
				}

				double Radius = 0.0;
				if (!LexTryParseString(Radius, *Tokens[0]) || Radius <= 0.0)
				{
					return UnrealMcp::MakeExecutionResult(TEXT("The radius value must be a positive number."), nullptr, true);
				}

				double StartAngle = 0.0;
				double ArcDegrees = 360.0;
				if (Tokens.Num() > 1 && !LexTryParseString(StartAngle, *Tokens[1]))
				{
					return UnrealMcp::MakeExecutionResult(TEXT("The optional startAngle value must be numeric."), nullptr, true);
				}
				if (Tokens.Num() > 2 && !LexTryParseString(ArcDegrees, *Tokens[2]))
				{
					return UnrealMcp::MakeExecutionResult(TEXT("The optional arcDegrees value must be numeric."), nullptr, true);
				}

				if (Tokens.Num() != 1 && Tokens.Num() != 2 && Tokens.Num() != 3 && Tokens.Num() != 6)
				{
					return UnrealMcp::MakeExecutionResult(TEXT("Provide either only radius, radius+angles, or radius+angles+centerX centerY centerZ."), nullptr, true);
				}

				TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
				ArgumentsObject->SetBoolField(TEXT("selectedOnly"), true);
				ArgumentsObject->SetNumberField(TEXT("radius"), Radius);
				ArgumentsObject->SetNumberField(TEXT("startAngleDegrees"), StartAngle);
				ArgumentsObject->SetNumberField(TEXT("arcDegrees"), ArcDegrees);

				if (Tokens.Num() == 6)
				{
					double CenterX = 0.0;
					double CenterY = 0.0;
					double CenterZ = 0.0;
					if (!LexTryParseString(CenterX, *Tokens[3])
						|| !LexTryParseString(CenterY, *Tokens[4])
						|| !LexTryParseString(CenterZ, *Tokens[5]))
					{
						return UnrealMcp::MakeExecutionResult(TEXT("centerX, centerY, and centerZ must be numeric."), nullptr, true);
					}

					ArgumentsObject->SetNumberField(TEXT("centerX"), CenterX);
					ArgumentsObject->SetNumberField(TEXT("centerY"), CenterY);
					ArgumentsObject->SetNumberField(TEXT("centerZ"), CenterZ);
				}

				return ExecuteTool(TEXT("unreal.layout_actors_circle"), *ArgumentsObject);
			}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/open_map")))
		{
		const FString Path = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/open_map"));
		if (Path.IsEmpty())
		{
			return UnrealMcp::MakeExecutionResult(TEXT("Usage: /open_map /Game/Path/To/Map"), nullptr, true);
		}

		TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
		ArgumentsObject->SetStringField(TEXT("path"), Path);
		return ExecuteTool(TEXT("unreal.open_map"), *ArgumentsObject);
	}

	if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/open_asset")))
	{
		const FString Path = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/open_asset"));
		if (Path.IsEmpty())
		{
			return UnrealMcp::MakeExecutionResult(TEXT("Usage: /open_asset /Game/Path/To/Asset"), nullptr, true);
		}

		TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
		ArgumentsObject->SetStringField(TEXT("path"), Path);
		return ExecuteTool(TEXT("unreal.open_asset"), *ArgumentsObject);
	}

	if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/browse")))
	{
		const FString Path = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/browse"));
		if (Path.IsEmpty())
		{
			return UnrealMcp::MakeExecutionResult(TEXT("Usage: /browse /Game/Path/Or/Asset"), nullptr, true);
		}

		TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
		ArgumentsObject->SetStringField(TEXT("path"), Path);
		return ExecuteTool(TEXT("unreal.sync_content_browser"), *ArgumentsObject);
	}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/compile_bp")))
		{
		const FString Path = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/compile_bp"));
		if (Path.IsEmpty())
		{
			return UnrealMcp::MakeExecutionResult(TEXT("Usage: /compile_bp /Game/Path/To/Blueprint"), nullptr, true);
		}

		TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			ArgumentsObject->SetStringField(TEXT("path"), Path);
			return ExecuteTool(TEXT("unreal.compile_blueprint"), *ArgumentsObject);
		}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/compile_bps")))
		{
			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			const FString Path = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/compile_bps"));
			ArgumentsObject->SetStringField(TEXT("path"), Path.IsEmpty() ? TEXT("/Game") : Path);
			ArgumentsObject->SetBoolField(TEXT("recursive"), true);
			return ExecuteTool(TEXT("unreal.compile_blueprints_in_path"), *ArgumentsObject);
		}

	if (TrimmedInput.Equals(TEXT("/delete_selected"), ESearchCase::IgnoreCase))
	{
		return ExecuteTool(TEXT("unreal.destroy_selected_actors"), *UnrealMcp::MakeEmptyObject());
	}

	if (TrimmedInput.Equals(TEXT("/save"), ESearchCase::IgnoreCase))
	{
		return ExecuteTool(TEXT("unreal.save_dirty_packages"), *UnrealMcp::MakeEmptyObject());
	}

	if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/new_bp")))
	{
		const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/new_bp"));
		TArray<FString> Tokens;
		Remainder.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() < 1)
		{
			return UnrealMcp::MakeExecutionResult(TEXT("Usage: /new_bp /Game/Blueprints/BP_Name [/Script/Engine.Actor]"), nullptr, true);
		}

		TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
		ArgumentsObject->SetStringField(TEXT("assetPath"), Tokens[0]);
		ArgumentsObject->SetStringField(TEXT("parentClass"), Tokens.Num() >= 2 ? Tokens[1] : TEXT("/Script/Engine.Actor"));
		ArgumentsObject->SetBoolField(TEXT("openAfterCreate"), true);
		ArgumentsObject->SetBoolField(TEXT("compile"), true);
		return ExecuteTool(TEXT("unreal.create_blueprint_class"), *ArgumentsObject);
	}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/spawn")))
		{
		const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/spawn"));
		TArray<FString> Tokens;
		Remainder.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() < 1)
		{
			return UnrealMcp::MakeExecutionResult(TEXT("Usage: /spawn <classPath> [x y z [pitch yaw roll]] [label]"), nullptr, true);
		}

		TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
		ArgumentsObject->SetStringField(TEXT("classPath"), Tokens[0]);

		int32 TokenIndex = 1;
		double NumericValues[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
		int32 ParsedNumericCount = 0;
		for (; TokenIndex < Tokens.Num() && ParsedNumericCount < 6; ++TokenIndex)
		{
			double ParsedValue = 0.0;
			if (!LexTryParseString(ParsedValue, *Tokens[TokenIndex]))
			{
				break;
			}

			NumericValues[ParsedNumericCount++] = ParsedValue;
		}

		if (ParsedNumericCount > 0)
		{
			ArgumentsObject->SetNumberField(TEXT("x"), NumericValues[0]);
		}
		if (ParsedNumericCount > 1)
		{
			ArgumentsObject->SetNumberField(TEXT("y"), NumericValues[1]);
		}
		if (ParsedNumericCount > 2)
		{
			ArgumentsObject->SetNumberField(TEXT("z"), NumericValues[2]);
		}
		if (ParsedNumericCount > 3)
		{
			ArgumentsObject->SetNumberField(TEXT("pitch"), NumericValues[3]);
		}
		if (ParsedNumericCount > 4)
		{
			ArgumentsObject->SetNumberField(TEXT("yaw"), NumericValues[4]);
		}
		if (ParsedNumericCount > 5)
		{
			ArgumentsObject->SetNumberField(TEXT("roll"), NumericValues[5]);
		}

		if (TokenIndex < Tokens.Num())
		{
			TArray<FString> LabelTokens;
			for (int32 LabelIndex = TokenIndex; LabelIndex < Tokens.Num(); ++LabelIndex)
			{
				LabelTokens.Add(Tokens[LabelIndex]);
			}

			FString Label = FString::Join(LabelTokens, TEXT(" "));
			if (!Label.IsEmpty())
			{
				ArgumentsObject->SetStringField(TEXT("label"), Label);
			}
		}

			return ExecuteTool(TEXT("unreal.spawn_actor"), *ArgumentsObject);
		}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/spawn_batch")))
		{
			const FString JsonText = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/spawn_batch"));
			if (JsonText.IsEmpty())
			{
				return UnrealMcp::MakeExecutionResult(TEXT("Usage: /spawn_batch {\"classPath\":\"/Script/Engine.PointLight\",\"items\":[{\"x\":0,\"y\":0,\"z\":150}]}"), nullptr, true);
			}

			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			if (!UnrealMcp::LoadJsonObject(JsonText, ArgumentsObject))
			{
				return UnrealMcp::MakeExecutionResult(TEXT("Failed to parse JSON arguments for /spawn_batch."), nullptr, true);
			}

			if (!ArgumentsObject->HasField(TEXT("selectSpawned")))
			{
				ArgumentsObject->SetBoolField(TEXT("selectSpawned"), true);
			}

			return ExecuteTool(TEXT("unreal.spawn_actor_batch"), *ArgumentsObject);
		}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/py_eval")))
		{
			const FString Command = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/py_eval"));
			if (Command.IsEmpty())
			{
				return UnrealMcp::MakeExecutionResult(TEXT("Usage: /py_eval <python-expression>"), nullptr, true);
			}

			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			ArgumentsObject->SetStringField(TEXT("command"), Command);
			ArgumentsObject->SetStringField(TEXT("mode"), TEXT("EvaluateStatement"));
			ArgumentsObject->SetStringField(TEXT("scope"), TEXT("Private"));
			ArgumentsObject->SetBoolField(TEXT("forceEnable"), true);
			ArgumentsObject->SetBoolField(TEXT("unattended"), true);
			return ExecuteTool(TEXT("unreal.execute_python"), *ArgumentsObject);
		}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/py_file")))
		{
			const FString Remainder = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/py_file"));
			TArray<FString> Tokens;
			Remainder.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 1)
			{
				return UnrealMcp::MakeExecutionResult(TEXT("Usage: /py_file <scriptPath> [arg1 arg2 ...]"), nullptr, true);
			}

			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			ArgumentsObject->SetStringField(TEXT("scriptPath"), Tokens[0]);
			ArgumentsObject->SetStringField(TEXT("scope"), TEXT("Private"));
			ArgumentsObject->SetBoolField(TEXT("forceEnable"), true);
			ArgumentsObject->SetBoolField(TEXT("unattended"), true);
			ArgumentsObject->SetBoolField(TEXT("allowOutsideProject"), false);

			if (Tokens.Num() > 1)
			{
				TArray<TSharedPtr<FJsonValue>> ArgsArray;
				for (int32 Index = 1; Index < Tokens.Num(); ++Index)
				{
					ArgsArray.Add(MakeShared<FJsonValueString>(Tokens[Index]));
				}
				ArgumentsObject->SetArrayField(TEXT("args"), ArgsArray);
			}

			return ExecuteTool(TEXT("unreal.execute_python_file"), *ArgumentsObject);
		}

		if (UnrealMcp::MatchesCommand(TrimmedInput, TEXT("/py")))
		{
			const FString Command = UnrealMcp::GetCommandRemainder(TrimmedInput, TEXT("/py"));
			if (Command.IsEmpty())
			{
				return UnrealMcp::MakeExecutionResult(TEXT("Usage: /py <python-code-or-file>"), nullptr, true);
			}

			TSharedPtr<FJsonObject> ArgumentsObject = UnrealMcp::MakeEmptyObject();
			ArgumentsObject->SetStringField(TEXT("command"), Command);
			ArgumentsObject->SetStringField(TEXT("mode"), TEXT("ExecuteFile"));
			ArgumentsObject->SetStringField(TEXT("scope"), TEXT("Private"));
			ArgumentsObject->SetBoolField(TEXT("forceEnable"), true);
			ArgumentsObject->SetBoolField(TEXT("unattended"), true);
			return ExecuteTool(TEXT("unreal.execute_python"), *ArgumentsObject);
		}

		return UnrealMcp::MakeExecutionResult(TEXT("Unknown command. Try /help."), nullptr, true);
	}

TSharedRef<IUnrealMcpAssistantHandle, ESPMode::ThreadSafe> FUnrealMcpModule::ExecuteAssistantTurnAsync(
	const FString& UserPrompt,
	const FString& ConversationContext,
	const FString& PreviousResponseId,
	TFunction<void(const FUnrealMcpAssistantEvent&)> OnEvent,
	TFunction<void(const FUnrealMcpAssistantTurnResult&)> OnComplete) const
{
	return UnrealMcp::CreateAssistantRun(
		this,
		UserPrompt,
		ConversationContext,
		PreviousResponseId,
		MoveTemp(OnEvent),
		MoveTemp(OnComplete));
}

IMPLEMENT_MODULE(FUnrealMcpModule, UnrealMcp)

#undef LOCTEXT_NAMESPACE
