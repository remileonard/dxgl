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
#include "glRenderer.h"
#include "glDirect3DDevice.h"
#include <string>
using namespace std;
#include "shadergen.h"
#include "shaders.h"

GenShader genshaders[256];
static __int64 current_shader = 0;
static __int64 current_texid[8];
static int shadercount = 0;
static int genindex = 0;
static bool initialized = false;
static bool isbuiltin = true;
GLuint current_prog;
int current_genshader;

/* Bits in Shader ID:
Bits 0-1 - Shading mode:  00=flat 01=gouraud 11=phong 10=flat per-pixel VS/FS
Bit 2 - Alpha test enable  FS
Bits 3-5 - Alpha test function:  FS
000=never  001=less  010=equal  011=lessequal
100=greater  101=notequal  110=lessequal  111=always
Bits 6-7 - Table fog:  FS
00 = none  01=exp  10=exp2  11=linear
Bits 8-9 - Vertex fog: same as table  VS
Bit 10 - Range based fog  VS/FS
Bit 11 - Specular highlights  VS/FS
Bit 12 - Stippled alpha  FS
Bit 13 - Color key transparency  FS
Bit 14-17 - Z bias  FS
Bits 18-20 - Number of lights  VS/FS
Bit 21 - Camera relative specular highlights  VS/FS
Bit 22 - Alpha blended color key  FS
Bits 23-24 - Diffuse material source  VS
Bits 25-26 - Specular material source  VS
Bits 27-28 - Ambient material source  VS
Bits 29-30 - Emissive material source  VS
Bits 31-33 - Number of textures  VS/FS
Bit 35 - Use diffuse color  VS
Bit 36 - Use specular color  VS
Bit 37 - Enable normals  VS
Bits 38-45 - Light types  VS/FS
Bits 46-48 - Number of blending weights  VS
Bit 49 - Normalize normals  VS
Bit 50 - Use transformed vertices  VS
*/

/* Bits in Texture Stage ID:
Bits 0-4: Texture color operation  FS
Bits 5-10: Texture color argument 1  FS
Bits 11-16: Texture color argument 2  FS
Bits 17-21: Texture alpha operation  FS
Bits 22-27: Texture alpha argument 1  FS
Bits 28-33: Texture alpha argument 2  FS
Bits 34-36: Texture coordinate index  VS
Bits 37-38: Texture coordinate flags  VS
Bits 39-40: U Texture address  GL
Bits 41-42: V Texture address  GL
Bits 43-45: Texture magnification filter  GL/FS
Bits 46-47: Texture minification filter  GL/FS
Bits 48-49: Texture mip filter  GL/FS
Bit 50: Enable texture coordinate transform  VS
Bits 51-52: Number of texcoord dimensions  VS
Bit 53: Projected texcoord  VS
Bits in texcoord ID:
00=2dim  01=3dim 10=4dim 11=1dim
Bits 54-56: Texture coordinate index  VS
Bits 57-58: Texture coordinate flags  VS
Bits in flags:
00=passthru 01=cameraspacenormal
10=cameraspaceposition 11=cameraspacereflectionvector
Bit 59: Texture image enabled
*/
void ZeroShaderArray()
{
	ZeroMemory(genshaders,256*sizeof(GenShader));
	current_shader = 0;
	isbuiltin = true;
}

void ClearShaders()
{
	for(int i = 0; i < shadercount; i++)
	{
		genshaders[i].id = 0;
		ZeroMemory(genshaders[i].texids,8*sizeof(__int64));
		if(genshaders[i].shader.prog) glDeleteProgram(genshaders[i].shader.prog);
		if(genshaders[i].shader.fs) glDeleteShader(genshaders[i].shader.fs);
		if(genshaders[i].shader.vs) glDeleteShader(genshaders[i].shader.vs);
		if(genshaders[i].shader.fsrc) delete genshaders[i].shader.fsrc;
		if(genshaders[i].shader.vsrc) delete genshaders[i].shader.vsrc;
		ZeroMemory(&genshaders[i].shader,sizeof(_GENSHADER));
	}
	current_genshader = -1;
	shadercount = 0;
	genindex = 0;
}

void SetShader(__int64 id, TEXTURESTAGE *texstate, int *texcoords, bool builtin)
{
	int shaderindex = -1;
	if(builtin)
	{
		if(isbuiltin && (shaders[id].prog == current_shader)) return;
		glUseProgram(shaders[id].prog);
		current_shader = shaders[id].prog;
		isbuiltin=true;
		current_genshader = -1;
	}
	else
	{
		if(!isbuiltin && (id == current_shader))
		{
			if(!memcmp(current_texid,texstate,8*sizeof(__int64))) return;
		}
		current_shader = id;
		isbuiltin=false;
		for(int i = 0; i < shadercount; i++)
		{
			if(genshaders[i].id == id)
			{
				if(!memcmp(genshaders[i].texids,texstate,8*sizeof(__int64)))
				{
					if(!memcmp(genshaders[i].texcoords,texcoords,8*sizeof(int)))
					{
						shaderindex = i;
						break;
					}
				}
			}
		}
		if(shaderindex == -1)
		{
			shadercount++;
			if(shadercount > 256) shadercount = 256;
			if(genshaders[genindex].shader.prog)
			{
				glUseProgram(0);
				glDeleteProgram(genshaders[genindex].shader.prog);
				glDeleteShader(genshaders[genindex].shader.vs);
				glDeleteShader(genshaders[genindex].shader.fs);
				delete genshaders[genindex].shader.vsrc;
				delete genshaders[genindex].shader.fsrc;
				ZeroMemory(&genshaders[genindex],sizeof(GenShader));
			}
			CreateShader(genindex,id,texstate,texcoords);
			shaderindex = genindex;
			genindex++;
			if(genindex >= 256) genindex = 0;
		}
		genshaders[shaderindex].id = id;
		memcpy(genshaders[shaderindex].texids,texstate,8*sizeof(__int64));
		memcpy(genshaders[shaderindex].texcoords,texcoords,8*sizeof(int));
		glUseProgram(genshaders[shaderindex].shader.prog);
		current_prog = genshaders[shaderindex].shader.prog;
		current_genshader = shaderindex;
	}
}

GLuint GetProgram()
{
	if(isbuiltin) return current_shader & 0xFFFFFFFF;
	else return current_prog;
}


#define REVISION 1
static const char header[] =
	"//REV" STR(REVISION) "\n\
#version 110\n";
static const char vertexshader[] = "//Vertex Shader\n";
static const char fragshader[] = "//Fragment Shader\n";
static const char idheader[] = "//ID: 0x";
static const char linefeed[] = "\n";
static const char mainstart[] = "void main()\n{\n";
static const char mainend[] = "} ";
// Attributes
static const char attr_xyz[] = "attribute vec3 xyz;\n";
static const char attr_rhw[] = "attribute float rhw;\n";
static const char attr_nxyz[] = "attribute vec3 nxyz;\n";
static const char const_nxyz[] = "const vec3 nxyz = vec3(0,0,0);\n";
static const char attr_blend[] = "attribute float blendX;\n";
static const char attr_rgba[] = "attribute vec4 rgbaX;\n";
static const char attr_s[] = "attribute float sX;\n";
static const char conv_s[] = "vec4 strqX = vec4(sX,0,0,1);\n";
static const char attr_st[] = "attribute vec2 stX;\n";
static const char conv_st[] = "vec4 strqX = vec4(stX[0],stX[1],0,1);\n";
static const char attr_str[] = "attribute vec3 strX;\n";
static const char conv_str[] = "vec4 strqX = vec4(strX[0],strX[1],strX[2],1);\n";
static const char attr_strq[] = "attribute vec4 strqX;\n";
// Uniforms
static const char unif_matrices[] = "uniform mat4 world;\n\
uniform mat4 view;\n\
uniform mat4 projection;\n\
uniform mat3 normalmat;\n";
static const char unif_material[] = "struct Material\n\
{\n\
vec4 diffuse;\n\
vec4 ambient;\n\
vec4 specular;\n\
vec4 emissive;\n\
float power;\n\
};\n\
uniform Material material;\n";
static const char lightstruct[] = "struct Light\n\
{\n\
vec4 diffuse;\n\
vec4 specular;\n\
vec4 ambient;\n\
vec3 position;\n\
vec3 direction;\n\
float range;\n\
float falloff;\n\
float constant;\n\
float linear;\n\
float quad;\n\
float theta;\n\
float phi;\n\
};\n";
static const char unif_light[] = "uniform Light lightX;\n";
static const char unif_ambient[] = "uniform vec4 ambientcolor;\n";
static const char unif_tex[] = "uniform sampler2D texX;\n";
// Variables
static const char var_common[] = "vec4 diffuse;\n\
vec4 specular;\n\
vec4 ambient;\n\
vec3 N;";
static const char var_color[] = "vec3 color;\n\
float alpha;\n";
static const char var_xyzw[] = "vec4 xyzw;\n";
// Operations
static const char op_transform[] = "xyzw = vec4(xyz,1);\n\
vec4 pos = (projection*(view*world))*xyzw;\n\
gl_Position = vec4(pos.x,-pos.y,pos.z,pos.w);\n";
static const char op_normalize[] = "N = normalize(normalmat*nxyz);\n";
static const char op_normalpassthru[] = "N = normalmat*nxyz;\n";
static const char op_passthru[] = "gl_Position = xyzw;\n";
static const char op_resetcolor[] = "diffuse = specular = vec4(0.0);\n\
ambient = ambientcolor / 255.0;\n";
static const char op_dirlight[] = "DirLight(lightX);\n";
static const char op_dirlightnospecular[] = "DirLightNoSpecular(lightX);\n";
static const char op_spotlight[] = "SpotLight(lightX);\n";
static const char op_colorout[] = "gl_FrontColor = (material.diffuse * diffuse) + (material.ambient * ambient) + material.emissive;\n\
gl_FrontSecondaryColor = (material.specular * specular);\n";
static const char op_colorfragout[] = "gl_FragColor = vec4(color,alpha);\n";
static const char op_colorfragin[] = "color = gl_Color.rgb;\n\
alpha = gl_Color.a;\n";
static const char op_texpassthru1[] = "gl_TexCoord[x] = ";
static const char op_texpassthru2s[] = "vec4(sX,0,0,1);\n";
static const char op_texpassthru2st[] = "vec4(stX,0,1);\n";
static const char op_texpassthru2str[] = "vec4(strX,1);\n";
static const char op_texpassthru2strq[] = "strqX;\n";
static const char op_texpassthru2null[] = "vec4(0,0,0,1);\n";

// Functions
static const char func_dirlight[] = "void DirLight(in Light light)\n\
{\n\
float NdotHV = 0.0;\n\
vec3 dir = normalize(-light.direction);\n\
ambient += light.ambient;\n\
float NdotL = max(dot(N,dir),0.0);\n\
diffuse += light.diffuse*NdotL;\n\
if(NdotL > 0.0)\n\
{\n\
vec3 eye = (-view[3].xyz / view[3].w);\n\
vec3 P = vec3((view*world)*xyzw);\n\
vec3 L = normalize(-light.direction.xyz - P);\n\
vec3 V = normalize(eye - P);\n\
NdotHV = max(dot(N,L+V),0.0);\n\
specular += (pow(NdotHV,float(material.power))*light.specular);\n\
ambient += light.ambient;\n\
}\n\
}\n";
static const char func_dirlightnospecular[] = "void DirLightNoSpecular(in Light light)\n\
{\n\
float NdotHV = 0.0;\n\
vec3 dir = normalize(-light.direction);\n\
ambient += light.ambient;\n\
float NdotL = max(dot(N,dir),0.0);\n\
diffuse += light.diffuse*NdotL;\n\
}\n";

void CreateShader(int index, __int64 id, TEXTURESTAGE *texstate, int *texcoords)
{
	string tmp;
	int i;
	bool hasdir = false;
	bool hasspot = false;
	int count;
	int numlights;
	char idstring[22];
	_snprintf(idstring,21,"%0.16I64X\n",id);
	idstring[21] = 0;
	genshaders[index].shader.vsrc = new string;
	genshaders[index].shader.fsrc = new string;
	// Create vertex shader
	//Header
	string *vsrc = genshaders[index].shader.vsrc;
	vsrc->append(header);
	vsrc->append(vertexshader);
	vsrc->append(idheader);
	vsrc->append(idstring);
	// Attributes
	vsrc->append(attr_xyz);
	if((id>>50)&1) vsrc->append(attr_rhw);
	tmp = attr_rgba;
	if((id>>35)&1)
	{
		tmp.replace(19,1,"0");
		vsrc->append(tmp);
	}
	if((id>>36)&1)
	{
		tmp.replace(19,1,"1");
		vsrc->append(tmp);
	}
	if((id>>37)&1) vsrc->append(attr_nxyz);
	else vsrc->append(const_nxyz);
	count = (id>>46)&7;
	if(count)
	{
		tmp = attr_blend;
		for(i = 0; i < count; i++)
		{
			tmp.replace(21,1,_itoa(i,idstring,10));
			vsrc->append(tmp);
		}
	}
	for(i = 0; i < 8; i++)
	{
		switch(texcoords[i])
		{
		case -1:
			continue;
		case 3:
			tmp = attr_s;
			tmp.replace(16,1,_itoa(i,idstring,10));
			break;
		case 0:
			tmp = attr_st;
			tmp.replace(17,1,_itoa(i,idstring,10));
			break;
		case 1:
			tmp = attr_str;
			tmp.replace(18,1,_itoa(i,idstring,10));
			break;
		case 2:
			tmp = attr_strq;
			tmp.replace(19,1,_itoa(i,idstring,10));
			break;
		}
		vsrc->append(tmp);
	}

	// Uniforms
	vsrc->append(unif_matrices); // Material
	vsrc->append(unif_material);
	vsrc->append(unif_ambient);
	numlights = (id>>18)&7;
	if(numlights) // Lighting
	{
		vsrc->append(lightstruct);
		tmp = unif_light;
		for(i = 0; i < numlights; i++)
		{
			tmp.replace(19,1,_itoa(i,idstring,10));
			vsrc->append(tmp);
		}
	}
	
	// Variables
	vsrc->append(var_common);
	if(!((id>>50)&1)) vsrc->append(var_xyzw);

	// Functions
	if(numlights)
	{
		for(i = 0; i < numlights; i++)
		{
			if(id>>(38+i)&1) hasspot = true;
			else hasdir = true;
		}
	}
	bool hasspecular = (id >> 11) & 1;
	if(hasspot) FIXME("Add spot lights");
	if(hasdir)
	{
		if(hasspecular) vsrc->append(func_dirlight);
		else vsrc->append(func_dirlightnospecular);
	}
	//Main
	vsrc->append(mainstart);
	if((id>>50)&1) vsrc->append(op_passthru);
	else vsrc->append(op_transform);
	vsrc->append(op_resetcolor);
	if((id>>49)&1) vsrc->append(op_normalize);
	else vsrc->append(op_normalpassthru);
	if(numlights)
	{
		for(i = 0; i < numlights; i++)
		{
			if(id>>(38+i)&1)
			{
				tmp = op_spotlight;
				tmp.replace(15,1,_itoa(i,idstring,10));
				vsrc->append(tmp);
			}
			else
			{
				if(hasspecular)
				{
					tmp = op_dirlight;
					tmp.replace(14,1,_itoa(i,idstring,10));
					vsrc->append(tmp);
				}
				else
				{
					tmp = op_dirlightnospecular;
					tmp.replace(24,1,_itoa(i,idstring,10));
					vsrc->append(tmp);
				}
			}
		}
	}
	vsrc->append(op_colorout);
	int texindex;
	for(i = 0; i < 8; i++)
	{
		if((texstate[i].shaderid>>50)&1)
		{
			FIXME("Support texture coordinate transform");
		}
		else
		{
			tmp = op_texpassthru1;
			tmp.replace(12,1,_itoa(i,idstring,10));
			vsrc->append(tmp);
			texindex = (texstate[i].shaderid>>54)&3;
			switch(texcoords[texindex])
			{
			case -1: // No texcoords
				vsrc->append(op_texpassthru2null);
				break;
			case 0: // st
				tmp = op_texpassthru2st;
				tmp.replace(7,1,_itoa(texindex,idstring,10));
				vsrc->append(tmp);
			default:
				break;
			case 1: // str
				tmp = op_texpassthru2str;
				tmp.replace(8,1,_itoa(texindex,idstring,10));
				vsrc->append(tmp);
				break;
			case 2: // strq
				tmp = op_texpassthru2strq;
				tmp.replace(4,1,_itoa(texindex,idstring,10));
				vsrc->append(tmp);
				break;
			case 3: // s
				tmp = op_texpassthru2s;
				tmp.replace(6,1,_itoa(texindex,idstring,10));
				vsrc->append(tmp);
				break;
			}
		}
	}
	vsrc->append(mainend);
#ifdef _DEBUG
	OutputDebugStringA("Vertex shader:\n");
	OutputDebugStringA(vsrc->c_str());
	OutputDebugStringA("\nCompiling vertex shader:\n");
#endif
	genshaders[index].shader.vs = glCreateShader(GL_VERTEX_SHADER);
	const char *src = vsrc->c_str();
	GLint srclen = strlen(src);
	glShaderSource(genshaders[index].shader.vs,1,&src,&srclen);
	glCompileShader(genshaders[index].shader.vs);
	GLint loglen,result;
	char *infolog = NULL;
	glGetShaderiv(genshaders[index].shader.vs,GL_COMPILE_STATUS,&result);
#ifdef _DEBUG
	if(!result)
	{
		glGetShaderiv(genshaders[index].shader.vs,GL_INFO_LOG_LENGTH,&loglen);
		infolog = (char*)malloc(loglen);
		glGetShaderInfoLog(genshaders[index].shader.vs,loglen,&result,infolog);
		OutputDebugStringA("Compilation failed. Error messages:\n");
		OutputDebugStringA(infolog);
		free(infolog);
	}
#endif
	// Create fragment shader
	string *fsrc = genshaders[index].shader.fsrc;
	fsrc->append(header);
	fsrc->append(fragshader);
	_snprintf(idstring,21,"%0.16I64X\n",id);
	idstring[21] = 0;
	fsrc->append(idheader);
	fsrc->append(idstring);
	// Uniforms
	for(i = 0; i < 8; i++)
	{
		if((texstate[i].shaderid & 31) == D3DTOP_DISABLE)break;
		tmp = unif_tex;
		tmp.replace(21,1,_itoa(i,idstring,10));
		fsrc->append(tmp);
	}
	// Variables
	fsrc->append(var_color);
	// Functions
	// Main
	fsrc->append(mainstart);
	fsrc->append(op_colorfragin);
	string arg1,arg2;
	int args[4];
	bool texfail;
	const string blendargs[] = {"color","gl_Color","texture2DProj(texX,gl_TexCoord[Y]).rgb",
		"texture2DProj(texX,gl_TexCoord[Y]).a","texfactor","gl_SecondaryColor","vec3(1,1,1)","1",".rgb",".a"};
	for(i = 0; i < 8; i++)
	{
		if((texstate[i].shaderid & 31) == D3DTOP_DISABLE)break;
		// Color stage
		texfail = false;
		args[0] = (texstate[i].shaderid>>5)&63;
		switch(args[0]&7) //arg1
		{
			case D3DTA_CURRENT:
			default:
				arg1 = blendargs[0];
				break;
			case D3DTA_DIFFUSE:
				arg1 = blendargs[1]+blendargs[8];
				break;
			case D3DTA_TEXTURE:
				if((texstate[i].shaderid >> 59)&1)
				{
					arg1 = blendargs[2];
					arg1.replace(17,1,_itoa(i,idstring,10));
					arg1.replace(31,1,_itoa((texstate[i].shaderid>>54)&7,idstring,10));
				}
				else texfail = true;
				break;
			case D3DTA_TFACTOR:
				FIXME("Support texture factor value");
				arg1 = blendargs[4];
				break;
			case D3DTA_SPECULAR:
				arg1 = blendargs[5]+blendargs[8];
				break;
		}
		if(args[0] & D3DTA_COMPLEMENT)
			arg1 = "(1.0 - " + arg1 + ")";
		args[1] = (texstate[i].shaderid>>11)&63;
		switch(args[1]&7) //arg2
		{
			case D3DTA_CURRENT:
			default:
				arg2 = blendargs[0];
				break;
			case D3DTA_DIFFUSE:
				arg2 = blendargs[1]+blendargs[8];
				break;
			case D3DTA_TEXTURE:
				if((texstate[i].shaderid >> 59)&1)
				{
					arg2 = blendargs[3];
					arg2.replace(17,1,_itoa(i,idstring,10));
					arg2.replace(31,1,_itoa((texstate[i].shaderid>>54)&7,idstring,10));
				}
				else texfail = true;
				break;
			case D3DTA_TFACTOR:
				FIXME("Support texture factor value");
				arg2 = blendargs[4];
				break;
			case D3DTA_SPECULAR:
				arg2 = blendargs[5]+blendargs[8];
				break;
		}
		if(args[1] & D3DTA_COMPLEMENT)
			arg2 = "(1.0 - " + arg2 + ")";
		if(!texfail) switch(texstate[i].shaderid & 31)
		{
		case D3DTOP_DISABLE:
		default:
			break;
		case D3DTOP_SELECTARG1:
			fsrc->append("color = " + arg1 + ";\n");
			break;
		case D3DTOP_SELECTARG2:
			fsrc->append("color = " + arg2 + ";\n");
			break;
		case D3DTOP_MODULATE:
			fsrc->append("color = " + arg1 + " * " + arg2 + ";\n");
			break;
		case D3DTOP_MODULATE2X:
			fsrc->append("color = (" + arg1 + " * " + arg2 + ") * 2.0;\n");
			break;
		case D3DTOP_MODULATE4X:
			fsrc->append("color = (" + arg1 + " * " + arg2 + ") * 4.0;\n");
			break;
		case D3DTOP_ADD:
			fsrc->append("color = " + arg1 + " + " + arg2 + ";\n");
			break;
		}
	}
	fsrc->append(op_colorfragout);
	fsrc->append(mainend);
#ifdef _DEBUG
	OutputDebugStringA("Fragment shader:\n");
	OutputDebugStringA(fsrc->c_str());
	OutputDebugStringA("\nCompiling fragment shader:\n");
#endif
	genshaders[index].shader.fs = glCreateShader(GL_FRAGMENT_SHADER);
	src = fsrc->c_str();
	srclen = strlen(src);
	glShaderSource(genshaders[index].shader.fs,1,&src,&srclen);
	glCompileShader(genshaders[index].shader.fs);
	glGetShaderiv(genshaders[index].shader.fs,GL_COMPILE_STATUS,&result);
#ifdef _DEBUG
	if(!result)
	{
		glGetShaderiv(genshaders[index].shader.fs,GL_INFO_LOG_LENGTH,&loglen);
		infolog = (char*)malloc(loglen);
		glGetShaderInfoLog(genshaders[index].shader.fs,loglen,&result,infolog);
		OutputDebugStringA("Compilation failed. Error messages:\n");
		OutputDebugStringA(infolog);
		free(infolog);
	}
	OutputDebugStringA("\nLinking program:\n");
#endif
	genshaders[index].shader.prog = glCreateProgram();
	glAttachShader(genshaders[index].shader.prog,genshaders[index].shader.vs);
	glAttachShader(genshaders[index].shader.prog,genshaders[index].shader.fs);
	glLinkProgram(genshaders[index].shader.prog);
	glGetProgramiv(genshaders[index].shader.prog,GL_LINK_STATUS,&result);
#ifdef _DEBUG
	if(!result)
	{
		glGetProgramiv(genshaders[index].shader.prog,GL_INFO_LOG_LENGTH,&loglen);
		infolog = (char*)malloc(loglen);
		glGetProgramInfoLog(genshaders[index].shader.prog,loglen,&result,infolog);
		OutputDebugStringA("Program link failed. Error messages:\n");
		OutputDebugStringA(infolog);
		free(infolog);
	}
#endif
	// Attributes
	genshaders[index].shader.attribs[0] = glGetAttribLocation(genshaders[index].shader.prog,"xyz");
	genshaders[index].shader.attribs[1] = glGetAttribLocation(genshaders[index].shader.prog,"rhw");
	genshaders[index].shader.attribs[2] = glGetAttribLocation(genshaders[index].shader.prog,"blend0");
	genshaders[index].shader.attribs[3] = glGetAttribLocation(genshaders[index].shader.prog,"blend1");
	genshaders[index].shader.attribs[4] = glGetAttribLocation(genshaders[index].shader.prog,"blend2");
	genshaders[index].shader.attribs[5] = glGetAttribLocation(genshaders[index].shader.prog,"blend3");
	genshaders[index].shader.attribs[6] = glGetAttribLocation(genshaders[index].shader.prog,"blend4");
	genshaders[index].shader.attribs[7] = glGetAttribLocation(genshaders[index].shader.prog,"nxyz");
	genshaders[index].shader.attribs[8] = glGetAttribLocation(genshaders[index].shader.prog,"rgba0");
	genshaders[index].shader.attribs[9] = glGetAttribLocation(genshaders[index].shader.prog,"rgba1");
	char attrS[] = "sX";
	for(int i = 0; i < 8; i++)
	{
		attrS[1] = i + '0';
		genshaders[index].shader.attribs[i+10] = glGetAttribLocation(genshaders[index].shader.prog,attrS);
	}
	char attrST[] = "stX";
	for(int i = 0; i < 8; i++)
	{
		attrST[2] = i + '0';
		genshaders[index].shader.attribs[i+18] = glGetAttribLocation(genshaders[index].shader.prog,attrST);
	}
	char attrSTR[] = "strX";
	for(int i = 0; i < 8; i++)
	{
		attrSTR[3] = i + '0';
		genshaders[index].shader.attribs[i+26] = glGetAttribLocation(genshaders[index].shader.prog,attrSTR);
	}
	char attrSTRQ[] = "strqX";
	for(int i = 0; i < 8; i++)
	{
		attrSTRQ[4] = i + '0';
		genshaders[index].shader.attribs[i+34] = glGetAttribLocation(genshaders[index].shader.prog,attrSTRQ);
	}
	// Uniforms
	genshaders[index].shader.uniforms[0] = glGetUniformLocation(genshaders[index].shader.prog,"world");
	genshaders[index].shader.uniforms[1] = glGetUniformLocation(genshaders[index].shader.prog,"view");
	genshaders[index].shader.uniforms[2] = glGetUniformLocation(genshaders[index].shader.prog,"projection");
	genshaders[index].shader.uniforms[3] = glGetUniformLocation(genshaders[index].shader.prog,"normalmat");
	// TODO: 4-14 world1-3 and texture0-7
	genshaders[index].shader.uniforms[15] = glGetUniformLocation(genshaders[index].shader.prog,"material.ambient");
	genshaders[index].shader.uniforms[16] = glGetUniformLocation(genshaders[index].shader.prog,"material.diffuse");
	genshaders[index].shader.uniforms[17] = glGetUniformLocation(genshaders[index].shader.prog,"material.specular");
	genshaders[index].shader.uniforms[18] = glGetUniformLocation(genshaders[index].shader.prog,"material.emissive");
	genshaders[index].shader.uniforms[19] = glGetUniformLocation(genshaders[index].shader.prog,"material.power");
	char uniflight[] = "lightX.            ";
	for(int i = 0; i < 8; i++)
	{
		uniflight[5] = i + '0';
		strcpy(uniflight+7,"diffuse");
		genshaders[index].shader.uniforms[20+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"specular");
		genshaders[index].shader.uniforms[21+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"ambient");
		genshaders[index].shader.uniforms[22+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"position");
		genshaders[index].shader.uniforms[23+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"direction");
		genshaders[index].shader.uniforms[24+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"range");
		genshaders[index].shader.uniforms[25+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"falloff");
		genshaders[index].shader.uniforms[26+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"constant");
		genshaders[index].shader.uniforms[27+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"linear");
		genshaders[index].shader.uniforms[28+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"quad");
		genshaders[index].shader.uniforms[29+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"theta");
		genshaders[index].shader.uniforms[30+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
		strcpy(uniflight+7,"phi");
		genshaders[index].shader.uniforms[31+(i*12)] = glGetUniformLocation(genshaders[index].shader.prog,uniflight);
	}
	char uniftex[] = "texX";
	for(int i = 0; i < 8; i++)
	{
		uniftex[3] = i + '0';
		genshaders[index].shader.uniforms[128+i] = glGetUniformLocation(genshaders[index].shader.prog,uniftex);
	}
	genshaders[index].shader.uniforms[136] = glGetUniformLocation(genshaders[index].shader.prog,"ambientcolor");
}