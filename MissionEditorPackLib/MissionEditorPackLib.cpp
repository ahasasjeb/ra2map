/*
	FinalSun/FinalAlert 2 Mission Editor

	Copyright (C) 1999-2024 Electronic Arts, Inc.
	Authored by Matthias Wagner

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#define _HAS_STD_BYTE 0 // see https://developercommunity.visualstudio.com/t/error-c2872-byte-ambiguous-symbol/93889
struct IUnknown;

#include "MissionEditorPackLib.h"
#include "RustCore.h"

#include <map>
#include <vector>
#include "mix_file.h"

#include "cc_file.h"
#include "xcc_dirs.h"
#include "shp_ts_file.h"
#include "tmp_ts_file.h"
#include "cc_file.h"
#include "pal_file.h"
#include "shp_ts_file.h"
#include "tmp_ts_file.h"
#include <shp_decode.h>
#include <mix_file.h>
#include <ddpf_conversion.h>
#include <vxl_file.h>
#include <math.h>
#include <hva_file.h>
#include "mix_file_write.h"
#include <locale>

#include <windows.h>
#include <windowsx.h>
#include <ddraw.h>
#include <commctrl.h>
#include <regex>

#include "VoxelNormals.h"


Cmix_file mixfiles[2000];
std::string mixfiles_names[2000];
Cshp_ts_file cur_shp; // for now only support one shp at once
Ctmp_ts_file cur_tmp;


DWORD dwMixFileCount = 0;


t_palet ts_palettes[256];
DWORD dwPalCount = 0;
DWORD conv_color[256][256];
DWORD transp_conv_color[256];
DDPIXELFORMAT cur_pf[256];
BOOL bFirstConv[256];

HBRUSH hTranspBrush = (HBRUSH)INVALID_HANDLE_VALUE;
HPEN hTranspPen = (HPEN)INVALID_HANDLE_VALUE;


#define MYASSERT(a,b) if(!a){ FSunPackLib::FSPL_EXCEPTION e; e.err_code=b; throw(e); }





__forceinline void CreateConvLookUpTable(DDPIXELFORMAT& pf, HTSPALETTE hPalette)
{

	/*fstream f;
	f.open("c:\\convtable.log", ios_base::out | ios_base::trunc );

	f << "CreateConvLookUpTable() called" << endl;
	f.flush();*/
	if (memcmp(&pf, &cur_pf[hPalette - 1], sizeof(DDPIXELFORMAT)) || bFirstConv[hPalette - 1])
	{
		bFirstConv[hPalette - 1] = FALSE;
		cur_pf[hPalette - 1] = pf;

		/*f << "Setting Conversion" << endl;
		f.flush();*/
		Cddpf_conversion conv;
		conv.set_pf(pf);

		/*f << "Calculating colors" << endl;
		f.flush();*/
		int i;
		for (i = 0;i < 256;i++)
		{
			conv_color[hPalette - 1][i] = conv.get_color(ts_palettes[hPalette - 1][i].r, ts_palettes[hPalette - 1][i].g, ts_palettes[hPalette - 1][i].b);
		}
		transp_conv_color[hPalette - 1] = conv.get_color(245, 245, 245);
	}

}


namespace FSunPackLib
{
	std::wstring utf8ToUtf16(const std::string& utf8)
	{
		// wstring_convert and codecvt_utf8_utf16 are deprecated in C++17, fallback to Win32
		if (utf8.size() == 0)
			// MultiByteToWideChar does not support passing in cbMultiByte == 0
			return L"";

		// unterminatedCountWChars will be the count of WChars NOT including the terminating zero (due to passing in utf8.size() instead of -1)
		auto utf8Count = utf8.size();
		auto unterminatedCountWChars = MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, utf8.data(), utf8Count, nullptr, 0);
		if (unterminatedCountWChars == 0)
		{
			throw std::runtime_error("UTF8 -> UTF16 conversion failed");
		}

		std::wstring utf16;
		utf16.resize(unterminatedCountWChars);
		if (MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, utf8.data(), utf8Count, utf16.data(), unterminatedCountWChars) == 0)
		{
			throw std::runtime_error("UTF8 -> UTF16 conversion failed");
		}
		return utf16;
	}

	// XCC uses CreateFileA to load files which does not support UTF8 filenames if you don't opt in through the app manifest (and that's only possible on Win10 1903+)
	// so provide some helpers that open the file through Cwin_handle with CreateFileW instead:

	template<class F>
	int open_write(F& file, const std::string& u8FilePath)
	{
		try
		{
			auto u16FilePath = utf8ToUtf16(u8FilePath);
			Cwin_handle target_file_handle(CreateFileW(u16FilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
			if (target_file_handle)
				return file.open(target_file_handle);
		}
		catch (std::runtime_error)
		{
			return 2;
		}
		return 1;
	}

	template<class F>
	int open_read(F& file, const std::string& u8FilePath)
	{
		try
		{
			auto u16FilePath = utf8ToUtf16(u8FilePath);
			Cwin_handle target_file_handle(CreateFileW(u16FilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
			if (target_file_handle)
				return file.open(target_file_handle);
		}
		catch (std::runtime_error)
		{
			return 2;
		}
		return 1;
	}

	extern "C" bool _DEBUG_EnableLogs = 0; // only useable in debug library builds
	extern "C" bool _DEBUG_EnableBreakpoints = 0; // only useable in debug library builds

	extern "C" int last_succeeded_operation = 0;


	BYTE* EncodeBase64(BYTE* sp, UINT len)
	{
		// Base64 encoding lives in the memory-safe Rust core now.
		const size_t need = (static_cast<size_t>(len) + 2) / 3 * 4;
		auto* copy = new BYTE[need + 1];
		size_t out_len = 0;
		if (rs_base64_encode(sp, len, copy, need, &out_len) != RS_OK)
		{
			delete[] copy;
			copy = new BYTE[1];
			copy[0] = 0;
			return copy;
		}
		copy[out_len] = 0; // null terminate
		return copy;
	}

	// dest should be at least as large as sp
	INT ConvertToF80(BYTE* sp, UINT len, BYTE* dest)
	{
		// Raw Format80 encoding lives in the memory-safe Rust core now.
		size_t out_len = 0;
		if (rs_f80_encode(sp, len, dest, static_cast<size_t>(len) * 2, &out_len) != RS_OK)
			return 0;
		return static_cast<INT>(out_len);
	}



	INT EncodeF80(BYTE* sp, UINT len, UINT nSections, BYTE** dest)
	{
		// Sectioned Format80 encoding lives in the memory-safe Rust core
		// now (same on-disk layout as before, bounds-checked throughout).
		if (nSections == 0)
		{
			*dest = nullptr;
			return 0;
		}

		*dest = new(BYTE[len * 4]); // as large as sp, to make sure it works

		size_t out_len = 0;
		if (rs_f80_pack_encode(sp, len, nSections, *dest, static_cast<size_t>(len) * 4, &out_len) != RS_OK)
		{
			delete[] *dest;
			*dest = nullptr;
			return 0;
		}
		return static_cast<INT>(out_len);
	}

	UINT EncodeIsoMapPack5(BYTE* sp, UINT SourceLength, BYTE** dp)
	{
		// Format5/LZO encoding lives in the memory-safe Rust core now.
		*dp = new(BYTE[SourceLength * 2]); // as big as source, makes sure it works!

		size_t out_len = 0;
		if (rs_pack5_encode(sp, SourceLength, *dp, static_cast<size_t>(SourceLength) * 2, &out_len) != RS_OK)
		{
			delete[] *dp;
			*dp = nullptr;
			return 0;
		}

		return static_cast<UINT>(out_len);
	}

	bool DecodeF80(const BYTE* const sp, const UINT SourceLength, std::vector<BYTE>& dp, const std::size_t max_size)
	{
		// Sectioned Format80 decoding lives in the memory-safe Rust core
		// now: the header walk (which previously dereferenced unvalidated
		// offsets) and the decode are both bounds-checked. The output
		// vector is only filled when every declared section validates.
		size_t out_len = 0;
		const int res = rs_f80_pack_decode(sp, SourceLength, nullptr, 0, max_size, &out_len);
		if (res != RS_ERR_SMALL_BUFFER)
			return false;
		dp.resize(out_len);
		return rs_f80_pack_decode(sp, SourceLength, dp.data(), dp.size(), max_size, &out_len) == RS_OK;
	}

	int DecodeBase64(const char* sp, std::vector<BYTE>& dest)
	{
		// Base64 decoding lives in the memory-safe Rust core now.
		const size_t len = strlen(reinterpret_cast<const char*>(sp));
		size_t out_len = 0;
		const int res = rs_base64_decode(sp, len, nullptr, 0, &out_len);
		if (res != RS_ERR_SMALL_BUFFER && res != RS_OK)
		{
			dest.clear();
			return 0;
		}
		dest.resize(out_len);
		if (rs_base64_decode(sp, len, dest.data(), dest.size(), &out_len) != RS_OK)
		{
			dest.clear();
			return 0;
		}
		return static_cast<int>(out_len);
	}

	UINT DecodeIsoMapPack5(BYTE* sp, UINT SourceLength, BYTE* dp, size_t dp_cap, HWND hProgressBar, BOOL bDebugMode)
	{
		// Format5/LZO decoding lives in the memory-safe Rust core now:
		// the section headers are validated before any write and every
		// section is decompressed with a bounds-checked LZO decoder, so a
		// corrupt map file can no longer overflow the caller's buffer
		// (the C++ code here used to run off the end on malformed data).
		(void)hProgressBar;
		(void)bDebugMode;

		size_t out_len = 0;
		if (dp == nullptr)
		{
			// measuring call: returns the validated decoded size, 0 on error
			return rs_pack5_decode(sp, SourceLength, nullptr, 0, RS_MAX_PACK_DECODE_SIZE, &out_len) == RS_ERR_SMALL_BUFFER
				? static_cast<UINT>(out_len) : 0;
		}
		return rs_pack5_decode(sp, SourceLength, dp, dp_cap, RS_MAX_PACK_DECODE_SIZE, &out_len) == RS_OK
			? static_cast<UINT>(out_len) : 0;
	}

	std::int32_t GetFirstPixelColor(IDirectDrawSurface4* pDDS)
	{
		std::int32_t color = 0;

		DDSURFACEDESC2 desc = { 0 };
		desc.dwSize = sizeof(DDSURFACEDESC2);

		if (pDDS->Lock(nullptr, &desc, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT | DDLOCK_NOSYSLOCK, nullptr) != DD_OK)
			return color;
		if (desc.lpSurface == nullptr)
		{
			pDDS->Unlock(nullptr);
			return color;
		}
		else
		{
			auto bytes_per_pixel = (desc.ddpfPixelFormat.dwRGBBitCount + 7) / 8;
			memcpy(&color, desc.lpSurface, bytes_per_pixel > 4 ? 4 : bytes_per_pixel);
			pDDS->Unlock(nullptr);

			return color;
		}

	}

	HRESULT SetColorKey(IDirectDrawSurface4* pDDS, COLORREF rgb)
	{
		DDPIXELFORMAT pf = { 0 };
		pf.dwSize = sizeof(DDPIXELFORMAT);
		pDDS->GetPixelFormat(&pf);

		ColorConverter c(pf);
		auto col = rgb == CLR_INVALID ? GetFirstPixelColor(pDDS) : c.GetColor(rgb);
		DDCOLORKEY color_key = { static_cast<DWORD>(col), static_cast<DWORD>(col) };
		return pDDS->SetColorKey(DDCKEY_SRCBLT, &color_key);
	}

	BOOL XCC_Initialize(BOOL bLoadFromRegistry)
	{
		if (bLoadFromRegistry)
			xcc_dirs::load_from_registry();

		return TRUE;
	}

	HMIXFILE XCC_OpenMix(LPCTSTR szMixFile, HMIXFILE hOwner)
	{
		DWORD i, d = 0xFFFFFFFF;
		for (i = 0;i <= dwMixFileCount && i < 2000;i++)
		{
			if (mixfiles[i].is_open() == false)
			{
				d = i;
				break;
			}
		}
		if (d == 0xFFFFFFFF)
			return NULL;

		std::string sMixFile = szMixFile;

		if (hOwner == NULL)
		{
			if (open_read(mixfiles[d], sMixFile))
				return NULL;
			//mixfiles[dwMixFileCount].enable_mix_expansion();
			mixfiles_names[d] = szMixFile;
			if (d == dwMixFileCount)
				dwMixFileCount++;

			return d + 1;//dwMixFileCount;
		}
		else if (hOwner > 0 && (hOwner - 1) < dwMixFileCount)
		{
			if (szMixFile[0] == L'_' && szMixFile[1] == L'I' && szMixFile[2] == L'D')
			{
				char id[256];
				strcpy_s(id, &sMixFile[3]);
				int iId = atoi(id);
				if (mixfiles[d].open(iId, mixfiles[hOwner - 1])) return NULL;
			}
			else
			{
				if (mixfiles[d].open(sMixFile, mixfiles[hOwner - 1]))
					return NULL;
			}

			mixfiles_names[d] = szMixFile;
			if (d == dwMixFileCount)
				dwMixFileCount++;
			return d + 1;
		}

		return NULL;
	}

	BOOL XCC_GetMixName(HMIXFILE hOwner, std::string& sMixFile)
	{
		if (hOwner == 0)
			return FALSE;
		if (hOwner > dwMixFileCount)
			return FALSE;

		sMixFile = mixfiles_names[hOwner - 1];
		return TRUE;
	}

	BOOL XCC_DoesFileExist(LPCSTR szFile, HMIXFILE hOwner)
	{
		if (hOwner == 0)
			return FALSE;
		if (hOwner > dwMixFileCount)
			return FALSE;

		t_game game = mixfiles[hOwner - 1].get_game();


		int id = mixfiles[hOwner - 1].get_id(game, szFile);

		if (mixfiles[hOwner - 1].get_index(id) < 0)
			return FALSE;

		return TRUE;
	}

	BOOL XCC_CloseMix(HMIXFILE hMixFile)
	{
		if (hMixFile<1 || hMixFile>dwMixFileCount)
			return FALSE;

		hMixFile--; // -1 to make it to an array index

		mixfiles_names[hMixFile].clear();

		if (mixfiles[hMixFile].is_open()) mixfiles[hMixFile].close();
		else
			return FALSE;

		return TRUE;
	}

	BOOL XCC_ExtractFile(const std::string& szFilename, const std::string& szSaveTo, HMIXFILE hOwner)
	{
		if (hOwner == NULL)
			return FALSE; // not supported yet

		hOwner--; // -1 to make it to an array index

		if (hOwner >= dwMixFileCount)
			return FALSE;

		if (!mixfiles[hOwner].is_open())
			return FALSE;

		Ccc_file file(true);

		if (szFilename[0] == '_' && szFilename[1] == 'I' && szFilename[2] == 'D')
		{
			char id[256];
			strcpy_s(id, &szFilename[3]);
			int iId = atoi(id);
			if (file.open(iId, mixfiles[hOwner]))
				return NULL;
		}
		else
		{
			if (file.open(szFilename, mixfiles[hOwner]) != 0)
				return FALSE;
		}

		
		Cfile32 target_file;
		if (open_write(target_file, szSaveTo))
			return FALSE;

		const int bufferSize = static_cast<int>(min(file.get_size(), 1024 * 1024));
		std::vector<byte> buffer(bufferSize);
		for (auto p = 0; p < file.get_size();)
		{
			const auto toRead = static_cast<int>(min(file.get_size() - p, bufferSize));
			if (file.read(buffer.data(), toRead))
				return FALSE;
			if (target_file.write(buffer.data(), toRead))
				return FALSE;
			p += toRead;
		}

		return TRUE;
	}

	BOOL XCC_ExtractFile(LPCSTR szFilename, LPCSTR szSaveTo, HMIXFILE hOwner)
	{
		return XCC_ExtractFile(std::string(szFilename), std::string(szSaveTo), hOwner);
	}



	BOOL XCC_GetSHPHeader(SHPHEADER* pHeader)
	{
		if (pHeader == NULL)
			return FALSE;

		if (!cur_shp.is_open())
			return FALSE;

		auto& head = cur_shp.header();
		memcpy(pHeader, &head, sizeof(SHPHEADER));


		return TRUE;
	}

	/*
	Returns the SHP image header of a image in a SHP file
	*/
	BOOL XCC_GetSHPImageHeader(int iImageIndex, SHPIMAGEHEADER* pImageHeader)
	{
		t_shp_ts_image_header imagehead;

		if (pImageHeader == NULL || iImageIndex < 0)
			return FALSE;


		if (!cur_shp.is_open())
			return FALSE;

		auto& head = cur_shp.header();

		if (iImageIndex >= head.c_images)
			return FALSE;

		imagehead = *cur_shp.get_image_header(iImageIndex);

		memcpy(pImageHeader, &imagehead, sizeof(SHPIMAGEHEADER));

		return TRUE;
	}



	BOOL SetCurrentTMP(LPCSTR szTMP, HMIXFILE hOwner)
	{
		if (cur_tmp.is_open())
			cur_tmp.close();

		if (hOwner == NULL)
		{
			if (open_read(cur_tmp, szTMP))
				return FALSE;
		}
		else
		{
			if (cur_tmp.open(szTMP, mixfiles[hOwner - 1]))
				return FALSE;
		}

		//if(cur_tmp.get_data()==NULL) return FALSE;
		//if(!cur_tmp.is_valid()) return FALSE;

		return TRUE;
	};

	BOOL SetCurrentSHP(LPCSTR szSHP, HMIXFILE hOwner)
	{
		if (cur_shp.is_open()) cur_shp.close();

		if (hOwner == NULL)
		{
			if (open_read(cur_shp, szSHP) != 0)
			{
				return FALSE;
			}
		}
		else
		{
			int id = mixfiles[hOwner - 1].get_id(mixfiles[hOwner - 1].get_game(), szSHP);
			int size = mixfiles[hOwner - 1].get_size(id);
			if (size == 0)
				OutputDebugString("NULL size");
			BYTE* b = new(BYTE[size]);
			mixfiles[hOwner - 1].seek(mixfiles[hOwner - 1].get_offset(id));
			mixfiles[hOwner - 1].read(b, size);
			cur_shp.load(Cvirtual_binary(b, size));
		}

		return TRUE;
	};

	BOOL XCC_GetTMPTileInfo(int iTile, POINT* lpPos, int* lpWidth, int* lpHeight, BYTE* lpDirection, BYTE* lpTileHeight, BYTE* lpTileType, RGBTRIPLE* lpRgbLeft, RGBTRIPLE* lpRgbRight)
	{
		if (!cur_tmp.is_open()) return FALSE;
		if (iTile >= cur_tmp.get_c_tiles() || iTile < 0) return FALSE;

		t_tmp_image_header ihead = *cur_tmp.get_image_header(iTile);
		if (lpDirection)
			*lpDirection = ihead.ramp_type;
		if (lpTileHeight)
			*lpTileHeight = ihead.height;
		if (lpTileType)
			*lpTileType = ihead.terrain_type;

		if (lpRgbLeft)
		{
			lpRgbLeft->rgbtRed = ihead.radar_red_left;
			lpRgbLeft->rgbtGreen = ihead.radar_green_left;
			lpRgbLeft->rgbtBlue = ihead.radar_blue_left;
		}

		if (lpRgbRight)
		{
			lpRgbRight->rgbtRed = ihead.radar_red_right;
			lpRgbRight->rgbtGreen = ihead.radar_green_right;
			lpRgbRight->rgbtBlue = ihead.radar_blue_right;
		}

		int cx = cur_tmp.get_cx();
		int cy = cur_tmp.get_cy();
		if (cur_tmp.has_extra_graphics(iTile))
		{
			int cy_extra = cur_tmp.get_cy_extra(iTile);
			int y_extra = cur_tmp.get_y_extra(iTile) - cur_tmp.get_y(iTile);
			int cx_extra = cur_tmp.get_cx_extra(iTile);
			int x_extra = cur_tmp.get_x_extra(iTile) - cur_tmp.get_x(iTile);
			int y_added = 0;//cur_tmp.get_x(iTile);
			int x_added = 0;//cur_tmp.get_y(iTile);

			if (y_extra < 0)
			{
				y_added -= y_extra;
				cy -= y_extra;
				y_extra = 0;

			}
			if (x_extra < 0)
			{
				x_added -= x_extra;
				cx -= x_extra;
				x_extra = 0;
			}


			if (cy_extra + y_extra > cy) cy = cy_extra + y_extra;
			if (cx_extra + x_extra > cx) cx = cx_extra + x_extra;

			if (lpPos != NULL)
			{
				//int xTile, yTile;
				//xTile=iTile%cur_tmp.get_cblocks_x();
				//yTile=iTile/cur_tmp.get_cblocks_x();

				//lpPos->x=-x_added;
				//lpPos->y=-y_added;
				lpPos->x =/*cur_tmp.get_x(iTile)-(24*xTile-24*yTile)*/-x_added;
				lpPos->y =/*cur_tmp.get_y(iTile)-(12*yTile+12*xTile)*/-y_added;
			}
			if (lpWidth != NULL) *lpWidth = cx;
			if (lpHeight != NULL) *lpHeight = cy;

		}
		else
		{
			if (lpPos != NULL)
			{
				lpPos->x = 0;//cur_tmp.get_x(iTile);;
				lpPos->y = 0;//cur_tmp.get_y(iTile);
			}
			if (lpHeight != NULL) *lpHeight = cur_tmp.get_cy();
			if (lpWidth != NULL) *lpWidth = cur_tmp.get_cx();
		}

		return TRUE;
	}

	BOOL XCC_GetTMPInfo(RECT* lpRect, int* iTileCount, int* iTilesX, int* iTilesY)
	{

		// if(!cur_tmp.is_open()) return FALSE;

		int x, y, cx, cy;
		cur_tmp.get_rect(x, y, cx, cy);
		if (lpRect != NULL)
		{
			lpRect->left = x;
			lpRect->top = y;
			lpRect->right = x + cx;
			lpRect->bottom = y + cy;
		}

		if (iTileCount != NULL) *iTileCount = cur_tmp.get_c_tiles();
		if (iTilesX != NULL) *iTilesX = cur_tmp.get_cblocks_y();
		if (iTilesY != NULL) *iTilesY = cur_tmp.get_cblocks_x();



		return TRUE;
	}


	BOOL LoadSHPImageInSurface(IDirectDraw4* pdd, HTSPALETTE hPalette, int iImageIndex, int iCount, LPDIRECTDRAWSURFACE4* pdds)
	{
		RGBTRIPLE rgb_transp;
		t_shp_ts_image_header imghead;
		DDSURFACEDESC2          ddsd;
		BYTE* image;


		if (hPalette == NULL || hPalette > dwPalCount) return NULL;


		auto& head = cur_shp.header();

		if (head.cx == 0 || head.cy == 0)
		{
			return FALSE;
		}


		rgb_transp.rgbtRed = 245;
		rgb_transp.rgbtGreen = 245;
		rgb_transp.rgbtBlue = 245;



		int pic;
		std::vector<byte> decode_image_buffer;
		for (pic = 0;pic < iCount;pic++)
		{
			if (cur_shp.get_image_header(iImageIndex + pic))
			{
				imghead = *(cur_shp.get_image_header(iImageIndex + pic));
				// if(imghead.offset!=0)
				{

					ZeroMemory(&ddsd, sizeof(ddsd));
					ddsd.dwSize = sizeof(DDSURFACEDESC2);
					ddsd.dwWidth = head.cx;
					ddsd.dwHeight = head.cy;
					ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH /*| DDSD_PIXELFORMAT*/;
					ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;

					if (pdd->CreateSurface(&ddsd, &pdds[pic], NULL) != DD_OK)
						return NULL;

					ddsd.dwFlags = DDSD_PITCH;
					pdds[pic]->GetSurfaceDesc(&ddsd);
					long pitch = ddsd.lPitch;

					DDPIXELFORMAT pf;
					memset(&pf, 0, sizeof(DDPIXELFORMAT));
					pf.dwSize = sizeof(DDPIXELFORMAT);

					pdds[pic]->GetPixelFormat(&pf);


					if (cur_shp.is_compressed(iImageIndex + pic))
					{
						decode_image_buffer.resize(imghead.cx * imghead.cy);
						image = decode_image_buffer.data();
						decode3(cur_shp.get_image(iImageIndex + pic), image, imghead.cx, imghead.cy);
					}
					else
						image = (unsigned char*)cur_shp.get_image(iImageIndex + pic);


					if ((pf.dwFlags & DDPF_RGB) && pf.dwRBitMask && pf.dwGBitMask && pf.dwBBitMask)
					{

						CreateConvLookUpTable(pf, hPalette);

						if (pdds[pic]->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL) == DD_OK)
						{

							BYTE* dest = (BYTE*)ddsd.lpSurface;
							pitch = ddsd.lPitch;

							int bytesize = (pf.dwRGBBitCount + 1) / 8;
							if (bytesize < 1) bytesize = 1;

							if (pf.dwRGBBitCount <= 8) bytesize = 1;
							else if (pf.dwRGBBitCount <= 16) bytesize = 2;
							else if (pf.dwRGBBitCount <= 24) bytesize = 3;
							else if (pf.dwRGBBitCount <= 32) bytesize = 4;

							if (dest)
							{
								int i, e;
								for (i = 0; i < head.cx; i++)
								{
									for (e = 0;e < head.cy;e++)
									{
										DWORD dwRead = 0xFFFFFFFF;
										DWORD dwWrite = i * bytesize + e * pitch;

										if (i >= imghead.x && e >= imghead.y && i < imghead.x + imghead.cx && e < imghead.y + imghead.cy)
											dwRead = (i - imghead.x) + (e - imghead.y) * imghead.cx;

										if (dwRead != 0xFFFFFFFF && image[dwRead] != 0)
										{
											DWORD col = conv_color[hPalette - 1][image[dwRead]];
											memcpy(&dest[dwWrite], &col, bytesize);
										}
										else
										{
											DWORD col = transp_conv_color[hPalette - 1];
											memcpy(&dest[dwWrite], &col, bytesize);
										}

									}
								}
							}
							pdds[pic]->Unlock(NULL);
						}

					}
					else
					{
						HDC hDC;
						while (pdds[pic]->GetDC(&hDC) == DDERR_WASSTILLDRAWING);
						{
							int i, e;
							for (i = 0; i < head.cx; i++)
							{
								for (e = 0;e < head.cy;e++)
								{
									DWORD dwRead = 0xFFFFFFFF;
									//DWORD dwWrite=i*bytesize+e*pitch;

									if (i >= imghead.x && e >= imghead.y && i < imghead.x + imghead.cx && e < imghead.y + imghead.cy)
										dwRead = (i - imghead.x) + (e - imghead.y) * imghead.cx;

									if (dwRead != 0xFFFFFFFF && image[dwRead] != 0)
									{
										t_palet_entry& p = ts_palettes[hPalette - 1][image[dwRead]];
										SetPixel(hDC, i, e, RGB(p.r, p.g, p.b));
									}
									else
									{
										SetPixel(hDC, i, e, RGB(245, 245, 245));
									}

								}
							}

						}

						pdds[pic]->ReleaseDC(hDC);

					}

					SetColorKey(pdds[pic], -1);

				}
			}
		}

		return TRUE;

	}

	BOOL LoadSHPImage(int iImageIndex, std::vector<BYTE>& pic)
	{
		t_shp_ts_image_header imghead;
		BYTE* image = NULL;




		auto& head = cur_shp.header();
		if (head.cx == 0 || head.cy == 0)
		{
			return FALSE;
		}


		std::vector<byte> decode_image_buffer;
		if (cur_shp.get_image_header(iImageIndex))
		{
			imghead = *(cur_shp.get_image_header(iImageIndex));
			// if(imghead.offset!=0)
			{

				if (cur_shp.is_compressed(iImageIndex))
				{
					decode_image_buffer.resize(imghead.cx * imghead.cy);
					image = decode_image_buffer.data();
					decode3(cur_shp.get_image(iImageIndex), image, imghead.cx, imghead.cy);
				}
				else
					image = (unsigned char*)cur_shp.get_image(iImageIndex);


				pic.resize(head.cx * head.cy);

				int i, e;
				for (i = 0; i < head.cx; i++)
				{
					for (e = 0;e < head.cy;e++)
					{
						DWORD dwRead = 0xFFFFFFFF;
						DWORD dwWrite = i + e * head.cx;

						if (i >= imghead.x && e >= imghead.y && i < imghead.x + imghead.cx && e < imghead.y + imghead.cy)
							dwRead = (i - imghead.x) + (e - imghead.y) * imghead.cx;

						if (dwRead != 0xFFFFFFFF)
						{
							pic[dwWrite] = image[dwRead];
						}
						else
							pic[dwWrite] = 0;

					}

				}

			}
		}

		return TRUE;

	}

	BOOL LoadSHPImage(int iImageIndex, int iCount, BYTE** lpPics)
	{
		t_shp_ts_image_header imghead;
		BYTE* image = NULL;




		auto& head = cur_shp.header();
		if (head.cx == 0 || head.cy == 0)
		{
			return FALSE;
		}




		int pic;
		std::vector<byte> decode_image_buffer;
		for (pic = 0;pic < iCount;pic++)
		{
			if (cur_shp.get_image_header(iImageIndex + pic))
			{
				imghead = *(cur_shp.get_image_header(iImageIndex + pic));
				// if(imghead.offset!=0)
				{

					if (cur_shp.is_compressed(iImageIndex + pic))
					{
						decode_image_buffer.resize(imghead.cx * imghead.cy);
						image = decode_image_buffer.data();
						decode3(cur_shp.get_image(iImageIndex + pic), image, imghead.cx, imghead.cy);
					}
					else
						image = (unsigned char*)cur_shp.get_image(iImageIndex + pic);


					lpPics[pic] = new(BYTE[head.cx * head.cy]);

					int i, e;
					for (i = 0; i < head.cx; i++)
					{
						for (e = 0;e < head.cy;e++)
						{
							DWORD dwRead = 0xFFFFFFFF;
							DWORD dwWrite = i + e * head.cx;

							if (i >= imghead.x && e >= imghead.y && i < imghead.x + imghead.cx && e < imghead.y + imghead.cy)
								dwRead = (i - imghead.x) + (e - imghead.y) * imghead.cx;

							if (dwRead != 0xFFFFFFFF)
							{
								lpPics[pic][dwWrite] = image[dwRead];
							}
							else
								lpPics[pic][dwWrite] = 0;

						}

					}

				}
			}
		}



		return TRUE;

	}

	void tmp_ts_draw(Ctmp_ts_file& f, byte* d, int i)
	{
		// The tile drawing kernel lives in the memory-safe Rust core now.
		// All reads are clamped to the containing file and all writes are
		// bounds-checked against the destination buffer, so corrupt TMP
		// data can no longer corrupt the heap.
		rs_tmp_tile_info info;
		info.std_cx = f.get_cx();
		info.std_cy = f.get_cy();
		info.has_extra = f.has_extra_graphics(i) ? 1 : 0;
		info.cx_extra = 0;
		info.cy_extra = 0;
		info.x_extra = 0;
		info.y_extra = 0;
		if (info.has_extra)
		{
			info.cx_extra = f.get_cx_extra(i);
			info.cy_extra = f.get_cy_extra(i);
			info.x_extra = f.get_x_extra(i) - f.get_x(i);
			info.y_extra = f.get_y_extra(i) - f.get_y(i);
		}

		int tile_cx = 0, tile_cy = 0;
		if (rs_tmp_ts_get_size(&info, &tile_cx, &tile_cy) != RS_OK)
			return; // invalid dimensions, leave the buffer untouched

		const byte* img = f.get_image(i);
		long long remaining = (f.data() + f.get_size()) - img;
		if (remaining < 0) remaining = 0;

		const int res = rs_tmp_ts_draw(d, static_cast<size_t>(tile_cx) * tile_cy, img, static_cast<size_t>(remaining), &info);
		if (res != RS_OK)
		{
			// Never fatal: a corrupt tile is simply left blank.
			char buf[256];
			sprintf_s(buf, "tmp_ts_draw: Rust core rejected tile %d (status %d)\n", i, res);
			OutputDebugStringA(buf);
		}
	}


	BOOL LoadTMPImageInSurface(IDirectDraw4* pdd, int iStart, int iCount, LPDIRECTDRAWSURFACE4* pdds, HTSPALETTE hPalette)
	{
		last_succeeded_operation = 2100;



		DDSURFACEDESC2          ddsd;
		RGBTRIPLE rgb_transp;

		if (hPalette == NULL || hPalette > dwPalCount) return NULL;

		rgb_transp.rgbtRed = 245;
		rgb_transp.rgbtGreen = 245;
		rgb_transp.rgbtBlue = 245;


		int pic;
		for (pic = 0;pic < iCount;pic++)
		{
			int cx, cy;
			int z = pic + iStart;


			if (!cur_tmp.get_index()[z])
			{

				pdds[pic] = NULL;
			}
			else
			{



				XCC_GetTMPTileInfo(z, NULL, &cx, &cy, NULL, NULL, NULL, NULL, NULL);


				if (cx > 0 && cy > 0)
				{
					//const byte* r = cur_tmp.get_image(iStart+pic); //new byte[cx * cy];
					// byte transp=r[0];
					ZeroMemory(&ddsd, sizeof(ddsd));
					ddsd.dwSize = sizeof(DDSURFACEDESC2);
					ddsd.dwWidth = cx;
					ddsd.dwHeight = cy;
					ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH /*| DDSD_PIXELFORMAT*/;
					ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

					byte* image = NULL;
					if (pdd->CreateSurface(&ddsd, &pdds[pic], NULL) == DD_OK)
					{


						last_succeeded_operation = 2101;

						DDPIXELFORMAT pf;
						memset(&pf, 0, sizeof(DDPIXELFORMAT));
						pf.dwSize = sizeof(DDPIXELFORMAT);
						pdds[pic]->GetPixelFormat(&pf);

						BOOL bFastLoaded = FALSE;

						if ((pf.dwFlags & DDPF_RGB) && pf.dwRBitMask && pf.dwGBitMask && pf.dwBBitMask)
						{

							last_succeeded_operation = 21011;

							CreateConvLookUpTable(pf, hPalette);
							int i, e;




							image = new byte[cx * cy];
							tmp_ts_draw(cur_tmp, image, iStart + pic);
							int bytesize = 1;//(pf.dwRGBBitCount+1)/8;
							if (pf.dwRGBBitCount <= 8) bytesize = 1;
							else if (pf.dwRGBBitCount <= 16) bytesize = 2;
							else if (pf.dwRGBBitCount <= 24) bytesize = 3;
							else if (pf.dwRGBBitCount <= 32) bytesize = 4;
							if (bytesize < 1) bytesize = 1;

							last_succeeded_operation = 21012;

							memset(&ddsd, 0, sizeof(DDSURFACEDESC2));
							ddsd.dwSize = sizeof(DDSURFACEDESC2);
							ddsd.dwFlags = DDSD_PITCH | DDSD_LPSURFACE;

							RECT lockr;
							lockr.left = 0;
							lockr.top = 0;
							lockr.right = cx;
							lockr.bottom = cy;

							if (pdds[pic]->Lock(&lockr, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL) == DD_OK)
							{
								last_succeeded_operation = 21013;


								BYTE* dest = (BYTE*)ddsd.lpSurface;
								int pitch = ddsd.lPitch;


								if (ddsd.dwFlags & DDSD_PIXELFORMAT)
								{


									// pixel format overwritten
									CreateConvLookUpTable(ddsd.ddpfPixelFormat, hPalette);

									int bytesize = 1;//(pf.dwRGBBitCount+1)/8;
									if (ddsd.ddpfPixelFormat.dwRGBBitCount <= 8) bytesize = 1;
									else if (ddsd.ddpfPixelFormat.dwRGBBitCount <= 16) bytesize = 2;
									else if (ddsd.ddpfPixelFormat.dwRGBBitCount <= 24) bytesize = 3;
									else if (ddsd.ddpfPixelFormat.dwRGBBitCount <= 32) bytesize = 4;
									if (bytesize < 1) bytesize = 1;


								}

								if (ddsd.dwFlags & DDSD_LINEARSIZE)
								{
									pitch = ddsd.dwLinearSize / cy;
								}



								memset(&ddsd, 0, sizeof(DDSURFACEDESC2));
								ddsd.dwSize = sizeof(DDSURFACEDESC2);
								ddsd.dwFlags = DDSD_WIDTH | DDSD_HEIGHT;

								pdds[pic]->GetSurfaceDesc(&ddsd);



								last_succeeded_operation = 21016;

								if (dest && !IsBadWritePtr(dest, pitch * cy)) // TODO: debug this case when using surfaces
								{

									bFastLoaded = TRUE;

									for (i = 0; i < cx; i++)
									{
										for (e = 0;e < cy;e++)
										{
											DWORD dwWrite = i * bytesize + e * pitch;
											DWORD dwRead = i + e * cx;

											if (image[dwRead] != 0)
											{
												DWORD col = conv_color[hPalette - 1][image[dwRead]];
												memcpy(&dest[dwWrite], &col, bytesize);
											}
											else
											{
												DWORD col = transp_conv_color[hPalette - 1];
												memcpy(&dest[dwWrite], &col, bytesize);
											}

										}
									}

								}




								pdds[pic]->Unlock(&lockr);



								last_succeeded_operation = 21014;



							}

							last_succeeded_operation = 21015;

							DDCOLORKEY              ddck;
							ddck.dwColorSpaceLowValue = transp_conv_color[hPalette - 1];
							ddck.dwColorSpaceHighValue = ddck.dwColorSpaceLowValue;
							pdds[pic]->SetColorKey(DDCKEY_SRCBLT, &ddck);
						}

					if (!bFastLoaded) // use GDI
					{
						int i, e;
						// Reuse the buffer the fast path already allocated and
						// drew into. Only allocate when the fast path never ran
						// (image == NULL). The previous unconditional new[]
						// overwrote `image` and leaked the fast-path allocation
						// whenever the fallback was taken.
						if (!image)
						{
							image = new byte[cx * cy];
							tmp_ts_draw(cur_tmp, image, iStart + pic);
						}
						HDC hDC;
						while (pdds[pic]->GetDC(&hDC) == DDERR_WASSTILLDRAWING) {};

							for (i = 0; i < cx; i++)
							{
								for (e = 0;e < cy;e++)
								{
									DWORD dwRead = i + e * cx;

									if (image[dwRead] != 0)
									{
										t_palet_entry& p = ts_palettes[hPalette - 1][image[dwRead]];
										SetPixel(hDC, i, e, RGB(p.r, p.g, p.b));
									}
									else
									{
										SetPixel(hDC, i, e, RGB(245, 245, 245));
									}

								}
							}

							pdds[pic]->ReleaseDC(hDC);
							SetColorKey(pdds[pic], RGB(245, 245, 245));

						}
					}

					if (image) delete[] image;
					image = NULL;


				}
			}
		}

		return TRUE;
	}


	BOOL LoadTMPImage(int iStart, int iCount, BYTE** lpTileArray)
	{
		last_succeeded_operation = 2100;


		int pic;
		for (pic = 0;pic < iCount;pic++)
		{
			int cx, cy;
			int z = pic + iStart;


			if (!cur_tmp.get_index()[z])
			{

				lpTileArray[pic] = NULL;
			}
			else
			{

				XCC_GetTMPTileInfo(z, NULL, &cx, &cy, NULL, NULL, NULL, NULL, NULL);


				if (cx > 0 && cy > 0)
				{
					byte* image = NULL;


					last_succeeded_operation = 2101;

					image = new byte[cx * cy];
					tmp_ts_draw(cur_tmp, image, iStart + pic);

					lpTileArray[pic] = image;

					image = NULL;
				}
			}
		}

		return TRUE;
	}





	Cvxl_file cur_vxl;
	Chva_file cur_hva;

	t_palet32bgr_entry color_table[256];

	void load_color_table(const t_palet palet, bool convert_palet)
	{
		t_palet p;

		memcpy(p, palet, sizeof(t_palet));
		if (convert_palet)
			convert_palet_18_to_24(p);

		for (long i = 0; i < 256; i++)
		{
			color_table[i].r = p[i].r;
			color_table[i].g = p[i].g;
			color_table[i].b = p[i].b;
		}
	}

	/*struct t_vector
	{
		double x;
		double y;
		double z;
	};*/

	const double pi = 3.141592654;

	template<class T>
	__forceinline void rotate_x(Vec3<T>& v, T a)
	{
		T l = sqrt(v.y() * v.y() + v.z() * v.z());
		T d_a = atan2(v.y(), v.z()) + a;
		v[1] = l * sin(d_a);
		v[2] = l * cos(d_a);
	}

	template<class T>
	__forceinline void rotate_y(Vec3<T>& v, T a)
	{
		T l = sqrt(v.x() * v.x() + v.z() * v.z());
		T d_a = atan2(v.x(), v.z()) + a;
		v[0] = l * sin(d_a);
		v[2] = l * cos(d_a);
	}

	template<class T>
	__forceinline void rotate_z(Vec3<T>& v, T a)
	{
		T l = sqrt(v.x() * v.x() + v.y() * v.y());
		T d_a = atan2(v.x(), v.y()) + a;
		v[0] = l * sin(d_a);
		v[1] = l * cos(d_a);;
	}

	template<class T>
	__forceinline void rotate_zxy(Vec3<T>& v, const Vec3<T>& r)
	{
		rotate_z(v, r.z());
		rotate_x(v, r.x());
		rotate_y(v, r.y());
	}

	BOOL SetCurrentVXL(LPCSTR lpVXLFile, HMIXFILE hMixFile)
	{
		last_succeeded_operation = 500;

		if (cur_vxl.is_open()) cur_vxl.close();
		if (cur_hva.is_open()) cur_hva.close();

		auto HVA = std::string(lpVXLFile);
		std::transform(HVA.begin(), HVA.end(), HVA.begin(), [](unsigned char c) { return std::tolower(c); });
		HVA = std::regex_replace(HVA, std::regex(".vxl$"), ".hva");

		if (hMixFile == NULL)
		{
			if (open_read(cur_vxl, lpVXLFile))
				return FALSE;
			if (open_read(cur_hva, HVA))
				return FALSE;
		}
		else
		{
			if (cur_vxl.open(lpVXLFile, mixfiles[hMixFile - 1]))
				return FALSE;
			if (cur_hva.open(HVA, mixfiles[hMixFile - 1]))
				return FALSE;
		}

		if (!cur_vxl.is_open())
			return FALSE;
		if (!cur_hva.is_open())
			return FALSE;

		return TRUE;
	}

	BOOL GetVXLInfo(int* cSections)
	{
		if (!cur_vxl.is_open())
			return FALSE;

		if (cSections) *cSections = cur_vxl.get_c_section_headers();

		return TRUE;
	}

	BOOL GetVXLSectionInfo(int section, VoxelNormalClass& normalClass)
	{
		if (!cur_vxl.is_open())
			return FALSE;
		if (section >= cur_vxl.get_c_section_headers())
			return FALSE;

		switch (cur_vxl.get_section_tailer(section)->unknown)
		{
		case 1:
			normalClass = VoxelNormalClass::Gen1;
			break;
		case 2:
			normalClass = VoxelNormalClass::TS;
			break;
		case 3:
			normalClass = VoxelNormalClass::Gen3;
			break;
		case 4:
			normalClass = VoxelNormalClass::RA2;
			break;
		default:
			normalClass = VoxelNormalClass::Unknown;
		}
		
		return TRUE;
	}




	void GetVXLSectionBounds(int iSection, const Vec3f& rotation, const Vec3f& modelOffset, Vec3f& minVec, Vec3f& maxVec)
	{
		// Bounds computation lives in the Rust core now (bounds-checked and
		// panic-free). It replicates the previous algorithm exactly.
		const t_vxl_section_tailer& tailer = *cur_vxl.get_section_tailer(iSection);

		rs_vxl_tailer rt;
		rt.scale = tailer.scale;
		rt.x_min_scale = tailer.x_min_scale;
		rt.y_min_scale = tailer.y_min_scale;
		rt.z_min_scale = tailer.z_min_scale;
		rt.x_max_scale = tailer.x_max_scale;
		rt.y_max_scale = tailer.y_max_scale;
		rt.z_max_scale = tailer.z_max_scale;
		rt.cx = tailer.cx;
		rt.cy = tailer.cy;
		rt.cz = tailer.cz;

		unsigned char id[16] = { 0 };
		memcpy(id, cur_vxl.get_section_header(iSection)->id, 16);

		const float rot[3] = { rotation.x(), rotation.y(), rotation.z() };
		const float off[3] = { modelOffset.x(), modelOffset.y(), modelOffset.z() };

		float mn[3], mx[3];
		int mainSection = 0;
		if (rs_vxl_compute_bounds(1, id, &rt, cur_hva.get_transform_matrix(0, iSection), rot, off, &mainSection, mn, mx) != RS_OK)
		{
			minVec = Vec3f();
			maxVec = Vec3f();
			return;
		}

		minVec = Vec3f(mn[0], mn[1], mn[2]);
		maxVec = Vec3f(mx[0], mx[1], mx[2]);
	}


	void RenderVXLSection(const VoxelNormalTable& normalTable, Vec3f lightDirection, const int iSection, int rtWidth, int rtHeight, const Vec3f& modelOffset, const Vec3f& rotation, const Vec3f& postHVAOffset, BYTE* image, BYTE* lighting, char* image_z, int* i_center_x, int* i_center_y, int ZAdjust, int* i_center_x_zmax, int* i_center_y_zmax, int i3dCenterX, int i3dCenterY)
	{
		// The voxel span walker lives in the memory-safe Rust core now.
		// Span reads are clamped to the section body and output writes are
		// bounds-checked, so malformed VXL data (common with modded voxels)
		// can no longer corrupt the heap.

		last_succeeded_operation = 13;

		if (rtWidth < 1 || rtHeight < 1 ||
			rtWidth > RS_MAX_RENDER_TARGET_DIM || rtHeight > RS_MAX_RENDER_TARGET_DIM)
			return;

		const auto& tailer = *cur_vxl.get_section_tailer(iSection);

		rs_vxl_tailer rt;
		rt.scale = tailer.scale;
		rt.x_min_scale = tailer.x_min_scale;
		rt.y_min_scale = tailer.y_min_scale;
		rt.z_min_scale = tailer.z_min_scale;
		rt.x_max_scale = tailer.x_max_scale;
		rt.y_max_scale = tailer.y_max_scale;
		rt.z_max_scale = tailer.z_max_scale;
		rt.cx = tailer.cx;
		rt.cy = tailer.cy;
		rt.cz = tailer.cz;

		const float rot[3] = { rotation.x(), rotation.y(), rotation.z() };
		const float ld[3] = { lightDirection.x(), lightDirection.y(), lightDirection.z() };
		const float mo[3] = { modelOffset.x(), modelOffset.y(), modelOffset.z() };
		const float po[3] = { postHVAOffset.x(), postHVAOffset.y(), postHVAOffset.z() };

		const byte* body = cur_vxl.get_section_body();
		long long bodyLen = (cur_vxl.data() + cur_vxl.get_size()) - body;
		if (bodyLen < 0) bodyLen = 0;

		rs_vxl_render_section(
			image, lighting, image_z, static_cast<size_t>(rtWidth) * rtHeight,
			rtWidth, rtHeight, &rt,
			cur_hva.get_transform_matrix(0, iSection),
			reinterpret_cast<const float*>(normalTable.data()), static_cast<unsigned int>(normalTable.size()),
			ld, rot, mo, po,
			body, static_cast<size_t>(bodyLen), tailer.span_data_ofs,
			cur_vxl.get_span_start_list(iSection), cur_vxl.get_span_end_list(iSection),
			i_center_x, i_center_y, i_center_x_zmax, i_center_y_zmax,
			i3dCenterX, i3dCenterY,
			&last_succeeded_operation
		);
	}

	VoxelNormalTable emptyNormalTable;

	BOOL LoadVXLImageInSurface(const VoxelNormalTables& normalTables, Vec3f lightDirection, IDirectDraw4* pdd, int iStart, int iCount, const Vec3f rotation, const Vec3f postHVAOffset, LPDIRECTDRAWSURFACE4* pdds, HTSPALETTE hPalette, int* lpXCenter, int* lpYCenter, int ZAdjust, int* lpXCenterZMax, int* lpYCenterZMax, int i3dCenterX, int i3dCenterY)
	{
		if (hPalette == NULL || hPalette > dwPalCount) return NULL;

		last_succeeded_operation = 1;

		int i;
		int cx_max = 0, cy_max = 0, cz_max = 0;

		Vec3f minCoords(10000, 10000, 10000);
		Vec3f maxCoords(-10000, -10000, -10000);

	// Calculate projected bounding box for the virtual render target
	int iBodySection = -1;
	int iLargestSection = 0;
	int iLargestVolume = 0;
	for (i = 0; i < cur_vxl.get_c_section_tailers(); i++)
	{
		const auto& header = cur_vxl.get_section_header(i);
		const auto& tailer = cur_vxl.get_section_tailer(i);
		Vec3f secMinVec, secMaxVec;
		GetVXLSectionBounds(i, rotation, postHVAOffset, secMinVec, secMaxVec);
		auto extent = secMaxVec - secMinVec;
		auto volume = extent.x() * extent.y() * extent.z();
		if (volume >= iLargestVolume)
		{
			iLargestVolume = volume;
			iLargestSection = i;
		}
		// id is char[16] and may not be NUL-terminated - check the prefix
		// safely instead of relying on strstr.
		if (strncmp(header->id, "BODY", 4) == 0)
			iBodySection = i;
		minCoords.minimum(secMinVec);
		maxCoords.maximum(secMaxVec);
	}

	const int iMainSection = iBodySection >= 0 ? iBodySection : iLargestSection;

	const Vec3f renderOffset = negate(minCoords);

	last_succeeded_operation = 2;


	const auto extents = (maxCoords - minCoords);
	int rtWidth = ceil(extents.x());
	int rtHeight = ceil(extents.y());
	// guard the allocation against corrupt bounds data (the Rust core
	// also rejects render targets beyond this size)
	if (rtWidth < 1 || rtHeight < 1 || rtWidth > RS_MAX_RENDER_TARGET_DIM || rtHeight > RS_MAX_RENDER_TARGET_DIM)
	{
		OutputDebugStringA("LoadVXLImageInSurface: corrupt section bounds, skipping\n");
		return NULL;
	}
	const int c_pixels = rtWidth * rtHeight;

		// MYASSERT(c_pixels,1);

		byte* image = new byte[c_pixels];
		byte* image_s = new byte[c_pixels];
		char* image_z = new char[c_pixels];

		memset(image, 0, c_pixels);
		memset(image_s, 0, c_pixels);
		memset(image_z, CHAR_MIN, c_pixels);

		int x_center = 0, y_center = 0;
		int x_center_zmax = 0, y_center_zmax = 0;

		for (i = 0; i < cur_vxl.get_c_section_headers(); i++)
		{
			auto iNormalTable = cur_vxl.get_section_tailer(i)->unknown;
			const auto& normalTable = normalTables.isValidTable(iNormalTable) ? normalTables.getTable(iNormalTable) : emptyNormalTable;
			if (i != iMainSection)
				RenderVXLSection(normalTable, lightDirection, i, rtWidth, rtHeight, renderOffset, rotation, postHVAOffset, image, image_s, image_z, NULL, NULL, 0, NULL, NULL, -1, -1);
			else
				RenderVXLSection(normalTable, lightDirection, i, rtWidth, rtHeight, renderOffset, rotation, postHVAOffset, image, image_s, image_z, &x_center, &y_center, ZAdjust, &x_center_zmax, &y_center_zmax, i3dCenterX, i3dCenterY);
		}

		last_succeeded_operation = 3;

		if (lpXCenter) *lpXCenter = x_center;
		if (lpYCenter) *lpYCenter = y_center;
		if (lpXCenterZMax) *lpXCenterZMax = x_center_zmax;
		if (lpYCenterZMax) *lpYCenterZMax = y_center_zmax;

		// calculate x/y values
		int left = 0, right = rtWidth, top = 0, bottom = rtHeight;



		// draw pic
		DDSURFACEDESC2 ddsd;
		ZeroMemory(&ddsd, sizeof(ddsd));
		ddsd.dwSize = sizeof(DDSURFACEDESC2);
		ddsd.dwWidth = right - left;
		ddsd.dwHeight = bottom - top;
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH /*| DDSD_PIXELFORMAT*/;
		ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;



		if (pdd->CreateSurface(&ddsd, &pdds[0], NULL) != DD_OK)
		{
			// The three rendering buffers were allocated above; surface
			// creation failed, so free them instead of leaking all three.
			delete[] image;
			delete[] image_s;
			delete[] image_z;
			return NULL;
		}

		ddsd.dwFlags = DDSD_PITCH;
		pdds[0]->GetSurfaceDesc(&ddsd);
		long pitch = ddsd.lPitch;

		DDPIXELFORMAT pf;
		memset(&pf, 0, sizeof(DDPIXELFORMAT));
		pf.dwSize = sizeof(DDPIXELFORMAT);

		pdds[0]->GetPixelFormat(&pf);

		last_succeeded_operation = 5;

		BOOL bUseGDI = FALSE;

		if (pf.dwFlags & DDPF_RGB && pf.dwRBitMask && pf.dwGBitMask && pf.dwBBitMask)
		{

			CreateConvLookUpTable(pf, hPalette);


			/*DDBLTFX fx;
			memset(&fx, 0, sizeof(DDBLTFX));
			fx.dwSize=sizeof(DDBLTFX);
			pdds[0]->Blt(NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
			*/

			last_succeeded_operation = 6;

			memset(&ddsd, 0, sizeof(DDSURFACEDESC2));
			ddsd.dwSize = sizeof(DDSURFACEDESC2);
			if (pdds[0]->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL) == DD_OK)
			{
				pitch = ddsd.lPitch;

				BYTE* dest = (BYTE*)ddsd.lpSurface;

				if (dest)
				{
					int bytesize = (pf.dwRGBBitCount + 1) / 8;
					if (bytesize < 1) bytesize = 1;

					if (pf.dwRGBBitCount <= 8) bytesize = 1;
					else if (pf.dwRGBBitCount <= 16) bytesize = 2;
					else if (pf.dwRGBBitCount <= 24) bytesize = 3;
					else if (pf.dwRGBBitCount <= 32) bytesize = 4;

					last_succeeded_operation = 7;
					int k, e;
					//HDC dc;
					//pdds[0]->GetDC(&dc);
					for (k = 0;k < rtWidth;k++)
					{
						for (e = 0;e < rtHeight;e++)
						{
							//t_palet_entry p=ts_palettes[hPalette-1][image[k+e*cl_max]];								

							if (k >= left && e >= top && k < right && e < bottom)
							{
								//SetPixel(dc, k-left, e-top, RGB(p.r, p.g, p.b) );
								DWORD dwWrite = k * bytesize + e * ddsd.lPitch;
								int pos = k + e * rtWidth;
								if (pos < c_pixels && image[pos] != 0)
								{
									DWORD col = conv_color[hPalette - 1][image[k + e * rtWidth]];
									memcpy(&dest[dwWrite], &col, bytesize);
								}
								else
								{
									DWORD col = transp_conv_color[hPalette - 1];
									memcpy(&dest[dwWrite], &col, bytesize);
								}


							}
						}

					}
					memcpy(&dest[0], &transp_conv_color[hPalette - 1], bytesize);
					//pdds[0]->ReleaseDC(dc);

				}
				pdds[0]->Unlock(NULL);
			}
			else bUseGDI = TRUE;
		}
		else bUseGDI = TRUE;

		if (bUseGDI)
		{
			HDC hDC;
			while (pdds[0]->GetDC(&hDC) == DDERR_WASSTILLDRAWING);


			last_succeeded_operation = 7;
			int k, e;
			//HDC dc;
			//pdds[0]->GetDC(&dc);
			for (k = 0;k < rtWidth;k++)
			{
				for (e = 0;e < rtHeight;e++)
				{
					//t_palet_entry p=ts_palettes[hPalette-1][image[k+e*cl_max]];								

					if (k >= left && e >= top && k < right && e < bottom)
					{
						//SetPixel(dc, k-left, e-top, RGB(p.r, p.g, p.b) );
						//DWORD dwWrite=k*bytesize+e*ddsd.lPitch;
						int pos = k + e * rtWidth;
						if (pos < c_pixels && image[pos] != 0)
						{
							t_palet_entry& p = ts_palettes[hPalette - 1][image[k + e * rtWidth]];
							SetPixel(hDC, k, e, RGB(p.r, p.g, p.b));
						}
						else
						{
							SetPixel(hDC, k, e, RGB(245, 245, 245));
						}

					}
				}
			}

			SetPixel(hDC, 0, 0, RGB(245, 245, 245));
			pdds[0]->ReleaseDC(hDC);

		}

		last_succeeded_operation = 70001;

		SetColorKey(pdds[0], -1);

		last_succeeded_operation = 70002;

		if (image) delete[]  image;
		if (image_s) delete[] image_s;
		if (image_z) delete[] image_z;

		//pal.close();
		return TRUE;
	}

	BOOL LoadVXLImage(const VoxelNormalTables& normalTables, Vec3f lightDirection, const Vec3f rotation, const Vec3f modelOffset, std::vector<BYTE>& image, std::vector<BYTE>& lighting, int* lpXCenter, int* lpYCenter, int ZAdjust, int* lpXCenterZMax, int* lpYCenterZMax, int i3dCenterX, int i3dCenterY, RECT* vxlrect)
	{
		last_succeeded_operation = 1;

		int i;
		int cx_max = 0, cy_max = 0, cz_max = 0;

		Vec3f minCoords(10000, 10000, 10000);
		Vec3f maxCoords(-10000, -10000, -10000);

		// Calculate projected bounding box for the virtual render target
		int iBodySection = -1;
		int iLargestSection = 0;
		int iLargestVolume = 0;
		for (i = 0; i < cur_vxl.get_c_section_tailers(); i++)
		{
			const auto& header = cur_vxl.get_section_header(i);
			const auto& tailer = cur_vxl.get_section_tailer(i);
			Vec3f secMinVec, secMaxVec;
			GetVXLSectionBounds(i, rotation, modelOffset, secMinVec, secMaxVec);
			auto extent = secMaxVec - secMinVec;
			auto volume = extent.x() * extent.y() * extent.z();
			if (volume >= iLargestVolume)
			{
				iLargestVolume = volume;
				iLargestSection = i;
			}
		if (strncmp(header->id, "BODY", 4) == 0)
			iBodySection = i;
		minCoords.minimum(secMinVec);
		maxCoords.maximum(secMaxVec);
	}

	const int iMainSection = iBodySection >= 0 ? iBodySection : iLargestSection;


	const Vec3f renderOffset = negate(minCoords);

	last_succeeded_operation = 2;


	const auto extents = (maxCoords - minCoords);
	int rtWidth = ceil(extents.x()) + 1;
	int rtHeight = ceil(extents.y()) + 1;
	// guard the allocation against corrupt bounds data
	if (rtWidth < 1 || rtHeight < 1 || rtWidth > RS_MAX_RENDER_TARGET_DIM || rtHeight > RS_MAX_RENDER_TARGET_DIM)
	{
		OutputDebugStringA("LoadVXLImage: corrupt section bounds, skipping\n");
		return FALSE;
	}
	const int c_pixels = rtWidth * rtHeight;

		image.clear();
		lighting.clear();
		image.resize(c_pixels, 0);
		lighting.resize(c_pixels, 255);
		std::vector<char> image_z(c_pixels, CHAR_MIN);

		int x_center = 0, y_center = 0;
		int x_center_zmax = 0, y_center_zmax = 0;

		for (i = 0; i < cur_vxl.get_c_section_headers(); i++)
		{
			auto iNormalTable = cur_vxl.get_section_tailer(i)->unknown;
			const auto& normalTable = normalTables.isValidTable(iNormalTable) ? normalTables.getTable(iNormalTable) : emptyNormalTable;
			if (i != iMainSection)
				RenderVXLSection(normalTable, lightDirection, i, rtWidth, rtHeight, renderOffset, rotation, modelOffset, image.data(), lighting.data(), image_z.data(), NULL, NULL, ZAdjust, NULL, NULL, -1, -1);
			else
				RenderVXLSection(normalTable, lightDirection, i, rtWidth, rtHeight, renderOffset, rotation, modelOffset, image.data(), lighting.data(), image_z.data(), &x_center, &y_center, ZAdjust, &x_center_zmax, &y_center_zmax, i3dCenterX, i3dCenterY);
		}

		last_succeeded_operation = 3;

		if (lpXCenter)
			*lpXCenter = x_center;
		if (lpYCenter)
			*lpYCenter = y_center;
		if (lpXCenterZMax)
			*lpXCenterZMax = x_center_zmax;
		if (lpYCenterZMax)
			*lpYCenterZMax = y_center_zmax;

		// calculate x/y values
		int left = 0, right = rtWidth, top = 0, bottom = rtHeight;

		int width = right - left;
		int height = bottom - top;

		if (vxlrect)
		{
			vxlrect->left = 0;
			vxlrect->right = width;
			vxlrect->bottom = height;
			vxlrect->top = 0;
		}

#ifdef _DEBUG
		// draw lines around RT
		for (i = 0; i < width; ++i)
		{
			lighting[i] = 1;
			lighting[i + (height - 1) * width] = 1;
			image[i] = 1;
			image[i + (height - 1) * width] = 1;
		}
		for (i = 0; i < height; ++i)
		{
			image[i * width + width - 1] = 1;
			image[i * width] = 1;
		}
		// draw center
		if (x_center > 0 && y_center > 0 && x_center < width - 1 && y_center < height - 1)
		{
			image[x_center + y_center * width] = 1;
			image[x_center - 1 + y_center * width] = 1;
			image[x_center + 1 + y_center * width] = 1;
			image[x_center + (y_center + 1) * width] = 1;
			image[x_center + (y_center - 1) * width] = 1;
		}
#endif

		image.resize(width * height);
		lighting.resize(width * height);

		return TRUE;
	}

	

	HTSPALETTE LoadTSPalette(const std::string& szPalette, HMIXFILE hPaletteOwner)
	{
		Cpal_file pal;
		RGBTRIPLE* paldata;

		if (dwPalCount > 255)
			return NULL;
		if (hPaletteOwner == NULL)
		{
			if (open_read(pal, szPalette))
				return NULL;
		}
		else
		{
			if (szPalette[0] == '_' && szPalette[1] == 'I' && szPalette[2] == 'D')
			{
				char id[256];
				strcpy_s(id, &szPalette[3]);
				int iId = atoi(id);
				if (pal.open(iId, mixfiles[hPaletteOwner - 1]))
					return NULL;
			}
			else
			{
				if (pal.open(szPalette, mixfiles[hPaletteOwner - 1]) != 0)
					return NULL;
			}
		}

		if (!pal.is_open())
			return NULL;

		dwPalCount++;


		paldata = (RGBTRIPLE*)pal.get_data();
		//t_palet t_p;
		memcpy(ts_palettes[dwPalCount - 1], paldata, 768);
		bFirstConv[dwPalCount - 1] = TRUE;
		convert_palet_18_to_24(ts_palettes[dwPalCount - 1]);

		pal.close();
		return dwPalCount;
		
	}

	HTSPALETTE LoadTSPalette(LPCSTR szPalette, HMIXFILE hPaletteOwner)
	{
		return LoadTSPalette(std::string(szPalette), hPaletteOwner);
	}

	BOOL SetTSPaletteEntry(HTSPALETTE hPalette, BYTE bIndex, RGBTRIPLE* rgb, RGBTRIPLE* orig)
	{
		if (hPalette == NULL || hPalette > dwPalCount)
			return FALSE;
		if (orig != NULL)
		{
			orig->rgbtRed = ts_palettes[hPalette - 1][bIndex].r;
			orig->rgbtGreen = ts_palettes[hPalette - 1][bIndex].g;
			orig->rgbtBlue = ts_palettes[hPalette - 1][bIndex].b;
		}
		if (rgb != NULL)
		{
			ts_palettes[hPalette - 1][bIndex].r = rgb->rgbtRed;
			ts_palettes[hPalette - 1][bIndex].g = rgb->rgbtGreen;
			ts_palettes[hPalette - 1][bIndex].b = rgb->rgbtBlue;

			bFirstConv[hPalette - 1] = TRUE;
			CreateConvLookUpTable(cur_pf[hPalette - 1], hPalette);
		}

		return TRUE;
	}

	t_game GameToXCCGame(FSunPackLib::Game game)
	{
		t_game xcc_game(game_unknown);
		switch (game)
		{
		case TS:
			xcc_game = game_ts;
			break;
		case RA2:
			xcc_game = game_ra2;
			break;
		case TS_FS:
			xcc_game = game_ts_fs;
			break;
		case RA2_YR:
			xcc_game = game_ra2_yr;
			break;
		}
		return xcc_game;
	}

	BOOL WriteMixFile(LPCTSTR lpMixFile, LPCSTR* lpFiles, DWORD dwFileCount, Game game)
	{
		if (!lpFiles)
			return FALSE;
		if (!lpMixFile)
			return FALSE;

		Cmix_file_write mix(GameToXCCGame(game));

		DWORD i;
		for (i = 0;i < dwFileCount;i++)
		{
			LPCSTR lpFile = lpFiles[i];
			Cvirtual_binary file = Cvirtual_binary(std::string(lpFile));

			if (file.data())
			{
				mix.add_file(lpFile, file);
			}
		}

		auto binary = mix.write();
		return binary.save(lpMixFile) == 0;
	}

	class ColorConverterImpl
	{
	public:
		ColorConverterImpl(const DDPIXELFORMAT& pf)
		{
			m_conf.set_pf(pf);
		}

		int GetColor(int r, int g, int b) const
		{
			return m_conf.get_color(r, g, b);
		}

		int GetColor(int a, int r, int g, int b) const
		{
			return m_conf.get_color(a, r, g, b);
		}

	private:
		mutable Cddpf_conversion m_conf;
	};

	ColorConverter::ColorConverter(const DDPIXELFORMAT& pf) :
		m_impl(new ColorConverterImpl(pf))
	{
	}

	int ColorConverter::GetColor(int r, int g, int b) const
	{
		return m_impl->GetColor(r, g, b);
	}

	int ColorConverter::GetColor(int a, int r, int g, int b) const
	{
		return m_impl->GetColor(a, r, g, b);
	}

	int ColorConverter::GetColor(COLORREF col) const
	{
		return GetColor((LOBYTE((col) >> 24)), GetRValue(col), GetGValue(col), GetBValue(col));
	}

};