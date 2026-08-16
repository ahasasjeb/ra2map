#pragma once

#include <afxstr.h>

// Converts the game's alphabetic waypoint representation (A..Z, AA..ZZ,
// AAA...) to a zero-based numeric waypoint id. Invalid text returns -1.
int GetWaypoint(const char* text);

// Converts a zero-based waypoint id to the game's unbounded alphabetic form.
// Negative ids return an empty string.
CString GetWaypoint(int waypoint);
