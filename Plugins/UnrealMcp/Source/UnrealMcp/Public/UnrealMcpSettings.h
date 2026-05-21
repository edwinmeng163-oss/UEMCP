#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UnrealMcpSettings.generated.h"

struct FPropertyChangedChainEvent;

UENUM()
enum class EAiProviderKind : uint8
{
	// Append-only. Never renumber or remove a kind, even if deprecated; UE serialization persists by numeric value. New kinds get the next free integer.
	OpenAiResponses    = 0 UMETA(DisplayName="OpenAI Responses API"),
	OpenAiChatCompat   = 1 UMETA(DisplayName="OpenAI-Compatible (chat/completions: Kimi/GLM/DeepSeek/Qwen/Ollama)"),
	AnthropicMessages  = 2 UMETA(DisplayName="Anthropic Messages"),
	Codex              = 3 UMETA(DisplayName="Codex CLI (local subprocess)"),
	CodexAppServer     = 4 UMETA(DisplayName="Codex Desktop / App Server (Plan B bridge)"),
};

USTRUCT()
struct UNREALMCP_API FAiProviderConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Stable provider key used by ActiveProviderId, for example openai-default."))
	FString Id;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Human-readable provider name shown in settings."))
	FString DisplayName;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Provider protocol or integration type."))
	EAiProviderKind Kind = EAiProviderKind::OpenAiResponses;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(GetOptions="GetProviderPresetOptions", ToolTip="Stable provider preset id, for example kimi, deepseek, or openai-responses. Empty means custom/backward-compatible behavior."))
	FString PresetId;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Provider endpoint URL. For chat-compatible providers, use the chat/completions endpoint."))
	FString BaseUrl;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="API key for this provider. Leave empty for local providers that do not require one.", PasswordField=true))
	FString ApiKey;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Model identifier sent to this provider."))
	FString Model;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Optional reasoning effort. Used only by OpenAI Responses and Anthropic Messages providers."))
	FString ReasoningEffort;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ClampMin="0", ToolTip="Per-provider output token limit. Set to 0 to fall back to the global AiMaxOutputTokens setting."))
	int32 MaxOutputTokens = 4096;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Path to the codex CLI binary (e.g. /opt/homebrew/bin/codex on Apple Silicon Homebrew). Find via `which codex` in a terminal. Used only by the Codex CLI provider. v0.25+ uses codex exec directly; codex-agent wrapper is no longer supported."))
	FString CodexBinaryPath;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Additional flags appended to `codex exec` invocation. Uses codex CLI -c key=\"value\" syntax. Example: -c reasoning_effort=\"high\". Baseline flags (model, sandbox_mode, reasoning_effort) are auto-injected. v0.25 no longer accepts the legacy -r/--reasoning flags (codex exec rejects them)."))
	FString CodexExtraArgs;
};

UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="Unreal MCP"))
class UNREALMCP_API UUnrealMcpSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UUnrealMcpSettings();

	virtual void PostInitProperties() override;
	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
	const FAiProviderConfig* FindActiveProvider() const;

	UFUNCTION()
	TArray<FString> GetProviderPresetOptions() const;

	UPROPERTY(EditAnywhere, Config, Category="Server")
	bool bEnableServer = true;

	UPROPERTY(EditAnywhere, Config, Category="Server", meta=(ClampMin="1024", ClampMax="65535"))
	int32 Port = 8765;

	UPROPERTY(EditAnywhere, Config, Category="Server")
	FString EndpointPath = TEXT("/mcp");

	UPROPERTY(EditAnywhere, Config, Category="Security", meta=(ToolTip="Optional bearer token. When set, clients must send Authorization: Bearer <token>."))
	FString AuthToken;

	UPROPERTY(EditAnywhere, Config, Category="Security", meta=(ToolTip="Requests with an Origin header must match one of these values. Leave the defaults unless you know you need more."))
	TArray<FString> AllowedOrigins;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Enable in-editor AI chat requests such as /ask."))
	bool bEnableAiAssistant = false;

	//   Example: Kimi (Moonshot)  Kind=OpenAiChatCompat BaseUrl=https://api.moonshot.cn/v1/chat/completions Model=moonshot-v1-8k
	//   Example: GLM (Zhipu)      Kind=OpenAiChatCompat BaseUrl=https://open.bigmodel.cn/api/paas/v4/chat/completions Model=glm-4
	//   Example: DeepSeek         Kind=OpenAiChatCompat BaseUrl=https://api.deepseek.com/v1/chat/completions Model=deepseek-chat
	//   Example: Anthropic        Kind=AnthropicMessages BaseUrl=https://api.anthropic.com/v1/messages Model=claude-sonnet-4-6
	//   Example: Codex            Kind=Codex CodexBinaryPath=/opt/homebrew/bin/codex CodexExtraArgs="-c reasoning_effort=\"high\""
	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(TitleProperty="DisplayName"))
	TArray<FAiProviderConfig> Providers;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ToolTip="Id of the provider currently used by the chat panel and /ask. Must match one of Providers[].Id."))
	FString ActiveProviderId;

	UPROPERTY(Config, meta=(DeprecatedProperty, DeprecationMessage="Use Providers[] + ActiveProviderId instead. This field will be removed in a future version."))
	FString OpenAIResponsesUrl = TEXT("https://api.openai.com/v1/responses");

	UPROPERTY(Config, meta=(PasswordField=true, DeprecatedProperty, DeprecationMessage="Use Providers[] + ActiveProviderId instead. This field will be removed in a future version."))
	FString OpenAIApiKey;

	UPROPERTY(Config, meta=(DeprecatedProperty, DeprecationMessage="Use Providers[] + ActiveProviderId instead. This field will be removed in a future version."))
	FString OpenAIModel = TEXT("gpt-5.1");

	UPROPERTY(Config, meta=(DeprecatedProperty, DeprecationMessage="Use Providers[] + ActiveProviderId instead. This field will be removed in a future version."))
	FString OpenAIReasoningEffort = TEXT("medium");

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ClampMin="1", ClampMax="24", ToolTip="Maximum number of model->tool->model iterations allowed for one /ask request."))
	int32 AiMaxToolRounds = 16;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ClampMin="128", ClampMax="8192", ToolTip="Upper bound for response tokens returned by the model for each request. Larger values reduce truncation but may cost more."))
	int32 AiMaxOutputTokens = 4096;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ClampMin="10", ClampMax="600", ToolTip="Total timeout in seconds for one OpenAI Responses request. Increase this for longer planning or tool-heavy turns."))
	float AiRequestTimeoutSeconds = 180.0f;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(ClampMin="10", ClampMax="600", ToolTip="Idle timeout in seconds while waiting for more streamed data from the AI provider."))
	float AiRequestActivityTimeoutSeconds = 120.0f;

	UPROPERTY(EditAnywhere, Config, Category="AI", meta=(MultiLine=true, ToolTip="Optional additional instructions appended to the built-in assistant prompt."))
	FString AssistantSystemPrompt;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FUnrealMcpProviderPresetsLegacyOpenAIMigrationTest;
#endif
	static const TCHAR* const BackupFileRelativePath;
	void ApplyLegacyOpenAIMigration_IfNeeded();
	void WriteProvidersBackup() const;
	bool LoadProvidersBackup();
#if WITH_EDITOR
	bool TryApplyPresetForProviderChange(const FPropertyChangedEvent& Event);
	bool IsProvidersChangeEvent(const FPropertyChangedEvent& Event) const;
#endif
};
