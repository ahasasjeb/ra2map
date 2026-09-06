#include "StdAfx.h"
#include "VulkanScene.h"
#include <algorithm>

namespace { thread_local VulkanScene* currentScene = nullptr; uint64_t assetGeneration = 1; }

VulkanScene::VulkanScene(rs_vulkan_renderer* renderer, IDirectDrawSurface4* back) : m_renderer(renderer), m_back(back) {}
VulkanScene::~VulkanScene() { if (currentScene == this) currentScene = nullptr; }
VulkanScene* VulkanScene::Current() { return currentScene; }
void VulkanScene::InvalidateAssets() { ++assetGeneration; }
VulkanScene* VulkanScene::ForPixels(const void* pixels) { return currentScene == pixels ? currentScene : nullptr; }
bool VulkanScene::IsTarget(IDirectDrawSurface4* surface) const { return m_active && surface == m_back; }

bool VulkanScene::Describe(IDirectDrawSurface4* surface, DDSURFACEDESC2& desc)
{
	if (!IsTarget(surface)) return false;
	desc.dwSize = sizeof(desc);
	surface->GetSurfaceDesc(&desc);
	desc.lpSurface = this; // token, never dereferenced by software pixel writers
	return true;
}

void VulkanScene::Begin(const RECT& source)
{
	if (m_assetGeneration != assetGeneration)
	{
		m_failed = rs_vulkan_scene_reset(m_renderer) != RS_OK;
		m_images.clear(); m_palettes.clear(); m_bitmaps.clear(); m_base.clear(); m_preview.clear();
		m_nextKey = 0; m_assetGeneration = assetGeneration;
	}
	currentScene = this; m_active = true; m_source = source;
	m_commands.clear(); m_overlayStart = m_overlayEnd = 0;
}

void VulkanScene::DropOverlay()
{
	if (m_overlayEnd > m_overlayStart && m_overlayEnd <= m_commands.size())
		m_commands.erase(m_commands.begin() + m_overlayStart, m_commands.begin() + m_overlayEnd);
	m_overlayStart = m_overlayEnd = 0;
}

void VulkanScene::SaveBase() { DropOverlay(); m_base = m_commands; }
void VulkanScene::RestoreBase() { m_commands = m_base; m_overlayStart = m_overlayEnd = 0; }
void VulkanScene::SavePreview() { DropOverlay(); m_preview = m_commands; }
void VulkanScene::RestorePreview() { if (!m_preview.empty()) m_commands = m_preview; m_overlayStart = m_overlayEnd = 0; }
void VulkanScene::BeginOverlay(const RECT& screen, bool highRes) { DropOverlay(); m_overlayStart = m_commands.size(); m_screen = screen; m_screenSpace = highRes; }
void VulkanScene::EndOverlay() { m_overlayEnd = m_commands.size(); m_screenSpace = false; }

unsigned int VulkanScene::Upload(const unsigned int* words, size_t count)
{
	unsigned int offset = 0;
	if (rs_vulkan_scene_upload(m_renderer, ++m_nextKey, words, count, &offset) != RS_OK)
		m_failed = true;
	return offset;
}

void VulkanScene::Indexed(const BYTE* pixels, int width, int height, const short* borders,
	const BYTE* lighting, const int* palette, const int* remap, bool half, int x, int y, const RECT& clip)
{
	if (!pixels || !palette || width <= 0 || height <= 0 || x >= clip.right || y >= clip.bottom || x + width <= clip.left || y + height <= clip.top) return;
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
				words[i] = pixels[i] | (lighting ? static_cast<unsigned int>(lighting[i]) << 8 : 0)
					| (((col - left - (row + 1) % 2) & 1) == 0 ? 65536u : 0u);
			}
		}
		image = m_images.emplace(key, Upload(words.data(), words.size())).first;
	}
	std::array<unsigned int, 256> colors;
	for (size_t i = 0; i < colors.size(); ++i) colors[i] = palette[i];
	auto pal = m_palettes.find(colors);
	if (pal == m_palettes.end()) pal = m_palettes.emplace(colors, Upload(colors.data(), colors.size())).first;
	rs_scene_command cmd{};
	cmd.rect[0] = x; cmd.rect[1] = y; cmd.rect[2] = width; cmd.rect[3] = height;
	cmd.clip[0] = clip.left; cmd.clip[1] = clip.top; cmd.clip[2] = clip.right; cmd.clip[3] = clip.bottom;
	cmd.data[1] = image->second; cmd.data[2] = width; cmd.data[3] = height;
	cmd.tint[0] = pal->second; cmd.tint[1] = remap ? *remap : 0;
	cmd.tint[2] = (half ? 1 : 0) | (remap ? 2 : 0) | (lighting ? 4 : 0);
	m_commands.push_back(cmd);
}

bool VulkanScene::Bitmap(IDirectDrawSurface4* source, int x, int y, const RECT* sourceRect)
{
	if (!source) return false;
	auto it = m_bitmaps.find(source);
	if (it == m_bitmaps.end())
	{
		DDSURFACEDESC2 desc{}; desc.dwSize = sizeof(desc);
		if (source->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_READONLY, nullptr) != DD_OK) return false;
		DDCOLORKEY key{};
		if (source->GetColorKey(DDCKEY_SRCBLT, &key) != DD_OK && desc.lpSurface)
            memcpy(&key.dwColorSpaceLowValue, desc.lpSurface, min<DWORD>(4, (desc.ddpfPixelFormat.dwRGBBitCount + 7) / 8));
		std::vector<unsigned int> words(static_cast<size_t>(desc.dwWidth) * desc.dwHeight);
		const int bytes = (desc.ddpfPixelFormat.dwRGBBitCount + 7) / 8;
		const unsigned int mask = desc.ddpfPixelFormat.dwRBitMask | desc.ddpfPixelFormat.dwGBitMask | desc.ddpfPixelFormat.dwBBitMask;
		for (unsigned int row = 0; row < desc.dwHeight; ++row)
			for (unsigned int col = 0; col < desc.dwWidth; ++col)
			{
				unsigned int color = 0;
				memcpy(&color, static_cast<const BYTE*>(desc.lpSurface) + row * desc.lPitch + col * bytes, bytes);
				words[static_cast<size_t>(row) * desc.dwWidth + col] = (color & mask) == (key.dwColorSpaceLowValue & mask) ? 0 : color | 0xff000000u;
			}
		source->Unlock(nullptr);
		it = m_bitmaps.emplace(source, BitmapAsset{ source, Upload(words.data(), words.size()), static_cast<int>(desc.dwWidth), static_cast<int>(desc.dwHeight) }).first;
	}
	const auto& asset = it->second;
	RECT rect = sourceRect ? *sourceRect : RECT{ 0, 0, asset.width, asset.height };
	if (rect.left < 0 || rect.top < 0 || rect.right > asset.width || rect.bottom > asset.height) return false;
	rs_scene_command cmd{};
	cmd.rect[0] = x; cmd.rect[1] = y; cmd.rect[2] = rect.right - rect.left; cmd.rect[3] = rect.bottom - rect.top;
	const RECT clip = m_screenSpace ? m_screen : m_source;
	cmd.clip[0] = clip.left; cmd.clip[1] = clip.top; cmd.clip[2] = clip.right; cmd.clip[3] = clip.bottom;
	cmd.data[0] = 1; cmd.data[1] = asset.offset; cmd.data[2] = asset.width; cmd.data[3] = asset.height;
	cmd.extra[0] = rect.left; cmd.extra[1] = rect.top;
	cmd.extra[2] = m_screenSpace ? 1 : 0;
	m_commands.push_back(cmd); return true;
}

void VulkanScene::Line(int x0, int y0, int x1, int y1, unsigned int color, int period, int width, int height)
{
	rs_scene_command cmd{};
	cmd.rect[0] = min(x0, x1); cmd.rect[1] = min(y0, y1);
	cmd.rect[2] = abs(x1 - x0) + 1; cmd.rect[3] = abs(y1 - y0) + 1;
	const RECT clip = m_screenSpace ? m_screen : m_source;
	cmd.clip[0] = max(0L, clip.left); cmd.clip[1] = max(0L, clip.top);
	cmd.clip[2] = min(static_cast<LONG>(width), clip.right); cmd.clip[3] = min(static_cast<LONG>(height), clip.bottom);
	cmd.data[0] = 2; cmd.tint[1] = color;
	cmd.line[0] = x0; cmd.line[1] = y0; cmd.line[2] = x1; cmd.line[3] = y1;
	cmd.extra[0] = period; cmd.extra[2] = m_screenSpace ? 1 : 0; m_commands.push_back(cmd);
}

bool VulkanScene::Present(const RECT& source, int width, int height, bool vsync)
{
	if (m_failed || width <= 0 || height <= 0) return false;
	const rs_scene_command empty{};
	return rs_vulkan_scene_present(m_renderer, m_commands.empty() ? &empty : m_commands.data(), m_commands.size(),
		static_cast<float>(source.left), static_cast<float>(source.top),
		static_cast<float>(source.right - source.left) / width, static_cast<float>(source.bottom - source.top) / height,
		width, height, vsync ? 1 : 0) == RS_OK;
}
