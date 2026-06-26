#include "Dom/JsonObject.h"
#include "McpAutomationBridgeSubsystem.h"
#include "McpAutomationBridgeHelpers.h"
#include "McpAutomationBridgeGlobals.h"
#include "Misc/OutputDevice.h"
#include "Async/Async.h"

// Ring buffer entry for log history
struct FMcpLogEntry
{
    FString Category;
    FString Verbosity;
    FString Message;
    double Timestamp;
};

// Define a custom output device to capture logs and stream them via the bridge
class FMcpLogOutputDevice : public FOutputDevice
{
public:
    FMcpLogOutputDevice(UMcpAutomationBridgeSubsystem* InSubsystem, bool bEnableStreaming = true)
        : Subsystem(InSubsystem)
        , bStreamingEnabled(bEnableStreaming)
    {
        // Pre-allocate ring buffer
        LogRingBuffer.SetNum(MaxRingBufferSize);
        RingWriteIndex = 0;
        TotalWritten = 0;
    }

    void SetStreamingEnabled(bool bEnabled) { bStreamingEnabled = bEnabled; }

    static constexpr int32 MaxRingBufferSize = 2000;

    // Read recent log entries. Returns up to Count entries, optionally filtered.
    TArray<FMcpLogEntry> ReadRecentLogs(int32 Count, const FString& CategoryFilter, const FString& VerbosityFilter) const
    {
        FScopeLock Lock(&RingBufferLock);
        TArray<FMcpLogEntry> Result;
        int32 Available = FMath::Min((int32)TotalWritten, MaxRingBufferSize);
        int32 StartIdx = (RingWriteIndex - Available + MaxRingBufferSize) % MaxRingBufferSize;

        for (int32 i = 0; i < Available && Result.Num() < Count; ++i)
        {
            int32 Idx = (StartIdx + i) % MaxRingBufferSize;
            const FMcpLogEntry& Entry = LogRingBuffer[Idx];
            if (Entry.Message.IsEmpty()) continue;

            // Apply filters
            if (!CategoryFilter.IsEmpty() && !Entry.Category.Contains(CategoryFilter, ESearchCase::IgnoreCase))
                continue;
            if (!VerbosityFilter.IsEmpty() && !Entry.Verbosity.Equals(VerbosityFilter, ESearchCase::IgnoreCase))
                continue;

            Result.Add(Entry);
        }
        return Result;
    }

    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
    {
        if (!Subsystem || !Subsystem->IsValidLowLevel())
        {
            return;
        }

        FString CategoryStr = Category.ToString();

        if (Category == LogMcpAutomationBridgeSubsystem.GetCategoryName() ||
            CategoryStr == TEXT("LogRHI") ||
            CategoryStr == TEXT("LogEOSSDK") ||
            CategoryStr == TEXT("LogCsvProfiler"))
        {
            return;
        }

        if (Verbosity == ELogVerbosity::Warning && CategoryStr == TEXT("LogSlateStyle"))
        {
            if (FString(V).Contains(TEXT("Missing Resource from 'ProfileVisualizerStyle'")))
            {
                return;
            }
        }

        if (CategoryStr == TEXT("LogStats"))
        {
             if (FString(V).Contains(TEXT("There is no thread with id")))
             {
                 return;
             }
        }

        FString VerbosityString;
        switch (Verbosity)
        {
            case ELogVerbosity::Fatal: VerbosityString = TEXT("Fatal"); break;
            case ELogVerbosity::Error: VerbosityString = TEXT("Error"); break;
            case ELogVerbosity::Warning: VerbosityString = TEXT("Warning"); break;
            case ELogVerbosity::Display: VerbosityString = TEXT("Display"); break;
            case ELogVerbosity::Log: VerbosityString = TEXT("Log"); break;
            case ELogVerbosity::Verbose: VerbosityString = TEXT("Verbose"); break;
            case ELogVerbosity::VeryVerbose: VerbosityString = TEXT("VeryVerbose"); break;
            default: VerbosityString = TEXT("Log"); break;
        }

        FString Message = FString(V);
        FString CategoryString = Category.ToString();

        // Store in ring buffer
        {
            FScopeLock Lock(&RingBufferLock);
            FMcpLogEntry& Entry = LogRingBuffer[RingWriteIndex % MaxRingBufferSize];
            Entry.Category = CategoryString;
            Entry.Verbosity = VerbosityString;
            Entry.Message = Message;
            Entry.Timestamp = FPlatformTime::Seconds();
            RingWriteIndex = (RingWriteIndex + 1) % MaxRingBufferSize;
            ++TotalWritten;
        }

        // Stream to connected sockets only if explicitly enabled (subscribe action)
        if (bStreamingEnabled)
        {
            const FString PayloadJson = FString::Printf(TEXT("{\"event\":\"log\",\"category\":\"%s\",\"verbosity\":\"%s\",\"message\":\"%s\"}"),
                *CategoryString, *VerbosityString, *Message.ReplaceCharWithEscapedChar());

            TWeakObjectPtr<UMcpAutomationBridgeSubsystem> WeakSubsystem(Subsystem);

            AsyncTask(ENamedThreads::GameThread, [WeakSubsystem, PayloadJson]()
            {
                if (UMcpAutomationBridgeSubsystem* StrongSubsystem = WeakSubsystem.Get())
                {
                   StrongSubsystem->SendRawMessage(PayloadJson);
                }
            });
        }
    }

private:
    UMcpAutomationBridgeSubsystem* Subsystem;
    bool bStreamingEnabled;
    mutable FCriticalSection RingBufferLock;
    TArray<FMcpLogEntry> LogRingBuffer;
    int32 RingWriteIndex;
    uint64 TotalWritten;
};

bool UMcpAutomationBridgeSubsystem::HandleLogAction(const FString& RequestId, const FString& Action, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FMcpBridgeWebSocket> RequestingSocket)
{
    if (Action != TEXT("manage_logs"))
    {
        return false;
    }

    if (!Payload.IsValid())
    {
        SendAutomationError(RequestingSocket, RequestId, TEXT("Missing payload."), TEXT("INVALID_PAYLOAD"));
        return true;
    }

    FString SubAction = GetJsonStringField(Payload, TEXT("subAction"));

    if (SubAction == TEXT("subscribe"))
    {
        if (!LogCaptureDevice.IsValid())
        {
            LogCaptureDevice = MakeShared<FMcpLogOutputDevice>(this, /*bEnableStreaming=*/true);
            GLog->AddOutputDevice(LogCaptureDevice.Get());
            UE_LOG(LogMcpAutomationBridgeSubsystem, Display, TEXT("Log streaming enabled by client request."));
        }
        else
        {
            // Device already exists (maybe from read auto-subscribe), enable streaming
            static_cast<FMcpLogOutputDevice*>(LogCaptureDevice.Get())->SetStreamingEnabled(true);
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("action"), TEXT("subscribe"));
        Result->SetBoolField(TEXT("subscribed"), true);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Subscribed to editor logs."), Result);
        return true;
    }
    else if (SubAction == TEXT("unsubscribe"))
    {
        if (LogCaptureDevice.IsValid())
        {
            GLog->RemoveOutputDevice(LogCaptureDevice.Get());
            LogCaptureDevice.Reset();
            UE_LOG(LogMcpAutomationBridgeSubsystem, Display, TEXT("Log streaming disabled by client request."));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("action"), TEXT("unsubscribe"));
        Result->SetBoolField(TEXT("subscribed"), false);
        SendAutomationResponse(RequestingSocket, RequestId, true, TEXT("Unsubscribed from editor logs."), Result);
        return true;
    }
    else if (SubAction == TEXT("read") || SubAction == TEXT("read_log") || SubAction == TEXT("get_log"))
    {
        // Ensure the log capture device exists (auto-subscribe for capture only, no streaming)
        if (!LogCaptureDevice.IsValid())
        {
            LogCaptureDevice = MakeShared<FMcpLogOutputDevice>(this, /*bEnableStreaming=*/false);
            GLog->AddOutputDevice(LogCaptureDevice.Get());
        }

        int32 Count = 100;
        if (Payload->HasField(TEXT("count")))
        {
            Count = static_cast<int32>(Payload->GetNumberField(TEXT("count")));
        }
        Count = FMath::Clamp(Count, 1, 2000);

        FString CategoryFilter;
        Payload->TryGetStringField(TEXT("category"), CategoryFilter);
        FString VerbosityFilter;
        Payload->TryGetStringField(TEXT("verbosity"), VerbosityFilter);

        FMcpLogOutputDevice* Device = static_cast<FMcpLogOutputDevice*>(LogCaptureDevice.Get());
        TArray<FMcpLogEntry> Entries = Device->ReadRecentLogs(Count, CategoryFilter, VerbosityFilter);

        TArray<TSharedPtr<FJsonValue>> LogArray;
        for (const FMcpLogEntry& Entry : Entries)
        {
            TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
            EntryObj->SetStringField(TEXT("category"), Entry.Category);
            EntryObj->SetStringField(TEXT("verbosity"), Entry.Verbosity);
            EntryObj->SetStringField(TEXT("message"), Entry.Message);
            EntryObj->SetNumberField(TEXT("timestamp"), Entry.Timestamp);
            LogArray.Add(MakeShared<FJsonValueObject>(EntryObj));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("action"), TEXT("read"));
        Result->SetArrayField(TEXT("logs"), LogArray);
        Result->SetNumberField(TEXT("count"), LogArray.Num());
        Result->SetNumberField(TEXT("requested"), Count);
        SendAutomationResponse(RequestingSocket, RequestId, true,
            FString::Printf(TEXT("Retrieved %d log entries"), LogArray.Num()), Result);
        return true;
    }

    SendAutomationError(RequestingSocket, RequestId, TEXT("Unknown subAction. Valid: subscribe, unsubscribe, read"), TEXT("INVALID_SUBACTION"));
    return true;
}
