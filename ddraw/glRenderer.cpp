// DXGL
// Copyright (C) 2012 William Feely

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

#include "common.h"
#include "glDirectDraw.h"
#include "glDirectDrawSurface.h"
#include "glDirectDrawPalette.h"
#include "glRenderer.h"
#include "glDirect3DDevice.h"
#include "glDirect3DLight.h"
#include "glutil.h"
#include "ddraw.h"
#include "scalers.h"
#include <string>
using namespace std;
#include "shadergen.h"
#include "matrix.h"

WNDCLASSEXA wndclass;
bool wndclasscreated = false;
GLuint backbuffer = 0;
int backx = 0;
int backy = 0;
BltVertex bltvertices[4];
const GLushort bltindices[4] = {0,1,2,3};


inline int _5to8bit(int number)
{
	return (number << 3)+(number>>2);
}
inline int _6to8bit(int number)
{
	return (number<<2)+(number>>4);
}

int oldswap = 0;
int swapinterval = 0;
inline void SetSwap(int swap)
{
	if(swap != oldswap)
	{
		wglSwapIntervalEXT(swap);
		oldswap = wglGetSwapIntervalEXT();
		oldswap = swap;
	}
}

int glRenderer::_UploadTexture(char *buffer, char *bigbuffer, GLuint texture, int x, int y, int bigx, int bigy, int pitch, int bigpitch, int bpp, int texformat, int texformat2, int texformat3)
{
	if(bpp == 15) bpp = 16;
	glBindTexture(GL_TEXTURE_2D,texture);  // Select surface's texture
	if((x == bigx && y == bigy) || !bigbuffer)
	{
		glTexImage2D(GL_TEXTURE_2D,0,texformat3,x,y,0,texformat,texformat2,buffer);
	}
	else
	{
		switch(bpp)
		{
		case 8:
			ScaleNearest8(bigbuffer,buffer,bigx,bigy,x,y,pitch,bigpitch);
			break;
		case 16:
			ScaleNearest16(bigbuffer,buffer,bigx,bigy,x,y,pitch/2,bigpitch/2);
			break;
		case 24:
			ScaleNearest24(bigbuffer,buffer,bigx,bigy,x,y,pitch,bigpitch);
			break;
		case 32:
			ScaleNearest32(bigbuffer,buffer,bigx,bigy,x,y,pitch/4,bigpitch/4);
			break;
		break;
		}
		glTexImage2D(GL_TEXTURE_2D,0,texformat3,bigx,bigy,0,texformat,texformat2,bigbuffer);
	}
	return 0;
}
int glRenderer::_DownloadTexture(char *buffer, char *bigbuffer, GLuint texture, int x, int y, int bigx, int bigy, int pitch, int bigpitch, int bpp, int texformat, int texformat2)
{
	glBindTexture(GL_TEXTURE_2D,texture);  // Select surface's texture
	if((bigx == x && bigy == y) || !bigbuffer)
	{
		glGetTexImage(GL_TEXTURE_2D,0,texformat,texformat2,buffer); // Shortcut for no scaling
	}
	else
	{
		glGetTexImage(GL_TEXTURE_2D,0,texformat,texformat2,bigbuffer);
		switch(bpp)
		{
		case 8:
			ScaleNearest8(buffer,bigbuffer,x,y,bigx,bigy,bigpitch,pitch);
			break;
		case 15:
		case 16:
			ScaleNearest16(buffer,bigbuffer,x,y,bigx,bigy,bigpitch/2,pitch/2);
			break;
		case 24:
			ScaleNearest24(buffer,bigbuffer,x,y,bigx,bigy,bigpitch,pitch);
			break;
		case 32:
			ScaleNearest32(buffer,bigbuffer,x,y,bigx,bigy,bigpitch/4,pitch/4);
			break;
		break;
		}
	}
	return 0;
}

glRenderer::glRenderer(int width, int height, int bpp, bool fullscreen, HWND hwnd, glDirectDraw7 *glDD7)
{
	MSG Msg;
	wndbusy = false;
	hDC = NULL;
	hRC = NULL;
	PBO = 0;
	hasHWnd = false;
	dib.enabled = false;
	hWnd = hwnd;
	hRenderWnd = NULL;
	InitializeCriticalSection(&cs);
	if(fullscreen)
	{
		SetWindowLongPtrA(hWnd,GWL_EXSTYLE,WS_EX_APPWINDOW);
		SetWindowLongPtrA(hWnd,GWL_STYLE,WS_POPUP);
		ShowWindow(hWnd,SW_MAXIMIZE);
	}
	if(width)
	{
		// TODO:  Adjust window rect
	}
	SetWindowPos(hWnd,HWND_TOP,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
	inputs[0] = (void*)width;
	inputs[1] = (void*)height;
	inputs[2] = (void*)bpp;
	inputs[3] = (void*)fullscreen;
	inputs[4] = (void*)hWnd;
	inputs[5] = glDD7;
	inputs[6] = this;
	wndbusy = true;
	hThread = CreateThread(NULL,0,ThreadEntry,inputs,0,NULL);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
}

glRenderer::~glRenderer()
{
	MSG Msg;
	EnterCriticalSection(&cs);
	wndbusy = true;
	SendMessage(hRenderWnd,GLEVENT_DELETE,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	LeaveCriticalSection(&cs);
	DeleteCriticalSection(&cs);
}


DWORD WINAPI glRenderer::ThreadEntry(void *entry)
{
	void **inputsin = (void**)entry;
	glRenderer *This = (glRenderer*)inputsin[6];
	return This->_Entry();
}

GLuint glRenderer::MakeTexture(GLint min, GLint mag, GLint wraps, GLint wrapt, DWORD width, DWORD height, GLint texformat1, GLint texformat2, GLint texformat3)
{
	EnterCriticalSection(&cs);
	inputs[0] = (void*)min;
	inputs[1] = (void*)mag;
	inputs[2] = (void*)wraps;
	inputs[3] = (void*)wrapt;
	inputs[4] = (void*)width;
	inputs[5] = (void*)height;
	inputs[6] = (void*)texformat1;
	inputs[7] = (void*)texformat2;
	inputs[8] = (void*)texformat3;
	SendMessage(hRenderWnd,GLEVENT_CREATE,0,0);
	LeaveCriticalSection(&cs);
	return (GLuint)outputs[0];
}

int glRenderer::UploadTexture(char *buffer, char *bigbuffer, GLuint texture, int x, int y,
	int bigx, int bigy, int pitch, int bigpitch, int bpp, int texformat, int texformat2, int texformat3)
{
	EnterCriticalSection(&cs);
	MSG Msg;
	inputs[0] = buffer;
	inputs[1] = bigbuffer;
	inputs[2] = (void*)texture;
	inputs[3] = (void*)x;
	inputs[4] = (void*)y;
	inputs[5] = (void*)bigx;
	inputs[6] = (void*)bigy;
	inputs[7] = (void*)pitch;
	inputs[8] = (void*)bigpitch;
	inputs[9] = (void*)bpp;
	inputs[10] = (void*)texformat;
	inputs[11] = (void*)texformat2;
	inputs[12] = (void*)texformat3;
	wndbusy = true;
	SendMessage(hRenderWnd,GLEVENT_UPLOAD,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	LeaveCriticalSection(&cs);
	return (int)outputs[0];
}

int glRenderer::DownloadTexture(char *buffer, char *bigbuffer, GLuint texture, int x, int y,
	int bigx, int bigy, int pitch, int bigpitch, int bpp, int texformat, int texformat2)
{
	EnterCriticalSection(&cs);
	MSG Msg;
	inputs[0] = buffer;
	inputs[1] = bigbuffer;
	inputs[2] = (void*)texture;
	inputs[3] = (void*)x;
	inputs[4] = (void*)y;
	inputs[5] = (void*)bigx;
	inputs[6] = (void*)bigy;
	inputs[7] = (void*)pitch;
	inputs[8] = (void*)bigpitch;
	inputs[9] = (void*)bpp;
	inputs[10] = (void*)texformat;
	inputs[11] = (void*)texformat2;
	wndbusy = true;
	SendMessage(hRenderWnd,GLEVENT_DOWNLOAD,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	LeaveCriticalSection(&cs);
	return (int)outputs[0];
}

void glRenderer::DeleteTexture(GLuint texture)
{
	EnterCriticalSection(&cs);
	inputs[0] = (void*)texture;
	SendMessage(hRenderWnd,GLEVENT_DELETETEX,0,0);
	LeaveCriticalSection(&cs);
}

HRESULT glRenderer::Blt(LPRECT lpDestRect, glDirectDrawSurface7 *src,
		glDirectDrawSurface7 *dest, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
	MSG Msg;
	EnterCriticalSection(&cs);
	inputs[0] = lpDestRect;
	inputs[1] = src;
	inputs[2] = dest;
	inputs[3] = lpSrcRect;
	inputs[4] = (void*)dwFlags;
	inputs[5] = lpDDBltFx;
	wndbusy = true;
	SendMessage(hRenderWnd,GLEVENT_BLT,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	LeaveCriticalSection(&cs);
	return (HRESULT)outputs[0];
}

void glRenderer::DrawScreen(GLuint texture, GLuint paltex, glDirectDrawSurface7 *dest, glDirectDrawSurface7 *src)
{
	MSG Msg;
	EnterCriticalSection(&cs);
	inputs[0] = (void*)texture;
	inputs[1] = (void*)paltex;
	inputs[2] = dest;
	inputs[3] = src;
	wndbusy = true;
	SendMessage(hRenderWnd,GLEVENT_DRAWSCREEN,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	LeaveCriticalSection(&cs);
}

void glRenderer::InitD3D(int zbuffer)
{
	MSG Msg;
	EnterCriticalSection(&cs);
	wndbusy = true;
	inputs[0] = (void*)zbuffer;
	SendMessage(hRenderWnd,GLEVENT_INITD3D,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	LeaveCriticalSection(&cs);
}

HRESULT glRenderer::Clear(glDirectDrawSurface7 *target, DWORD dwCount, LPD3DRECT lpRects, DWORD dwFlags, DWORD dwColor, D3DVALUE dvZ, DWORD dwStencil)
{
	MSG Msg;
	EnterCriticalSection(&cs);
	wndbusy = true;
	inputs[0] = target;
	inputs[1] = (void*)dwCount;
	inputs[2] = lpRects;
	inputs[3] = (void*)dwFlags;
	inputs[4] = (void*)dwColor;
	memcpy(&inputs[5],&dvZ,4);
	inputs[6] = (void*)dwStencil;
	SendMessage(hRenderWnd,GLEVENT_CLEAR,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	LeaveCriticalSection(&cs);
	return (HRESULT)outputs[0];
}

void glRenderer::Flush()
{
	MSG Msg;
	EnterCriticalSection(&cs);
	wndbusy = true;
	SendMessage(hRenderWnd,GLEVENT_FLUSH,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	LeaveCriticalSection(&cs);
}

HRESULT glRenderer::DrawPrimitives(glDirect3DDevice7 *device, GLenum mode, GLVERTEX *vertices, int *texformats, DWORD count, LPWORD indices,
	DWORD indexcount, DWORD flags)
{
	MSG Msg;
	EnterCriticalSection(&cs);
	wndbusy = true;
	inputs[0] = device;
	inputs[1] = (void*)mode;
	inputs[2] = vertices;
	inputs[3] = texformats;
	inputs[4] = (void*)count;
	inputs[5] = indices;
	inputs[6] = (void*)indexcount;
	inputs[7] = (void*)flags;
	SendMessage(hRenderWnd,GLEVENT_DRAWPRIMITIVES,0,0);
	while(wndbusy)
	{
		while(PeekMessage(&Msg,hRenderWnd,0,0,PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		Sleep(0);
	}
	return (HRESULT)outputs[0];
}


DWORD glRenderer::_Entry()
{
	MSG Msg;
	EnterCriticalSection(&cs);
	_InitGL((int)inputs[0],(int)inputs[1],(int)inputs[2],(int)inputs[3],(HWND)inputs[4],(glDirectDraw7*)inputs[5]);
	LeaveCriticalSection(&cs);
	while(GetMessage(&Msg, NULL, 0, 0) > 0)
	{
        TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}
	return 0;
}

BOOL glRenderer::_InitGL(int width, int height, int bpp, int fullscreen, HWND hWnd, glDirectDraw7 *glDD7)
{
	ddInterface = glDD7;
	if(!wndclasscreated)
	{
		wndclass.cbSize = sizeof(WNDCLASSEXA);
		wndclass.style = 0;
		wndclass.lpfnWndProc = RenderWndProc;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;
		wndclass.hInstance = (HINSTANCE)GetModuleHandle(NULL);
		wndclass.hIcon = NULL;
		wndclass.hCursor = NULL;
		wndclass.hbrBackground = NULL;
		wndclass.lpszMenuName = NULL;
		wndclass.lpszClassName = "DXGLRenderWindow";
		wndclass.hIconSm = NULL;
		RegisterClassExA(&wndclass);
		wndclasscreated = true;
	}
	if(hDC) ReleaseDC(hRenderWnd,hDC);
	if(hRenderWnd) DestroyWindow(hRenderWnd);
	RECT rectRender;
	GetClientRect(hWnd,&rectRender);
	if(hWnd)
	{
		hRenderWnd = CreateWindowA("DXGLRenderWindow","Renderer",WS_CHILD|WS_VISIBLE,0,0,rectRender.right - rectRender.left,
			rectRender.bottom - rectRender.top,hWnd,NULL,wndclass.hInstance,this);
		hasHWnd = true;
		SetWindowPos(hRenderWnd,HWND_TOP,0,0,rectRender.right,rectRender.bottom,SWP_SHOWWINDOW);
	}
	else
	{
		width = GetSystemMetrics(SM_CXSCREEN);
		height = GetSystemMetrics(SM_CYSCREEN);
		hRenderWnd = CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST,
			"DXGLRenderWindow","Renderer",WS_POPUP,0,0,width,height,0,0,NULL,this);
		hasHWnd = false;
		SetWindowPos(hRenderWnd,HWND_TOP,0,0,rectRender.right,rectRender.bottom,SWP_SHOWWINDOW|SWP_NOACTIVATE);
	}
	if(hRC)
	{
		wglMakeCurrent(NULL,NULL);
		wglDeleteContext(hRC);
	};
	PIXELFORMATDESCRIPTOR pfd;
	GLuint pf;
	ZeroMemory(&pfd,sizeof(PIXELFORMATDESCRIPTOR));
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	if(hasHWnd) pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	else pfd.dwFlags = pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
	pfd.iPixelType = PFD_TYPE_RGBA;
	if(hasHWnd) pfd.cColorBits = bpp;
	else
	{
		pfd.cColorBits = 24;
		pfd.cAlphaBits = 8;
	}
	pfd.iLayerType = PFD_MAIN_PLANE;
	hDC = GetDC(hRenderWnd);
	if(!hDC)
	{
		DEBUG("glDirectDraw7::InitGL: Can not create hDC\n");
		return FALSE;
	}
	pf = ChoosePixelFormat(hDC,&pfd);
	if(!pf)
	{
		DEBUG("glDirectDraw7::InitGL: Can not get pixelformat\n");
		return FALSE;
	}
	if(!SetPixelFormat(hDC,pf,&pfd))
		DEBUG("glDirectDraw7::InitGL: Can not set pixelformat\n");
	gllock = true;
	hRC = wglCreateContext(hDC);
	if(!hRC)
	{
		DEBUG("glDirectDraw7::InitGL: Can not create GL context\n");
		gllock = false;
		return FALSE;
	}
	if(!wglMakeCurrent(hDC,hRC))
	{
		DEBUG("glDirectDraw7::InitGL: Can not activate GL context\n");
		wglDeleteContext(hRC);
		hRC = NULL;
		ReleaseDC(hRenderWnd,hDC);
		hDC = NULL;
		gllock = false;
		return FALSE;
	}
	wndbusy = false;
	gllock = false;
	InitGLExt();
	SetSwap(1);
	SetSwap(0);
	glViewport(0,0,width,height);
	glDisable(GL_DEPTH_TEST);
	const GLubyte *glver = glGetString(GL_VERSION);
	gl_caps.Version = (GLfloat)atof((char*)glver);
	if(gl_caps.Version >= 2)
	{
		glver = glGetString(GL_SHADING_LANGUAGE_VERSION);
		gl_caps.ShaderVer = (GLfloat)atof((char*)glver);
	}
	else gl_caps.ShaderVer = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE,&gl_caps.TextureMax);
	CompileShaders();
	InitFBO();
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();
	SwapBuffers(hDC);
	SetActiveTexture(0);
	if(!hasHWnd)
	{
		dib.enabled = true;
		dib.width = width;
		dib.height = height;
		dib.pitch = (((width<<3)+31)&~31) >>3;
		dib.pixels = NULL;
		dib.hdc = CreateCompatibleDC(NULL);
		ZeroMemory(&dib.info,sizeof(BITMAPINFO));
		dib.info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		dib.info.bmiHeader.biBitCount = 32;
		dib.info.bmiHeader.biWidth = width;
		dib.info.bmiHeader.biHeight = height;
		dib.info.bmiHeader.biCompression = BI_RGB;
		dib.info.bmiHeader.biPlanes = 1;
		dib.hbitmap = CreateDIBSection(dib.hdc,&dib.info,DIB_RGB_COLORS,(void**)&dib.pixels,NULL,0);
		glGenBuffers(1,&PBO);
		glBindBuffer(GL_PIXEL_PACK_BUFFER,PBO);
		glBufferData(GL_PIXEL_PACK_BUFFER,width*height*4,NULL,GL_STREAM_READ);
		glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
	}
	return TRUE;
}

HRESULT glRenderer::_Blt(LPRECT lpDestRect, glDirectDrawSurface7 *src,
	glDirectDrawSurface7 *dest, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
	LONG sizes[6];
	RECT r,r2;
	if(((dest->ddsd.ddsCaps.dwCaps & (DDSCAPS_FRONTBUFFER)) &&
		(dest->ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)) ||
		((dest->ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) &&
		!(dest->ddsd.ddsCaps.dwCaps & DDSCAPS_FLIP)))
	{
		GetClientRect(hWnd,&r);
		GetClientRect(hRenderWnd,&r2);
		if(memcmp(&r2,&r,sizeof(RECT)))
		SetWindowPos(hRenderWnd,NULL,0,0,r.right,r.bottom,SWP_SHOWWINDOW);
	}
	wndbusy = false;
	ddInterface->GetSizes(sizes);
	int error;
	error = SetFBO(dest->texture,0,false);
	glViewport(0,0,dest->fakex,dest->fakey);
	RECT destrect;
	DDSURFACEDESC2 ddsd;
	ddsd.dwSize = sizeof(DDSURFACEDESC2);
	dest->GetSurfaceDesc(&ddsd);
	if(!lpDestRect)
	{
		destrect.left = 0;
		destrect.top = 0;
		destrect.right = ddsd.dwWidth;
		destrect.bottom = ddsd.dwHeight;
	}
	else destrect = *lpDestRect;
	RECT srcrect;
	DDSURFACEDESC2 ddsdSrc;
	ddsdSrc.dwSize = sizeof(DDSURFACEDESC2);
	if(src) src->GetSurfaceDesc(&ddsdSrc);
	if(!lpSrcRect)
	{
		srcrect.left = 0;
		srcrect.top = 0;
		srcrect.right = ddsdSrc.dwWidth;
		srcrect.bottom = ddsdSrc.dwHeight;
	}
	else srcrect = *lpSrcRect;
	bltvertices[1].x = bltvertices[3].x = (GLfloat)destrect.left * ((GLfloat)dest->fakex/(GLfloat)ddsd.dwWidth);
	bltvertices[0].x = bltvertices[2].x = (GLfloat)destrect.right * ((GLfloat)dest->fakex/(GLfloat)ddsd.dwWidth);
	bltvertices[0].y = bltvertices[1].y = (GLfloat)dest->fakey-((GLfloat)destrect.top * ((GLfloat)dest->fakey/(GLfloat)ddsd.dwHeight));
	bltvertices[2].y = bltvertices[3].y = (GLfloat)dest->fakey-((GLfloat)destrect.bottom * ((GLfloat)dest->fakey/(GLfloat)ddsd.dwHeight));
	bltvertices[1].s = bltvertices[3].s = (GLfloat)srcrect.left / (GLfloat)ddsdSrc.dwWidth;
	bltvertices[0].s = bltvertices[2].s = (GLfloat)srcrect.right / (GLfloat)ddsdSrc.dwWidth;
	bltvertices[0].t = bltvertices[1].t = (GLfloat)srcrect.top / (GLfloat)ddsdSrc.dwHeight;
	bltvertices[2].t = bltvertices[3].t = (GLfloat)srcrect.bottom / (GLfloat)ddsdSrc.dwHeight;
	if(dest->zbuffer) glClear(GL_DEPTH_BUFFER_BIT);
	if(dwFlags & DDBLT_COLORFILL)
	{
		SetShader(PROG_FILL,NULL,NULL,true);
		switch(ddInterface->GetBPP())
		{
		case 8:
			bltvertices[0].r = bltvertices[0].g = bltvertices[0].b =
				bltvertices[1].r = bltvertices[1].g = bltvertices[1].b =
				bltvertices[2].r = bltvertices[2].g = bltvertices[2].b =
				bltvertices[3].r = bltvertices[3].g = bltvertices[3].b = (GLubyte)lpDDBltFx->dwFillColor;
			break;
		case 15:
			bltvertices[0].r = bltvertices[1].r = bltvertices[2].r = bltvertices[3].r =
				_5to8bit((lpDDBltFx->dwFillColor>>10) & 31);
			bltvertices[0].g = bltvertices[1].g = bltvertices[2].g = bltvertices[3].g =
				_5to8bit((lpDDBltFx->dwFillColor>>5) & 31);
			bltvertices[0].b = bltvertices[1].b = bltvertices[2].b = bltvertices[3].b =
				_5to8bit(lpDDBltFx->dwFillColor & 31);
			break;
		case 16:
			bltvertices[0].r = bltvertices[1].r = bltvertices[2].r = bltvertices[3].r =
				_5to8bit((lpDDBltFx->dwFillColor>>11) & 31);
			bltvertices[0].g = bltvertices[1].g = bltvertices[2].g = bltvertices[3].g =
				_6to8bit((lpDDBltFx->dwFillColor>>5) & 63);
			bltvertices[0].b = bltvertices[1].b = bltvertices[2].b = bltvertices[3].b =
				_5to8bit(lpDDBltFx->dwFillColor & 31);
			break;
		case 24:
		case 32:
			bltvertices[0].r = bltvertices[1].r = bltvertices[2].r = bltvertices[3].r =
				((lpDDBltFx->dwFillColor>>16) & 255);
			bltvertices[0].g = bltvertices[1].g = bltvertices[2].g = bltvertices[3].g =
				((lpDDBltFx->dwFillColor>>8) & 255);
			bltvertices[0].b = bltvertices[1].b = bltvertices[2].b = bltvertices[3].b =
				(lpDDBltFx->dwFillColor & 255);
		default:
			break;
		}
	}
	if((dwFlags & DDBLT_KEYSRC) && (src && src->colorkey[0].enabled) && !(dwFlags & DDBLT_COLORFILL))
	{
		SetShader(PROG_CKEY,NULL,NULL,true);
		GLint keyloc = glGetUniformLocation(shaders[PROG_CKEY].prog,"keyIn");
		switch(ddInterface->GetBPP())
		{
		case 8:
			glUniform3i(keyloc,src->colorkey[0].key.dwColorSpaceHighValue,src->colorkey[0].key.dwColorSpaceHighValue,
				src->colorkey[0].key.dwColorSpaceHighValue);
			break;
		case 15:
			glUniform3i(keyloc,_5to8bit(src->colorkey[0].key.dwColorSpaceHighValue>>10 & 31),
				_5to8bit(src->colorkey[0].key.dwColorSpaceHighValue>>5 & 31),
				_5to8bit(src->colorkey[0].key.dwColorSpaceHighValue & 31));
			break;
		case 16:
			glUniform3i(keyloc,_5to8bit(src->colorkey[0].key.dwColorSpaceHighValue>>11 & 31),
				_6to8bit(src->colorkey[0].key.dwColorSpaceHighValue>>5 & 63),
				_5to8bit(src->colorkey[0].key.dwColorSpaceHighValue & 31));
			break;
		case 24:
		case 32:
		default:
			glUniform3i(keyloc,(src->colorkey[0].key.dwColorSpaceHighValue>>16 & 255),
				(src->colorkey[0].key.dwColorSpaceHighValue>>8 & 255),
				(src->colorkey[0].key.dwColorSpaceHighValue & 255));
			break;
		}
		GLint texloc = glGetUniformLocation(shaders[PROG_CKEY].prog,"myTexture");
		glUniform1i(texloc,0);
	}
	else if(!(dwFlags & DDBLT_COLORFILL))
	{
		SetShader(PROG_TEXTURE,NULL,NULL,true);
		GLint texloc = glGetUniformLocation(shaders[PROG_TEXTURE].prog,"Texture");
		glUniform1i(texloc,0);
	}
	SetActiveTexture(0);
	if(src) glBindTexture(GL_TEXTURE_2D,src->GetTexture());
	GLuint prog = GetProgram()&0xffffffff;
	GLint viewloc = glGetUniformLocation(prog,"view");
	glUniform4f(viewloc,0,(GLfloat)dest->fakex,0,(GLfloat)dest->fakey);
	dest->dirty |= 2;
	GLint xyloc = glGetAttribLocation(prog,"xy");
	glEnableVertexAttribArray(xyloc);
	glVertexAttribPointer(xyloc,2,GL_FLOAT,false,sizeof(BltVertex),&bltvertices[0].x);
	GLint rgbloc = glGetAttribLocation(prog,"rgb");
	if(rgbloc != -1)
	{
		glEnableVertexAttribArray(rgbloc);
		glVertexAttribPointer(rgbloc,3,GL_UNSIGNED_BYTE,true,sizeof(BltVertex),&bltvertices[0].r);
	}
	if(!(dwFlags & DDBLT_COLORFILL))
	{
		GLint stloc = glGetAttribLocation(prog,"st");
		glEnableVertexAttribArray(stloc);
		glVertexAttribPointer(stloc,2,GL_FLOAT,false,sizeof(BltVertex),&bltvertices[0].s);
	}
	glDrawRangeElements(GL_TRIANGLE_STRIP,0,3,4,GL_UNSIGNED_SHORT,bltindices);
	SetFBO(0,0,false);
	if(((ddsd.ddsCaps.dwCaps & (DDSCAPS_FRONTBUFFER)) &&
		(ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)) ||
		((ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) &&
		!(ddsd.ddsCaps.dwCaps & DDSCAPS_FLIP)))_DrawScreen(dest->texture,dest->paltex,dest,dest);
	return DD_OK;
}

GLuint glRenderer::_MakeTexture(GLint min, GLint mag, GLint wraps, GLint wrapt, DWORD width, DWORD height, GLint texformat1, GLint texformat2, GLint texformat3)
{
	GLuint texture;
	glGenTextures(1,&texture);
	glBindTexture(GL_TEXTURE_2D,texture);
	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,(GLfloat)min);
	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,(GLfloat)mag);
	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,(GLfloat)wraps);
	glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,(GLfloat)wrapt);
	glTexImage2D(GL_TEXTURE_2D,0,texformat3,width,height,0,texformat1,texformat2,NULL);
	return texture;
}

void glRenderer::_DrawBackbuffer(GLuint *texture, int x, int y)
{
	GLfloat view[4];
	SetActiveTexture(0);
	if(!backbuffer)
	{
		backbuffer = _MakeTexture(GL_LINEAR,GL_LINEAR,GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE,x,y,GL_BGRA,GL_UNSIGNED_BYTE,GL_RGBA8);
		backx = x;
		backy = y;
	}
	if((backx != x) || (backy != y))
	{
		glBindTexture(GL_TEXTURE_2D,backbuffer);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,x,y,0,GL_BGRA,GL_UNSIGNED_BYTE,NULL);
		backx = x;
		backy = y;
	}
	SetFBO(backbuffer,0,false);
	view[0] = view[2] = 0;
	view[1] = (GLfloat)x;
	view[3] = (GLfloat)y;
	glViewport(0,0,x,y);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glBindTexture(GL_TEXTURE_2D,*texture);
	*texture = backbuffer;
	GLuint prog = GetProgram();
	GLint viewloc = glGetUniformLocation(prog,"view");
	glUniform4f(viewloc,view[0],view[1],view[2],view[3]);

	bltvertices[0].s = bltvertices[0].t = bltvertices[1].t = bltvertices[2].s = 1.;
	bltvertices[1].s = bltvertices[2].t = bltvertices[3].s = bltvertices[3].t = 0.;
	bltvertices[0].y = bltvertices[1].y = bltvertices[1].x = bltvertices[3].x = 0.;
	bltvertices[0].x = bltvertices[2].x = (float)x;
	bltvertices[2].y = bltvertices[3].y = (float)y;
	GLint xyloc = glGetAttribLocation(prog,"xy");
	glEnableVertexAttribArray(xyloc);
	glVertexAttribPointer(xyloc,2,GL_FLOAT,false,sizeof(BltVertex),&bltvertices[0].x);
	GLint stloc = glGetAttribLocation(prog,"st");
	glEnableVertexAttribArray(stloc);
	glVertexAttribPointer(stloc,2,GL_FLOAT,false,sizeof(BltVertex),&bltvertices[0].s);
	glDrawRangeElements(GL_TRIANGLE_STRIP,0,3,4,GL_UNSIGNED_SHORT,bltindices);
	SetFBO(0,0,false);
}

void glRenderer::_DrawScreen(GLuint texture, GLuint paltex, glDirectDrawSurface7 *dest, glDirectDrawSurface7 *src)
{
	RECT r,r2;
	if((dest->ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE))
	{
		GetClientRect(hWnd,&r);
		GetClientRect(hRenderWnd,&r2);
		if(memcmp(&r2,&r,sizeof(RECT)))
		SetWindowPos(hRenderWnd,NULL,0,0,r.right,r.bottom,SWP_SHOWWINDOW);
	}
	wndbusy = false;
	RECT *viewrect = &r2;
	SetSwap(swapinterval);
	LONG sizes[6];
	GLfloat view[4];
	GLint viewport[4];
	if(src->dirty & 1)
	{
		_UploadTexture(src->buffer,src->bigbuffer,texture,src->ddsd.dwWidth,src->ddsd.dwHeight,
			src->fakex,src->fakey,src->ddsd.lPitch,
			(NextMultipleOf4((ddInterface->GetBPPMultipleOf8()/8)*src->fakex)),
			src->ddsd.ddpfPixelFormat.dwRGBBitCount,src->texformat,src->texformat2,src->texformat3);
		src->dirty &= ~1;
	}
	if(dest->ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
	{
		if(ddInterface->GetFullscreen())
		{
			ddInterface->GetSizes(sizes);
			viewport[0] = viewport[1] = 0;
			viewport[2] = sizes[4];
			viewport[3] = sizes[5];
			view[0] = (GLfloat)-(sizes[4]-sizes[0])/2;
			view[1] = (GLfloat)(sizes[4]-sizes[0])/2+sizes[0];
			view[2] = (GLfloat)(sizes[5]-sizes[1])/2+sizes[1];
			view[3] = (GLfloat)-(sizes[5]-sizes[1])/2;
		}
		else
		{
			viewport[0] = viewport[1] = 0;
			viewport[2] = viewrect->right;
			viewport[3] = viewrect->bottom;
			ClientToScreen(hRenderWnd,(LPPOINT)&viewrect->left);
			ClientToScreen(hRenderWnd,(LPPOINT)&viewrect->right);
			view[0] = (GLfloat)viewrect->left;
			view[1] = (GLfloat)viewrect->right;
			view[2] = (GLfloat)dest->fakey-(GLfloat)viewrect->top;
			view[3] = (GLfloat)dest->fakey-(GLfloat)viewrect->bottom;
		}
	}
	else
	{
		view[0] = 0;
		view[1] = (GLfloat)dest->fakex;
		view[2] = 0;
		view[3] = (GLfloat)dest->fakey;
	}
	SetFBO(0,0,false);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	if(ddInterface->GetBPP() == 8)
	{
		SetShader(PROG_PAL256,NULL,NULL,true);
		glBindTexture(GL_TEXTURE_2D,paltex);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,256,1,0,GL_RGBA,GL_UNSIGNED_BYTE,dest->palette->GetPalette(NULL));
		GLint palloc = glGetUniformLocation(shaders[PROG_PAL256].prog,"ColorTable");
		GLint texloc = glGetUniformLocation(shaders[PROG_PAL256].prog,"IndexTexture");
		glUniform1i(texloc,0);
		glUniform1i(palloc,1);
		SetActiveTexture(0);
		glBindTexture(GL_TEXTURE_2D,texture);
		SetActiveTexture(1);
		glBindTexture(GL_TEXTURE_2D,paltex);
		SetActiveTexture(0);
		if(dxglcfg.scalingfilter)
		{
			_DrawBackbuffer(&texture,dest->fakex,dest->fakey);
			SetShader(PROG_TEXTURE,NULL,NULL,true);
			glBindTexture(GL_TEXTURE_2D,texture);
			GLuint prog = GetProgram() & 0xFFFFFFFF;
			GLint texloc = glGetUniformLocation(prog,"Texture");
			glUniform1i(texloc,0);
		}
	}
	else
	{
		SetShader(PROG_TEXTURE,NULL,NULL,true);
		glBindTexture(GL_TEXTURE_2D,texture);
		GLuint prog = GetProgram() & 0xFFFFFFFF;
		GLint texloc = glGetUniformLocation(prog,"Texture");
		glUniform1i(texloc,0);
	}
	glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
	GLuint prog = GetProgram();
	GLint viewloc = glGetUniformLocation(prog,"view");
	glUniform4f(viewloc,view[0],view[1],view[2],view[3]);
	if(ddInterface->GetFullscreen())
	{
		bltvertices[0].x = bltvertices[2].x = (float)sizes[0];
		bltvertices[0].y = bltvertices[1].y = bltvertices[1].x = bltvertices[3].x = 0.;
		bltvertices[2].y = bltvertices[3].y = (float)sizes[1];
	}
	else
	{
		bltvertices[0].x = bltvertices[2].x = (float)dest->fakex;
		bltvertices[0].y = bltvertices[1].y = bltvertices[1].x = bltvertices[3].x = 0.;
		bltvertices[2].y = bltvertices[3].y = (float)dest->fakey;
	}
	bltvertices[0].s = bltvertices[0].t = bltvertices[1].t = bltvertices[2].s = 1.;
	bltvertices[1].s = bltvertices[2].t = bltvertices[3].s = bltvertices[3].t = 0.;
	GLint xyloc = glGetAttribLocation(prog,"xy");
	glEnableVertexAttribArray(xyloc);
	glVertexAttribPointer(xyloc,2,GL_FLOAT,false,sizeof(BltVertex),&bltvertices[0].x);
	GLint stloc = glGetAttribLocation(prog,"st");
	glEnableVertexAttribArray(stloc);
	glVertexAttribPointer(stloc,2,GL_FLOAT,false,sizeof(BltVertex),&bltvertices[0].s);
	GLint rgbloc = glGetAttribLocation(prog,"rgb");
	if(rgbloc != -1)
	{
		glEnableVertexAttribArray(rgbloc);
		glVertexAttribPointer(rgbloc,3,GL_UNSIGNED_BYTE,true,sizeof(BltVertex),&bltvertices[0].r);
	}
	glDrawRangeElements(GL_TRIANGLE_STRIP,0,3,4,GL_UNSIGNED_SHORT,bltindices);
	glFlush();
	if(hasHWnd) SwapBuffers(hDC);
	else
	{
		glReadBuffer(GL_FRONT);
		glBindBuffer(GL_PIXEL_PACK_BUFFER,PBO);
		GLint packalign;
		glGetIntegerv(GL_PACK_ALIGNMENT,&packalign);
		glPixelStorei(GL_PACK_ALIGNMENT,1);
		glReadPixels(0,0,sizes[4],sizes[5],GL_BGRA,GL_UNSIGNED_BYTE,0);
		GLubyte *pixels = (GLubyte*)glMapBuffer(GL_PIXEL_PACK_BUFFER,GL_READ_ONLY);
		for(int i = 0; i < sizes[5];i++)
		{
			memcpy(&dib.pixels[dib.pitch*i],
				&pixels[((sizes[5]-1)-i)*(sizes[4]*4)],sizes[4]*4);
		}
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
		glPixelStorei(GL_PACK_ALIGNMENT,packalign);
		HDC hRenderDC = (HDC)::GetDC(hRenderWnd);
		HGDIOBJ hPrevObj = 0;
		POINT dest = {0,0};
		POINT src = {0,0};
		SIZE wnd = {dib.width,dib.height};
		BLENDFUNCTION func = {AC_SRC_OVER,0,255,AC_SRC_ALPHA};
		hPrevObj = SelectObject(dib.hdc,dib.hbitmap);
		ClientToScreen(hRenderWnd,&dest);
		UpdateLayeredWindow(hRenderWnd,hRenderDC,&dest,&wnd,
			dib.hdc,&src,0,&func,ULW_ALPHA);
		SelectObject(dib.hdc,hPrevObj);
		::ReleaseDC(hRenderWnd,hRenderDC);
	}

}

void glRenderer::_DeleteTexture(GLuint texture)
{
	glDeleteTextures(1,&texture);
}

void glRenderer::_InitD3D(int zbuffer)
{
	wndbusy = false;
	glHint(GL_PERSPECTIVE_CORRECTION_HINT,GL_NICEST);
	GLfloat ambient[] = {0.0,0.0,0.0,0.0};
	if(zbuffer) glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_DITHER);
}

void glRenderer::_Clear(glDirectDrawSurface7 *target, DWORD dwCount, LPD3DRECT lpRects, DWORD dwFlags, DWORD dwColor, D3DVALUE dvZ, DWORD dwStencil)
{
	if(dwCount)
	{
		outputs[0] = (void*)DDERR_INVALIDPARAMS;
		FIXME("glDirect3DDevice7::Clear:  Cannot clear rects yet.");
		wndbusy = false;
		return;
	}
	outputs[0] = (void*)D3D_OK;
	wndbusy = false;
	GLfloat color[4];
	dwordto4float(dwColor,color);
	if(target->zbuffer) SetFBO(target->texture,target->GetZBuffer()->texture,target->GetZBuffer()->hasstencil);
	else SetFBO(target->texture,0,false);
	int clearbits = 0;
	if(D3DCLEAR_TARGET)
	{
		clearbits |= GL_COLOR_BUFFER_BIT;
		glClearColor(color[0],color[1],color[2],color[3]);
	}
	if(D3DCLEAR_ZBUFFER)
	{
		clearbits |= GL_DEPTH_BUFFER_BIT;
		glClearDepth(dvZ);
	}
	if(D3DCLEAR_STENCIL)
	{
		clearbits |= GL_STENCIL_BUFFER_BIT;
		glClearStencil(dwStencil);
	}
	glClear(clearbits);
	if(target->zbuffer) target->zbuffer->dirty |= 2;
	target->dirty |= 2;
}

void glRenderer::_Flush()
{
	wndbusy = false;
	glFlush();
}

void glRenderer::_DrawPrimitives(glDirect3DDevice7 *device, GLenum mode, GLVERTEX *vertices, int *texformats, DWORD count, LPWORD indices,
	DWORD indexcount, DWORD flags)
{
	bool transformed;
	char svar[] = "sX";
	char stvar[] = "stX";
	char strvar[] = "strX";
	char strqvar[] = "strqX";
	int i;
	if(vertices[1].data) transformed = true;
	else transformed = false;
	if(!vertices[0].data)
	{
		outputs[0] = (void*)DDERR_INVALIDPARAMS;
		wndbusy = false;
		return;
	}
	__int64 shader = device->SelectShader(vertices);
	SetShader(shader,device->texstages,texformats,0);
	_GENSHADER prog = genshaders[current_genshader].shader;
	glEnableVertexAttribArray(prog.attribs[0]);
	glVertexAttribPointer(prog.attribs[0],3,GL_FLOAT,false,vertices[0].stride,vertices[0].data);
	if(transformed)
	{
		if(prog.attribs[1] != -1)
		{
			glEnableVertexAttribArray(prog.attribs[1]);
			glVertexAttribPointer(prog.attribs[1],4,GL_FLOAT,false,vertices[1].stride,vertices[1].data);
		}
	}
	for(i = 0; i < 5; i++)
	{
		if(vertices[i+2].data)
		{
			if(prog.attribs[i+2] != -1)
			{
				glEnableVertexAttribArray(prog.attribs[i+2]);
				glVertexAttribPointer(prog.attribs[i+2],1,GL_FLOAT,false,vertices[i+2].stride,vertices[i+2].data);
			}
		}
	}
	if(vertices[7].data)
	{
		if(prog.attribs[7] != -1)
		{
			glEnableVertexAttribArray(prog.attribs[7]);
			glVertexAttribPointer(prog.attribs[7],3,GL_FLOAT,false,vertices[7].stride,vertices[7].data);
		}
	}
	for(i = 0; i < 2; i++)
	{
		if(vertices[i+8].data)
		{
			if(prog.attribs[8+i] != -1)
			{
				glEnableVertexAttribArray(prog.attribs[8+i]);
				glVertexAttribPointer(prog.attribs[8+i],4,GL_UNSIGNED_BYTE,true,vertices[i+8].stride,vertices[i+8].data);
			}
		}
	}
	for(i = 0; i < 8; i++)
	{
		{
			switch(texformats[i])
			{
			case -1: // Null
				break;
			case 0: // st
				if(prog.attribs[i+18] != -1)
				{
					glEnableVertexAttribArray(prog.attribs[i+18]);
					glVertexAttribPointer(prog.attribs[i+18],2,GL_FLOAT,false,vertices[i+10].stride,vertices[i+10].data);
				}
				break;
			case 1: // str
				if(prog.attribs[i+26] != -1)
				{
					glEnableVertexAttribArray(prog.attribs[i+26]);
					glVertexAttribPointer(prog.attribs[i+26],3,GL_FLOAT,false,vertices[i+10].stride,vertices[i+10].data);
				}
				break;
			case 2: // strq
				if(prog.attribs[i+34] != -1)
				{
					glEnableVertexAttribArray(prog.attribs[i+34]);
					glVertexAttribPointer(prog.attribs[i+34],4,GL_FLOAT,false,vertices[i+10].stride,vertices[i+10].data);
				}
				break;
			case 3: // s
				if(prog.attribs[i+10] != -1)
				{
					glEnableVertexAttribArray(prog.attribs[i+10]);
					glVertexAttribPointer(prog.attribs[i+10],1,GL_FLOAT,false,vertices[i+10].stride,vertices[i+10].data);
				}
				break;
			}

		}
	}
	if(device->normal_dirty) device->UpdateNormalMatrix();
	if(prog.uniforms[0] != -1) glUniformMatrix4fv(prog.uniforms[0],1,false,device->matWorld);
	if(prog.uniforms[1] != -1) glUniformMatrix4fv(prog.uniforms[1],1,false,device->matView);
	if(prog.uniforms[2] != -1) glUniformMatrix4fv(prog.uniforms[2],1,false,device->matProjection);
	if(prog.uniforms[3] != -1) glUniformMatrix3fv(prog.uniforms[3],1,true,device->matNormal);

	if(prog.uniforms[15] != -1) glUniform4fv(prog.uniforms[15],1,(GLfloat*)&device->material.ambient);
	if(prog.uniforms[16] != -1) glUniform4fv(prog.uniforms[16],1,(GLfloat*)&device->material.diffuse);
	if(prog.uniforms[17] != -1) glUniform4fv(prog.uniforms[17],1,(GLfloat*)&device->material.specular);
	if(prog.uniforms[18] != -1) glUniform4fv(prog.uniforms[18],1,(GLfloat*)&device->material.emissive);
	if(prog.uniforms[19] != -1) glUniform1f(prog.uniforms[19],device->material.power);
	int lightindex = 0;
	char lightname[] = "lightX.xxxxxxxxxxxxxxxx";
	for(i = 0; i < 8; i++)
	{
		if(device->gllights[i] != -1)
		{
			if(prog.uniforms[20+(i*12)] != -1)
				glUniform4fv(prog.uniforms[20+(i*12)],1,(GLfloat*)&device->lights[device->gllights[i]]->light.dcvDiffuse);
			if(prog.uniforms[21+(i*12)] != -1)
				glUniform4fv(prog.uniforms[21+(i*12)],1,(GLfloat*)&device->lights[device->gllights[i]]->light.dcvSpecular);
			if(prog.uniforms[22+(i*12)] != -1)
				glUniform4fv(prog.uniforms[22+(i*12)],1,(GLfloat*)&device->lights[device->gllights[i]]->light.dcvAmbient);
			if(prog.uniforms[24+(i*12)] != -1)
				glUniform3fv(prog.uniforms[24+(i*12)],1,(GLfloat*)&device->lights[device->gllights[i]]->light.dvDirection);
			if(device->lights[device->gllights[i]]->light.dltType != D3DLIGHT_DIRECTIONAL)
			{
				if(prog.uniforms[23+(i*12)] != -1)
					glUniform3fv(prog.uniforms[23+(i*12)],1,(GLfloat*)&device->lights[device->gllights[i]]->light.dvPosition);
				if(prog.uniforms[25+(i*12)] != -1)
					glUniform1f(prog.uniforms[25+(i*12)],device->lights[device->gllights[i]]->light.dvRange);
				if(prog.uniforms[26+(i*12)] != -1)
					glUniform1f(prog.uniforms[26+(i*12)],device->lights[device->gllights[i]]->light.dvFalloff);
				if(prog.uniforms[27+(i*12)] != -1)
					glUniform1f(prog.uniforms[27+(i*12)],device->lights[device->gllights[i]]->light.dvAttenuation0);
				if(prog.uniforms[28+(i*12)] != -1)
					glUniform1f(prog.uniforms[28+(i*12)],device->lights[device->gllights[i]]->light.dvAttenuation1);
				if(prog.uniforms[29+(i*12)] != -1)
					glUniform1f(prog.uniforms[29+(i*12)],device->lights[device->gllights[i]]->light.dvAttenuation2);
				if(prog.uniforms[30+(i*12)] != -1)
					glUniform1f(prog.uniforms[30+(i*12)],device->lights[device->gllights[i]]->light.dvTheta);
				if(prog.uniforms[31+(i*12)] != -1)
					glUniform1f(prog.uniforms[31+(i*12)],device->lights[device->gllights[i]]->light.dvPhi);
			}
		}
		lightindex++;
	}

	DWORD ambient = device->renderstate[D3DRENDERSTATE_AMBIENT];
	if(prog.uniforms[136] != -1)
		glUniform4f(prog.uniforms[136],RGBA_GETRED(ambient),RGBA_GETGREEN(ambient),
			RGBA_GETBLUE(ambient),RGBA_GETALPHA(ambient));

	for(i = 0; i < 8; i++)
	{
		if(device->texstages[i].colorop == D3DTOP_DISABLE) break;
		if(device->texstages[i].texture)
		{
			if(device->texstages[i].texture->dirty & 1)
			{
				_UploadTexture(device->texstages[i].texture->buffer,device->texstages[i].texture->bigbuffer,
					device->texstages[i].texture->texture,device->texstages[i].texture->ddsd.dwWidth,
					device->texstages[i].texture->ddsd.dwHeight,device->texstages[i].texture->fakex,
					device->texstages[i].texture->fakey,device->texstages[i].texture->ddsd.lPitch,
					(device->texstages[i].texture->ddsd.ddpfPixelFormat.dwRGBBitCount/8*device->texstages[i].texture->fakex),
					device->texstages[i].texture->ddsd.ddpfPixelFormat.dwRGBBitCount,device->texstages[i].texture->texformat,
					device->texstages[i].texture->texformat2,device->texstages[i].texture->texformat3);
				device->texstages[i].texture->dirty &= ~1;
			}
			if(device->texstages[i].texture)
				device->texstages[i].texture->SetFilter(i,device->texstages[i].glmagfilter,device->texstages[i].glminfilter);
			SetTexture(i,device->texstages[i].texture->texture);
		}
		else SetTexture(i,0);
		glUniform1i(prog.uniforms[128+i],i);
	}

	if(device->glDDS7->zbuffer) SetFBO(device->glDDS7->texture,device->glDDS7->zbuffer->texture,device->glDDS7->zbuffer->hasstencil);
	else SetFBO(device->glDDS7->texture,0,false);
	glViewport(device->viewport.dwX,device->viewport.dwY,device->viewport.dwWidth,device->viewport.dwHeight);
	if(indices) glDrawElements(mode,indexcount,GL_UNSIGNED_SHORT,indices);
	else glDrawArrays(mode,0,count);
	if(device->glDDS7->zbuffer) device->glDDS7->zbuffer->dirty |= 2;
	device->glDDS7->dirty |= 2;
	if(flags & D3DDP_WAIT) glFlush();
	outputs[0] = (void*)D3D_OK;
	wndbusy = false;
	return;
}

LRESULT glRenderer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int oldx,oldy;
	float mulx, muly;
	float tmpfloats[16];
	int translatex, translatey;
	LPARAM newpos;
	HWND hParent;
	LONG sizes[6];
	HCURSOR cursor;
	switch(msg)
	{
	case WM_CREATE:
		SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)this);
		return 0;
	case WM_DESTROY:
		if(hRC)
		{
			if(dib.enabled)
			{
				if(dib.hbitmap)	DeleteObject(dib.hbitmap);
				if(dib.hdc)	DeleteDC(dib.hdc);
				ZeroMemory(&dib,sizeof(DIB));
			}
			DeleteShaders();
			DeleteFBO();
			if(PBO)
			{
				glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
				glDeleteBuffers(1,&PBO);
				PBO = 0;
			}
			if(backbuffer)
			{
				glDeleteTextures(1,&backbuffer);
				backbuffer = 0;
				backx = 0;
				backy = 0;
			}
			wglMakeCurrent(NULL,NULL);
			wglDeleteContext(hRC);
		};
		if(hDC) ReleaseDC(hRenderWnd,hDC);
		hDC = NULL;
		wndbusy = false;
		PostQuitMessage(0);
		return 0;
	case WM_SETCURSOR:
		hParent = GetParent(hwnd);
		cursor = (HCURSOR)GetClassLong(hParent,GCL_HCURSOR);
		SetCursor(cursor);
		return SendMessage(hParent,msg,wParam,lParam);
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_MOUSEWHEEL:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_XBUTTONDBLCLK:
	case WM_MOUSEHWHEEL:
		hParent = GetParent(hwnd);
		if((dxglcfg.scaler != 0) && ddInterface->GetFullscreen())
		{
			oldx = LOWORD(lParam);
			oldy = HIWORD(lParam);
			ddInterface->GetSizes(sizes);
			mulx = (float)sizes[2] / (float)sizes[0];
			muly = (float)sizes[3] / (float)sizes[1];
			translatex = (sizes[4]-sizes[0])/2;
			translatey = (sizes[5]-sizes[1])/2;
			oldx -= translatex;
			oldy -= translatey;
			oldx = (int)((float)oldx * mulx);
			oldy = (int)((float)oldy * muly);
			if(oldx < 0) oldx = 0;
			if(oldy < 0) oldy = 0;
			if(oldx >= sizes[2]) oldx = sizes[2]-1;
			if(oldy >= sizes[3]) oldy = sizes[3]-1;
			newpos = oldx + (oldy << 16);
			return SendMessage(hParent,msg,wParam,newpos);
		}
		else return SendMessage(hParent,msg,wParam,lParam);
	case GLEVENT_DELETE:
		DestroyWindow(hRenderWnd);
		return 0;
	case GLEVENT_CREATE:
		outputs[0] = (void*)_MakeTexture((GLint)inputs[0],(GLint)inputs[1],(GLint)inputs[2],(GLint)inputs[3],
			(DWORD)inputs[4],(DWORD)inputs[5],(GLint)inputs[6],(GLint)inputs[7],(GLint)inputs[8]);
		return 0;
	case GLEVENT_UPLOAD:
		outputs[0] = (void*)_UploadTexture((char*)inputs[0],(char*)inputs[1],(GLuint)inputs[2],(int)inputs[3],
			(int)inputs[4],(int)inputs[5],(int)inputs[6],(int)inputs[7],(int)inputs[8],(int)inputs[9],
			(int)inputs[10],(int)inputs[11],(int)inputs[12]);
		wndbusy = false;
		return 0;
	case GLEVENT_DOWNLOAD:
		outputs[0] = (void*)_DownloadTexture((char*)inputs[0],(char*)inputs[1],(GLuint)inputs[2],(int)inputs[3],
			(int)inputs[4],(int)inputs[5],(int)inputs[6],(int)inputs[7],(int)inputs[8],(int)inputs[9],
			(int)inputs[10],(int)inputs[11]);
		wndbusy = false;
		return 0;
	case GLEVENT_DELETETEX:
		_DeleteTexture((GLuint)inputs[0]);
		return 0;
	case GLEVENT_BLT:
		outputs[0] = (void*)_Blt((LPRECT)inputs[0],(glDirectDrawSurface7*)inputs[1],(glDirectDrawSurface7*)inputs[2],
			(LPRECT)inputs[3],(DWORD)inputs[4],(LPDDBLTFX)inputs[5]);
		return 0;
	case GLEVENT_DRAWSCREEN:
		_DrawScreen((GLuint)inputs[0],(GLuint)inputs[1],(glDirectDrawSurface7*)inputs[2],(glDirectDrawSurface7*)inputs[3]);
		return 0;
	case GLEVENT_INITD3D:
		_InitD3D((int)inputs[0]);
		return 0;
	case GLEVENT_CLEAR:
		memcpy(&tmpfloats[0],&inputs[5],4);
		_Clear((glDirectDrawSurface7*)inputs[0],(DWORD)inputs[1],(LPD3DRECT)inputs[2],(DWORD)inputs[3],(DWORD)inputs[4],
			tmpfloats[0],(DWORD)inputs[6]);
		return 0;
	case GLEVENT_FLUSH:
		_Flush();
		return 0;
	case GLEVENT_DRAWPRIMITIVES:
		_DrawPrimitives((glDirect3DDevice7*)inputs[0],(GLenum)inputs[1],(GLVERTEX*)inputs[2],(int*)inputs[3],(DWORD)inputs[4],
			(LPWORD)inputs[5],(DWORD)inputs[6],(DWORD)inputs[7]);
		return 0;
	default:
		return DefWindowProc(hwnd,msg,wParam,lParam);
	}
	return 0;
}


// Render Window event handler
LRESULT CALLBACK RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	glRenderer* instance = reinterpret_cast<glRenderer*>(GetWindowLongPtr(hwnd,GWLP_USERDATA));
	if(!instance)
	{
		if(msg == WM_CREATE)
			instance = reinterpret_cast<glRenderer*>(*(LONG_PTR*)lParam);
		else return DefWindowProc(hwnd,msg,wParam,lParam);
	}
	return instance->WndProc(hwnd,msg,wParam,lParam);
}