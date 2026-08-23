#include "StdAfx.h"
#include "PalettedImageComposition.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace
{
	constexpr std::int64_t MaximumCanvasDimension = 4096;

	bool GetCenteredDimension(const std::int64_t minimum, const std::int64_t maximum,
		std::int64_t& dimension, std::int64_t& anchor)
	{
		const std::int64_t left = std::max<std::int64_t>(0, -minimum);
		const std::int64_t right = std::max<std::int64_t>(0, maximum);
		if (left == 0 && right == 0)
		{
			dimension = 1;
			anchor = 0;
			return true;
		}

		dimension = left >= right ? left * 2 : right * 2 - 1;
		anchor = dimension / 2;
		return dimension > 0 && dimension <= MaximumCanvasDimension;
	}
}

namespace PalettedImageComposition
{
	CompositeImage Compose(const std::span<const Layer> layers, const CanvasMode mode,
		const std::uint8_t neutralLighting)
	{
		std::int64_t minimumX = 0;
		std::int64_t minimumY = 0;
		std::int64_t maximumX = 0;
		std::int64_t maximumY = 0;
		bool hasLayer = false;
		bool hasLighting = false;

		for (const auto& layer : layers)
		{
			if (layer.width <= 0 || layer.height <= 0)
				continue;

			const auto pixelCount = static_cast<std::uint64_t>(layer.width) * layer.height;
			if (pixelCount > layer.colors.size())
				continue;

			const std::int64_t left = static_cast<std::int64_t>(layer.offsetX) - layer.anchorX;
			const std::int64_t top = static_cast<std::int64_t>(layer.offsetY) - layer.anchorY;
			const std::int64_t right = left + layer.width;
			const std::int64_t bottom = top + layer.height;
			minimumX = hasLayer ? std::min(minimumX, left) : left;
			minimumY = hasLayer ? std::min(minimumY, top) : top;
			maximumX = hasLayer ? std::max(maximumX, right) : right;
			maximumY = hasLayer ? std::max(maximumY, bottom) : bottom;
			hasLayer = true;
			hasLighting = hasLighting || pixelCount <= layer.lighting.size();
		}

		CompositeImage result;
		if (!hasLayer)
			return result;

		std::int64_t width = maximumX - minimumX;
		std::int64_t height = maximumY - minimumY;
		std::int64_t anchorX = -minimumX;
		std::int64_t anchorY = -minimumY;
		if (mode == CanvasMode::CenteredOrigin)
		{
			if (!GetCenteredDimension(minimumX, maximumX, width, anchorX) ||
				!GetCenteredDimension(minimumY, maximumY, height, anchorY))
				return result;
		}

		if (width <= 0 || height <= 0 || width > MaximumCanvasDimension ||
			height > MaximumCanvasDimension ||
			static_cast<std::uint64_t>(width) * height > std::numeric_limits<std::size_t>::max())
			return result;

		result.width = static_cast<int>(width);
		result.height = static_cast<int>(height);
		result.anchorX = static_cast<int>(anchorX);
		result.anchorY = static_cast<int>(anchorY);
		const auto resultSize = static_cast<std::size_t>(width * height);
		result.colors.assign(resultSize, 0);
		if (hasLighting)
			result.lighting.assign(resultSize, neutralLighting);

		for (const auto& layer : layers)
		{
			if (layer.width <= 0 || layer.height <= 0)
				continue;

			const auto pixelCount = static_cast<std::uint64_t>(layer.width) * layer.height;
			if (pixelCount > layer.colors.size())
				continue;

			const bool layerHasLighting = pixelCount <= layer.lighting.size();
			const std::int64_t destinationX = anchorX +
				static_cast<std::int64_t>(layer.offsetX) - layer.anchorX;
			const std::int64_t destinationY = anchorY +
				static_cast<std::int64_t>(layer.offsetY) - layer.anchorY;
			for (int y = 0; y < layer.height; ++y)
			{
				for (int x = 0; x < layer.width; ++x)
				{
					const auto sourcePosition = static_cast<std::size_t>(y) * layer.width + x;
					if (layer.colors[sourcePosition] == 0)
						continue;

					const std::int64_t outputX = destinationX + x;
					const std::int64_t outputY = destinationY + y;
					if (outputX < 0 || outputY < 0 || outputX >= width || outputY >= height)
						continue;

					const auto destinationPosition = static_cast<std::size_t>(outputY) * result.width +
						static_cast<std::size_t>(outputX);
					result.colors[destinationPosition] = layer.colors[sourcePosition];
					if (hasLighting)
						result.lighting[destinationPosition] = layerHasLighting ?
							layer.lighting[sourcePosition] : neutralLighting;
				}
			}
		}

		return result;
	}
}
