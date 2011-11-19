// DXGL
// Copyright (C) 2011 William Feely

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
#include "shaders.h"
#include "ddraw.h"
#include "glDirectDraw.h"
#include "glDirectDrawSurface.h"
#include "glDirectDrawPalette.h"
#include "glDirectDrawClipper.h"

inline int NextMultipleOf4(int number){return ((number+3) & (~3));}
inline int NextMultipleOf2(int number){return ((number+1) & (~1));}

// DDRAW7 routines
glDirectDrawSurface7::glDirectDrawSurface7(LPDIRECTDRAW7 lpDD7, LPDDSURFACEDESC2 lpDDSurfaceDesc2, LPDIRECTDRAWSURFACE7 *lplpDDSurface7, HRESULT *error, bool copysurface, glDirectDrawPalette *palettein)
{
	locked = 0;
	bitmapinfo = (BITMAPINFO *)malloc(sizeof(BITMAPINFO)+(255*sizeof(RGBQUAD)));
	palette = NULL;
	clipper = NULL;
	hdc = NULL;
	dds1 = NULL;
	dds2 = NULL;
	dds3 = NULL;
	dds4 = NULL;
	if(copysurface)
	{
		FIXME("glDirectDrawSurface7::glDirectDrawSurface7: copy surface stub\n");
		*error = DDERR_GENERIC;
		return;
	}
	else
	{
		ddInterface = (glDirectDraw7 *)lpDD7;
		ddsd = *lpDDSurfaceDesc2;
	}
	LONG sizes[6];
	ddInterface->GetSizes(sizes);
	if(!(ddsd.dwFlags & DDSD_CAPS))
	{
		*error = DDERR_INVALIDPARAMS;
		return;
	}
	if(ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
	{
		if(((ddsd.dwFlags & DDSD_WIDTH) || (ddsd.dwFlags & DDSD_HEIGHT)
			|| (ddsd.dwFlags & DDSD_PIXELFORMAT)) && !(ddsd.ddsCaps.dwCaps & DDSCAPS_BACKBUFFER))
		{
			*error = DDERR_INVALIDPARAMS;
			return;
		}
		else
		{
			if(ddInterface->GetFullscreen())
			{
				fakex = sizes[0];
				fakey = sizes[1];
				ddsd.dwWidth = sizes[2];
				ddsd.dwHeight = sizes[3];
				ddsd.dwFlags |= (DDSD_WIDTH | DDSD_HEIGHT);
				*error = DD_OK;
			}
			else
			{
				fakex = ddsd.dwWidth = GetSystemMetrics(SM_CXSCREEN);
				fakey = ddsd.dwHeight = GetSystemMetrics(SM_CYSCREEN);
				ddsd.dwFlags |= (DDSD_WIDTH | DDSD_HEIGHT);
				*error = DD_OK;
			}
		}
		if(ddInterface->GetBPP() == 8)
		{
			if(!palettein) palette = new glDirectDrawPalette(DDPCAPS_8BIT|DDPCAPS_ALLOW256|DDPCAPS_PRIMARYSURFACE,NULL,NULL);
			else
			{
				palette = palettein;
				palette->AddRef();
			}
			glGenTextures(1,&paltex);
			glBindTexture(GL_TEXTURE_2D,paltex);
			glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
			glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
			glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
			glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
			glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
		}
		else paltex = 0;
	}
	else
	{
		if((ddsd.dwFlags & DDSD_WIDTH) && (ddsd.dwFlags & DDSD_HEIGHT))
		{
			fakex = ddsd.dwWidth;
			fakey = ddsd.dwHeight;
		}
		else
		{
			*error = DDERR_INVALIDPARAMS;
			return;
		}
	}
	if(ddsd.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
	{
		BITMAPINFO info;
		if(ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
		{
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = fakex;
			info.bmiHeader.biHeight = fakey;
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biCompression = BI_RGB;
			info.bmiHeader.biSizeImage = 0;
			info.bmiHeader.biXPelsPerMeter = 0;
			info.bmiHeader.biYPelsPerMeter = 0;
			info.bmiHeader.biClrImportant = 0;
			info.bmiHeader.biClrUsed = 0;
			info.bmiHeader.biBitCount = (WORD)ddInterface->GetBPP();
			*bitmapinfo = info;
			surfacetype=1;
		}
		else
		{
			if(ddsd.dwFlags & DDSD_PIXELFORMAT) surfacetype=0;
			else
			{
				info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				info.bmiHeader.biWidth = fakex;
				info.bmiHeader.biHeight = fakey;
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biCompression = BI_RGB;
				info.bmiHeader.biSizeImage = 0;
				info.bmiHeader.biXPelsPerMeter = 0;
				info.bmiHeader.biYPelsPerMeter = 0;
				info.bmiHeader.biClrImportant = 0;
				info.bmiHeader.biClrUsed = 0;
				info.bmiHeader.biBitCount = (WORD)ddInterface->GetBPP();
				*bitmapinfo = info;
				surfacetype=1;
			}
		}
	}
	else
	{
		bitmapinfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapinfo->bmiHeader.biPlanes = 1;
		bitmapinfo->bmiHeader.biSizeImage = 0;
		bitmapinfo->bmiHeader.biXPelsPerMeter = 0;
		bitmapinfo->bmiHeader.biYPelsPerMeter = 0;
		bitmapinfo->bmiHeader.biClrImportant = 0;
		bitmapinfo->bmiHeader.biClrUsed = 0;
		bitmapinfo->bmiHeader.biCompression = BI_RGB;
		bitmapinfo->bmiHeader.biBitCount = (WORD)ddInterface->GetBPP();
		surfacetype=2;
	}
	bitmapinfo->bmiHeader.biWidth = ddsd.dwWidth;
	bitmapinfo->bmiHeader.biHeight = ddsd.dwHeight;
	switch(surfacetype)
	{
	case 0:
		buffer = (char *)malloc((ddsd.ddpfPixelFormat.dwRGBBitCount * fakex * fakey)/8);
		if(!buffer) *error = DDERR_OUTOFMEMORY;
		break;
	case 1:
		buffer = NULL;
		break;
	case 2:
		buffer = NULL;
		glGenTextures(1,&texture);
		glBindTexture(GL_TEXTURE_2D,texture);
		glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
		if(dxglcfg.scalingfilter == 0)
		{
			glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
			glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		}
		else
		{
			glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
			glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		}
		glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
		if(ddsd.dwFlags & DDSD_PIXELFORMAT)
		{
			switch(ddsd.ddpfPixelFormat.dwRGBBitCount)
			{
			case 8:
			case 16:
			case 32:
				break;

			}
		}
		else
		{
			ddsd.ddpfPixelFormat.dwRGBBitCount = ddInterface->GetBPP();
			switch(ddInterface->GetBPP())
			{
			case 8:
				texformat = GL_LUMINANCE;
				texformat2 = GL_UNSIGNED_BYTE;
				ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8;
				ddsd.ddpfPixelFormat.dwRBitMask = 0;
				ddsd.ddpfPixelFormat.dwGBitMask = 0;
				ddsd.ddpfPixelFormat.dwBBitMask = 0;
				ddsd.lPitch = ddsd.dwWidth;
				break;
			case 15:
				texformat = GL_BGRA;
				texformat2 = GL_UNSIGNED_SHORT_1_5_5_5_REV;
				ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
				ddsd.ddpfPixelFormat.dwRBitMask = 0x7C00;
				ddsd.ddpfPixelFormat.dwGBitMask = 0x3E0;
				ddsd.ddpfPixelFormat.dwBBitMask = 0x1F;
				ddsd.lPitch = ddsd.dwWidth*2;
				ddsd.ddpfPixelFormat.dwRGBBitCount = 16;
				break;
			case 16:
				texformat = GL_RGB;
				texformat2 = GL_UNSIGNED_SHORT_5_6_5;
				ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
				ddsd.ddpfPixelFormat.dwRBitMask = 0xF800;
				ddsd.ddpfPixelFormat.dwGBitMask = 0x7E0;
				ddsd.ddpfPixelFormat.dwBBitMask = 0x1F;
				ddsd.lPitch = ddsd.dwWidth*2;
				break;
			case 24:
				texformat = GL_BGR;
				texformat2 = GL_UNSIGNED_BYTE;
				ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
				ddsd.ddpfPixelFormat.dwRBitMask = 0xFF0000;
				ddsd.ddpfPixelFormat.dwGBitMask = 0xFF00;
				ddsd.ddpfPixelFormat.dwBBitMask = 0xFF;
				ddsd.lPitch = ddsd.dwWidth*4;
			case 32:
				texformat = GL_BGRA;
				texformat2 = GL_UNSIGNED_BYTE;
				ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
				ddsd.ddpfPixelFormat.dwRBitMask = 0xFF0000;
				ddsd.ddpfPixelFormat.dwGBitMask = 0xFF00;
				ddsd.ddpfPixelFormat.dwBBitMask = 0xFF;
				ddsd.lPitch = ddsd.dwWidth*4;
				break;
			default:
				*error = DDERR_INVALIDPIXELFORMAT;
				return;
			}
		}
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,fakex,fakey,0,texformat,texformat2,NULL);
	}

	refcount = 1;
	*error = DD_OK;
	backbuffer = NULL;
	if(ddsd.ddsCaps.dwCaps & DDSCAPS_COMPLEX)
	{
		if(ddsd.ddsCaps.dwCaps & DDSCAPS_FLIP)
		{
			if((ddsd.dwFlags & DDSD_BACKBUFFERCOUNT) && (ddsd.dwBackBufferCount > 0))
			{
				if(!(ddsd.ddsCaps.dwCaps & DDSCAPS_BACKBUFFER))	ddsd.ddsCaps.dwCaps |= DDSCAPS_FRONTBUFFER;
				DDSURFACEDESC2 ddsdBack;
				memcpy(&ddsdBack,&ddsd,ddsd.dwSize);
				ddsdBack.dwBackBufferCount--;
				ddsdBack.ddsCaps.dwCaps |= DDSCAPS_BACKBUFFER;
				glDirectDrawSurface7 *tmp;
				backbuffer = new glDirectDrawSurface7(ddInterface,&ddsdBack,(LPDIRECTDRAWSURFACE7 *)&tmp,error,false,palette);
			}
			else if (ddsd.dwFlags & DDSD_BACKBUFFERCOUNT){}
			else *error = DDERR_INVALIDPARAMS;
		}
	}
	ddInterface->AddRef();
}
glDirectDrawSurface7::~glDirectDrawSurface7()
{
	if(dds1) dds1->Release();
	if(dds2) dds2->Release();
	if(dds3) dds3->Release();
	if(dds4) dds4->Release();
	if(paltex)glDeleteTextures(1,&paltex);
	if(bitmapinfo) free(bitmapinfo);
	if(palette) palette->Release();
	if(backbuffer) backbuffer->Release();
	ddInterface->Release();
}
HRESULT WINAPI glDirectDrawSurface7::QueryInterface(REFIID riid, void** ppvObj)
{
	if(riid == IID_IDirectDrawSurface7)
	{
		this->AddRef();
		*ppvObj = this;
		return DD_OK;
	}
	if(riid == IID_IDirectDrawSurface4)
	{
		if(dds4)
		{
			*ppvObj = dds4;
			dds4->AddRef();
			return DD_OK;
		}
		else
		{
			this->AddRef();
			*ppvObj = new glDirectDrawSurface4(this);
			dds4 = (glDirectDrawSurface4*)*ppvObj;
			return DD_OK;
		}
	}
	if(riid == IID_IDirectDrawSurface3)
	{
		if(dds3)
		{
			*ppvObj = dds3;
			dds3->AddRef();
			return DD_OK;
		}
		else
		{
			this->AddRef();
			*ppvObj = new glDirectDrawSurface3(this);
			dds3 = (glDirectDrawSurface3*)*ppvObj;
			return DD_OK;
		}
	}
	if(riid == IID_IDirectDrawSurface2)
	{
		if(dds2)
		{
			*ppvObj = dds2;
			dds2->AddRef();
			return DD_OK;
		}
		else
		{
			this->AddRef();
			*ppvObj = new glDirectDrawSurface2(this);
			dds2 = (glDirectDrawSurface2*)*ppvObj;
			return DD_OK;
		}
	}
	if(riid == IID_IDirectDrawSurface)
	{
		if(dds1)
		{
			*ppvObj = dds1;
			dds1->AddRef();
			return DD_OK;
		}
		else
		{
			this->AddRef();
			*ppvObj = new glDirectDrawSurface1(this);
			dds1 = (glDirectDrawSurface1*)*ppvObj;
			return DD_OK;
		}
	}
	return E_NOINTERFACE;
}
ULONG WINAPI glDirectDrawSurface7::AddRef()
{
	refcount++;
	return refcount;
}
ULONG WINAPI glDirectDrawSurface7::Release()
{
	ULONG ret;
	refcount--;
	ret = refcount;
	if(refcount == 0) delete this;
	return ret;
}
HRESULT WINAPI glDirectDrawSurface7::AddAttachedSurface(LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface)
{
	FIXME("glDirectDrawSurface7::AddAttachedSurface: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::AddOverlayDirtyRect(LPRECT lpRect)
{
	FIXME("glDirectDrawSurface7::AddOverlayDirtyRect: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
	LONG sizes[6];
	ddInterface->GetSizes(sizes);
	int error;
	if(GLEXT_ARB_framebuffer_object)
	{
		glBindFramebuffer(GL_FRAMEBUFFER,ddInterface->fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
		error = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	}
	else if(GLEXT_EXT_framebuffer_object)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,ddInterface->fbo);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
		error = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	}
	glPushAttrib(GL_VIEWPORT_BIT);
	glViewport(0,0,fakex,fakey);
	RECT destrect;
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
	lpDDSrcSurface->GetSurfaceDesc(&ddsdSrc);
	if(!lpSrcRect)
	{
		srcrect.left = 0;
		srcrect.top = 0;
		srcrect.right = ddsdSrc.dwWidth;
		srcrect.right = ddsdSrc.dwHeight;
	}
	else srcrect = *lpSrcRect;
	GLfloat coords[8];
	coords[0] = (GLfloat)destrect.left * ((GLfloat)sizes[0]/(GLfloat)ddsd.dwWidth);
	coords[1] = (GLfloat)destrect.right * ((GLfloat)sizes[0]/(GLfloat)ddsd.dwWidth);
	coords[2] = (GLfloat)fakey-((GLfloat)destrect.top * ((GLfloat)sizes[1]/(GLfloat)ddsd.dwHeight));
	coords[3] = (GLfloat)fakey-((GLfloat)destrect.bottom * ((GLfloat)sizes[1]/(GLfloat)ddsd.dwHeight));
	coords[4] = (GLfloat)srcrect.left / (GLfloat)ddsdSrc.dwWidth;
	coords[5] = (GLfloat)srcrect.right / (GLfloat)ddsdSrc.dwWidth;
	coords[6] = (GLfloat)srcrect.top / (GLfloat)ddsdSrc.dwHeight;
	coords[7] = (GLfloat)srcrect.bottom / (GLfloat)ddsdSrc.dwHeight;
	glMatrixMode(GL_PROJECTION);
	glClear(GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();
	glOrtho(0,fakex,fakey,0,0,1);
	glMatrixMode(GL_MODELVIEW);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D,((glDirectDrawSurface7*)lpDDSrcSurface)->GetTexture());
	glBegin(GL_QUADS);
	glTexCoord2f(coords[4], coords[6]);
	glVertex2f(coords[0], coords[2]);
	glTexCoord2f(coords[5], coords[6]);
	glVertex2f( coords[1], coords[2]);
	glTexCoord2f(coords[5], coords[7]);
	glVertex2f( coords[1], coords[3]);
	glTexCoord2f(coords[4], coords[7]);
	glVertex2f(coords[0], coords[3]);
	glEnd();
	glDisable(GL_TEXTURE_2D);
	if(GLEXT_ARB_framebuffer_object)
	{
		glBindFramebuffer(GL_FRAMEBUFFER,0);
	}
	else if(GLEXT_EXT_framebuffer_object)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
	}
	glPopAttrib();
	if(ddsd.ddsCaps.dwCaps & (DDSCAPS_FRONTBUFFER | DDSCAPS_PRIMARYSURFACE)) RenderScreen(texture);
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags)
{
	FIXME("glDirectDrawSurface7::BltBatch: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
{
	FIXME("glDirectDrawSurface7::BltFast: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface)
{
	FIXME("glDirectDrawSurface7::DeleteAttachedSurface: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback)
{
	FIXME("glDirectDrawSurface7::EnumAttachedSurfaces: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpfnCallback)
{
	FIXME("glDirectDrawSurface7::EnumOverlayZOrders: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::Flip(LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride, DWORD dwFlags)
{
	if(lpDDSurfaceTargetOverride) return DDERR_GENERIC;
	if(ddsd.ddsCaps.dwCaps & DDSCAPS_FLIP)
	{
		if(ddsd.ddsCaps.dwCaps & DDSCAPS_BACKBUFFER) return DDERR_INVALIDOBJECT;
		GLuint *textures = new GLuint[ddsd.dwBackBufferCount+1];
		textures[0] = texture;
		glDirectDrawSurface7 *tmp = this;
		for(DWORD i = 0; i < ddsd.dwBackBufferCount; i++)
		{
			tmp = tmp->GetBackbuffer();
			textures[i+1] = tmp->GetTexture();
		}
		GLuint tmptex = textures[0];
		memmove(textures,&textures[1],ddsd.dwBackBufferCount*sizeof(GLuint));
		textures[ddsd.dwBackBufferCount] = tmptex;
		tmp = this;
		this->SetTexture(textures[0]);
		for(DWORD i = 0; i < ddsd.dwBackBufferCount; i++)
		{
			tmp = tmp->GetBackbuffer();
			tmp->SetTexture(textures[i+1]);
		}
		RenderScreen(textures[0]);
		delete textures;
	}
	else return DDERR_NOTFLIPPABLE;
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::GetAttachedSurface(LPDDSCAPS2 lpDDSCaps, LPDIRECTDRAWSURFACE7 FAR *lplpDDAttachedSurface)
{
	DDSCAPS2 ddsComp;
	backbuffer->GetCaps(&ddsComp);
	unsigned __int64 comp1,comp2;
	memcpy(&comp1,lpDDSCaps,sizeof(unsigned __int64));
	memcpy(&comp2,&ddsComp,sizeof(unsigned __int64));
	if((comp1 & comp2) == comp1)
	{
		*lplpDDAttachedSurface = backbuffer;
		backbuffer->AddRef();
		return DD_OK;
	}
	else
	{
		FIXME("glDirectDrawSurface7::GetAttachedSurface: stub\n");
		return DDERR_GENERIC;
	}
}
HRESULT WINAPI glDirectDrawSurface7::GetBltStatus(DWORD dwFlags)
{
	FIXME("glDirectDrawSurface7::GetBltStatus: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetCaps(LPDDSCAPS2 lpDDSCaps)
{
	memcpy(lpDDSCaps,&ddsd.ddsCaps,sizeof(DDSCAPS2));
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::GetClipper(LPDIRECTDRAWCLIPPER FAR *lplpDDClipper)
{
	FIXME("glDirectDrawSurface7::GetClipper: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	FIXME("glDirectDrawSurface7::GetColorKey: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetDC(HDC FAR *lphDC)
{
	if(hdc) return DDERR_DCALREADYCREATED;
	HRESULT error;
	unsigned char *bitmap;
	error = this->Lock(NULL,&ddsd,0,NULL);
	if(error != DD_OK) return error;
	hdc = CreateCompatibleDC(NULL);
	bitmapinfo->bmiHeader.biWidth = ddsd.lPitch / (bitmapinfo->bmiHeader.biBitCount / 8);
	if(ddInterface->GetBPP() == 8)
		memcpy(bitmapinfo->bmiColors,palette->GetPalette(NULL),1024);
	hbitmap = CreateDIBitmap(hdc,&bitmapinfo->bmiHeader,CBM_INIT,ddsd.lpSurface,bitmapinfo,DIB_RGB_COLORS);
	HGDIOBJ temp = SelectObject(hdc,hbitmap);
	DeleteObject(temp);
	*lphDC = hdc;
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::GetFlipStatus(DWORD dwFlags)
{
	FIXME("glDirectDrawSurface7::GetFlipStatus: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetOverlayPosition(LPLONG lplX, LPLONG lplY)
{
	FIXME("glDirectDrawSurface7::GetOverlayPosition: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetPalette(LPDIRECTDRAWPALETTE FAR *lplpDDPalette)
{
	HRESULT err;
	if(palette)
	{
		palette->AddRef();
		*lplpDDPalette = palette;
		err = DD_OK;
	}
	else
	{
		err = DDERR_NOPALETTEATTACHED;
		*lplpDDPalette = NULL;
	}
	return err;
}
HRESULT WINAPI glDirectDrawSurface7::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat)
{
	FIXME("glDirectDrawSurface7::GetPixelFormat: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc)
{
	if(!lpDDSurfaceDesc) return DDERR_INVALIDPARAMS;
	memcpy(lpDDSurfaceDesc,&ddsd,lpDDSurfaceDesc->dwSize);
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC2 lpDDSurfaceDesc)
{
	FIXME("glDirectDrawSurface7::Initialize: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::IsLost()
{
	FIXME("glDirectDrawSurface7::IsLost: stub\n");
	return DDERR_GENERIC;
}

inline RGBTRIPLE _32to24(unsigned long color)
{
	RGBTRIPLE tmp;
	tmp.rgbtBlue = color & 0xFF;
	tmp.rgbtGreen = (color >> 8) & 0xFF;
	tmp.rgbtRed = (color >> 16) & 0xFF;
	return tmp;
}

HRESULT WINAPI glDirectDrawSurface7::Lock(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
	if(locked) return DDERR_SURFACEBUSY;
	DWORD x,y;
	unsigned char *bitmap = (unsigned char *)malloc((ddInterface->GetBPPPowerOf2()/8) * ddsd.dwWidth * ddsd.dwHeight);
	unsigned short *&bmp16 = (unsigned short *&)bitmap;
	RGBTRIPLE *&bmp24 = (RGBTRIPLE *&)bitmap;
	unsigned long *&bmp32 = (unsigned long *&)bitmap;
	unsigned char *temptex;
	unsigned short *&tmptex16 = (unsigned short *&)temptex;
	unsigned long *&tmptex32 = (unsigned long *&)temptex;
	if(ddInterface->GetBPP() == 24) ddsd.lPitch = ddsd.dwWidth * (ddInterface->GetBPPMultipleOf8()/8);
	else ddsd.lPitch = ddsd.dwWidth * (ddInterface->GetBPPMultipleOf8()/8);
	float mulx, muly;
	if(!bitmap) return DDERR_OUTOFMEMORY;
	switch(surfacetype)
	{
	case 0:
		FIXME("glDirectDrawSurface7::Lock: surface type 0 not supported yet");
		return DDERR_UNSUPPORTED;
		break;
	case 1:
		FIXME("glDirectDrawSurface7::Lock: surface type 1 not supported yet");
		return DDERR_UNSUPPORTED;
		break;
	case 2:
		glBindTexture(GL_TEXTURE_2D,this->texture);  // Select surface's texture
		if(fakex == ddsd.dwWidth && fakey == ddsd.dwHeight)
		{
			if(ddInterface->GetBPP() == 24)
			{
				temptex  = (unsigned char *)malloc(NextMultipleOf4((ddInterface->GetBPPPowerOf2()/8)*fakex)*fakey);
				for(x = 0; x < ddsd.dwWidth*ddsd.dwHeight; x++)
					bmp24[x] = _32to24(tmptex32[x]);
				glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,fakex,fakey,0,texformat,texformat2,temptex);
				free(temptex);
			}
			glGetTexImage(GL_TEXTURE_2D,0,texformat,texformat2,bitmap); // Shortcut for no scaling
		}
		else
		{
			mulx = (float)fakex / (float)ddsd.dwWidth;
			muly = (float)fakey / (float)ddsd.dwHeight;
			temptex = (unsigned char *)malloc(NextMultipleOf4((ddInterface->GetBPPPowerOf2()/8)*fakex)*fakey);
			if(!temptex)
			{
				free(bitmap);
				return DDERR_OUTOFMEMORY;
			}
			glGetTexImage(GL_TEXTURE_2D,0,texformat,texformat2,temptex);
			switch(ddInterface->GetBPPMultipleOf8())
			{
			case 8:
				for(y = 0; y < ddsd.dwHeight; y++)
					for(x = 0; x < ddsd.dwWidth; x++)
						bitmap[x + (ddsd.dwWidth*y)] = temptex[(int)(x*mulx) + (fakex*(int)(y*muly))];
				break;
			case 16:
				for(y = 0; y < ddsd.dwHeight; y++)
					for(x = 0; x < ddsd.dwWidth; x++)
						bmp16[x + (ddsd.dwWidth*y)] = tmptex16[(int)(x*mulx) + (fakex*(int)(y*muly))];
				break;
			case 24:
				for(y = 0; y < ddsd.dwHeight; y++)
					for(x = 0; x < ddsd.dwWidth; x++)
						bmp24[x + (ddsd.dwWidth*y)] = _32to24(tmptex32[(int)(x*mulx) + (fakex*(int)(y*muly))]);
					break;
			case 32:
				for(y = 0; y < ddsd.dwHeight; y++)
					for(x = 0; x < ddsd.dwWidth; x++)
						bmp32[x + (ddsd.dwWidth*y)] = tmptex32[(int)(x*mulx) + (fakex*(int)(y*muly))];
				break;
			break;
			}
			free(temptex);
		}
	}
	ddsd.lpSurface = bitmap;
	memcpy(lpDDSurfaceDesc,&ddsd,lpDDSurfaceDesc->dwSize);
	locked++;
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::ReleaseDC(HDC hDC)
{
	if(!hdc) return DDERR_INVALIDOBJECT;
	if(hDC != hdc) return DDERR_INVALIDOBJECT;
	GetDIBits(hDC,hbitmap,0,ddsd.dwHeight,ddsd.lpSurface,bitmapinfo,DIB_RGB_COLORS);
	Unlock(NULL);
	DeleteObject(hbitmap);
	hbitmap = NULL;
	DeleteDC(hdc);
	hdc = NULL;
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::Restore()
{
	FIXME("glDirectDrawSurface7::Restore: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper)
{
	if(clipper) clipper->Release();
	clipper = (glDirectDrawClipper *)lpDDClipper;
	if(clipper)clipper->AddRef();
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	FIXME("glDirectDrawSurface7::GetColorKey: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::SetOverlayPosition(LONG lX, LONG lY)
{
	FIXME("glDirectDrawSurface7::SetOverlayPosition: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette)
{
	if(palette)
	{
		palette->Release();
		if(!lpDDPalette) palette = new glDirectDrawPalette(DDPCAPS_8BIT|DDPCAPS_ALLOW256|DDPCAPS_PRIMARYSURFACE,NULL,NULL);
	}
	if(lpDDPalette)
	{
		palette = (glDirectDrawPalette *)lpDDPalette;
		palette->AddRef();
	}
	return DD_OK;
}

inline unsigned int _24to32(RGBTRIPLE color)
{
	return color.rgbtBlue | (color.rgbtGreen << 8) | (color.rgbtRed << 16);
}

HRESULT WINAPI glDirectDrawSurface7::Unlock(LPRECT lpRect)
{
	if(!locked) return DDERR_NOTLOCKED;
	locked--;
	unsigned char *bitmap = (unsigned char *)ddsd.lpSurface;
	unsigned short *&bmp16 = (unsigned short *&)bitmap;
	RGBTRIPLE *&bmp24 = (RGBTRIPLE *&)bitmap;
	unsigned long *&bmp32 = (unsigned long *&)bitmap;
	unsigned char *temptex;
	unsigned short *&tmptex16 = (unsigned short *&)temptex;
	unsigned long *&tmptex32 = (unsigned long *&)temptex;
	float mulx, muly;
	DWORD x,y;
	glBindTexture(GL_TEXTURE_2D,this->texture);  // Select surface's texture
	if(ddsd.dwWidth == fakex && ddsd.dwHeight == fakey)
	{
		if(ddInterface->GetBPP() == 24)
		{
			temptex  = (unsigned char *)malloc(NextMultipleOf4((ddInterface->GetBPPPowerOf2()/8)*fakex)*fakey);
			for(x = 0; x < ddsd.dwWidth*ddsd.dwHeight; x++)
				tmptex32[x] = _24to32(bmp24[x]);
			glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,fakex,fakey,0,texformat,texformat2,temptex);
			free(temptex);
		}
		else glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,fakex,fakey,0,texformat,texformat2,bitmap);
	}
	else
	{
		temptex  = (unsigned char *)malloc(NextMultipleOf4((ddInterface->GetBPPPowerOf2()/8)*fakex)*fakey);
		if(!temptex) return DDERR_OUTOFMEMORY;
		mulx = (float)ddsd.dwWidth / (float)fakex;
		muly = (float)ddsd.dwHeight / (float)fakey;
		switch(ddInterface->GetBPPMultipleOf8())
		{
		case 8:
			for(y = 0; y < fakey; y++)
				for(x = 0; x < fakex; x++)
					temptex[x + (NextMultipleOf4(fakex)*y)] = bitmap[(int)(x*mulx) + (ddsd.dwWidth*(int)(y*muly))];
			break;
		case 16:
			for(y = 0; y < fakey; y++)
				for(x = 0; x < fakex; x++)
					tmptex16[x + (NextMultipleOf2(fakex)*y)] = bmp16[(int)(x*mulx) + (ddsd.dwWidth*(int)(y*muly))];
			break;
		case 24:
			for(y = 0; y < fakey; y++)
				for(x = 0; x < fakex; x++)
					tmptex32[x + (fakex*y)] = _24to32(bmp24[(int)(x*mulx) + (ddsd.dwWidth*(int)(y*muly))]);
			break;
		case 32:
			for(y = 0; y < fakey; y++)
				for(x = 0; x < fakex; x++)
					tmptex32[x + (fakex*y)] = bmp32[(int)(x*mulx) + (ddsd.dwWidth*(int)(y*muly))];
			break;
		break;
		}
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,fakex,fakey,0,texformat,texformat2,temptex);
		free(temptex);
	}
	if(ddsd.lpSurface) free(ddsd.lpSurface);
	ddsd.lpSurface = NULL;
	if(ddsd.ddsCaps.dwCaps & (DDSCAPS_FRONTBUFFER | DDSCAPS_PRIMARYSURFACE)) RenderScreen(texture);
	return DD_OK;
}
HRESULT WINAPI glDirectDrawSurface7::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE7 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx)
{
	FIXME("glDirectDrawSurface7::UpdateOverlay: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::UpdateOverlayDisplay(DWORD dwFlags)
{
	FIXME("glDirectDrawSurface7::UpdateOverlayDisplay: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSReference)
{
	FIXME("glDirectDrawSurface7::UpdateOverlayZOrder: stub\n");
	return DDERR_GENERIC;
}

void glDirectDrawSurface7::RenderScreen(GLuint texture)
{
	LONG sizes[6];
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	RECT r,r2;
	if(ddsd.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
	{
		if(ddInterface->GetFullscreen())
		{
			ddInterface->GetSizes(sizes);
			glOrtho((signed)-(sizes[4]-fakex)/2,(sizes[4]-fakex)/2+fakex,
				(signed)-(sizes[5]-fakey)/2,(sizes[5]-fakey)/2+fakey,0,1);
		}
		else
		{
			HWND hwnd,hrender;
			ddInterface->GetHandles(&hwnd,&hrender);
			GetClientRect(hwnd,&r);
			GetClientRect(hrender,&r2);
			if(memcmp(&r2,&r,sizeof(RECT)))
				SetWindowPos(hrender,NULL,0,0,r.right,r.bottom,SWP_SHOWWINDOW);
			r2 = r;
			ClientToScreen(hwnd,(LPPOINT)&r2.left);
			ClientToScreen(hwnd,(LPPOINT)&r2.right);
			glViewport(0,0,r.right,r.bottom);
			glOrtho(r2.left,r2.right,fakey-r2.bottom,fakey-r2.top,0,1);
		}
	}
	else glOrtho(0,fakex,fakey,0,0,1);
	glMatrixMode(GL_MODELVIEW);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	if(ddInterface->GetBPP() == 8)
	{
		GLint palprog = ddInterface->PalProg();
		glUseProgram(palprog);
		glBindTexture(GL_TEXTURE_2D,paltex);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,256,1,0,GL_RGBA,GL_UNSIGNED_BYTE,palette->GetPalette(NULL));
		GLint palloc = glGetUniformLocation(palprog,"ColorTable");
		GLint texloc = glGetUniformLocation(palprog,"IndexTexture");
		glUniform1i(texloc,0);
		glUniform1i(palloc,1);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D,texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D,paltex);
		glActiveTexture(GL_TEXTURE0);
	}
	else
	{
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D,texture);
	}
	glBegin(GL_QUADS);
	glTexCoord2f(0., 1.);
	glVertex2f(0., 0.);
	glTexCoord2f(1., 1.);
	glVertex2f( (float)fakex, 0.);
	glTexCoord2f(1., 0.);
	glVertex2f( (float)fakex, (float)fakey);
	glTexCoord2f(0., 0.);
	glVertex2f(0., (float)fakey);
	glEnd();
	glDisable(GL_TEXTURE_2D);
	glFlush();
	SwapBuffers(ddInterface->hDC);
}
// ddraw 2+ api
HRESULT WINAPI glDirectDrawSurface7::GetDDInterface(LPVOID FAR *lplpDD)
{
	*lplpDD = ddInterface;
	FIXME("glDirectDrawSurface7::GetDDInterface: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::PageLock(DWORD dwFlags)
{
	FIXME("glDirectDrawSurface7::PageLock: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::PageUnlock(DWORD dwFlags)
{
	FIXME("glDirectDrawSurface7::PageUnlock: stub\n");
	return DDERR_GENERIC;
}
// ddraw 3+ api
HRESULT WINAPI glDirectDrawSurface7::SetSurfaceDesc(LPDDSURFACEDESC2 lpddsd2, DWORD dwFlags)
{
	FIXME("glDirectDrawSurface7::SetSurfaceDesc: stub\n");
	return DDERR_GENERIC;
}
// ddraw 4+ api
HRESULT WINAPI glDirectDrawSurface7::SetPrivateData(REFGUID guidTag, LPVOID  lpData, DWORD   cbSize, DWORD   dwFlags)
{
	FIXME("glDirectDrawSurface7::SetPrivateData: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetPrivateData(REFGUID guidTag, LPVOID  lpBuffer, LPDWORD lpcbBufferSize)
{
	FIXME("glDirectDrawSurface7::GetPrivateData: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::FreePrivateData(REFGUID guidTag)
{
	FIXME("glDirectDrawSurface7::FreePrivateData: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetUniquenessValue(LPDWORD lpValue)
{
	FIXME("glDirectDrawSurface7::GetUniquenessValue: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::ChangeUniquenessValue()
{
	FIXME("glDirectDrawSurface7::ChangeUniquenessValue: stub\n");
	return DDERR_GENERIC;
}
// ddraw 7 api
HRESULT WINAPI glDirectDrawSurface7::SetPriority(DWORD dwPriority)
{
	FIXME("glDirectDrawSurface7::SetPriority: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetPriority(LPDWORD lpdwPriority)
{
	FIXME("glDirectDrawSurface7::GetPriority: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::SetLOD(DWORD dwMaxLOD)
{
	FIXME("glDirectDrawSurface7::SetLOD: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::GetLOD(LPDWORD lpdwMaxLOD)
{
	FIXME("glDirectDrawSurface7::GetLOD: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface7::Unlock2(LPVOID lpSurfaceData)
{
	return Unlock((LPRECT)lpSurfaceData);
}

// DDRAW1 wrapper
glDirectDrawSurface1::glDirectDrawSurface1(glDirectDrawSurface7 *gl_DDS7)
{
	glDDS7 = gl_DDS7;
	refcount = 1;
}
glDirectDrawSurface1::~glDirectDrawSurface1()
{
	glDDS7->dds1 = NULL;
	glDDS7->Release();
}
HRESULT WINAPI glDirectDrawSurface1::QueryInterface(REFIID riid, void** ppvObj)
{
	return glDDS7->QueryInterface(riid,ppvObj);
}
ULONG WINAPI glDirectDrawSurface1::AddRef()
{
	refcount++;
	return refcount;
}
ULONG WINAPI glDirectDrawSurface1::Release()
{
	ULONG ret;
	refcount--;
	ret = refcount;
	if(refcount == 0) delete this;
	return ret;
}
HRESULT WINAPI glDirectDrawSurface1::AddAttachedSurface(LPDIRECTDRAWSURFACE lpDDSAttachedSurface)
{
	return glDDS7->AddAttachedSurface((LPDIRECTDRAWSURFACE7)lpDDSAttachedSurface);
}
HRESULT WINAPI glDirectDrawSurface1::AddOverlayDirtyRect(LPRECT lpRect)
{
	return glDDS7->AddOverlayDirtyRect(lpRect);
}
HRESULT WINAPI glDirectDrawSurface1::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
	return glDDS7->Blt(lpDestRect,((glDirectDrawSurface1*)lpDDSrcSurface)->GetDDS7(),lpSrcRect,dwFlags,lpDDBltFx);
}
HRESULT WINAPI glDirectDrawSurface1::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags)
{
	return glDDS7->BltBatch(lpDDBltBatch,dwCount,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface1::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
{
	return glDDS7->BltFast(dwX,dwY,(LPDIRECTDRAWSURFACE7)lpDDSrcSurface,lpSrcRect,dwTrans);
}
HRESULT WINAPI glDirectDrawSurface1::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE lpDDSAttachedSurface)
{
	return glDDS7->DeleteAttachedSurface(dwFlags,(LPDIRECTDRAWSURFACE7)lpDDSAttachedSurface);
}
HRESULT WINAPI glDirectDrawSurface1::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback)
{
	return glDDS7->EnumAttachedSurfaces(lpContext,(LPDDENUMSURFACESCALLBACK7)lpEnumSurfacesCallback);
}
HRESULT WINAPI glDirectDrawSurface1::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback)
{
	return glDDS7->EnumOverlayZOrders(dwFlags,lpContext,(LPDDENUMSURFACESCALLBACK7)lpfnCallback);
}
HRESULT WINAPI glDirectDrawSurface1::Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags)
{
	return glDDS7->Flip((LPDIRECTDRAWSURFACE7)lpDDSurfaceTargetOverride,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface1::GetAttachedSurface(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE FAR *lplpDDAttachedSurface)
{
	HRESULT error;
	glDirectDrawSurface7 *attachedsurface;
	glDirectDrawSurface1 *attached1;
	DDSCAPS2 ddscaps1;
	ddscaps1.dwCaps = lpDDSCaps->dwCaps;
	ddscaps1.dwCaps2 = ddscaps1.dwCaps3 = ddscaps1.dwCaps4 = 0;
	error = glDDS7->GetAttachedSurface(&ddscaps1,(LPDIRECTDRAWSURFACE7 FAR *)&attachedsurface);
	if(error == DD_OK)
	{
		attachedsurface->QueryInterface(IID_IDirectDrawSurface,(void **)&attached1);
		attachedsurface->Release();
		*lplpDDAttachedSurface = attached1;
	}
	return error;
}
HRESULT WINAPI glDirectDrawSurface1::GetBltStatus(DWORD dwFlags)
{
	return glDDS7->GetBltStatus(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface1::GetCaps(LPDDSCAPS lpDDSCaps)
{
	HRESULT error;
	DDSCAPS2 ddsCaps1;
	error =  glDDS7->GetCaps(&ddsCaps1);
	ZeroMemory(&ddsCaps1,sizeof(DDSCAPS2));
	memcpy(lpDDSCaps,&ddsCaps1,sizeof(DDSCAPS));
	return error;
}
HRESULT WINAPI glDirectDrawSurface1::GetClipper(LPDIRECTDRAWCLIPPER FAR *lplpDDClipper)
{
	return glDDS7->GetClipper(lplpDDClipper);
}
HRESULT WINAPI glDirectDrawSurface1::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	return glDDS7->GetColorKey(dwFlags,lpDDColorKey);
}
HRESULT WINAPI glDirectDrawSurface1::GetDC(HDC FAR *lphDC)
{
	return glDDS7->GetDC(lphDC);
}
HRESULT WINAPI glDirectDrawSurface1::GetFlipStatus(DWORD dwFlags)
{
	return glDDS7->GetFlipStatus(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface1::GetOverlayPosition(LPLONG lplX, LPLONG lplY)
{
	return glDDS7->GetOverlayPosition(lplX,lplY);
}
HRESULT WINAPI glDirectDrawSurface1::GetPalette(LPDIRECTDRAWPALETTE FAR *lplpDDPalette)
{
	return glDDS7->GetPalette(lplpDDPalette);
}
HRESULT WINAPI glDirectDrawSurface1::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat)
{
	return glDDS7->GetPixelFormat(lpDDPixelFormat);
}
HRESULT WINAPI glDirectDrawSurface1::GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc)
{
	return glDDS7->GetSurfaceDesc((LPDDSURFACEDESC2)lpDDSurfaceDesc);
}
HRESULT WINAPI glDirectDrawSurface1::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc)
{
	FIXME("glDirectDrawSurface1::Initialize: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface1::IsLost()
{
	return glDDS7->IsLost();
}
HRESULT WINAPI glDirectDrawSurface1::Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
	return glDDS7->Lock(lpDestRect,(LPDDSURFACEDESC2)lpDDSurfaceDesc,dwFlags,hEvent);
}
HRESULT WINAPI glDirectDrawSurface1::ReleaseDC(HDC hDC)
{
	return glDDS7->ReleaseDC(hDC);
}
HRESULT WINAPI glDirectDrawSurface1::Restore()
{
	return glDDS7->Restore();
}
HRESULT WINAPI glDirectDrawSurface1::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper)
{
	return glDDS7->SetClipper(lpDDClipper);
}
HRESULT WINAPI glDirectDrawSurface1::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	return glDDS7->SetColorKey(dwFlags,lpDDColorKey);
}
HRESULT WINAPI glDirectDrawSurface1::SetOverlayPosition(LONG lX, LONG lY)
{
	return glDDS7->SetOverlayPosition(lX,lY);
}
HRESULT WINAPI glDirectDrawSurface1::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette)
{
	return glDDS7->SetPalette(lpDDPalette);
}
HRESULT WINAPI glDirectDrawSurface1::Unlock(LPVOID lpSurfaceData)
{
	return glDDS7->Unlock2(lpSurfaceData);
}
HRESULT WINAPI glDirectDrawSurface1::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx)
{
	return glDDS7->UpdateOverlay(lpSrcRect,(LPDIRECTDRAWSURFACE7)lpDDDestSurface,lpDestRect,dwFlags,lpDDOverlayFx);
}
HRESULT WINAPI glDirectDrawSurface1::UpdateOverlayDisplay(DWORD dwFlags)
{
	return glDDS7->UpdateOverlayDisplay(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface1::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE lpDDSReference)
{
	return glDDS7->UpdateOverlayZOrder(dwFlags,(LPDIRECTDRAWSURFACE7)lpDDSReference);
}

// DDRAW2 wrapper
glDirectDrawSurface2::glDirectDrawSurface2(glDirectDrawSurface7 *gl_DDS7)
{
	glDDS7 = gl_DDS7;
	refcount = 1;
}
glDirectDrawSurface2::~glDirectDrawSurface2()
{
	glDDS7->dds2 = NULL;
	glDDS7->Release();
}
HRESULT WINAPI glDirectDrawSurface2::QueryInterface(REFIID riid, void** ppvObj)
{
	return glDDS7->QueryInterface(riid,ppvObj);
}
ULONG WINAPI glDirectDrawSurface2::AddRef()
{
	refcount++;
	return refcount;
}
ULONG WINAPI glDirectDrawSurface2::Release()
{
	ULONG ret;
	refcount--;
	ret = refcount;
	if(refcount == 0) delete this;
	return ret;
}
HRESULT WINAPI glDirectDrawSurface2::AddAttachedSurface(LPDIRECTDRAWSURFACE2 lpDDSAttachedSurface)
{
	return glDDS7->AddAttachedSurface((LPDIRECTDRAWSURFACE7)lpDDSAttachedSurface);
}
HRESULT WINAPI glDirectDrawSurface2::AddOverlayDirtyRect(LPRECT lpRect)
{
	return glDDS7->AddOverlayDirtyRect(lpRect);
}
HRESULT WINAPI glDirectDrawSurface2::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE2 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
	return glDDS7->Blt(lpDestRect,((glDirectDrawSurface2*)lpDDSrcSurface)->GetDDS7(),lpSrcRect,dwFlags,lpDDBltFx);
}
HRESULT WINAPI glDirectDrawSurface2::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags)
{
	return glDDS7->BltBatch(lpDDBltBatch,dwCount,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface2::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE2 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
{
	return glDDS7->BltFast(dwX,dwY,(LPDIRECTDRAWSURFACE7)lpDDSrcSurface,lpSrcRect,dwTrans);
}
HRESULT WINAPI glDirectDrawSurface2::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE2 lpDDSAttachedSurface)
{
	return glDDS7->DeleteAttachedSurface(dwFlags,(LPDIRECTDRAWSURFACE7)lpDDSAttachedSurface);
}
HRESULT WINAPI glDirectDrawSurface2::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback)
{
	return glDDS7->EnumAttachedSurfaces(lpContext,(LPDDENUMSURFACESCALLBACK7)lpEnumSurfacesCallback);
}
HRESULT WINAPI glDirectDrawSurface2::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback)
{
	return glDDS7->EnumOverlayZOrders(dwFlags,lpContext,(LPDDENUMSURFACESCALLBACK7)lpfnCallback);
}
HRESULT WINAPI glDirectDrawSurface2::Flip(LPDIRECTDRAWSURFACE2 lpDDSurfaceTargetOverride, DWORD dwFlags)
{
	return glDDS7->Flip((LPDIRECTDRAWSURFACE7)lpDDSurfaceTargetOverride,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface2::GetAttachedSurface(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE2 FAR *lplpDDAttachedSurface)
{
	HRESULT error;
	glDirectDrawSurface7 *attachedsurface;
	glDirectDrawSurface2 *attached1;
	DDSCAPS2 ddscaps1;
	ddscaps1.dwCaps = lpDDSCaps->dwCaps;
	ddscaps1.dwCaps2 = ddscaps1.dwCaps3 = ddscaps1.dwCaps4 = 0;
	error = glDDS7->GetAttachedSurface(&ddscaps1,(LPDIRECTDRAWSURFACE7 FAR *)&attachedsurface);
	if(error == DD_OK)
	{
		attachedsurface->QueryInterface(IID_IDirectDrawSurface2,(void **)&attached1);
		attachedsurface->Release();
		*lplpDDAttachedSurface = attached1;
	}
	return error;
}
HRESULT WINAPI glDirectDrawSurface2::GetBltStatus(DWORD dwFlags)
{
	return glDDS7->GetBltStatus(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface2::GetCaps(LPDDSCAPS lpDDSCaps)
{
	HRESULT error;
	DDSCAPS2 ddsCaps1;
	error =  glDDS7->GetCaps(&ddsCaps1);
	ZeroMemory(&ddsCaps1,sizeof(DDSCAPS2));
	memcpy(lpDDSCaps,&ddsCaps1,sizeof(DDSCAPS));
	return error;
}
HRESULT WINAPI glDirectDrawSurface2::GetClipper(LPDIRECTDRAWCLIPPER FAR *lplpDDClipper)
{
	return glDDS7->GetClipper(lplpDDClipper);
}
HRESULT WINAPI glDirectDrawSurface2::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	return glDDS7->GetColorKey(dwFlags,lpDDColorKey);
}
HRESULT WINAPI glDirectDrawSurface2::GetDC(HDC FAR *lphDC)
{
	return glDDS7->GetDC(lphDC);
}
HRESULT WINAPI glDirectDrawSurface2::GetFlipStatus(DWORD dwFlags)
{
	return glDDS7->GetFlipStatus(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface2::GetOverlayPosition(LPLONG lplX, LPLONG lplY)
{
	return glDDS7->GetOverlayPosition(lplX,lplY);
}
HRESULT WINAPI glDirectDrawSurface2::GetPalette(LPDIRECTDRAWPALETTE FAR *lplpDDPalette)
{
	return glDDS7->GetPalette(lplpDDPalette);
}
HRESULT WINAPI glDirectDrawSurface2::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat)
{
	return glDDS7->GetPixelFormat(lpDDPixelFormat);
}
HRESULT WINAPI glDirectDrawSurface2::GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc)
{
	return glDDS7->GetSurfaceDesc((LPDDSURFACEDESC2)lpDDSurfaceDesc);
}
HRESULT WINAPI glDirectDrawSurface2::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc)
{
	FIXME("glDirectDrawSurface1::Initialize: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface2::IsLost()
{
	return glDDS7->IsLost();
}
HRESULT WINAPI glDirectDrawSurface2::Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
	return glDDS7->Lock(lpDestRect,(LPDDSURFACEDESC2)lpDDSurfaceDesc,dwFlags,hEvent);
}
HRESULT WINAPI glDirectDrawSurface2::ReleaseDC(HDC hDC)
{
	return glDDS7->ReleaseDC(hDC);
}
HRESULT WINAPI glDirectDrawSurface2::Restore()
{
	return glDDS7->Restore();
}
HRESULT WINAPI glDirectDrawSurface2::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper)
{
	return glDDS7->SetClipper(lpDDClipper);
}
HRESULT WINAPI glDirectDrawSurface2::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	return glDDS7->SetColorKey(dwFlags,lpDDColorKey);
}
HRESULT WINAPI glDirectDrawSurface2::SetOverlayPosition(LONG lX, LONG lY)
{
	return glDDS7->SetOverlayPosition(lX,lY);
}
HRESULT WINAPI glDirectDrawSurface2::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette)
{
	return glDDS7->SetPalette(lpDDPalette);
}
HRESULT WINAPI glDirectDrawSurface2::Unlock(LPVOID lpSurfaceData)
{
	return glDDS7->Unlock2(lpSurfaceData);
}
HRESULT WINAPI glDirectDrawSurface2::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE2 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx)
{
	return glDDS7->UpdateOverlay(lpSrcRect,(LPDIRECTDRAWSURFACE7)lpDDDestSurface,lpDestRect,dwFlags,lpDDOverlayFx);
}
HRESULT WINAPI glDirectDrawSurface2::UpdateOverlayDisplay(DWORD dwFlags)
{
	return glDDS7->UpdateOverlayDisplay(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface2::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE2 lpDDSReference)
{
	return glDDS7->UpdateOverlayZOrder(dwFlags,(LPDIRECTDRAWSURFACE7)lpDDSReference);
}
HRESULT WINAPI glDirectDrawSurface2::GetDDInterface(LPVOID FAR *lplpDD)
{
	return glDDS7->GetDDInterface(lplpDD);
}
HRESULT WINAPI glDirectDrawSurface2::PageLock(DWORD dwFlags)
{
	return glDDS7->PageLock(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface2::PageUnlock(DWORD dwFlags)
{
	return glDDS7->PageUnlock(dwFlags);
}

// DDRAW3 wrapper
glDirectDrawSurface3::glDirectDrawSurface3(glDirectDrawSurface7 *gl_DDS7)
{
	glDDS7 = gl_DDS7;
	refcount = 1;
}
glDirectDrawSurface3::~glDirectDrawSurface3()
{
	glDDS7->dds3 = NULL;
	glDDS7->Release();
}
HRESULT WINAPI glDirectDrawSurface3::QueryInterface(REFIID riid, void** ppvObj)
{
	return glDDS7->QueryInterface(riid,ppvObj);
}
ULONG WINAPI glDirectDrawSurface3::AddRef()
{
	refcount++;
	return refcount;
}
ULONG WINAPI glDirectDrawSurface3::Release()
{
	ULONG ret;
	refcount--;
	ret = refcount;
	if(refcount == 0) delete this;
	return ret;
}
HRESULT WINAPI glDirectDrawSurface3::AddAttachedSurface(LPDIRECTDRAWSURFACE3 lpDDSAttachedSurface)
{
	return glDDS7->AddAttachedSurface((LPDIRECTDRAWSURFACE7)lpDDSAttachedSurface);
}
HRESULT WINAPI glDirectDrawSurface3::AddOverlayDirtyRect(LPRECT lpRect)
{
	return glDDS7->AddOverlayDirtyRect(lpRect);
}
HRESULT WINAPI glDirectDrawSurface3::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE3 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
	return glDDS7->Blt(lpDestRect,((glDirectDrawSurface3*)lpDDSrcSurface)->GetDDS7(),lpSrcRect,dwFlags,lpDDBltFx);
}
HRESULT WINAPI glDirectDrawSurface3::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags)
{
	return glDDS7->BltBatch(lpDDBltBatch,dwCount,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface3::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE3 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
{
	return glDDS7->BltFast(dwX,dwY,(LPDIRECTDRAWSURFACE7)lpDDSrcSurface,lpSrcRect,dwTrans);
}
HRESULT WINAPI glDirectDrawSurface3::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE3 lpDDSAttachedSurface)
{
	return glDDS7->DeleteAttachedSurface(dwFlags,(LPDIRECTDRAWSURFACE7)lpDDSAttachedSurface);
}
HRESULT WINAPI glDirectDrawSurface3::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpEnumSurfacesCallback)
{
	return glDDS7->EnumAttachedSurfaces(lpContext,(LPDDENUMSURFACESCALLBACK7)lpEnumSurfacesCallback);
}
HRESULT WINAPI glDirectDrawSurface3::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK lpfnCallback)
{
	return glDDS7->EnumOverlayZOrders(dwFlags,lpContext,(LPDDENUMSURFACESCALLBACK7)lpfnCallback);
}
HRESULT WINAPI glDirectDrawSurface3::Flip(LPDIRECTDRAWSURFACE3 lpDDSurfaceTargetOverride, DWORD dwFlags)
{
	return glDDS7->Flip((LPDIRECTDRAWSURFACE7)lpDDSurfaceTargetOverride,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface3::GetAttachedSurface(LPDDSCAPS lpDDSCaps, LPDIRECTDRAWSURFACE3 FAR *lplpDDAttachedSurface)
{
	HRESULT error;
	glDirectDrawSurface7 *attachedsurface;
	glDirectDrawSurface3 *attached1;
	DDSCAPS2 ddscaps1;
	ddscaps1.dwCaps = lpDDSCaps->dwCaps;
	ddscaps1.dwCaps2 = ddscaps1.dwCaps3 = ddscaps1.dwCaps4 = 0;
	error = glDDS7->GetAttachedSurface(&ddscaps1,(LPDIRECTDRAWSURFACE7 FAR *)&attachedsurface);
	if(error == DD_OK)
	{
		attachedsurface->QueryInterface(IID_IDirectDrawSurface3,(void **)&attached1);
		attachedsurface->Release();
		*lplpDDAttachedSurface = attached1;
	}
	return error;
}
HRESULT WINAPI glDirectDrawSurface3::GetBltStatus(DWORD dwFlags)
{
	return glDDS7->GetBltStatus(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface3::GetCaps(LPDDSCAPS lpDDSCaps)
{
	HRESULT error;
	DDSCAPS2 ddsCaps1;
	error =  glDDS7->GetCaps(&ddsCaps1);
	ZeroMemory(&ddsCaps1,sizeof(DDSCAPS2));
	memcpy(lpDDSCaps,&ddsCaps1,sizeof(DDSCAPS));
	return error;
}
HRESULT WINAPI glDirectDrawSurface3::GetClipper(LPDIRECTDRAWCLIPPER FAR *lplpDDClipper)
{
	return glDDS7->GetClipper(lplpDDClipper);
}
HRESULT WINAPI glDirectDrawSurface3::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	return glDDS7->GetColorKey(dwFlags,lpDDColorKey);
}
HRESULT WINAPI glDirectDrawSurface3::GetDC(HDC FAR *lphDC)
{
	return glDDS7->GetDC(lphDC);
}
HRESULT WINAPI glDirectDrawSurface3::GetFlipStatus(DWORD dwFlags)
{
	return glDDS7->GetFlipStatus(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface3::GetOverlayPosition(LPLONG lplX, LPLONG lplY)
{
	return glDDS7->GetOverlayPosition(lplX,lplY);
}
HRESULT WINAPI glDirectDrawSurface3::GetPalette(LPDIRECTDRAWPALETTE FAR *lplpDDPalette)
{
	return glDDS7->GetPalette(lplpDDPalette);
}
HRESULT WINAPI glDirectDrawSurface3::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat)
{
	return glDDS7->GetPixelFormat(lpDDPixelFormat);
}
HRESULT WINAPI glDirectDrawSurface3::GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc)
{
	return glDDS7->GetSurfaceDesc((LPDDSURFACEDESC2)lpDDSurfaceDesc);
}
HRESULT WINAPI glDirectDrawSurface3::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC lpDDSurfaceDesc)
{
	FIXME("glDirectDrawSurface1::Initialize: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface3::IsLost()
{
	return glDDS7->IsLost();
}
HRESULT WINAPI glDirectDrawSurface3::Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
	return glDDS7->Lock(lpDestRect,(LPDDSURFACEDESC2)lpDDSurfaceDesc,dwFlags,hEvent);
}
HRESULT WINAPI glDirectDrawSurface3::ReleaseDC(HDC hDC)
{
	return glDDS7->ReleaseDC(hDC);
}
HRESULT WINAPI glDirectDrawSurface3::Restore()
{
	return glDDS7->Restore();
}
HRESULT WINAPI glDirectDrawSurface3::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper)
{
	return glDDS7->SetClipper(lpDDClipper);
}
HRESULT WINAPI glDirectDrawSurface3::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	return glDDS7->SetColorKey(dwFlags,lpDDColorKey);
}
HRESULT WINAPI glDirectDrawSurface3::SetOverlayPosition(LONG lX, LONG lY)
{
	return glDDS7->SetOverlayPosition(lX,lY);
}
HRESULT WINAPI glDirectDrawSurface3::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette)
{
	return glDDS7->SetPalette(lpDDPalette);
}
HRESULT WINAPI glDirectDrawSurface3::Unlock(LPVOID lpSurfaceData)
{
	return glDDS7->Unlock2(lpSurfaceData);
}
HRESULT WINAPI glDirectDrawSurface3::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE3 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx)
{
	return glDDS7->UpdateOverlay(lpSrcRect,(LPDIRECTDRAWSURFACE7)lpDDDestSurface,lpDestRect,dwFlags,lpDDOverlayFx);
}
HRESULT WINAPI glDirectDrawSurface3::UpdateOverlayDisplay(DWORD dwFlags)
{
	return glDDS7->UpdateOverlayDisplay(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface3::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE3 lpDDSReference)
{
	return glDDS7->UpdateOverlayZOrder(dwFlags,(LPDIRECTDRAWSURFACE7)lpDDSReference);
}
HRESULT WINAPI glDirectDrawSurface3::GetDDInterface(LPVOID FAR *lplpDD)
{
	return glDDS7->GetDDInterface(lplpDD);
}
HRESULT WINAPI glDirectDrawSurface3::PageLock(DWORD dwFlags)
{
	return glDDS7->PageLock(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface3::PageUnlock(DWORD dwFlags)
{
	return glDDS7->PageUnlock(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface3::SetSurfaceDesc(LPDDSURFACEDESC lpddsd, DWORD dwFlags)
{
	return glDDS7->SetSurfaceDesc((LPDDSURFACEDESC2)lpddsd,dwFlags);
}

// DDRAW4 wrapper
glDirectDrawSurface4::glDirectDrawSurface4(glDirectDrawSurface7 *gl_DDS7)
{
	glDDS7 = gl_DDS7;
	refcount = 1;
}
glDirectDrawSurface4::~glDirectDrawSurface4()
{
	glDDS7->dds4 = NULL;
	glDDS7->Release();
}
HRESULT WINAPI glDirectDrawSurface4::QueryInterface(REFIID riid, void** ppvObj)
{
	return glDDS7->QueryInterface(riid,ppvObj);
}
ULONG WINAPI glDirectDrawSurface4::AddRef()
{
	refcount++;
	return refcount;
}
ULONG WINAPI glDirectDrawSurface4::Release()
{
	ULONG ret;
	refcount--;
	ret = refcount;
	if(refcount == 0) delete this;
	return ret;
}
HRESULT WINAPI glDirectDrawSurface4::AddAttachedSurface(LPDIRECTDRAWSURFACE4 lpDDSAttachedSurface)
{
	return glDDS7->AddAttachedSurface((LPDIRECTDRAWSURFACE7)lpDDSAttachedSurface);
}
HRESULT WINAPI glDirectDrawSurface4::AddOverlayDirtyRect(LPRECT lpRect)
{
	return glDDS7->AddOverlayDirtyRect(lpRect);
}
HRESULT WINAPI glDirectDrawSurface4::Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE4 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
	return glDDS7->Blt(lpDestRect,((glDirectDrawSurface4*)lpDDSrcSurface)->GetDDS7(),lpSrcRect,dwFlags,lpDDBltFx);
}
HRESULT WINAPI glDirectDrawSurface4::BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags)
{
	return glDDS7->BltBatch(lpDDBltBatch,dwCount,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE4 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
{
	return glDDS7->BltFast(dwX,dwY,(LPDIRECTDRAWSURFACE7)lpDDSrcSurface,lpSrcRect,dwTrans);
}
HRESULT WINAPI glDirectDrawSurface4::DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE4 lpDDSAttachedSurface)
{
	return glDDS7->DeleteAttachedSurface(dwFlags,(LPDIRECTDRAWSURFACE7)lpDDSAttachedSurface);
}
HRESULT WINAPI glDirectDrawSurface4::EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK2 lpEnumSurfacesCallback)
{
	return glDDS7->EnumAttachedSurfaces(lpContext,(LPDDENUMSURFACESCALLBACK7)lpEnumSurfacesCallback);
}
HRESULT WINAPI glDirectDrawSurface4::EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK2 lpfnCallback)
{
	return glDDS7->EnumOverlayZOrders(dwFlags,lpContext,(LPDDENUMSURFACESCALLBACK7)lpfnCallback);
}
HRESULT WINAPI glDirectDrawSurface4::Flip(LPDIRECTDRAWSURFACE4 lpDDSurfaceTargetOverride, DWORD dwFlags)
{
	return glDDS7->Flip((LPDIRECTDRAWSURFACE7)lpDDSurfaceTargetOverride,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::GetAttachedSurface(LPDDSCAPS2 lpDDSCaps2, LPDIRECTDRAWSURFACE4 FAR *lplpDDAttachedSurface)
{
	HRESULT error;
	glDirectDrawSurface7 *attachedsurface;
	glDirectDrawSurface4 *attached1;
	error = glDDS7->GetAttachedSurface(lpDDSCaps2,(LPDIRECTDRAWSURFACE7 FAR *)&attachedsurface);
	if(error == DD_OK)
	{
		attachedsurface->QueryInterface(IID_IDirectDrawSurface4,(void **)&attached1);
		attachedsurface->Release();
		*lplpDDAttachedSurface = attached1;
	}
	return error;
}
HRESULT WINAPI glDirectDrawSurface4::GetBltStatus(DWORD dwFlags)
{
	return glDDS7->GetBltStatus(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::GetCaps(LPDDSCAPS2 lpDDSCaps)
{
	return glDDS7->GetCaps(lpDDSCaps);
}
HRESULT WINAPI glDirectDrawSurface4::GetClipper(LPDIRECTDRAWCLIPPER FAR *lplpDDClipper)
{
	return glDDS7->GetClipper(lplpDDClipper);
}
HRESULT WINAPI glDirectDrawSurface4::GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	return glDDS7->GetColorKey(dwFlags,lpDDColorKey);
}
HRESULT WINAPI glDirectDrawSurface4::GetDC(HDC FAR *lphDC)
{
	return glDDS7->GetDC(lphDC);
}
HRESULT WINAPI glDirectDrawSurface4::GetFlipStatus(DWORD dwFlags)
{
	return glDDS7->GetFlipStatus(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::GetOverlayPosition(LPLONG lplX, LPLONG lplY)
{
	return glDDS7->GetOverlayPosition(lplX,lplY);
}
HRESULT WINAPI glDirectDrawSurface4::GetPalette(LPDIRECTDRAWPALETTE FAR *lplpDDPalette)
{
	return glDDS7->GetPalette(lplpDDPalette);
}
HRESULT WINAPI glDirectDrawSurface4::GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat)
{
	return glDDS7->GetPixelFormat(lpDDPixelFormat);
}
HRESULT WINAPI glDirectDrawSurface4::GetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc)
{
	return glDDS7->GetSurfaceDesc(lpDDSurfaceDesc);
}
HRESULT WINAPI glDirectDrawSurface4::Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC2 lpDDSurfaceDesc)
{
	FIXME("glDirectDrawSurface1::Initialize: stub\n");
	return DDERR_GENERIC;
}
HRESULT WINAPI glDirectDrawSurface4::IsLost()
{
	return glDDS7->IsLost();
}
HRESULT WINAPI glDirectDrawSurface4::Lock(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
	return glDDS7->Lock(lpDestRect,lpDDSurfaceDesc,dwFlags,hEvent);
}
HRESULT WINAPI glDirectDrawSurface4::ReleaseDC(HDC hDC)
{
	return glDDS7->ReleaseDC(hDC);
}
HRESULT WINAPI glDirectDrawSurface4::Restore()
{
	return glDDS7->Restore();
}
HRESULT WINAPI glDirectDrawSurface4::SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper)
{
	return glDDS7->SetClipper(lpDDClipper);
}
HRESULT WINAPI glDirectDrawSurface4::SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey)
{
	return glDDS7->SetColorKey(dwFlags,lpDDColorKey);
}
HRESULT WINAPI glDirectDrawSurface4::SetOverlayPosition(LONG lX, LONG lY)
{
	return glDDS7->SetOverlayPosition(lX,lY);
}
HRESULT WINAPI glDirectDrawSurface4::SetPalette(LPDIRECTDRAWPALETTE lpDDPalette)
{
	return glDDS7->SetPalette(lpDDPalette);
}
HRESULT WINAPI glDirectDrawSurface4::Unlock(LPRECT lpRect)
{
	return glDDS7->Unlock2(lpRect);
}
HRESULT WINAPI glDirectDrawSurface4::UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE4 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx)
{
	return glDDS7->UpdateOverlay(lpSrcRect,(LPDIRECTDRAWSURFACE7)lpDDDestSurface,lpDestRect,dwFlags,lpDDOverlayFx);
}
HRESULT WINAPI glDirectDrawSurface4::UpdateOverlayDisplay(DWORD dwFlags)
{
	return glDDS7->UpdateOverlayDisplay(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE4 lpDDSReference)
{
	return glDDS7->UpdateOverlayZOrder(dwFlags,(LPDIRECTDRAWSURFACE7)lpDDSReference);
}
HRESULT WINAPI glDirectDrawSurface4::GetDDInterface(LPVOID FAR *lplpDD)
{
	return glDDS7->GetDDInterface(lplpDD);
}
HRESULT WINAPI glDirectDrawSurface4::PageLock(DWORD dwFlags)
{
	return glDDS7->PageLock(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::PageUnlock(DWORD dwFlags)
{
	return glDDS7->PageUnlock(dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::SetSurfaceDesc(LPDDSURFACEDESC2 lpddsd, DWORD dwFlags)
{
	return glDDS7->SetSurfaceDesc(lpddsd,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::SetPrivateData(REFGUID guidTag, LPVOID  lpData, DWORD   cbSize, DWORD   dwFlags)
{
	return glDDS7->SetPrivateData(guidTag,lpData,cbSize,dwFlags);
}
HRESULT WINAPI glDirectDrawSurface4::GetPrivateData(REFGUID guidTag, LPVOID  lpBuffer, LPDWORD lpcbBufferSize)
{
	return glDDS7->GetPrivateData(guidTag,lpBuffer,lpcbBufferSize);
}
HRESULT WINAPI glDirectDrawSurface4::FreePrivateData(REFGUID guidTag)
{
	return glDDS7->FreePrivateData(guidTag);
}
HRESULT WINAPI glDirectDrawSurface4::GetUniquenessValue(LPDWORD lpValue)
{
	return glDDS7->GetUniquenessValue(lpValue);
}
HRESULT WINAPI glDirectDrawSurface4::ChangeUniquenessValue()
{
	return glDDS7->ChangeUniquenessValue();
}
