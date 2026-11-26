#pragma once

#include <cstdint>
#include <string>
#include "VulkanScene.h"

enum class TileSetVulkanRecoveryState
{
	Healthy,
	RetryScheduled,
	RetryPaused,
	RetryDue,
	Stopped,
};

enum class TileSetVulkanInitResult
{
	Succeeded,
	Minimized,
	SwapchainOutOfDate,
	Failed,
};

struct TileSetVulkanRecoveryAction
{
	bool startRetryTimer = false;
	unsigned int retryDelayMs = 0;
	uint64_t retryTimerToken = 0;
	bool cancelRetryTimer = false;
	bool startTransientTimer = false;
	unsigned int transientDelayMs = 0;
	uint64_t transientTimerToken = 0;
	bool cancelTransientTimer = false;
	bool invalidate = false;
	bool shouldReleaseRenderer = false;
	std::string logMessage;
};

// Bounded automatic recovery state machine for CTileSetBrowserView's Vulkan
// renderer. Keeps failed renderers torn down between delayed retries, caps
// consecutive failures, ignores normal minimize/resize churn, and requires
// explicit user retry once automatic recovery stops.
class TileSetVulkanRecovery
{
public:
	static constexpr int kDefaultMaxAutoRetries = 3;
	static constexpr unsigned int kDefaultRetryDelayMs = 250;
	static constexpr int kDefaultMaxTransientSwapchainRetries = 3;
	static constexpr unsigned int kDefaultTransientSwapchainDelayMs = 50;
	static constexpr uint64_t kRetryTimerTokenBase = 0x10000u;
	static constexpr uint64_t kTransientTimerTokenBase = 0x20000u;
	static constexpr uint64_t kTimerTokenGenerationMask = 0xFFFFu;

	static bool IsRetryTimerToken(uint64_t token)
	{
		return token >= kRetryTimerTokenBase && token <= (kRetryTimerTokenBase | kTimerTokenGenerationMask);
	}

	static bool IsTransientTimerToken(uint64_t token)
	{
		return token >= kTransientTimerTokenBase && token <= (kTransientTimerTokenBase | kTimerTokenGenerationMask);
	}

	explicit TileSetVulkanRecovery(
		int maxAutoRetries = kDefaultMaxAutoRetries,
		unsigned int retryDelayMs = kDefaultRetryDelayMs,
		int maxTransientSwapchainRetries = kDefaultMaxTransientSwapchainRetries,
		unsigned int transientDelayMs = kDefaultTransientSwapchainDelayMs)
		: m_maxAutoRetries(maxAutoRetries < 0 ? 0 : maxAutoRetries)
		, m_retryDelayMs(retryDelayMs)
		, m_maxTransientSwapchainRetries(maxTransientSwapchainRetries < 0 ? 0 : maxTransientSwapchainRetries)
		, m_transientDelayMs(transientDelayMs)
	{
	}

	TileSetVulkanRecoveryState GetState() const { return m_state; }
	int GetConsecutiveFailures() const { return m_consecutiveFailures; }
	int GetMaxAutoRetries() const { return m_maxAutoRetries; }
	int GetConsecutiveSwapchainRetries() const { return m_consecutiveSwapchainRetries; }
	uint64_t GetActiveRetryTimerToken() const { return m_activeRetryTimerToken; }
	uint64_t GetActiveTransientTimerToken() const { return m_activeTransientTimerToken; }
	bool IsWaitingForTransientRetry() const { return m_activeTransientTimerToken != 0; }
	bool IsStopped() const { return m_state == TileSetVulkanRecoveryState::Stopped; }
	bool IsRecovering() const
	{
		return m_state == TileSetVulkanRecoveryState::RetryScheduled
			|| m_state == TileSetVulkanRecoveryState::RetryPaused
			|| m_state == TileSetVulkanRecoveryState::RetryDue;
	}
	bool ShouldDrawFallback() const
	{
		return m_state == TileSetVulkanRecoveryState::Stopped
			|| m_state == TileSetVulkanRecoveryState::RetryScheduled
			|| m_state == TileSetVulkanRecoveryState::RetryPaused;
	}
	const std::string& GetFirstError() const { return m_firstError; }
	const std::string& GetLastError() const { return m_lastError; }

	bool CanAttemptRender(bool visible, int width, int height, TileSetVulkanRecoveryAction& outAction)
	{
		outAction = {};
		if (m_destroyed)
			return false;

		if (!visible || width <= 0 || height <= 0)
		{
			outAction = OnMinimizedOrHidden();
			return false;
		}

		if (m_state == TileSetVulkanRecoveryState::Healthy || m_state == TileSetVulkanRecoveryState::RetryDue)
		{
			if (m_activeTransientTimerToken != 0)
				return false;
			if (m_lastWidth == 0 && m_lastHeight == 0)
			{
				m_lastWidth = width;
				m_lastHeight = height;
			}
			return true;
		}

		if (m_state == TileSetVulkanRecoveryState::RetryPaused)
		{
			m_state = TileSetVulkanRecoveryState::RetryScheduled;
			m_activeRetryTimerToken = NextRetryTimerToken();
			outAction.startRetryTimer = true;
			outAction.retryDelayMs = m_retryDelayMs;
			outAction.retryTimerToken = m_activeRetryTimerToken;
		}
		return false;
	}

	TileSetVulkanRecoveryAction OnInitializeResult(TileSetVulkanInitResult result, const std::string& errorMessage)
	{
		switch (result)
		{
		case TileSetVulkanInitResult::Succeeded:
			return {};
		case TileSetVulkanInitResult::Minimized:
			return OnMinimizedOrHidden();
		case TileSetVulkanInitResult::SwapchainOutOfDate:
			return OnSwapchainOutOfDate("initialization", errorMessage);
		case TileSetVulkanInitResult::Failed:
			return RecordFailure("initialization", errorMessage);
		}
		return {};
	}

	TileSetVulkanRecoveryAction OnPresentResult(VulkanPresentResult result, const std::string& errorMessage)
	{
		switch (result)
		{
		case VulkanPresentResult::Succeeded:
		{
			TileSetVulkanRecoveryAction action{};
			m_state = TileSetVulkanRecoveryState::Healthy;
			m_consecutiveFailures = 0;
			m_consecutiveSwapchainRetries = 0;
			m_activeRetryTimerToken = 0;
			m_activeTransientTimerToken = 0;
			m_firstError.clear();
			m_lastError.clear();
			m_hasLoggedFirstError = false;
			m_hasLoggedStopped = false;
			action.cancelRetryTimer = true;
			action.cancelTransientTimer = true;
			return action;
		}
		case VulkanPresentResult::Minimized:
			return OnMinimizedOrHidden();
		case VulkanPresentResult::SwapchainOutOfDate:
			return OnSwapchainOutOfDate("present", errorMessage);
		case VulkanPresentResult::Failed:
			return RecordFailure("present", errorMessage);
		}
		return {};
	}

	TileSetVulkanRecoveryAction OnMinimizedOrHidden()
	{
		TileSetVulkanRecoveryAction action{};
		m_activeTransientTimerToken = 0;
		action.cancelTransientTimer = true;
		if (m_state == TileSetVulkanRecoveryState::RetryScheduled || m_state == TileSetVulkanRecoveryState::RetryDue)
		{
			m_state = TileSetVulkanRecoveryState::RetryPaused;
			m_activeRetryTimerToken = 0;
			action.cancelRetryTimer = true;
		}
		return action;
	}

	TileSetVulkanRecoveryAction OnSize(bool minimized, int width, int height, bool visible)
	{
		TileSetVulkanRecoveryAction action{};
		if (m_destroyed)
			return action;

		if (minimized || width <= 0 || height <= 0)
			return OnMinimizedOrHidden();

		const bool sizeChanged = (width != m_lastWidth || height != m_lastHeight);
		if (sizeChanged)
		{
			m_lastWidth = width;
			m_lastHeight = height;
			m_consecutiveSwapchainRetries = 0;
			m_activeTransientTimerToken = 0;
			action.cancelTransientTimer = true;
			action.invalidate = true;
		}
		else if (m_activeTransientTimerToken == 0)
		{
			action.invalidate = true;
		}

		if (m_state == TileSetVulkanRecoveryState::RetryPaused && visible)
		{
			m_state = TileSetVulkanRecoveryState::RetryScheduled;
			m_activeRetryTimerToken = NextRetryTimerToken();
			action.startRetryTimer = true;
			action.retryDelayMs = m_retryDelayMs;
			action.retryTimerToken = m_activeRetryTimerToken;
		}
		return action;
	}

	TileSetVulkanRecoveryAction OnShowWindow(bool visible, int width, int height)
	{
		TileSetVulkanRecoveryAction action{};
		if (m_destroyed)
			return action;

		if (!visible)
			return OnMinimizedOrHidden();

		if (m_state == TileSetVulkanRecoveryState::RetryPaused && width > 0 && height > 0)
		{
			m_state = TileSetVulkanRecoveryState::RetryScheduled;
			m_activeRetryTimerToken = NextRetryTimerToken();
			action.startRetryTimer = true;
			action.retryDelayMs = m_retryDelayMs;
			action.retryTimerToken = m_activeRetryTimerToken;
			action.invalidate = true;
		}
		return action;
	}

	TileSetVulkanRecoveryAction OnRetryTimer(uint64_t timerToken, bool visible, int width, int height)
	{
		TileSetVulkanRecoveryAction action{};
		if (m_destroyed || timerToken == 0 || timerToken != m_activeRetryTimerToken)
			return action;

		m_activeRetryTimerToken = 0;
		action.cancelRetryTimer = true;
		if (m_state != TileSetVulkanRecoveryState::RetryScheduled)
			return action;

		if (!visible || width <= 0 || height <= 0)
		{
			m_state = TileSetVulkanRecoveryState::RetryPaused;
			return action;
		}

		m_state = TileSetVulkanRecoveryState::RetryDue;
		action.invalidate = true;
		return action;
	}

	TileSetVulkanRecoveryAction OnTransientSwapchainTimer(uint64_t timerToken, bool visible, int width, int height)
	{
		TileSetVulkanRecoveryAction action{};
		if (m_destroyed || timerToken == 0 || timerToken != m_activeTransientTimerToken)
			return action;

		m_activeTransientTimerToken = 0;
		action.cancelTransientTimer = true;
		if (!visible || width <= 0 || height <= 0)
			return action;

		if (m_state == TileSetVulkanRecoveryState::Healthy || m_state == TileSetVulkanRecoveryState::RetryDue)
			action.invalidate = true;
		return action;
	}

	TileSetVulkanRecoveryAction OnRetryTimerScheduleFailed()
	{
		TileSetVulkanRecoveryAction action{};
		m_activeRetryTimerToken = 0;
		m_activeTransientTimerToken = 0;
		action.cancelRetryTimer = true;
		action.cancelTransientTimer = true;
		action.shouldReleaseRenderer = true;
		action.invalidate = true;
		m_state = TileSetVulkanRecoveryState::Stopped;
		if (!m_hasLoggedStopped)
		{
			m_hasLoggedStopped = true;
			action.logMessage = BuildStoppedLogMessage("could not schedule retry timer");
		}
		return action;
	}

	TileSetVulkanRecoveryAction OnTransientTimerScheduleFailed()
	{
		m_activeTransientTimerToken = 0;
		return RecordFailure("swapchain recovery", "could not schedule transient swapchain retry timer");
	}

	TileSetVulkanRecoveryAction RequestManualRetry(bool visible, int width, int height)
	{
		TileSetVulkanRecoveryAction action{};
		if (m_destroyed)
			return action;

		m_activeRetryTimerToken = 0;
		m_activeTransientTimerToken = 0;
		action.cancelRetryTimer = true;
		action.cancelTransientTimer = true;
		m_consecutiveFailures = 0;
		m_consecutiveSwapchainRetries = 0;
		m_firstError.clear();
		m_lastError.clear();
		m_hasLoggedFirstError = false;
		m_hasLoggedStopped = false;
		m_state = TileSetVulkanRecoveryState::Healthy;
		if (visible && width > 0 && height > 0)
			action.invalidate = true;
		return action;
	}

	TileSetVulkanRecoveryAction OnDestroy()
	{
		TileSetVulkanRecoveryAction action{};
		m_destroyed = true;
		m_activeRetryTimerToken = 0;
		m_activeTransientTimerToken = 0;
		action.cancelRetryTimer = true;
		action.cancelTransientTimer = true;
		action.shouldReleaseRenderer = true;
		if (m_state == TileSetVulkanRecoveryState::RetryScheduled || m_state == TileSetVulkanRecoveryState::RetryDue)
			m_state = TileSetVulkanRecoveryState::RetryPaused;
		return action;
	}

private:
	uint64_t NextRetryTimerToken()
	{
		const uint64_t token = kRetryTimerTokenBase | (m_nextRetryTimerGeneration & kTimerTokenGenerationMask);
		++m_nextRetryTimerGeneration;
		return token;
	}

	uint64_t NextTransientTimerToken()
	{
		const uint64_t token = kTransientTimerTokenBase | (m_nextTransientTimerGeneration & kTimerTokenGenerationMask);
		++m_nextTransientTimerGeneration;
		return token;
	}

	TileSetVulkanRecoveryAction OnSwapchainOutOfDate(const char* stage, const std::string& errorMessage)
	{
		if (m_consecutiveSwapchainRetries < m_maxTransientSwapchainRetries)
		{
			++m_consecutiveSwapchainRetries;
			m_activeTransientTimerToken = NextTransientTimerToken();
			TileSetVulkanRecoveryAction action{};
			action.startTransientTimer = true;
			action.transientDelayMs = m_transientDelayMs;
			action.transientTimerToken = m_activeTransientTimerToken;
			return action;
		}

		const std::string fallbackError = errorMessage.empty()
			? "swapchain remained out of date across repeated redraws"
			: errorMessage;
		return RecordFailure(stage, fallbackError);
	}

	TileSetVulkanRecoveryAction RecordFailure(const char* stage, const std::string& errorMessage)
	{
		TileSetVulkanRecoveryAction action{};
		action.shouldReleaseRenderer = true;
		action.cancelTransientTimer = true;
		m_activeTransientTimerToken = 0;
		m_consecutiveSwapchainRetries = 0;

		const std::string normalized = errorMessage.empty() ? "unknown Vulkan error" : errorMessage;
		m_lastError = normalized;

		std::string firstLog;
		if (!m_hasLoggedFirstError)
		{
			m_firstError = normalized;
			m_hasLoggedFirstError = true;
			firstLog = std::string("TileSetBrowserView Vulkan ") + stage + " failed: " + m_firstError;
		}

		++m_consecutiveFailures;
		if (m_consecutiveFailures <= m_maxAutoRetries)
		{
			m_state = TileSetVulkanRecoveryState::RetryScheduled;
			m_activeRetryTimerToken = NextRetryTimerToken();
			action.startRetryTimer = true;
			action.retryDelayMs = m_retryDelayMs;
			action.retryTimerToken = m_activeRetryTimerToken;
			action.logMessage = firstLog;
			return action;
		}

		m_state = TileSetVulkanRecoveryState::Stopped;
		m_activeRetryTimerToken = 0;
		action.cancelRetryTimer = true;
		if (!m_hasLoggedStopped)
		{
			m_hasLoggedStopped = true;
			const std::string stoppedLog = BuildStoppedLogMessage(nullptr);
			if (!firstLog.empty())
				action.logMessage = firstLog + "\n" + stoppedLog;
			else
				action.logMessage = stoppedLog;
		}
		else
		{
			action.logMessage = firstLog;
		}
		return action;
	}

	std::string BuildStoppedLogMessage(const char* extraReason) const
	{
		std::string message = "TileSetBrowserView Vulkan auto-recovery stopped after "
			+ std::to_string(m_maxAutoRetries) + " retries";
		if (extraReason != nullptr && *extraReason != '\0')
			message += std::string(" (") + extraReason + ")";
		if (!m_firstError.empty())
		{
			message += "; first error: " + m_firstError;
			if (!m_lastError.empty() && m_lastError != m_firstError)
				message += "; last error: " + m_lastError;
		}
		return message;
	}

	int m_maxAutoRetries = kDefaultMaxAutoRetries;
	unsigned int m_retryDelayMs = kDefaultRetryDelayMs;
	int m_maxTransientSwapchainRetries = kDefaultMaxTransientSwapchainRetries;
	unsigned int m_transientDelayMs = kDefaultTransientSwapchainDelayMs;
	TileSetVulkanRecoveryState m_state = TileSetVulkanRecoveryState::Healthy;
	int m_consecutiveFailures = 0;
	int m_consecutiveSwapchainRetries = 0;
	uint64_t m_activeRetryTimerToken = 0;
	uint64_t m_activeTransientTimerToken = 0;
	uint64_t m_nextRetryTimerGeneration = 1;
	uint64_t m_nextTransientTimerGeneration = 1;
	int m_lastWidth = 0;
	int m_lastHeight = 0;
	bool m_hasLoggedFirstError = false;
	bool m_hasLoggedStopped = false;
	bool m_destroyed = false;
	std::string m_firstError;
	std::string m_lastError;
};
