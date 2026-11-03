/*
 *  This file is part of Joe's Own Editor for Windows.
 *  Copyright (c) 2014 John J. Jordan.
 *
 *  Joe's Own Editor for Windows is free software: you can redistribute it 
 *  and/or modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, either version 2 of the 
 *  License, or (at your option) any later version.
 *
 *  Joe's Own Editor for Windows is distributed in the hope that it will
 *  be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Joe's Own Editor for Windows.  If not, see
 *  <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <assert.h>

#include "putty.h"
#include "jwconfig.h"
#include "jwutils.h"
#include "jwglobals.h"

#define JW_ROOT_KEY L"Software\\JoeEditor\\"

int jw_line_spacing;

static wchar_t *getkeyname()
{
	static wchar_t keyname[256];

	wcscpy(keyname, JW_ROOT_KEY);
	wcscat(keyname, jw_personality);

	return keyname;
}

/* Personal frontend settings use the same root as the editor core. */
static const wchar_t *profile_path(void)
{
    static wchar_t path[MAX_PATH];
    wchar_t root[MAX_PATH];
    DWORD length;
    if(path[0])return path;
    length=GetEnvironmentVariableW(L"MIJOE_CONFIG_HOME",root,MAX_PATH);
    if(!length) {
        length=GetEnvironmentVariableW(L"USERPROFILE",root,MAX_PATH);
        if(!length || length>=MAX_PATH-20)return NULL;
        wcscat(root,L"\\.mj");
    } else if(length>=MAX_PATH-20)return NULL;
    if(!CreateDirectoryW(root,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS)return NULL;
    wcscpy(path,root);wcscat(path,L"\\windows.ini");return path;
}
static int profile_number(const wchar_t *key,int fallback)
{
    const wchar_t *path=profile_path();
    return path ? (int)GetPrivateProfileIntW(jw_personality,key,fallback,path) : fallback;
}
static void save_number(const wchar_t *key,int value)
{
    const wchar_t *path=profile_path();wchar_t text[40];
    if(path) {wsprintfW(text,L"%d",value);WritePrivateProfileStringW(jw_personality,key,text,path);}
}

void jwLoadConfig(Config *cfg)
{
	HKEY hkey;

	/* Basics -- not overridable via registry settings */

	strcpy(cfg->host, "");
	cfg->port = 666;
	cfg->close_on_exit = FORCE_ON;
	cfg->warn_on_close = 0;
	cfg->blink_cur = 1;
	cfg->beep = 1;
	cfg->fullscreenonaltenter = 1;
	strcpy(cfg->line_codepage, "utf-8");

	cfg->savelines = 0;
	cfg->scrollbar = 0;
	cfg->scrollbar_in_fullscreen = 0;

	cfg->window_border = 2;

	strcpy(cfg->wintitle, "Joe's Own Editor");
	cfg->font_quality = FQ_CLEARTYPE;

	/* Defaults */

	cfg->cursor_type = 0;

	cfg->height = 40;
	cfg->width = 120;

	strcpy(cfg->font.name, "Consolas");
	cfg->font.isbold = 0;
	cfg->font.height = 10;
	jw_line_spacing = 0;

	/* Load from registry... */

	if (RegOpenKeyExW(HKEY_CURRENT_USER, getkeyname(), 0, KEY_ALL_ACCESS, &hkey) == ERROR_SUCCESS)
	{
		DWORD sz = MAX_PATH;
		DWORD type;
		DWORD val;

		sz = sizeof(DWORD);
		if (ERROR_SUCCESS == RegQueryValueExW(hkey, L"Rows", NULL, &type, (LPBYTE)&val, &sz) && type == REG_DWORD)
		{
			cfg->height = val;
		}

		sz = sizeof(DWORD);
		if (ERROR_SUCCESS == RegQueryValueExW(hkey, L"Columns", NULL, &type, (LPBYTE)&val, &sz) && type == REG_DWORD)
		{
			cfg->width = val;
		}

		sz = sizeof(DWORD);
		if (ERROR_SUCCESS == RegQueryValueExW(hkey, L"FontSize", NULL, &type, (LPBYTE)&val, &sz) && type == REG_DWORD)
		{
			cfg->font.height = val;
		}

		sz = sizeof(DWORD);
		if (ERROR_SUCCESS == RegQueryValueExW(hkey, L"LineSpacing", NULL, &type, (LPBYTE)&val, &sz) && type == REG_DWORD && (int)val >= -64 && (int)val <= 32)
			jw_line_spacing = (int)val;

		sz = sizeof(DWORD);
		if (ERROR_SUCCESS == RegQueryValueExW(hkey, L"FontBold", NULL, &type, (LPBYTE)&val, &sz) && type == REG_DWORD)
		{
			cfg->font.isbold = val;
		}

		sz = sizeof(DWORD);
		if (ERROR_SUCCESS == RegQueryValueExW(hkey, L"Cursor", NULL, &type, (LPBYTE)&val, &sz) && type == REG_DWORD)
		{
			cfg->cursor_type = val;
		}

		sz = 64;
		RegQueryValueExA(hkey, "FontName", NULL, &type, (LPBYTE)cfg->font.name, &sz);

		RegCloseKey(hkey);
	}

    /* Existing registry values are a read-only migration fallback. */
    cfg->height=profile_number(L"Rows",cfg->height);
    cfg->width=profile_number(L"Columns",cfg->width);
    cfg->font.height=profile_number(L"FontSize",cfg->font.height);
    jw_line_spacing=profile_number(L"LineSpacing",jw_line_spacing);
    cfg->font.isbold=profile_number(L"FontBold",cfg->font.isbold);
    cfg->cursor_type=profile_number(L"Cursor",cfg->cursor_type);
    if(cfg->font.height<6 || cfg->font.height>72)cfg->font.height=10;
    if(jw_line_spacing < -64 || jw_line_spacing>32)jw_line_spacing=0;
    if(cfg->height<2 || cfg->height>500)cfg->height=40;
    if(cfg->width<10 || cfg->width>1000)cfg->width=120;
    if(profile_path()) {
        /* Use the wide API for the Unicode profile filename. */
        {
            wchar_t font[64],fallback[64];
            MultiByteToWideChar(CP_ACP,0,cfg->font.name,-1,fallback,64);
            GetPrivateProfileStringW(jw_personality,L"FontName",fallback,font,64,profile_path());
            WideCharToMultiByte(CP_ACP,0,font,-1,cfg->font.name,sizeof(cfg->font.name),NULL,NULL);
        }
        jwSaveSettings(cfg);save_number(L"Rows",cfg->height);save_number(L"Columns",cfg->width);
    }

	/* Cheat: preset the window x and y coordinates so that JOE doesn't get a resize and kill the copyright banner */
	jw_initialcols = cfg->width;
	jw_initialrows = cfg->height;
}

void jwSaveSettings(Config *cfg)
{
    const wchar_t *path=profile_path();
    wchar_t font[64];
    save_number(L"FontSize",cfg->font.height);save_number(L"LineSpacing",jw_line_spacing);
    save_number(L"FontBold",cfg->font.isbold);save_number(L"Cursor",cfg->cursor_type);
    if(path) {
        MultiByteToWideChar(CP_ACP,0,cfg->font.name,-1,font,64);
        WritePrivateProfileStringW(jw_personality,L"FontName",font,path);
    }
}

int jwLoadWindowLoc(POINT *point)
{
	HKEY hkey;
	int got = 0;

	if (RegOpenKeyExW(HKEY_CURRENT_USER, getkeyname(), 0, KEY_ALL_ACCESS, &hkey) == ERROR_SUCCESS)
	{
		DWORD sz = MAX_PATH;
		DWORD type;
		DWORD val;

		sz = sizeof(DWORD);
		if (ERROR_SUCCESS == RegQueryValueExW(hkey, L"X", NULL, &type, (LPBYTE)&val, &sz) && type == REG_DWORD)
		{
			point->x = val;
			got++;
		}

		sz = sizeof(DWORD);
		if (ERROR_SUCCESS == RegQueryValueExW(hkey, L"Y", NULL, &type, (LPBYTE)&val, &sz) && type == REG_DWORD)
		{
			point->y = val;
			got++;
		}

		RegCloseKey(hkey);
	}

    {
        int x=profile_number(L"X",0x7fffffff),y=profile_number(L"Y",0x7fffffff);
        if(x!=0x7fffffff && y!=0x7fffffff){point->x=x;point->y=y;got=2;}
    }
	if (got != 2)
	{
		return 1;
	}
	
	/* Make sure the point is actually on the screen */

#ifndef NO_MULTIMON
	if (!MonitorFromPoint(*point, MONITOR_DEFAULTTONULL))
	{
		return 1;
	}
#else
	if (point->x < GetSystemMetrics(SM_XVIRTUALSCREEN) || point->x >= GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
	    point->y < GetSystemMetrics(SM_YVIRTUALSCREEN) || point->y >= GetSystemMetrics(SM_CYVIRTUALSCREEN))
	{
		return 1;
	}
#endif

	return 0;
}

void jwSaveWindowLoc(POINT point)
{
    save_number(L"X",point.x);save_number(L"Y",point.y);
}

void jwSaveWindowSize(int rows,int cols)
{
    save_number(L"Rows",rows);save_number(L"Columns",cols);
}

void jwSaveWindowCoords(HWND hwnd, int rows, int cols)
{
	RECT rect;

	/* Don't save if window is maximized */
	if (!IsZoomed(hwnd) && GetWindowRect(hwnd, &rect))
	{
		POINT p;
		p.x = rect.left;
		p.y = rect.top;

		jwSaveWindowLoc(p);
		jwSaveWindowSize(rows, cols);
	}
}
