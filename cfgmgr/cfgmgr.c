// DXGL
// Copyright (C) 2011-2017 William Feely

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>
#include <ShellAPI.h>
#ifndef LSTATUS
typedef LONG LSTATUS;
#endif
#include "crc32.h"
#include "LibSha256.h"
#include "cfgmgr.h"
#include "../ddraw/resource.h"
#include <tchar.h>
#include <math.h>
#include <stdarg.h>
#include "inih/ini.h"

TCHAR regkeyglobal[] = _T("Software\\DXGL\\Global");
TCHAR regkeybase[] = _T("Software\\DXGL\\");
TCHAR regkeydxgl[] = _T("Software\\DXGL");

DXGLCFG defaultmask;

/**
* Gets the hexadecimal digit for a number; the number must be less than 16
* or 0x10.
* @param c
*  Number from 0 to 15 or 0x0 to 0xF
* @return
*  A character representing a hexidecimal digit, letters are uppercase.
*/
static unsigned char hexdigit(unsigned char c)
{
	if (c < 10) return c + '0';
	else return (c + 'A' - 10);
}

INT_PTR CALLBACK CompatDialogCallback(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
	case WM_INITDIALOG:
		SetTimer(hWnd, 0, 3000, NULL);
		return TRUE;
	case WM_TIMER:
		EndDialog(hWnd, 0);
		break;
	default:
		return FALSE;
	}
	return FALSE;
}

void ShowRestartDialog()
{
	LPTSTR cmdline;
	LPTSTR cmdline2;
	STARTUPINFO startupinfo;
	PROCESS_INFORMATION procinfo;
	BOOL(WINAPI *_GetModuleHandleEx)(DWORD dwFlags, LPCTSTR lpModuleName, HMODULE* phModule) = FALSE;
	HMODULE hKernel32 = LoadLibrary(_T("kernel32.dll"));
	HMODULE hddraw;
	if (hKernel32)
	{
#ifdef _UNICODE
		_GetModuleHandleEx = (BOOL(WINAPI*)(DWORD, LPCTSTR, HMODULE*))GetProcAddress(hKernel32, "GetModuleHandleExW");
#else
		_GetModuleHandleEx = (BOOL(WINAPI*)(DWORD, LPCTSTR, HMODULE*))GetProcAddress(hKernel32, "GetModuleHandleExA");
#endif
	}
	if (_GetModuleHandleEx) _GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)ShowRestartDialog, &hddraw);
	else hddraw = GetModuleHandle(_T("ddraw.dll"));  //For old versions of Windows, may fail but they shouldn't have AppCompat anyways.
	if (hKernel32) FreeLibrary(hKernel32);
	DialogBox(hddraw, MAKEINTRESOURCE(IDD_COMPAT), NULL, CompatDialogCallback);
	cmdline = GetCommandLine();
	cmdline2 = (LPTSTR)malloc((_tcslen(cmdline) + 1)*sizeof(TCHAR));
	_tcscpy(cmdline2, cmdline);
	ZeroMemory(&startupinfo, sizeof(STARTUPINFO));
	startupinfo.cb = sizeof(STARTUPINFO);
	CreateProcess(NULL, cmdline2, NULL, NULL, FALSE, 0, NULL, NULL, &startupinfo, &procinfo);
	CloseHandle(procinfo.hProcess);
	CloseHandle(procinfo.hThread);
	free(cmdline2);
	ExitProcess(0);
}

BOOL AddCompatFlag(LPTSTR flag)
{
	BOOL is64 = FALSE;
	HKEY hKey;
	LRESULT error, error2;
	TCHAR filename[MAX_PATH + 1];
	TCHAR buffer[1024];
	DWORD sizeout;
	HMODULE hKernel32 = LoadLibrary(_T("kernel32.dll"));
	BOOL(WINAPI *iswow64)(HANDLE, PBOOL) = NULL;
	if (hKernel32) iswow64 = (BOOL(WINAPI*)(HANDLE, PBOOL))GetProcAddress(hKernel32, "IsWow64Process");
	if (iswow64) iswow64(GetCurrentProcess(), &is64);
	if (hKernel32) FreeLibrary(hKernel32);
	error = RegCreateKeyEx(HKEY_CURRENT_USER, _T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers"),
		0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
	if (error == ERROR_SUCCESS)
	{
		GetModuleFileName(NULL, filename, MAX_PATH);
		ZeroMemory(buffer, 1024 * sizeof(TCHAR));
		sizeout = 1024 * sizeof(TCHAR);
		error2 = RegQueryValueEx(hKey, filename, NULL, NULL, (LPBYTE)buffer, &sizeout);
		if (error2 == ERROR_SUCCESS)
		{
			if (_tcsstr(buffer,flag))
			{ 
				RegCloseKey(hKey);
				return TRUE;
			}
			else
			{
				_tcscat(buffer, _T(" "));
				_tcscat(buffer, flag);
				error2 = RegSetValueEx(hKey, filename, 0, REG_SZ, (BYTE*)buffer, (_tcslen(buffer) + 1)*sizeof(TCHAR));
				if (error2 == ERROR_SUCCESS) ShowRestartDialog();
				else
				{
					RegCloseKey(hKey);
					return FALSE;
				}
			}
		}
		else if (error2 == ERROR_FILE_NOT_FOUND)
		{
			error2 = RegSetValueEx(hKey, filename, 0, REG_SZ, (BYTE*)flag, (_tcslen(flag) + 1)*sizeof(TCHAR));
			if (error2 == ERROR_SUCCESS) ShowRestartDialog();
			else
			{
				RegCloseKey(hKey);
				return FALSE;
			}
		}
		else
		{
			RegCloseKey(hKey);
			return FALSE;
		}

	}
	return FALSE;
}

BOOL DelCompatFlag(LPTSTR flag, BOOL initial)
{
	BOOL is64 = FALSE;
	HKEY hKey;
	HKEY hKeyWrite = NULL;
	LRESULT error;
	TCHAR filename[MAX_PATH + 1];
	TCHAR buffer[1024];
	TCHAR *bufferpos;
	TCHAR writekey[67];
	DWORD sizeout;
	SHELLEXECUTEINFO info;
	BOOL(WINAPI *iswow64)(HANDLE, PBOOL) = NULL;
	HMODULE hKernel32 = LoadLibrary(_T("kernel32.dll"));
	if (hKernel32) iswow64 = (BOOL(WINAPI*)(HANDLE, PBOOL))GetProcAddress(hKernel32, "IsWow64Process");
	if (iswow64) iswow64(GetCurrentProcess(), &is64);
	if (hKernel32) FreeLibrary(hKernel32);
	// Check system first.
	_tcscpy(writekey,_T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers"));
	if (is64) error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, writekey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey);
	else error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, writekey, 0, KEY_READ, &hKey);
	if (error == ERROR_SUCCESS)
	{
		GetModuleFileName(NULL, filename, MAX_PATH);
		ZeroMemory(buffer, 1024 * sizeof(TCHAR));
		sizeout = 1024 * sizeof(TCHAR);
		if (RegQueryValueEx(hKey, filename, NULL, NULL, (LPBYTE)buffer, &sizeout) == ERROR_SUCCESS)
		{
			bufferpos = _tcsstr(buffer, flag);
			if (bufferpos)
			{
				memmove(bufferpos, bufferpos + _tcslen(flag), (_tcslen(bufferpos + _tcslen(flag))+1)*sizeof(TCHAR));
				if(is64) error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, writekey, 0, KEY_WRITE | KEY_WOW64_64KEY, &hKeyWrite);
				else error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, writekey, 0, KEY_WRITE, &hKeyWrite);
				if (error == ERROR_SUCCESS)
				{
					error = RegSetValueEx(hKeyWrite, filename, 0, REG_SZ, (BYTE*)buffer, (_tcslen(bufferpos + _tcslen(flag)))*sizeof(TCHAR)+sizeof(TCHAR));
					RegCloseKey(hKeyWrite);
					hKeyWrite = NULL;
				}
				if (error == ERROR_ACCESS_DENIED)
				{
					if (!initial)
					{
						if (MessageBox(NULL, _T("DXGL has detected an incompatible AppCompat flag for the program you are currently running and requires administrative rights to remove it.\nWould you like to continue?"),
							_T("AppCompat error"), MB_YESNO | MB_ICONWARNING) == IDYES)
						{
							TCHAR command[(MAX_PATH*2)+32];
							if (is64) _tcscpy(command,_T("ADD \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers\" /reg:64 /f /v \""));
							else _tcscpy(command,_T("ADD \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers\" /f /v \""));
							_tcscat(command,filename);
							_tcscat(command,_T("\" /t REG_SZ /d \""));
							_tcscat(command,buffer);
							_tcscat(command,_T("\""));
							ZeroMemory(&info, sizeof(SHELLEXECUTEINFO));
							info.cbSize = sizeof(SHELLEXECUTEINFO);
							info.lpVerb = _T("runas");
							info.lpFile = _T("reg.exe");
							info.lpParameters = command;
							info.nShow = SW_SHOWNORMAL;
							info.fMask = SEE_MASK_NOCLOSEPROCESS;
							ShellExecuteEx(&info);
							WaitForSingleObject(info.hProcess, INFINITE);
							GetExitCodeProcess(info.hProcess, (LPDWORD)&error);
						}
						if (!error) ShowRestartDialog();
						else
						{
							MessageBox(NULL, _T("Registry value could not be updated.  Your program may crash as a result."), _T("Error"), MB_OK | MB_ICONWARNING);
							if(hKeyWrite) RegCloseKey(hKeyWrite);
							RegCloseKey(hKey);
						}
						return FALSE;
					}
					else
					{
						if (hKeyWrite) RegCloseKey(hKeyWrite);
						RegCloseKey(hKey);
						return FALSE;
					}
				}
				else if (error == ERROR_SUCCESS) ShowRestartDialog();
			}
		}
		RegCloseKey(hKey);
	}
	// Next check user.
	_tcscpy(writekey, _T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers"));
	error = RegOpenKeyEx(HKEY_CURRENT_USER, writekey, 0, KEY_READ, &hKey);
	if (error == ERROR_SUCCESS)
	{
		GetModuleFileName(NULL, filename, MAX_PATH);
		ZeroMemory(buffer, 1024 * sizeof(TCHAR));
		sizeout = 1024 * sizeof(TCHAR);
		if (RegQueryValueEx(hKey, filename, NULL, NULL, (LPBYTE)buffer, &sizeout) == ERROR_SUCCESS)
		{
			bufferpos = _tcsstr(buffer, flag);
			if (bufferpos)
			{
				memmove(bufferpos, bufferpos + _tcslen(flag), (_tcslen(bufferpos + _tcslen(flag))+1)*sizeof(TCHAR));
				error = RegOpenKeyEx(HKEY_CURRENT_USER, writekey, 0, KEY_WRITE, &hKeyWrite);
				if (error == ERROR_SUCCESS)
				{
					error = RegSetValueEx(hKeyWrite, filename, 0, REG_SZ, (BYTE*)buffer, (_tcslen(bufferpos + _tcslen(flag)))*sizeof(TCHAR)+sizeof(TCHAR));
					RegCloseKey(hKeyWrite);
					hKeyWrite = NULL;
				}
				if (error == ERROR_ACCESS_DENIED)
				{
					if (hKeyWrite)
					{
						RegCloseKey(hKeyWrite);
						hKeyWrite = NULL;
					}
					if (!initial)
					{
						if (MessageBox(NULL, _T("DXGL has detected an incompatible AppCompat flag for the program you are currently running and requires administrative rights to remove it.\nWould you like to continue?"),
							_T("AppCompat error"), MB_YESNO | MB_ICONWARNING) == IDYES)
						{
							TCHAR command[(MAX_PATH * 2) + 32];
							_tcscpy(command,_T("ADD \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers\" /f /v \""));
							_tcscat(command,filename);
							_tcscat(command,_T("\" /t REG_SZ /d \""));
							_tcscat(command,buffer);
							_tcscat(command,_T("\""));
							ZeroMemory(&info, sizeof(SHELLEXECUTEINFO));
							info.cbSize = sizeof(SHELLEXECUTEINFO);
							info.lpVerb = _T("runas");
							info.lpFile = _T("reg.exe");
							info.lpParameters = command;
							info.nShow = SW_SHOWNORMAL;
							info.fMask = SEE_MASK_NOCLOSEPROCESS;
							ShellExecuteEx(&info);
							WaitForSingleObject(info.hProcess, INFINITE);
							GetExitCodeProcess(info.hProcess, (LPDWORD)&error);
						}
						if (!error)
						{
							ShowRestartDialog();
						}
						else MessageBox(NULL, _T("Registry value could not be updated.  Your program may crash as a result."), _T("Error"), MB_OK | MB_ICONWARNING);
						if (hKeyWrite) RegCloseKey(hKeyWrite);
						return FALSE;
					}
					else
					{
						if (hKeyWrite) RegCloseKey(hKeyWrite);
						return FALSE;
					}
				}
				else if (error == ERROR_SUCCESS)
				{
					ShowRestartDialog();
				}
			}
		}
		RegCloseKey(hKey);
	}
	return TRUE;
}

void GetDirFromPath(LPTSTR path)
{
	int i;
	int len = _tcslen(path);
	for(i = len; i > 0; i--)
	{
		if((path[i] == '\\') || (path[i] == '/'))
		{
			path[i] = 0;
			break;
		}
	}
}

int FindStringInMultiSz(LPTSTR multisz, LPCTSTR comp)
{
	LPTSTR str = multisz;
	while(str[0] != 0)
	{
		if(!_tcscmp(str,comp)) return 1;
		str += (_tcslen(str) + 1);
	}
	return 0;
}

void AddStringToMultiSz(LPTSTR multisz, LPCTSTR string)
{
	LPTSTR str = multisz;
	while(str[0] != 0)
	{
		str += (_tcslen(str) + 1);
	}
	_tcscpy(str,string);
}


BOOL ReadBool(HKEY hKey, BOOL original, BOOL *mask, LPCTSTR value)
{
	DWORD dwOut;
	DWORD sizeout = 4;
	DWORD regdword = REG_DWORD;
	LSTATUS error = RegQueryValueEx(hKey,value,NULL,&regdword,(LPBYTE)&dwOut,&sizeout);
	if(error == ERROR_SUCCESS)
	{
		*mask = TRUE;
		if(dwOut) return TRUE;
		else return FALSE;
	}
	else
	{
		*mask = FALSE;
		return original;
	}
}

DWORD ReadDeprecatedBool(HKEY hKey, DWORD original, DWORD *mask, LPCTSTR value, DWORD true_value, DWORD false_value)
{
	DWORD dwOut;
	DWORD sizeout = 4;
	DWORD regdword = REG_DWORD;
	LSTATUS error = RegQueryValueEx(hKey, value, NULL, &regdword, (LPBYTE)&dwOut, &sizeout);
	if (error == ERROR_SUCCESS)
	{
		*mask = 1;
		if (dwOut) return true_value;
		else return false_value;
	}
	else
	{
		*mask = 0;
		return original;
	}
}

DWORD ReadDWORDWithObsolete(HKEY hKey, DWORD original, DWORD *mask, LPCTSTR value,
	unsigned int obsolete_count, ...) // obsolete items are LPCTSTRs
{
	LPCTSTR obsolete;
	va_list va;
	int i;
	DWORD dwOut;
	DWORD sizeout = 4;
	DWORD regdword = REG_DWORD;
	LSTATUS error = RegQueryValueEx(hKey, value, NULL, &regdword, (LPBYTE)&dwOut, &sizeout);
	if (error == ERROR_SUCCESS)
	{
		*mask = 1;
		return dwOut;
	}
	else
	{
		va_start(va, obsolete_count);
		for (i = 0; i < obsolete_count; i++)
		{
			regdword = REG_DWORD;
			obsolete = va_arg(va, LPCTSTR);
			error = RegQueryValueEx(hKey, obsolete, NULL, &regdword, (LPBYTE)&dwOut, &sizeout);
			if (error == ERROR_SUCCESS)
			{
				*mask = 1;
				va_end(va);
				return dwOut;
			}
		}
		*mask = 0;
		va_end(va);
		return original;
	}
}

DWORD ReadDWORD(HKEY hKey, DWORD original, DWORD *mask, LPCTSTR value)
{
	DWORD dwOut;
	DWORD sizeout = 4;
	DWORD regdword = REG_DWORD;
	LSTATUS error = RegQueryValueEx(hKey,value,NULL,&regdword,(LPBYTE)&dwOut,&sizeout);
	if(error == ERROR_SUCCESS)
	{
		*mask = 1;
		return dwOut;
	}
	else
	{
		*mask = 0;
		return original;
	}
}

float ReadFloatWithObsolete(HKEY hKey, float original, float *mask, LPCTSTR value,
	unsigned int obsolete_count, ...) // obsolete items are LPCTSTRs
{
	LPCTSTR obsolete;
	va_list va;
	int i;
	float dwOut;
	DWORD sizeout = 4;
	DWORD regdword = REG_DWORD;
	LSTATUS error = RegQueryValueEx(hKey, value, NULL, &regdword, (LPBYTE)&dwOut, &sizeout);
	if (error == ERROR_SUCCESS)
	{
		*mask = 1.0f;
		return dwOut;
	}
	else
	{
		va_start(va, obsolete_count);
		for (i = 0; i < obsolete_count; i++)
		{
			regdword = REG_DWORD;
			obsolete = va_arg(va, LPCTSTR);
			error = RegQueryValueEx(hKey, obsolete, NULL, &regdword, (LPBYTE)&dwOut, &sizeout);
			if (error == ERROR_SUCCESS)
			{
				*mask = 1.0f;
				va_end(va);
				return dwOut;
			}
		}
		*mask = 0.0f;
		va_end(va);
		return original;
	}
}

float ReadFloat(HKEY hKey, float original, float *mask, LPCTSTR value)
{
	float dwOut;
	DWORD sizeout = 4;
	DWORD regdword = REG_DWORD;
	LSTATUS error = RegQueryValueEx(hKey,value,NULL,&regdword,(LPBYTE)&dwOut,&sizeout);
	if(error == ERROR_SUCCESS)
	{
		*mask = 1.0f;
		return dwOut;
	}
	else
	{
		*mask = 0.0f;
		return original;
	}
}

void ReadPath(HKEY hKey, TCHAR *path, TCHAR *mask, LPCTSTR value)
{
	DWORD sizeout = MAX_PATH*sizeof(TCHAR);
	DWORD regsz = REG_SZ;
	LSTATUS error = RegQueryValueEx(hKey,value,NULL,&regsz,(LPBYTE)path,&sizeout);
	if(error == ERROR_SUCCESS) mask[0] = 0xFF;
	else
	{
		mask[0] = 0;
		path[0] = 0;
	}
}

void ReadSettings(HKEY hKey, DXGLCFG *cfg, DXGLCFG *mask, BOOL global, BOOL dll, LPTSTR dir)
{
	DXGLCFG *cfgmask;
	TCHAR path[MAX_PATH+1];
	LONG error;
	DWORD regsz = REG_SZ;
	DWORD sizeout=4;
	if (mask) cfgmask = mask;
	else cfgmask = &defaultmask;
	cfg->scaler = ReadDWORD(hKey, cfg->scaler, &cfgmask->scaler, _T("ScalingMode"));
	cfg->fullmode = ReadDWORD(hKey, cfg->fullmode, &cfgmask->fullmode, _T("FullscreenWindowMode"));
	cfg->colormode = ReadBool(hKey,cfg->colormode,&cfgmask->colormode,_T("ChangeColorDepth"));
	cfg->postfilter = ReadDWORDWithObsolete(hKey, cfg->postfilter, &cfgmask->postfilter, _T("PostprocessFilter"),
		1, _T("FirstScaleFilter"));
	cfg->postsizex = ReadFloatWithObsolete(hKey, cfg->postsizex, &cfgmask->postsizex, _T("PostprocessScaleX"),
		1, _T("FirstScaleX"));
	cfg->postsizey = ReadFloatWithObsolete(hKey, cfg->postsizey, &cfgmask->postsizey, _T("PostprocessScaleY"),
		1, _T("FirstScaleY"));
	cfg->scalingfilter = ReadDWORD(hKey,cfg->scalingfilter,&cfgmask->scalingfilter,_T("ScalingFilter"));
	cfg->texfilter = ReadDWORD(hKey,cfg->texfilter,&cfgmask->texfilter,_T("TextureFilter"));
	cfg->anisotropic = ReadDWORD(hKey,cfg->anisotropic,&cfgmask->anisotropic,_T("AnisotropicFiltering"));
	cfg->msaa = ReadDWORD(hKey,cfg->msaa,&cfgmask->msaa,_T("Antialiasing"));
	cfg->aspect3d = ReadDWORD(hKey,cfg->aspect3d,&cfgmask->aspect3d,_T("AdjustAspectRatio"));
	cfg->primaryscale = ReadDWORD(hKey,cfg->primaryscale,&cfgmask->primaryscale,_T("AdjustPrimaryResolution"));
	cfg->primaryscalex = ReadFloat(hKey,cfg->primaryscalex,&cfgmask->primaryscalex,_T("PrimaryScaleX"));
	cfg->primaryscaley = ReadFloat(hKey,cfg->primaryscaley,&cfgmask->primaryscaley,_T("PrimaryScaleY"));
	cfg->EnableShader = ReadBool(hKey, cfg->EnableShader, &cfgmask->EnableShader, _T("EnableShader"));
	ReadPath(hKey,cfg->shaderfile,cfgmask->shaderfile,_T("ShaderFile"));
	cfg->SortModes = ReadDWORD(hKey,cfg->SortModes,&cfgmask->SortModes,_T("SortModes"));
	cfg->AddColorDepths = ReadDeprecatedBool(hKey, cfg->AddColorDepths, &cfgmask->AddColorDepths, _T("AllColorDepths"), 1 | 4 | 16, 0);
	cfg->AddColorDepths = ReadDWORD(hKey,cfg->AddColorDepths,&cfgmask->AddColorDepths,_T("AddColorDepths"));
	cfg->AddModes = ReadDeprecatedBool(hKey, cfg->AddModes, &cfgmask->AddModes, _T("ExtraModes"),7,0);
	cfg->AddModes = ReadDWORD(hKey, cfg->AddModes, &cfgmask->AddModes, _T("AddModes"));
	cfg->vsync = ReadDWORD(hKey,cfg->vsync,&cfgmask->vsync,_T("VSync"));
	cfg->TextureFormat = ReadDWORD(hKey,cfg->TextureFormat,&cfgmask->TextureFormat,_T("TextureFormat"));
	cfg->TexUpload = ReadDWORD(hKey,cfg->TexUpload,&cfgmask->TexUpload,_T("TexUpload"));
	cfg->Windows8Detected = ReadBool(hKey,cfg->Windows8Detected,&cfgmask->Windows8Detected,_T("Windows8Detected"));
	cfg->DPIScale = ReadDWORD(hKey,cfg->DPIScale,&cfgmask->DPIScale,_T("DPIScale"));
	cfg->aspect = ReadFloat(hKey, cfg->aspect, &cfgmask->aspect, _T("ScreenAspect"));
	cfg->DebugNoExtFramebuffer = ReadBool(hKey, cfg->DebugNoExtFramebuffer, &cfgmask->DebugNoExtFramebuffer, _T("DebugNoExtFramebuffer"));
	cfg->DebugNoArbFramebuffer = ReadBool(hKey, cfg->DebugNoArbFramebuffer, &cfgmask->DebugNoArbFramebuffer, _T("DebugNoArbFramebuffer"));
	cfg->DebugNoES2Compatibility = ReadBool(hKey, cfg->DebugNoES2Compatibility, &cfgmask->DebugNoES2Compatibility, _T("DebugNoES2Compatibility"));
	cfg->DebugNoExtDirectStateAccess = ReadBool(hKey, cfg->DebugNoExtDirectStateAccess, &cfgmask->DebugNoExtDirectStateAccess, _T("DebugNoExtDirectStateAccess"));
	cfg->DebugNoArbDirectStateAccess = ReadBool(hKey, cfg->DebugNoArbDirectStateAccess, &cfgmask->DebugNoArbDirectStateAccess, _T("DebugNoArbDirectStateAccess"));
	cfg->DebugNoSamplerObjects = ReadBool(hKey, cfg->DebugNoSamplerObjects, &cfgmask->DebugNoSamplerObjects, _T("DebugNoSamplerObjects"));
	cfg->DebugNoGpuShader4 = ReadBool(hKey, cfg->DebugNoGpuShader4, &cfgmask->DebugNoGpuShader4, _T("DebugNoGpuShader4"));
	cfg->DebugNoGLSL130 = ReadBool(hKey, cfg->DebugNoGLSL130, &cfgmask->DebugNoGLSL130, _T("DebugNoGLSL130"));
	cfg->DebugMaxGLVersionMajor = ReadDWORD(hKey, cfg->DebugMaxGLVersionMajor, &cfgmask->DebugMaxGLVersionMajor, _T("DebugMaxGLVersionMajor"));
	cfg->DebugMaxGLVersionMinor = ReadDWORD(hKey, cfg->DebugMaxGLVersionMinor, &cfgmask->DebugMaxGLVersionMinor, _T("DebugMaxGLVersionMinor"));
	if(!global && dll)
	{
		sizeout = 0;
		if(!dir) GetModuleFileName(NULL,path,MAX_PATH);
		else _tcsncpy(path,dir,MAX_PATH+1);
		GetDirFromPath(path);
		error = RegQueryValueEx(hKey,_T("InstallPath"),NULL,&regsz,NULL,&sizeout);
		if(error == ERROR_FILE_NOT_FOUND)
			RegSetValueEx(hKey, _T("InstallPath"), 0, REG_MULTI_SZ,
				(LPBYTE)path, _tcslen(path) * sizeof(TCHAR));
	}
}

void WriteBool(HKEY hKey, BOOL value, BOOL mask, LPCTSTR name)
{
	const DWORD one = 1;
	const DWORD zero = 0;
	if(mask)
	{
		if(value) RegSetValueEx(hKey,name,0,REG_DWORD,(BYTE*)&one,4);
		else RegSetValueEx(hKey,name,0,REG_DWORD,(BYTE*)&zero,4);
	}
	else RegDeleteValue(hKey,name);
}

void WriteDWORDDeleteObsolete(HKEY hKey, DWORD value, DWORD mask, LPCTSTR name,
	unsigned int obsolete_count, ...) // obsolete items are LPCTSTRs
{
	LPCTSTR obsolete;
	va_list va;
	int i;
	va_start(va, obsolete_count);
	for (i = 0; i < obsolete_count; i++)
	{
		obsolete = va_arg(va, LPCTSTR);
		RegDeleteValue(hKey, obsolete);
	}
	va_end(va);
	if (mask) RegSetValueEx(hKey, name, 0, REG_DWORD, (BYTE*)&value, 4);
	else RegDeleteValue(hKey, name);
}

void WriteDWORD(HKEY hKey, DWORD value, DWORD mask, LPCTSTR name)
{
	if(mask) RegSetValueEx(hKey,name,0,REG_DWORD,(BYTE*)&value,4);
	else RegDeleteValue(hKey,name);
}
void WritePath(HKEY hKey, const TCHAR *path, const TCHAR *mask, LPCTSTR name)
{
	if(mask[0]) RegSetValueEx(hKey,name,0,REG_SZ,(BYTE*)path,(_tcslen(path)+1)*sizeof(TCHAR));
	else RegDeleteValue(hKey,name);
}

void WriteFloatDeleteObsolete(HKEY hKey, float value, float mask, LPCTSTR name,
	unsigned int obsolete_count, ...) // obsolete items are LPCTSTRs
{
	LPCTSTR obsolete;
	va_list va;
	int i;
	va_start(va, obsolete_count);
	for (i = 0; i < obsolete_count; i++)
	{
		obsolete = va_arg(va, LPCTSTR);
		RegDeleteValue(hKey, obsolete);
	}
	va_end(va);
	if (fabsf(mask) > 0.5f) RegSetValueEx(hKey, name, 0, REG_DWORD, (BYTE*)&value, 4);
	else RegDeleteValue(hKey, name);
}
void WriteFloat(HKEY hKey, float value, float mask, LPCTSTR name)
{
	if (fabsf(mask) > 0.5f) RegSetValueEx(hKey, name, 0, REG_DWORD, (BYTE*)&value, 4);
	else RegDeleteValue(hKey, name);
}

void WriteSettings(HKEY hKey, const DXGLCFG *cfg, const DXGLCFG *mask, BOOL global)
{
	const DXGLCFG *cfgmask;
	if(mask) cfgmask = mask;
	else cfgmask = &defaultmask;
	memset(&defaultmask,1,sizeof(DXGLCFG));
	defaultmask.aspect = 1.0f;
	defaultmask.postsizex = 1.0f;
	defaultmask.postsizey = 1.0f;
	WriteDWORD(hKey,cfg->scaler,cfgmask->scaler,_T("ScalingMode"));
	WriteDWORD(hKey, cfg->fullmode, cfgmask->fullmode, _T("FullscreenWindowMode"));
	WriteBool(hKey,cfg->colormode,cfgmask->colormode,_T("ChangeColorDepth"));
	WriteDWORDDeleteObsolete(hKey, cfg->postfilter, cfgmask->postfilter, _T("PostprocessFilter"),
		1, _T("FirstScaleFilter"));
	WriteFloatDeleteObsolete(hKey, cfg->postsizex, cfgmask->postsizex, _T("PostprocessScaleX"),
		1, _T("FirstScaleX"));
	WriteFloatDeleteObsolete(hKey, cfg->postsizey, cfgmask->postsizey, _T("PostprocessScaleY"),
		1, _T("FirstScaleY"));
	WriteDWORD(hKey,cfg->scalingfilter,cfgmask->scalingfilter,_T("ScalingFilter"));
	WriteDWORD(hKey,cfg->texfilter,cfgmask->texfilter,_T("TextureFilter"));
	WriteDWORD(hKey,cfg->anisotropic,cfgmask->anisotropic,_T("AnisotropicFiltering"));
	WriteDWORD(hKey,cfg->msaa,cfgmask->msaa,_T("Antialiasing"));
	WriteDWORD(hKey,cfg->aspect3d,cfgmask->aspect3d,_T("AdjustAspectRatio"));
	WriteBool(hKey,cfg->primaryscale,cfgmask->primaryscale,_T("AdjustPrimaryResolution"));
	WriteFloat(hKey,cfg->primaryscalex,cfgmask->primaryscalex,_T("PrimaryScaleX"));
	WriteFloat(hKey,cfg->primaryscaley,cfgmask->primaryscaley,_T("PrimaryScaleY"));
	WriteBool(hKey, cfg->EnableShader, cfgmask->EnableShader, _T("EnableShader"));
	WritePath(hKey,cfg->shaderfile,cfgmask->shaderfile,_T("ShaderFile"));
	WriteDWORD(hKey,cfg->SortModes,cfgmask->SortModes,_T("SortModes"));
	WriteBool(hKey,cfg->AddColorDepths,cfgmask->AddColorDepths,_T("AddColorDepths"));
	WriteBool(hKey,cfg->AddModes,cfgmask->AddModes,_T("AddModes"));
	WriteDWORD(hKey,cfg->vsync,cfgmask->vsync,_T("VSync"));
	WriteDWORD(hKey,cfg->TextureFormat,cfgmask->TextureFormat,_T("TextureFormat"));
	WriteDWORD(hKey,cfg->TexUpload,cfgmask->TexUpload,_T("TexUpload"));
	WriteBool(hKey,cfg->Windows8Detected,cfgmask->Windows8Detected,_T("Windows8Detected"));
	WriteDWORD(hKey,cfg->DPIScale,cfgmask->DPIScale,_T("DPIScale"));
	WriteFloat(hKey, cfg->aspect, cfgmask->aspect, _T("ScreenAspect"));
	WriteBool(hKey, cfg->DebugNoExtFramebuffer, cfgmask->DebugNoExtFramebuffer, _T("DebugNoExtFramebuffer"));
	WriteBool(hKey, cfg->DebugNoArbFramebuffer, cfgmask->DebugNoArbFramebuffer, _T("DebugNoArbFramebuffer"));
	WriteBool(hKey, cfg->DebugNoES2Compatibility, cfgmask->DebugNoES2Compatibility, _T("DebugNoES2Compatibility"));
	WriteBool(hKey, cfg->DebugNoExtDirectStateAccess, cfgmask->DebugNoExtDirectStateAccess, _T("DebugNoExtDirectStateAccess"));
	WriteBool(hKey, cfg->DebugNoArbDirectStateAccess, cfgmask->DebugNoArbDirectStateAccess, _T("DebugNoArbDirectStateAccess"));
	WriteBool(hKey, cfg->DebugNoSamplerObjects, cfgmask->DebugNoSamplerObjects, _T("DebugNoSamplerObjects"));
	WriteBool(hKey, cfg->DebugNoGpuShader4, cfgmask->DebugNoGpuShader4, _T("DebugNoGpuShader4"));
	WriteBool(hKey, cfg->DebugNoGLSL130, cfgmask->DebugNoGLSL130, _T("DebugNoGLSL130"));
	WriteDWORD(hKey, cfg->DebugMaxGLVersionMajor, cfgmask->DebugMaxGLVersionMajor, _T("DebugMaxGLVersionMajor"));
	WriteDWORD(hKey, cfg->DebugMaxGLVersionMinor, cfgmask->DebugMaxGLVersionMinor, _T("DebugMaxGLVersionMinor"));
}

TCHAR newregname[MAX_PATH+65];

BOOL CheckProfileExists(LPTSTR path)
{
	Sha256Context sha_context;
	SHA256_HASH sha256;
	TCHAR sha256string[65];
	TCHAR regkey[MAX_PATH + 80];
	TCHAR filename[MAX_PATH + 1];
	TCHAR pathlwr[MAX_PATH + 1];
	HKEY hKey;
	LONG error;
	int i;
	_tcscpy(regkey, regkeybase);
	_tcscat(regkey, _T("Profiles\\"));
	_tcscpy(filename, path);
	for (i = _tcslen(filename); (i > 0) && (filename[i] != 92) && (filename[i] != 47); i--);
	i++;
	_tcscat(regkey, &filename[i]);
	_tcscat(regkey, _T("-"));
	i--;
	filename[i] = 0;
	_tcslwr(filename);
	Sha256Initialise(&sha_context);
	Sha256Update(&sha_context, filename, _tcslen(filename));
	Sha256Finalise(&sha_context, &sha256);
	for (i = 0; i < (256 / 8); i++)
	{
		sha256string[i * 2] = (TCHAR)hexdigit(sha256.bytes[i] >> 4);
		sha256string[(i * 2) + 1] = (TCHAR)hexdigit(sha256.bytes[i] & 0xF);
	}
	sha256string[256 / 4] = 0;
	_tcscat(regkey, sha256string);
	error = RegOpenKeyEx(HKEY_CURRENT_USER, regkey, 0, KEY_READ, &hKey);
	if (error = ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return TRUE;
	}
	else return FALSE;
}

LPTSTR MakeNewConfig(LPTSTR path)
{
	Sha256Context sha_context;
	SHA256_HASH sha256;
	TCHAR sha256string[65];
	TCHAR pathlwr[MAX_PATH + 1];
	HKEY hKey;
	DXGLCFG tmp;
	TCHAR regkey[MAX_PATH + 80];
	int i;
	TCHAR filename[MAX_PATH + 1];
	_tcsncpy(pathlwr, path, MAX_PATH);
	for (i = _tcslen(pathlwr); (i > 0) && (pathlwr[i] != 92) && (pathlwr[i] != 47); i--);
	pathlwr[i] = 0;
	_tcslwr(pathlwr);
	Sha256Initialise(&sha_context);
	Sha256Update(&sha_context, pathlwr, _tcslen(pathlwr));
	Sha256Finalise(&sha_context, &sha256);
	for (i = 0; i < (256 / 8); i++)
	{
		sha256string[i * 2] = (TCHAR)hexdigit(sha256.bytes[i] >> 4);
		sha256string[(i * 2) + 1] = (TCHAR)hexdigit(sha256.bytes[i] & 0xF);
	}
	sha256string[256 / 4] = 0;
	_tcscpy(regkey,regkeybase);
	_tcsncpy(filename,path,MAX_PATH);
	filename[MAX_PATH] = 0;
	for(i = _tcslen(filename); (i > 0) && (filename[i] != 92) && (filename[i] != 47); i--);
	i++;
	_tcscat(regkey, _T("Profiles\\"));
	_tcscat(regkey,&filename[i]);
	_tcscat(regkey,_T("-"));
	_tcscat(regkey,sha256string);
	_tcscpy(newregname,&filename[i]);
	_tcscat(newregname,_T("-"));
	_tcscat(newregname,sha256string);
	RegCreateKeyEx(HKEY_CURRENT_USER,regkey,0,NULL,0,KEY_ALL_ACCESS,NULL,&hKey,NULL);
	ReadSettings(hKey,&tmp,NULL,FALSE,TRUE,path);
	RegCloseKey(hKey);
	return newregname;
}

void GetDefaultConfig(DXGLCFG *cfg)
{
	BOOL Windows8Detected = FALSE;
	ZeroMemory(cfg, sizeof(DXGLCFG));
	cfg->DPIScale = 1;
	cfg->AddModes = 1;
	if (!cfg->Windows8Detected)
	{
		OSVERSIONINFO osver;
		osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx(&osver);
		if (osver.dwMajorVersion > 6) Windows8Detected = TRUE;
		if ((osver.dwMajorVersion == 6) && (osver.dwMinorVersion >= 2)) Windows8Detected = TRUE;
		if (Windows8Detected) cfg->AddColorDepths = 1 | 4 | 16;
	}
}

DWORD INIBoolValue(const char *value)
{
	if (value[0] == 'F') return 0;
	if (value[0] == 'f') return 0;
	if (value[0] == 'T') return 1;
	if (value[0] == 't') return 1;
	if (!atoi(value)) return 0;
	return 1;
}

DWORD INIIntValue(const char *value)
{
	return atoi(value);
}

DWORD INIHexValue(const char *value)
{
	return (DWORD)strtoul(value, NULL, 0);
}

float INIAspectValue(const char *value)
{
	char *ptr;
	float numerator, denominator;
	if (!strcmp(value, "Default")) return 0.0f;
	else
	{
		// Check for colon
		ptr = strstr(value, ":");
		if (ptr)
		{
			*ptr = 0;
			numerator = atof(value);
			denominator = atof(ptr + 1);
			return numerator / denominator;
		}
		else return atof(value);
	}
}

float INIFloatValue(const char *value)
{
	return (float)atof(value);
}

int ReadINICallback(DXGLCFG *cfg, const char *section, const char *name,
	const char *value)
{
	if (!stricmp(section, "system"))
	{
		if (!stricmp(name, "NoWriteRegistry")) cfg->NoWriteRegistry = INIBoolValue(value);
		if (!stricmp(name, "OverrideDefaults")) cfg->OverrideDefaults = INIBoolValue(value);
	}
	if (!stricmp(section, "display"))
	{
		if (!stricmp(name, "ScalingMode")) cfg->scaler = INIIntValue(value);
		if (!stricmp(name, "FullscreenWindowMode")) cfg->fullmode = INIIntValue(value);
		if (!stricmp(name, "ChangeColorDepth")) cfg->colormode = INIBoolValue(value);
		if (!stricmp(name, "AllColorDepths"))
		{
			if (!cfg->ParsedAddColorDepths)
			{
				if (INIBoolValue(value)) cfg->AddColorDepths = 1 | 4 | 16;
				else cfg->AddColorDepths = 0;
			}
		}
		if (!stricmp(name, "AddColorDepths"))
		{
			cfg->ParsedAddColorDepths = TRUE;
			cfg->AddColorDepths = INIIntValue(value);
		}
		if (!stricmp(name, "ExtraModes"))
		{
			if (!cfg->ParsedAddModes)
			{
				if (INIBoolValue(value)) cfg->AddModes = 7;
				else cfg->AddModes = 0;
			}
		}
		if (!stricmp(name, "AddModes"))
		{
			cfg->ParsedAddModes = TRUE;
			cfg->AddModes = INIIntValue(value);
		}
		if (!stricmp(name, "SortModes")) cfg->SortModes = INIIntValue(value);
	}
	if (!stricmp(section, "scaling"))
	{
		if (!stricmp(name, "ScalingFilter")) cfg->scalingfilter = INIIntValue(value);
		if (!stricmp(name, "AdjustPrimaryResolution")) cfg->primaryscale = INIIntValue(value);
		if (!stricmp(name, "PrimaryScaleX")) cfg->primaryscalex = INIFloatValue(value);
		if (!stricmp(name, "PrimaryScaleY")) cfg->primaryscaley = INIFloatValue(value);
		if (!stricmp(name, "ScreenAspect")) cfg->aspect = INIAspectValue(value);
		if (!stricmp(name, "DPIScale")) cfg->DPIScale = INIIntValue(value);
	}
	if (!stricmp(section, "postprocess"))
	{
		if (!stricmp(name, "PostprocessFilter")) cfg->postfilter = INIIntValue(value);
		if (!stricmp(name, "PostprocessScaleX")) cfg->postsizex = INIFloatValue(value);
		if (!stricmp(name, "PostprocessScaleY")) cfg->postsizex = INIFloatValue(value);
		if (!stricmp(name, "EnableShader")) cfg->EnableShader = INIBoolValue(value);
		if (!stricmp(name, "ShaderFile")) strncpy(cfg->shaderfile, value, MAX_PATH);
	}
	if (!stricmp(section, "d3d"))
	{
		if (!stricmp(name, "TextureFilter")) cfg->texfilter = INIIntValue(value);
		if (!stricmp(name, "AnisotropicFiltering")) cfg->anisotropic = INIIntValue(value);
		if (!stricmp(name, "Antialiasing")) cfg->msaa = INIHexValue(value);
		if (!stricmp(name, "D3DAspect")) cfg->aspect3d = INIIntValue(value);
	}
	if (!stricmp(section, "advanced"))
	{
		if (!stricmp(name, "VSync")) cfg->vsync = INIIntValue(value);
		if (!stricmp(name, "TextureFormat")) cfg->TextureFormat = INIIntValue(value);
		if (!stricmp(name, "TexUpload")) cfg->TexUpload = INIIntValue(value);
	}
	if (!stricmp(section, "debug"))
	{
		if (!stricmp(name, "DebugNoExtFramebuffer")) cfg->DebugNoExtFramebuffer = INIBoolValue(value);
		if (!stricmp(name, "DebugNoArbFramebuffer")) cfg->DebugNoArbFramebuffer = INIBoolValue(value);
		if (!stricmp(name, "DebugNoES2Compatibility")) cfg->DebugNoES2Compatibility = INIBoolValue(value);
		if (!stricmp(name, "DebugNoExtDirectStateAccess")) cfg->DebugNoExtDirectStateAccess = INIBoolValue(value);
		if (!stricmp(name, "DebugNoArbDirectStateAccess")) cfg->DebugNoArbDirectStateAccess = INIBoolValue(value);
		if (!stricmp(name, "DebugNoSamplerObjects")) cfg->DebugNoSamplerObjects = INIBoolValue(value);
		if (!stricmp(name, "DebugNoGpuShader4")) cfg->DebugNoGpuShader4 = INIBoolValue(value);
		if (!stricmp(name, "DebugNoGLSL130")) cfg->DebugNoGLSL130 = INIBoolValue(value);
		if (!stricmp(name, "DebugMaxGLVersionMajor")) cfg->DebugMaxGLVersionMajor = INIIntValue(value);
		if (!stricmp(name, "DebugMaxGLVersionMinor")) cfg->DebugMaxGLVersionMinor = INIIntValue(value);
	}
	return 1;
}

void ReadINI(DXGLCFG *cfg)
{
	FILE *file;
	TCHAR inipath[MAX_PATH + 10];
	GetModuleFileName(NULL, inipath, MAX_PATH);
	GetDirFromPath(inipath);
	_tcscat(inipath, _T("\\dxgl.ini"));
	file = _tfopen(inipath, _T("r"));
	if (file)
	{
		ini_parse_file(file, ReadINICallback, cfg);
		fclose(file);
	}
}

void GetCurrentConfig(DXGLCFG *cfg, BOOL initial)
{
	HKEY hKey;
	Sha256Context sha_context;
	SHA256_HASH sha256;
	TCHAR sha256string[65];
	TCHAR regkey[MAX_PATH+80];
	FILE *file;
	TCHAR filename[MAX_PATH+1];
	int i;
	BOOL DPIAwarePM = FALSE;
	HMODULE hSHCore = NULL;
	HMODULE hUser32 = NULL;
	GetModuleFileName(NULL, filename, MAX_PATH);
	_tcscpy(regkey, regkeybase);
	_tcscat(regkey, _T("Profiles\\"));
	for (i = _tcslen(filename); (i > 0) && (filename[i] != 92) && (filename[i] != 47); i--);
	i++;
	_tcscat(regkey, &filename[i]);
	_tcscat(regkey, _T("-"));
	i--;
	filename[i] = 0;
	_tcslwr(filename);
	Sha256Initialise(&sha_context);
	Sha256Update(&sha_context, filename, _tcslen(filename));
	Sha256Finalise(&sha_context, &sha256);
	for (i = 0; i < (256 / 8); i++)
	{
		sha256string[i * 2] = (TCHAR)hexdigit(sha256.bytes[i] >> 4);
		sha256string[(i * 2) + 1] = (TCHAR)hexdigit(sha256.bytes[i] & 0xF);
	}
	sha256string[256 / 4] = 0;
	_tcscat(regkey,sha256string);
	GetGlobalConfig(cfg, initial);
	ReadINI(cfg);
	if (cfg->OverrideDefaults)
	{
		GetDefaultConfig(cfg);
		ReadINI(cfg);
	}
	if (!cfg->Windows8Detected)
	{
		OSVERSIONINFO osver;
		osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx(&osver);
		if (osver.dwMajorVersion > 6) cfg->Windows8Detected = TRUE;
		if ((osver.dwMajorVersion == 6) && (osver.dwMinorVersion >= 2)) cfg->Windows8Detected = TRUE;
		if (cfg->Windows8Detected) cfg->AddColorDepths = 1 | 4 | 16;
	}
	if (initial) RegOpenKeyEx(HKEY_CURRENT_USER, regkey, 0, KEY_READ, &hKey);
	else
	{
		RegCreateKeyEx(HKEY_CURRENT_USER, regkeyglobal, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
		if (hKey) RegCloseKey(hKey);
		RegCreateKeyEx(HKEY_CURRENT_USER, regkey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
	}
	if (hKey)
	{
		ReadSettings(hKey, cfg, NULL, FALSE, TRUE, NULL);
		RegCloseKey(hKey);
	}
	hKey = NULL;
	if (cfg->DPIScale == 2)	AddCompatFlag(_T("HIGHDPIAWARE"));
	else DelCompatFlag(_T("HIGHDPIAWARE"),initial);
	if (initial)
	{
		if (cfg->DPIScale == 1)
		{
			hSHCore = LoadLibrary(_T("SHCore.dll"));
			if (hSHCore)
			{
				HRESULT(WINAPI *_SetProcessDpiAwareness)(DWORD value)
					= (HRESULT(WINAPI*)(DWORD))GetProcAddress(hSHCore, "SetProcessDpiAwareness");
				if (_SetProcessDpiAwareness)
				{
					DPIAwarePM = TRUE;
					_SetProcessDpiAwareness(2);
				}
			}
			if (!DPIAwarePM)
			{
				hUser32 = LoadLibrary(_T("User32.dll"));
				if (hUser32)
				{
					BOOL(WINAPI *_SetProcessDPIAware)()
						= (BOOL(WINAPI*)())GetProcAddress(hUser32, "SetProcessDPIAware");
					if (_SetProcessDPIAware) _SetProcessDPIAware();
				}
			}
			if (hSHCore) FreeLibrary(hSHCore);
			if (hUser32) FreeLibrary(hUser32);
		}
	}
	//if(!cfg->colormode) DelCompatFlag(_T("DWM8And16BitMitigation"), initial);  // Windows 10 compatibility issues; not needed?
}
void GetGlobalConfig(DXGLCFG *cfg, BOOL initial)
{
	HKEY hKey = NULL;
	GetDefaultConfig(cfg);
	RegOpenKeyEx(HKEY_CURRENT_USER, regkeyglobal, 0, KEY_READ, &hKey);
	if (hKey)
	{
		ReadSettings(hKey, cfg, NULL, TRUE, FALSE, NULL);
		RegCloseKey(hKey);
	}
}

void GetConfig(DXGLCFG *cfg, DXGLCFG *mask, LPCTSTR name)
{
	HKEY hKey;
	TCHAR regkey[MAX_PATH + 80];
	_tcscpy(regkey,regkeybase);
	_tcscat(regkey, _T("Profiles\\"));
	_tcsncat(regkey, name, MAX_PATH);
	ZeroMemory(cfg,sizeof(DXGLCFG));
	cfg->DPIScale = 1;
	RegCreateKeyEx(HKEY_CURRENT_USER,regkey,0,NULL,0,KEY_ALL_ACCESS,NULL,&hKey,NULL);
	ReadSettings(hKey,cfg,mask,FALSE,FALSE,NULL);
	RegCloseKey(hKey);
}
void SetGlobalConfig(const DXGLCFG *cfg)
{
	HKEY hKey;
	RegCreateKeyEx(HKEY_CURRENT_USER,regkeyglobal,0,NULL,0,KEY_ALL_ACCESS,NULL,&hKey,NULL);
	WriteSettings(hKey,cfg,NULL,TRUE);
	RegCloseKey(hKey);
}

void SetConfig(const DXGLCFG *cfg, const DXGLCFG *mask, LPCTSTR name)
{
	HKEY hKey;
	TCHAR regkey[MAX_PATH + 80];
	_tcscpy(regkey, regkeybase);
	_tcscat(regkey, _T("Profiles\\"));
	_tcsncat(regkey, name, MAX_PATH);
	RegCreateKeyEx(HKEY_CURRENT_USER, regkey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
	WriteSettings(hKey,cfg,mask,FALSE);
	RegCloseKey(hKey);
}

/**
  * Checks the registry configuration version and if outdated upgrades to
  * the latest version - currently version 1
  * Alpha version configuration is assumed to be version 0.
  */
void UpgradeConfig()
{
	DWORD version = 0;
	DWORD keyindex;
	HKEY hKey;
	HKEY hKeyProfile;
	HKEY hKeyDest;
	TCHAR regkey[MAX_PATH + 24];
	TCHAR subkey[MAX_PATH];
	TCHAR exepath[(MAX_PATH * 2) + 1];
	FILE *file;
	TCHAR crcstr[10];
	unsigned long crc;
	DWORD numoldconfig;
	DWORD numvalue;
	DWORD oldconfigcount;
	DWORD olddirsize = 1024;
	TCHAR *olddir = NULL;
	DWORD oldvaluesize = 1024;
	TCHAR *oldvalue = NULL;
	TCHAR *ptr;
	size_t length;
	CFGREG *oldkeys = NULL;
	DWORD regtype;
	DWORD sizeout, sizeout2;
	Sha256Context sha_context;
	LONG error;
	LONG error2;
	int i;
	// Check configuration version first
	_tcscpy(regkey, regkeybase);
	error = RegCreateKeyEx(HKEY_CURRENT_USER, regkey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
	if (error != ERROR_SUCCESS)
	{
		MessageBox(NULL, _T("Could not open registry key to upgrade DXGL"), _T("Fatal error"), MB_ICONSTOP | MB_OK);
		ExitProcess(error);
	}
	sizeout = 4;
	regtype = REG_DWORD;
	error = RegQueryValueEx(hKey, _T("Configuration Version"), NULL, &regtype, &version, &sizeout);
	if (error != ERROR_SUCCESS) version = 0;  // Version is 0 if not set (alpha didn't have version)
	if (regtype != REG_DWORD) version = 0; // Is the key the wrong type?
	if (version >= 1) return;  // If version is 1 no need to upgrade.
ver0to1:
	// Count profiles
	keyindex = 0;
	numoldconfig = 0;
	olddir = malloc(olddirsize);
	do
	{
		sizeout = MAX_PATH;
		error = RegEnumKeyEx(hKey, keyindex, &subkey, &sizeout,
			NULL, NULL, NULL, NULL);
		keyindex++;
		if (error == ERROR_SUCCESS)
		{
			error2 = RegOpenKeyEx(hKey, subkey, 0, KEY_READ, &hKeyProfile);
			if (error2 == ERROR_SUCCESS)
			{
				regtype = REG_MULTI_SZ;
				sizeout = olddirsize;
				error2 = RegQueryValueEx(hKeyProfile, _T("InstallPaths"), NULL,
					&regtype, olddir, &sizeout);
				if (error2 == ERROR_MORE_DATA)
				{
					olddirsize = sizeout;
					olddir = realloc(olddir, olddirsize);
					if (!olddir)
					{
						MessageBox(NULL, _T("Out of memory updating registry"), _T("Fatal error"), MB_ICONSTOP | MB_OK);
						ExitProcess(error);
					}
					sizeout = olddirsize;
					error2 = RegQueryValueEx(hKeyProfile, _T("InstallPaths"), NULL,
						&regtype, olddir, &sizeout);
				}
				if (error2 == ERROR_SUCCESS)
				{
					if (regtype == REG_MULTI_SZ)
					{
						// Parse MULTI_SZ and count install paths
						ptr = olddir;
						do
						{
							length = _tcslen(ptr);
							if (length)
							{
								numoldconfig++;
								ptr += length + 1;
							}
						} while (length > 0);
					}
				}
				RegCloseKey(hKeyProfile);
			}
		}
	} while (error == ERROR_SUCCESS);
	// Read the old profiles into a list
	// Just need the keys; will transfer the settings later
	oldkeys = (CFGREG*)malloc(numoldconfig * sizeof(CFGREG));
	if (!oldkeys)
	{
		free(olddir);
		MessageBox(NULL, _T("Out of memory updating registry"), _T("Fatal error"), MB_ICONSTOP | MB_OK);
		ExitProcess(ERROR_NOT_ENOUGH_MEMORY);
	}
	ZeroMemory(oldkeys, numoldconfig * sizeof(CFGREG));
	oldconfigcount = 0;
	keyindex = 0;
	do
	{
		sizeout = MAX_PATH;
		error = RegEnumKeyEx(hKey, keyindex, &subkey, &sizeout,
			NULL, NULL, NULL, NULL);
		keyindex++;
		if (error == ERROR_SUCCESS)
		{
			error2 = RegOpenKeyEx(hKey, subkey, 0, KEY_READ, &hKeyProfile);
			if (error2 == ERROR_SUCCESS)
			{
				regtype = REG_MULTI_SZ;
				sizeout = olddirsize;
				error2 = RegQueryValueEx(hKeyProfile, _T("InstallPaths"), NULL,
					&regtype, olddir, &sizeout);
				if (error2 == ERROR_MORE_DATA)
				{
					olddirsize = sizeout;
					olddir = realloc(olddir, olddirsize);
					if (!olddir)
					{
						MessageBox(NULL, _T("Out of memory updating registry"), _T("Fatal error"), MB_ICONSTOP | MB_OK);
						ExitProcess(error);
					}
					sizeout = olddirsize;
					error2 = RegQueryValueEx(hKeyProfile, _T("InstallPaths"), NULL,
						&regtype, olddir, &sizeout);
				}
				if (error2 == ERROR_SUCCESS)
				{
					if (regtype == REG_MULTI_SZ)
					{
						// Parse MULTI_SZ build profiles
						ptr = olddir;
						do
						{
							length = _tcslen(ptr);
							if (length)
							{
								_tcsncpy(oldkeys[oldconfigcount].InstallPath, ptr, MAX_PATH);
								_tcsncpy(oldkeys[oldconfigcount].InstallPathLowercase, ptr, MAX_PATH);
								_tcslwr(oldkeys[oldconfigcount].InstallPathLowercase);
								_tcsncpy(oldkeys[oldconfigcount].OldKey, subkey, MAX_PATH);
								if (!_tcscmp(subkey, _T("DXGLTestApp")))
								{
									_tcscpy(oldkeys[oldconfigcount].EXEFile, _T("dxgltest.exe-0"));
									oldkeys[oldconfigcount].nocrc = TRUE;
								}
								else
								{
									_tcsncpy(oldkeys[oldconfigcount].EXEFile, subkey, MAX_PATH);
									oldkeys[oldconfigcount].nocrc = FALSE;
								}
								if (_tcsrchr(oldkeys[oldconfigcount].EXEFile, _T('-')))
								{
									_tcscpy(oldkeys[oldconfigcount].crc32, _tcsrchr(oldkeys[oldconfigcount].EXEFile, _T('-')) + 1);
									*(_tcsrchr(oldkeys[oldconfigcount].EXEFile, _T('-'))) = 0;
								}
								else
								{
									_tcscpy(oldkeys[oldconfigcount].crc32, "0");
								}
								_tcscpy(exepath, oldkeys[oldconfigcount].InstallPath);
								_tcscat(exepath, _T("\\"));
								_tcscat(exepath, oldkeys[oldconfigcount].EXEFile);
								if (!oldkeys[oldconfigcount].nocrc)
								{
									file = _tfopen(exepath, _T("rb"));
									if (file != NULL) Crc32_ComputeFile(file, &crc);
									else crc = 0;
									_itot(crc, crcstr, 16);
									if (file)
									{
										fclose(file);
										if (!_tcsicmp(crcstr, oldkeys[oldconfigcount].crc32))
											oldkeys[oldconfigcount].exe_found = TRUE;
										else oldkeys[oldconfigcount].exe_found = FALSE;
									}
									else oldkeys[oldconfigcount].exe_found = FALSE;
								}
								else oldkeys[oldconfigcount].exe_found = TRUE;
								Sha256Initialise(&sha_context);
								Sha256Update(&sha_context, oldkeys[oldconfigcount].InstallPathLowercase, length);
								Sha256Finalise(&sha_context, &oldkeys[oldconfigcount].PathHash);
								for (i = 0; i < (256 / 8); i++)
								{
									oldkeys[oldconfigcount].PathHashString[i * 2] = (TCHAR)hexdigit(oldkeys[oldconfigcount].PathHash.bytes[i] >> 4);
									oldkeys[oldconfigcount].PathHashString[(i * 2) + 1] = (TCHAR)hexdigit(oldkeys[oldconfigcount].PathHash.bytes[i] & 0xF);
								}
								oldkeys[oldconfigcount].PathHashString[256 / 4] = 0;
								oldconfigcount++;
								if (oldconfigcount > numoldconfig)
								{
									free(oldkeys);
									goto ver0to1;
								}
								ptr += length + 1;
							}
						} while (length > 0);
					}
				}
				RegCloseKey(hKeyProfile);
			}
		}
	} while (error == ERROR_SUCCESS);
	// Transfer matching profiles
	oldvalue = malloc(oldvaluesize);
	for (i = 0; i < oldconfigcount; i++)
	{
		if (oldkeys[i].exe_found)
		{
			error = RegOpenKeyEx(hKey, oldkeys[i].OldKey, 0, KEY_READ, &hKeyProfile);
			if (error == ERROR_SUCCESS)
			{
				_tcscpy(exepath, _T("Profiles\\"));
				_tcscat(exepath, oldkeys[i].EXEFile);
				_tcscat(exepath, _T("-"));
				_tcscat(exepath, oldkeys[i].PathHashString);
				error = RegCreateKeyEx(hKey, exepath, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKeyDest, NULL);
				if (error == ERROR_SUCCESS)
				{
					numvalue = 0;
					do
					{
						sizeout = olddirsize;
						sizeout2 = oldvaluesize;
						error = RegEnumValue(hKeyProfile, numvalue, olddir, &sizeout, NULL, &regtype, oldvalue, &sizeout2);
						if (error == ERROR_MORE_DATA)
						{
							if (sizeout > olddirsize)
							{
								olddirsize = sizeout;
								olddir = realloc(olddir, olddirsize);
								if (!olddir)
								{
									MessageBox(NULL, _T("Out of memory updating registry"), _T("Fatal error"), MB_ICONSTOP | MB_OK);
									ExitProcess(error);
								}
							}
							if (sizeout2 > oldvaluesize)
							{
								oldvaluesize = sizeout2;
								oldvalue = realloc(oldvalue, oldvaluesize);
								if (!oldvalue)
								{
									MessageBox(NULL, _T("Out of memory updating registry"), _T("Fatal error"), MB_ICONSTOP | MB_OK);
									ExitProcess(error);
								}
							}
							sizeout = olddirsize;
							sizeout2 = oldvaluesize;
							error = RegEnumValue(hKeyProfile, numvalue, olddir, &sizeout, NULL, &regtype, oldvalue, &sizeout2);
						}
						if (error == ERROR_SUCCESS)
						{
							if (_tcsnicmp(olddir, _T("InstallPaths"), sizeout))
								RegSetValueEx(hKeyDest, olddir, 0, regtype, oldvalue, sizeout2);
						}
						numvalue++;
					} while (error == ERROR_SUCCESS);
					RegSetValueEx(hKeyDest, _T("InstallPath"), 0, REG_SZ, oldkeys[i].InstallPath,
						((_tcslen(oldkeys[i].InstallPath) + 1) * sizeof(TCHAR)));
					RegCloseKey(hKeyDest);
				}
				RegCloseKey(hKeyProfile);
			}
		}
	}
	// Delete old registry keys
	for (i = 0; i < oldconfigcount; i++)
		RegDeleteKey(hKey, oldkeys[i].OldKey);
	// Clean up and write registry version
	if(olddir) free(olddir);
	if(oldvalue) free(oldvalue);
	sizeout = 1;
	RegSetValueEx(hKey, _T("Configuration Version"), 0, REG_DWORD, &sizeout, 4);
	RegCloseKey(hKey);
}