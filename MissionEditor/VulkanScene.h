#pragma once

#include "RustCore.h"
#include <array>
#include <map>
#include <tuple>
#include <vector>

#ifdef NOSURFACES_EXTRACT
#error VulkanScene requires indexed TMP textures; disable NOSURFACES_EXTRACT.
#endif
static_assert(sizeof(rs_scene_command) == 96, "scene command must match the shader layout");

// Records the existing painter order without locking/rasterizing map surfaces.
// DirectDraw is used only to decode/upload legacy bitmap assets on cache misses.
class VulkanScene
{
public:
	VulkanScene(rs_vulkan_renderer* renderer, IDirectDrawSurface4* back);
	~VulkanScene();
	static VulkanScene* Current();
	static void InvalidateAssets();
	static VulkanScene* ForPixels(const void* pixels);
	bool Describe(IDirectDrawSurface4* surface, DDSURFACEDESC2& desc);
	bool IsTarget(IDirectDrawSurface4* surface) const;
	void Begin(const RECT& source);
	void SaveBase();
	void RestoreBase();
	void SavePreview();
	void RestorePreview();
	void BeginOverlay(const RECT& screen, bool highRes);
	void EndOverlay();
	bool Present(const RECT& source, int width, int height, bool vsync);
	void Indexed(const BYTE* pixels, int width, int height, const short* borders,
		const BYTE* lighting, const int* palette, const int* remap,
		bool half, int x, int y, const RECT& clip);
	bool Bitmap(IDirectDrawSurface4* source, int x, int y, const RECT* sourceRect = nullptr);
	void Line(int x0, int y0, int x1, int y1, unsigned int color, int period, int width, int height);

private:
	unsigned int Upload(const unsigned int* words, size_t count);
	void DropOverlay();
	rs_vulkan_renderer* m_renderer;
	IDirectDrawSurface4* m_back;
	RECT m_source{};
	RECT m_screen{};
	bool m_screenSpace = false;
	bool m_active = false;
	bool m_failed = false;
	uint64_t m_nextKey = 0;
	uint64_t m_assetGeneration = 0;
	std::vector<rs_scene_command> m_commands, m_base, m_preview;
	size_t m_overlayStart = 0, m_overlayEnd = 0;
	std::map<std::tuple<const void*, int, int, const void*>, unsigned int> m_images;
	std::map<std::array<unsigned int, 256>, unsigned int> m_palettes;
	struct BitmapAsset { CComPtr<IDirectDrawSurface4> surface; unsigned int offset; int width, height; };
	std::map<IDirectDrawSurface4*, BitmapAsset> m_bitmaps;
};
