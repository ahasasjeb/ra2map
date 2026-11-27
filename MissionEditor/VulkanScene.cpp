#include "StdAfx.h"
#include "VulkanScene.h"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>

namespace
{
	std::atomic<uint64_t> assetGeneration{ 1 };
	std::atomic<uint64_t> nextBitmapId{ 1 };

	bool ClipLineToRect(int& x0, int& y0, int& x1, int& y1, const RECT& clip)
	{
		const double minX = static_cast<double>(clip.left);
		const double minY = static_cast<double>(clip.top);
		const double maxX = static_cast<double>(clip.right - 1);
		const double maxY = static_cast<double>(clip.bottom - 1);
		if (maxX < minX || maxY < minY)
			return false;

		const double dx = static_cast<double>(x1) - x0;
		const double dy = static_cast<double>(y1) - y0;
		double t0 = 0.0;
		double t1 = 1.0;

		auto clipEdge = [&](double p, double q) -> bool
		{
			if (p == 0.0)
				return q >= 0.0;
			const double r = q / p;
			if (p < 0.0)
			{
				if (r > t1) return false;
				if (r > t0) t0 = r;
			}
			else
			{
				if (r < t0) return false;
				if (r < t1) t1 = r;
			}
			return true;
		};

		if (!clipEdge(-dx, static_cast<double>(x0) - minX) ||
			!clipEdge(dx, maxX - static_cast<double>(x0)) ||
			!clipEdge(-dy, static_cast<double>(y0) - minY) ||
			!clipEdge(dy, maxY - static_cast<double>(y0)))
			return false;

		const int nx0 = static_cast<int>(std::lround(x0 + t0 * dx));
		const int ny0 = static_cast<int>(std::lround(y0 + t0 * dy));
		const int nx1 = static_cast<int>(std::lround(x0 + t1 * dx));
		const int ny1 = static_cast<int>(std::lround(y0 + t1 * dy));
		x0 = nx0;
		y0 = ny0;
		x1 = nx1;
		y1 = ny1;
		return true;
	}
}

uint64_t NextRgbaBitmapId()
{
	return nextBitmapId.fetch_add(1, std::memory_order_relaxed);
}

VulkanScene::VulkanScene(rs_vulkan_renderer* renderer)
	: m_renderer(renderer)
{
}

void VulkanScene::InvalidateAssets()
{
	assetGeneration.fetch_add(1, std::memory_order_relaxed);
}

void VulkanScene::Begin(const RECT& source)
{
	const uint64_t currentGen = assetGeneration.load(std::memory_order_relaxed);
	if (m_assetGeneration != currentGen)
	{
		m_failed = rs_vulkan_scene_reset(m_renderer) != RS_OK;
		m_images.clear();
		m_palettes.clear();
		m_bitmaps.clear();
		m_base.clear();
		m_preview.clear();
		m_nextKey = 0;
		m_assetGeneration = currentGen;
	}
	m_active = true;
	m_hasPreview = false;
	m_source = source;
	m_screenSpace = false;
	m_commands.clear();
	m_overlayStart = m_overlayEnd = 0;
}

void VulkanScene::DropOverlay()
{
	if (m_overlayEnd > m_overlayStart && m_overlayEnd <= m_commands.size())
		m_commands.erase(m_commands.begin() + m_overlayStart, m_commands.begin() + m_overlayEnd);
	m_overlayStart = m_overlayEnd = 0;
}

void VulkanScene::SaveBase()
{
	DropOverlay();
	m_base = m_commands;
}

void VulkanScene::RestoreBase()
{
	m_commands = m_base;
	m_overlayStart = m_overlayEnd = 0;
}

void VulkanScene::SavePreview()
{
	DropOverlay();
	m_preview = m_commands;
	m_hasPreview = true;
}

void VulkanScene::RestorePreview()
{
	if (m_hasPreview)
		m_commands = m_preview;
	m_overlayStart = m_overlayEnd = 0;
}

void VulkanScene::BeginOverlay(const RECT& screen, bool highRes)
{
	DropOverlay();
	m_overlayStart = m_commands.size();
	m_screen = screen;
	m_screenSpace = highRes;
}

void VulkanScene::EndOverlay()
{
	m_overlayEnd = m_commands.size();
	m_screenSpace = false;
}

unsigned int VulkanScene::Upload(const unsigned int* words, size_t count, uint64_t* outKey)
{
	const uint64_t key = ++m_nextKey;
	if (outKey)
		*outKey = key;
	unsigned int offset = 0;
	if (m_renderer != nullptr)
	{
		if (rs_vulkan_scene_upload(m_renderer, key, words, count, &offset) != RS_OK)
			m_failed = true;
	}
	return offset;
}

void VulkanScene::Indexed(const BYTE* pixels, int width, int height, const short* borders,
	const BYTE* lighting, const int* palette, const int* remap, bool half, int x, int y, const RECT& clip)
{
	if (!pixels || !palette || width <= 0 || height <= 0 ||
		x >= clip.right || y >= clip.bottom || x + width <= clip.left || y + height <= clip.top)
		return;

	const auto key = std::make_tuple(pixels, width, height, lighting);
	auto image = m_images.find(key);
	if (image == m_images.end())
	{
		std::vector<unsigned int> words(static_cast<size_t>(width) * height);
		for (int row = 0; row < height; ++row)
		{
			const int left = borders ? max(0, static_cast<int>(borders[row * 2])) : 0;
			const int right = borders ? min(width - 1, static_cast<int>(borders[row * 2 + 1])) : width - 1;
			for (int col = left; col <= right; ++col)
			{
				const size_t i = static_cast<size_t>(row) * width + col;
				words[i] = pixels[i]
					| (lighting ? static_cast<unsigned int>(lighting[i]) << 8 : 0)
					| (((col - left - (row + 1) % 2) & 1) == 0 ? 65536u : 0u);
			}
		}
		image = m_images.emplace(key, Upload(words.data(), words.size())).first;
	}

	std::array<unsigned int, 256> colors;
	for (size_t i = 0; i < colors.size(); ++i)
		colors[i] = palette[i];
	auto pal = m_palettes.find(colors);
	if (pal == m_palettes.end())
		pal = m_palettes.emplace(colors, Upload(colors.data(), colors.size())).first;

	constexpr LONG kMaxCoord = 65536;
	rs_scene_command cmd{};
	cmd.rect[0] = std::clamp(x, -static_cast<int>(kMaxCoord), static_cast<int>(kMaxCoord));
	cmd.rect[1] = std::clamp(y, -static_cast<int>(kMaxCoord), static_cast<int>(kMaxCoord));
	cmd.rect[2] = width;
	cmd.rect[3] = height;
	cmd.clip[0] = std::clamp(clip.left, -kMaxCoord, kMaxCoord);
	cmd.clip[1] = std::clamp(clip.top, -kMaxCoord, kMaxCoord);
	cmd.clip[2] = std::clamp(clip.right, -kMaxCoord, kMaxCoord);
	cmd.clip[3] = std::clamp(clip.bottom, -kMaxCoord, kMaxCoord);
	cmd.data[1] = image->second;
	cmd.data[2] = width;
	cmd.data[3] = height;
	cmd.tint[0] = pal->second;
	cmd.tint[1] = remap ? *remap : 0;
	cmd.tint[2] = (half ? 1 : 0) | (remap ? 2 : 0) | (lighting ? 4 : 0);
	m_commands.push_back(cmd);
}

bool VulkanScene::Bitmap(const RgbaBitmap& source, int x, int y, const RECT* sourceRect)
{
	if (source.empty() || source.id == 0)
		return false;

	auto it = m_bitmaps.find(source.id);
	if (it == m_bitmaps.end())
	{
		const size_t count = static_cast<size_t>(source.width) * source.height;
		uint64_t assetKey = 0;
		const unsigned int offset = Upload(source.pixels.data(), count, &assetKey);
		it = m_bitmaps.emplace(source.id, BitmapAsset{
			offset,
			source.width,
			source.height,
			assetKey
		}).first;
	}

	const auto& asset = it->second;
	const RECT rect = sourceRect ? *sourceRect : RECT{ 0, 0, asset.width, asset.height };
	if (rect.left < 0 || rect.top < 0 || rect.right > asset.width || rect.bottom > asset.height ||
		rect.right <= rect.left || rect.bottom <= rect.top)
		return false;

	const RECT clip = m_screenSpace ? m_screen : m_source;
	const int w = rect.right - rect.left;
	const int h = rect.bottom - rect.top;
	if (x >= clip.right || y >= clip.bottom || x + w <= clip.left || y + h <= clip.top)
		return true;

	constexpr LONG kMaxCoord = 65536;
	rs_scene_command cmd{};
	cmd.rect[0] = std::clamp(x, -static_cast<int>(kMaxCoord), static_cast<int>(kMaxCoord));
	cmd.rect[1] = std::clamp(y, -static_cast<int>(kMaxCoord), static_cast<int>(kMaxCoord));
	cmd.rect[2] = w;
	cmd.rect[3] = h;
	cmd.clip[0] = std::clamp(clip.left, -kMaxCoord, kMaxCoord);
	cmd.clip[1] = std::clamp(clip.top, -kMaxCoord, kMaxCoord);
	cmd.clip[2] = std::clamp(clip.right, -kMaxCoord, kMaxCoord);
	cmd.clip[3] = std::clamp(clip.bottom, -kMaxCoord, kMaxCoord);
	cmd.data[0] = 1;
	cmd.data[1] = asset.offset;
	cmd.data[2] = asset.width;
	cmd.data[3] = asset.height;
	cmd.extra[0] = rect.left;
	cmd.extra[1] = rect.top;
	cmd.extra[2] = m_screenSpace ? 1u : 0u;
	m_commands.push_back(cmd);
	return true;
}

void VulkanScene::ReleaseBitmap(uint64_t bitmapId)
{
	if (bitmapId == 0)
		return;
	const auto it = m_bitmaps.find(bitmapId);
	if (it == m_bitmaps.end())
		return;
	if (m_renderer != nullptr && it->second.assetKey != 0)
		rs_vulkan_scene_release(m_renderer, it->second.assetKey);
	m_bitmaps.erase(it);
}

void VulkanScene::Line(int x0, int y0, int x1, int y1, unsigned int color, int period, bool invert)
{
	const RECT clip = m_screenSpace ? m_screen : m_source;
	if (clip.right <= clip.left || clip.bottom <= clip.top)
		return;

	constexpr int kSafeCoord = 32768;
	const auto exceedsShaderRange = [&](int ax, int ay, int bx, int by)
	{
		if (std::abs(ax) > kSafeCoord || std::abs(ay) > kSafeCoord ||
			std::abs(bx) > kSafeCoord || std::abs(by) > kSafeCoord)
			return true;
		const long long dx = std::abs(static_cast<long long>(bx) - ax);
		const long long dy = std::abs(static_cast<long long>(by) - ay);
		return 2 * dx * dy + max(dx, dy) > INT_MAX;
	};

	if (max(x0, x1) < clip.left || min(x0, x1) >= clip.right ||
		max(y0, y1) < clip.top || min(y0, y1) >= clip.bottom)
		return;

	if (exceedsShaderRange(x0, y0, x1, y1))
	{
		if (!ClipLineToRect(x0, y0, x1, y1, clip) || exceedsShaderRange(x0, y0, x1, y1))
			return;
	}

	constexpr LONG kMaxCoord = 65536;
	rs_scene_command cmd{};
	cmd.rect[0] = min(x0, x1);
	cmd.rect[1] = min(y0, y1);
	cmd.rect[2] = abs(x1 - x0) + 1;
	cmd.rect[3] = abs(y1 - y0) + 1;
	cmd.clip[0] = std::clamp(clip.left, -kMaxCoord, kMaxCoord);
	cmd.clip[1] = std::clamp(clip.top, -kMaxCoord, kMaxCoord);
	cmd.clip[2] = std::clamp(clip.right, -kMaxCoord, kMaxCoord);
	cmd.clip[3] = std::clamp(clip.bottom, -kMaxCoord, kMaxCoord);
	cmd.data[0] = 2;
	cmd.tint[1] = color;
	cmd.line[0] = x0;
	cmd.line[1] = y0;
	cmd.line[2] = x1;
	cmd.line[3] = y1;
	cmd.extra[0] = max(1, period);
	cmd.extra[1] = invert ? 1u : 0u;
	cmd.extra[2] = m_screenSpace ? 1u : 0u;
	m_commands.push_back(cmd);
}

VulkanPresentResult VulkanScene::PresentWithResult(const RECT& source, int width, int height, bool vsync)
{
	if (m_failed)
		return VulkanPresentResult::Failed;
	if (width <= 0 || height <= 0 || source.right <= source.left || source.bottom <= source.top)
		return VulkanPresentResult::Minimized;
	const rs_scene_command empty{};
	const int status = rs_vulkan_scene_present(
		m_renderer,
		m_commands.empty() ? &empty : m_commands.data(),
		m_commands.size(),
		static_cast<float>(source.left),
		static_cast<float>(source.top),
		static_cast<float>(source.right - source.left) / width,
		static_cast<float>(source.bottom - source.top) / height,
		width,
		height,
		vsync ? 1 : 0);
	if (status == RS_OK)
		return VulkanPresentResult::Succeeded;
	if (status == RS_VULKAN_MINIMIZED)
		return VulkanPresentResult::Minimized;
	if (status == RS_VULKAN_SWAPCHAIN_OUT_OF_DATE)
		return VulkanPresentResult::SwapchainOutOfDate;
	return VulkanPresentResult::Failed;
}
