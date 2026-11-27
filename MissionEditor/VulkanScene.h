#pragma once

#include "RustCore.h"
#include <array>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

static_assert(sizeof(rs_scene_command) == 96, "scene command must match the shader layout");

struct RgbaBitmap
{
	uint64_t id = 0;
	int width = 0;
	int height = 0;
	std::vector<unsigned int> pixels; // 0 = transparent, 0xffRRGGBB (little-endian BGRA) = opaque

	bool empty() const
	{
		return width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height;
	}
};

uint64_t NextRgbaBitmapId();

enum class VulkanPresentResult
{
	Succeeded,
	Minimized,
	SwapchainOutOfDate,
	Failed,
};

struct MainCanvasPresentDecision
{
	bool releaseRenderer = false;
	bool scheduleCoalescedRedraw = false;
	bool scheduleImmediateRetry = false;
	bool cancelCoalescedRedraw = false;
};

struct MainCanvasPresentState
{
	static constexpr int kMaxHardFailures = 5;
	static constexpr int kMaxSwapchainRetries = 4;

	bool lastPresentSucceeded = false;
	RECT lastPresentSourceRect{};
	int consecutiveHardFailures = 0;
	int consecutiveSwapchainRetries = 0;
	bool resizePreview = false;

	void OnResizeNonZero()
	{
		consecutiveSwapchainRetries = 0;
	}

	void OnRendererReleased()
	{
		lastPresentSucceeded = false;
		resizePreview = false;
		consecutiveSwapchainRetries = 0;
	}

	MainCanvasPresentDecision ApplyResult(
		VulkanPresentResult result,
		const RECT& source,
		bool isSourceOverride)
	{
		MainCanvasPresentDecision decision;
		switch (result)
		{
		case VulkanPresentResult::Succeeded:
			consecutiveHardFailures = 0;
			consecutiveSwapchainRetries = 0;
			lastPresentSucceeded = true;
			if (!isSourceOverride)
				lastPresentSourceRect = source;
			decision.cancelCoalescedRedraw = true;
			break;

		case VulkanPresentResult::Minimized:
			lastPresentSucceeded = false;
			resizePreview = false;
			decision.cancelCoalescedRedraw = true;
			break;

		case VulkanPresentResult::SwapchainOutOfDate:
			lastPresentSucceeded = false;
			resizePreview = false;
			if (++consecutiveSwapchainRetries <= kMaxSwapchainRetries)
			{
				decision.scheduleCoalescedRedraw = true;
			}
			else
			{
				consecutiveSwapchainRetries = 0;
				decision.releaseRenderer = true;
				decision.cancelCoalescedRedraw = true;
				if (++consecutiveHardFailures < kMaxHardFailures)
					decision.scheduleImmediateRetry = true;
			}
			break;

		case VulkanPresentResult::Failed:
		default:
			lastPresentSucceeded = false;
			resizePreview = false;
			consecutiveSwapchainRetries = 0;
			decision.releaseRenderer = true;
			decision.cancelCoalescedRedraw = true;
			if (++consecutiveHardFailures < kMaxHardFailures)
				decision.scheduleImmediateRetry = true;
			break;
		}
		return decision;
	}
};

// Records scene draw commands in painter order for the Vulkan GPU compute compositor.
class VulkanScene
{
public:
	explicit VulkanScene(rs_vulkan_renderer* renderer);
	~VulkanScene() = default;

	static void InvalidateAssets();

	void Begin(const RECT& source);
	bool IsActive() const { return m_active; }
	void SaveBase();
	void RestoreBase();
	void SavePreview();
	void RestorePreview();
	bool HasPreview() const { return m_active && m_hasPreview; }
	void BeginOverlay(const RECT& screen, bool highRes);
	void EndOverlay();
	VulkanPresentResult PresentWithResult(const RECT& source, int width, int height, bool vsync);

	void Indexed(const BYTE* pixels, int width, int height, const short* borders,
		const BYTE* lighting, const int* palette, const int* remap,
		bool half, int x, int y, const RECT& clip);
	bool Bitmap(const RgbaBitmap& source, int x, int y, const RECT* sourceRect = nullptr);
	void Line(int x0, int y0, int x1, int y1, unsigned int color, int period = 1, bool invert = false);
#ifdef _DEBUG
	const std::vector<rs_scene_command>& GetCommandsForTest() const { return m_commands; }
	const RECT& GetSourceForTest() const { return m_source; }
#endif

private:
	unsigned int Upload(const unsigned int* words, size_t count);
	void DropOverlay();

	rs_vulkan_renderer* m_renderer;
	RECT m_source{};
	RECT m_screen{};
	bool m_screenSpace = false;
	bool m_active = false;
	bool m_hasPreview = false;
	bool m_failed = false;
	uint64_t m_nextKey = 0;
	uint64_t m_assetGeneration = 0;
	std::vector<rs_scene_command> m_commands, m_base, m_preview;
	size_t m_overlayStart = 0, m_overlayEnd = 0;
	std::map<std::tuple<const void*, int, int, const void*>, unsigned int> m_images;
	std::map<std::array<unsigned int, 256>, unsigned int> m_palettes;
	struct BitmapAsset { unsigned int offset; int width, height; };
	std::map<uint64_t, BitmapAsset> m_bitmaps;
};
