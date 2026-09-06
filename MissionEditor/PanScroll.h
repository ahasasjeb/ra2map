#pragma once

#include <algorithm>
#include <cmath>

// Keep fractional movement across frames; timer delivery and rendering can vary.
class PanScrollMotion
{
public:
	void Reset()
	{
		m_fractionX = m_fractionY = 0.0;
	}

	CPoint Advance(int distanceX, int distanceY, double elapsedSeconds)
	{
		// Preserve the previous nominal speed: distance / 3 every 16 ms.
		// Do not jump across the map after a stalled UI thread.
		const double factor = std::clamp(elapsedSeconds, 0.0, 0.05) / 0.048;
		return CPoint(AdvanceAxis(distanceX, factor, m_fractionX),
			AdvanceAxis(distanceY, factor, m_fractionY));
	}

	void Clamp(bool clampedX, bool clampedY)
	{
		if (clampedX) m_fractionX = 0.0;
		if (clampedY) m_fractionY = 0.0;
	}

private:
	static int AdvanceAxis(int distance, double factor, double& fraction)
	{
		if (distance == 0 || (distance > 0 && fraction < 0) || (distance < 0 && fraction > 0))
			fraction = 0.0;
		const double movement = distance * factor + fraction;
		const int pixels = static_cast<int>(std::trunc(movement));
		fraction = movement - pixels;
		return pixels;
	}

	double m_fractionX = 0.0;
	double m_fractionY = 0.0;
};

struct PanExposedBands
{
	RECT x;
	RECT y;
};

inline PanExposedBands GetPanExposedBands(const RECT& area, int shiftX, int shiftY)
{
	PanExposedBands bands = {
		{ area.left, area.top, area.left, area.bottom },
		{ area.left, area.top, area.right, area.top }
	};
	if (shiftX > 0)
	{
		bands.x.right = (std::min)(area.right, area.left + shiftX);
		bands.y.left = bands.x.right;
	}
	else if (shiftX < 0)
	{
		bands.x.left = (std::max)(area.left, area.right + shiftX);
		bands.x.right = area.right;
		bands.y.right = bands.x.left;
	}
	if (shiftY > 0)
		bands.y.bottom = (std::min)(area.bottom, area.top + shiftY);
	else if (shiftY < 0)
	{
		bands.y.top = (std::max)(area.top, area.bottom + shiftY);
		bands.y.bottom = area.bottom;
	}
	return bands;
}
