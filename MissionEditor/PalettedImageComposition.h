#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace PalettedImageComposition
{
	enum class CanvasMode
	{
		Tight,
		CenteredOrigin
	};

	struct Layer
	{
		std::span<const std::uint8_t> colors;
		std::span<const std::uint8_t> lighting;
		int width = 0;
		int height = 0;
		int anchorX = 0;
		int anchorY = 0;
		int offsetX = 0;
		int offsetY = 0;
	};

	struct CompositeImage
	{
		std::vector<std::uint8_t> colors;
		std::vector<std::uint8_t> lighting;
		int width = 0;
		int height = 0;
		int anchorX = 0;
		int anchorY = 0;
	};

	// Combines transparent paletted layers around a shared logical origin.
	// Tight mode returns the exact union and its translated origin. CenteredOrigin
	// keeps the origin at the canvas center for SHP objects, whose draw path does
	// not carry a separate anchor.
	CompositeImage Compose(std::span<const Layer> layers, CanvasMode mode,
		std::uint8_t neutralLighting = 46);
}
