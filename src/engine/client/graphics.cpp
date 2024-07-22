/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/detect.h>
#include <base/math.h>

#include <base/system.h>
#include <engine/external/pnglite/pnglite.h>

#include <engine/shared/config.h>
#include <engine/graphics.h>
#include <engine/storage.h>
#include <engine/keys.h>
#include <engine/console.h>

#include <math.h> // cosf, sinf

#include "graphics.h"
#include "colored_shbin.h"
#include "textured_shbin.h"

#define GL_MAX_TEXTURE_SIZE 256
#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static C3D_Mtx projection;

struct Shader {
	DVLB_s* dvlb;
	shaderProgram_s program;
	int uf_projection;
};
static Shader* currShader;
static Shader shaders[2];
static C3D_RenderTarget* bottomTarget;
static int m_CurrVertices = 0;
static int m_StartVertex = 0;

void CGraphics_3DS::Flush()
{
	if(m_CurrVertices == 0)
		return;

	if(m_RenderEnable)
	{
		int ThisBatch = m_NumVertices - m_StartVertex;
		C3D_DrawArrays(GPU_TRIANGLES, m_StartVertex, ThisBatch);
		m_StartVertex = m_NumVertices;
	}

	// Reset pointer
	m_CurrVertices = 0;
}

void CGraphics_3DS::AddVertices(int Count)
{
	m_NumVertices += Count;
	m_CurrVertices += Count;
	if((m_NumVertices + Count) >= MAX_VERTICES)
	{
		Flush();
		Swap();
	}
}

void CGraphics_3DS::Rotate(const CPoint &rCenter, CVertex *pPoints, int NumPoints)
{
	float c = cosf(m_Rotation);
	float s = sinf(m_Rotation);
	float x, y;
	int i;

	for(i = 0; i < NumPoints; i++)
	{
		x = pPoints[i].m_Pos.x - rCenter.x;
		y = pPoints[i].m_Pos.y - rCenter.y;
		pPoints[i].m_Pos.x = x * c - y * s + rCenter.x;
		pPoints[i].m_Pos.y = x * s + y * c + rCenter.y;
	}
}

unsigned char CGraphics_3DS::Sample(int w, int h, const unsigned char *pData, int u, int v, int Offset, int ScaleW, int ScaleH, int Bpp)
{
	int Value = 0;
	for(int x = 0; x < ScaleW; x++)
		for(int y = 0; y < ScaleH; y++)
			Value += pData[((v+y)*w+(u+x))*Bpp+Offset];
	return Value/(ScaleW*ScaleH);
}

unsigned char *CGraphics_3DS::Rescale(int Width, int Height, int NewWidth, int NewHeight, int Format, const unsigned char *pData)
{
	unsigned char *pTmpData;
	int ScaleW = Width/NewWidth;
	int ScaleH = Height/NewHeight;

	int Bpp = 3;
	if(Format == CImageInfo::FORMAT_RGBA)
		Bpp = 4;

	pTmpData = (unsigned char *)mem_alloc(NewWidth*NewHeight*Bpp, 1);

	int c = 0;
	for(int y = 0; y < NewHeight; y++)
		for(int x = 0; x < NewWidth; x++, c++)
		{
			pTmpData[c*Bpp] = Sample(Width, Height, pData, x*ScaleW, y*ScaleH, 0, ScaleW, ScaleH, Bpp);
			pTmpData[c*Bpp+1] = Sample(Width, Height, pData, x*ScaleW, y*ScaleH, 1, ScaleW, ScaleH, Bpp);
			pTmpData[c*Bpp+2] = Sample(Width, Height, pData, x*ScaleW, y*ScaleH, 2, ScaleW, ScaleH, Bpp);
			if(Bpp == 4)
				pTmpData[c*Bpp+3] = Sample(Width, Height, pData, x*ScaleW, y*ScaleH, 3, ScaleW, ScaleH, Bpp);
		}

	return pTmpData;
}

CGraphics_3DS::CGraphics_3DS()
{
	m_NumVertices = 0;

	m_ScreenX0 = 0;
	m_ScreenY0 = 0;
	m_ScreenX1 = 0;
	m_ScreenY1 = 0;

	m_ScreenWidth = -1;
	m_ScreenHeight = -1;

	m_Rotation = 0;
	m_Drawing = 0;
	m_InvalidTexture = 0;

	m_TextureMemoryUsage = 0;

	m_RenderEnable = true;
	m_DoScreenshot = false;
}

void CGraphics_3DS::ClipEnable(int x, int y, int w, int h)
{
	//C3D_SetScissor(GPU_SCISSOR_NORMAL, x, y, x+w, y+h);
}

void CGraphics_3DS::ClipDisable()
{
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0,0,0,0);
}

void CGraphics_3DS::BlendNone()
{
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_MAX, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
}

void CGraphics_3DS::BlendNormal()
{
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

void CGraphics_3DS::BlendAdditive()
{
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
}

void CGraphics_3DS::WrapNormal()
{
	
}

void CGraphics_3DS::WrapClamp()
{
	
}

int CGraphics_3DS::MemoryUsage() const
{
	return m_TextureMemoryUsage;
}

void CGraphics_3DS::MapScreen(float TopLeftX, float TopLeftY, float BottomRightX, float BottomRightY)
{
	m_ScreenX0 = TopLeftX;
	m_ScreenY0 = TopLeftY;
	m_ScreenX1 = BottomRightX;
	m_ScreenY1 = BottomRightY;

	Mtx_OrthoTilt(&projection, m_ScreenX0, m_ScreenX1, m_ScreenY1, m_ScreenY0, -10.0f, 10.0f, true);
	for (int i=0; i<2; i++)
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, shaders[i].uf_projection, &projection);
}

void CGraphics_3DS::GetScreen(float *pTopLeftX, float *pTopLeftY, float *pBottomRightX, float *pBottomRightY)
{
	*pTopLeftX = m_ScreenX0;
	*pTopLeftY = m_ScreenY0;
	*pBottomRightX = m_ScreenX1;
	*pBottomRightY = m_ScreenY1;
}

void CGraphics_3DS::LinesBegin()
{
	dbg_assert(m_Drawing == 0, "called Graphics()->LinesBegin twice");
	m_Drawing = DRAWING_LINES;
	SetColor(1,1,1,1);
}

void CGraphics_3DS::LinesEnd()
{
	dbg_assert(m_Drawing == DRAWING_LINES, "called Graphics()->LinesEnd without begin");
	Flush();
	m_Drawing = 0;
}

void CGraphics_3DS::LinesDraw(const CLineItem *pArray, int Num)
{
	dbg_assert(m_Drawing == DRAWING_LINES, "called Graphics()->LinesDraw without begin");

	for(int i = 0; i < Num; ++i)
	{
		m_aVertices[m_NumVertices + 2*i].m_Pos.x = pArray[i].m_X0;
		m_aVertices[m_NumVertices + 2*i].m_Pos.y = pArray[i].m_Y0;
		m_aVertices[m_NumVertices + 2*i].m_Tex = m_aTexture[0];
		m_aVertices[m_NumVertices + 2*i].m_Color = m_aColor[0];

		m_aVertices[m_NumVertices + 2*i + 1].m_Pos.x = pArray[i].m_X1;
		m_aVertices[m_NumVertices + 2*i + 1].m_Pos.y = pArray[i].m_Y1;
		m_aVertices[m_NumVertices + 2*i + 1].m_Tex = m_aTexture[1];
		m_aVertices[m_NumVertices + 2*i + 1].m_Color = m_aColor[1];

		m_aVertices[m_NumVertices + 2*i + 2].m_Pos.x = pArray[i].m_X1;
		m_aVertices[m_NumVertices + 2*i + 2].m_Pos.y = pArray[i].m_Y1;
		m_aVertices[m_NumVertices + 2*i + 2].m_Tex = m_aTexture[1];
		m_aVertices[m_NumVertices + 2*i + 2].m_Color = m_aColor[1];
	}

	AddVertices(3*Num);
}

int CGraphics_3DS::UnloadTexture(int Index)
{
	if(Index == m_InvalidTexture)
		return 0;

	if(Index < 0)
		return 0;

	C3D_TexDelete(&m_aTextures[Index].m_Tex);
	m_aTextures[Index].m_Next = m_FirstFreeTexture;
	m_TextureMemoryUsage -= m_aTextures[Index].m_MemSize;
	m_FirstFreeTexture = Index;
	return 0;
}

static inline u32 CalcZOrder(u32 a)
{
	// Simplified "Interleave bits by Binary Magic Numbers" from
	// http://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN
	a = (a | (a << 2)) & 0x33;
	a = (a | (a << 1)) & 0x55;
	return a;
	// equivalent to return (a & 1) | ((a & 2) << 1) | (a & 4) << 2;
	//  but compiles to less instructions
}

// Pixels are arranged in a recursive Z-order curve / Morton offset
// They are arranged into 8x8 tiles, where each 8x8 tile is composed of
//  four 4x4 subtiles, which are in turn composed of four 2x2 subtiles
static void ToMortonTexture(C3D_Tex* tex, u32* dst, u32* src, int originX, int originY, int width, int height)
{
	unsigned int pixel, mortonX, mortonY;
	unsigned int dstX, dstY, tileX, tileY;

	for (int y = 0; y < height; y++)
	{
		dstY    = tex->height - 1 - (y + originY);
		tileY   = dstY & ~0x07;
		mortonY = CalcZOrder(dstY & 0x07) << 1;

		for (int x = 0; x < width; x++)
		{
			dstX    = x + originX;
			tileX   = dstX & ~0x07;
			mortonX = CalcZOrder(dstX & 0x07);
			pixel   = src[x + (y * width)];

			u8 r = pixel & 0xff;
			u8 g = (pixel >> 8) & 0xff;
			u8 b = (pixel >> 16) & 0xff;
			u8 a = (pixel >> 24) & 0xff;
			pixel = (a<<0) | (b<<8) | (g<<16) | (r<<24);

			dst[(mortonX | mortonY) + (tileX * 8) + (tileY * tex->width)] = pixel;
		}
	}
}

int CGraphics_3DS::LoadTextureRawSub(int TextureID, int x, int y, int Width, int Height, int Format, const void *pData)
{
    return 0;
}

int CGraphics_3DS::LoadTextureRaw(int Width, int Height, int Format, const void *pData, int StoreFormat, int Flags)
{
	u8* pTexData = (u8*)pData;
	u8* pTmpData = 0;

	int Tex = 0;

	// don't waste memory on texture if we are stress testing
	if(g_Config.m_DbgStress)
		return 	m_InvalidTexture;

	// grab texture
	Tex = m_FirstFreeTexture;
	m_FirstFreeTexture = m_aTextures[Tex].m_Next;
	m_aTextures[Tex].m_Next = -1;

	if(!(Flags&TEXLOAD_NORESAMPLE) && (Format == CImageInfo::FORMAT_RGBA || Format == CImageInfo::FORMAT_RGB))
	{
		if(Width > GL_MAX_TEXTURE_SIZE || Height > GL_MAX_TEXTURE_SIZE)
		{
			int NewWidth = min(Width, GL_MAX_TEXTURE_SIZE);
			float div = NewWidth/(float)Width;
			int NewHeight = Height * div;
			pTmpData = Rescale(Width, Height, NewWidth, NewHeight, Format, pTexData);
			pTexData = pTmpData;
			Width = NewWidth;
			Height = NewHeight;
		}
		else if(Width > 16 && Height > 16 && g_Config.m_GfxTextureQuality == 0)
		{
			pTmpData = Rescale(Width, Height, Width/2, Height/2, Format, pTexData);
			pTexData = pTmpData;
			Width /= 2;
			Height /= 2;
		}
	}

	int PixelSize = 4;
	if(StoreFormat == CImageInfo::FORMAT_RGB)
		PixelSize = 3;

	C3D_Tex* cTex = &m_aTextures[Tex].m_Tex;
	C3D_TexInit(cTex, Width, Height, (PixelSize==4) ? GPU_RGBA8 : GPU_RGB8);
	u32* data = (u32*)malloc(Width*Height*PixelSize);
	if (!data)
	{
		if (pTmpData) mem_free(pTmpData);
		return m_InvalidTexture;
	}

	ToMortonTexture(cTex, data, (u32*)pTexData, 0, 0, Width, Height);
	if (pTmpData) mem_free(pTmpData);
	C3D_TexSetFilter(cTex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexUpload(cTex, data);
	free(data);

	// calculate memory usage
	{
		m_aTextures[Tex].m_MemSize = Width*Height*PixelSize;
	}

	m_TextureMemoryUsage += m_aTextures[Tex].m_MemSize;
	return Tex;
}

// simple uncompressed RGBA loaders
int CGraphics_3DS::LoadTexture(const char *pFilename, int StorageType, int StoreFormat, int Flags)
{
	int l = str_length(pFilename);
	int ID;
	CImageInfo Img;

	if(l < 3)
		return m_InvalidTexture;
	if(LoadPNG(&Img, pFilename, StorageType))
	{
		if (StoreFormat == CImageInfo::FORMAT_AUTO)
			StoreFormat = Img.m_Format;

		ID = LoadTextureRaw(Img.m_Width, Img.m_Height, Img.m_Format, Img.m_pData, StoreFormat, Flags);
		mem_free(Img.m_pData);
		if(ID != m_InvalidTexture && g_Config.m_Debug)
			dbg_msg("graphics/texture", "loaded %s", pFilename);
		return ID;
	}

	return m_InvalidTexture;
}

int CGraphics_3DS::LoadPNG(CImageInfo *pImg, const char *pFilename, int StorageType)
{
	char aCompleteFilename[512];
	unsigned char *pBuffer;
	png_t Png; // ignore_convention

	// open file for reading
	png_init(0,0); // ignore_convention

	IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType, aCompleteFilename, sizeof(aCompleteFilename));
	if(File)
		io_close(File);
	else
	{
		dbg_msg("game/png", "failed to open file. filename='%s'", pFilename);
		return 0;
	}

	int Error = png_open_file(&Png, aCompleteFilename); // ignore_convention
	if(Error != PNG_NO_ERROR)
	{
		dbg_msg("game/png", "failed to open file. filename='%s'", aCompleteFilename);
		if(Error != PNG_FILE_ERROR)
			png_close_file(&Png); // ignore_convention
		return 0;
	}

	if(Png.depth != 8 || (Png.color_type != PNG_TRUECOLOR && Png.color_type != PNG_TRUECOLOR_ALPHA)) // ignore_convention
	{
		dbg_msg("game/png", "invalid format. filename='%s'", aCompleteFilename);
		png_close_file(&Png); // ignore_convention
		return 0;
	}

	pBuffer = (unsigned char *)mem_alloc(Png.width * Png.height * Png.bpp, 1); // ignore_convention
	png_get_data(&Png, pBuffer); // ignore_convention
	png_close_file(&Png); // ignore_convention

	pImg->m_Width = Png.width; // ignore_convention
	pImg->m_Height = Png.height; // ignore_convention
	if(Png.color_type == PNG_TRUECOLOR) // ignore_convention
		pImg->m_Format = CImageInfo::FORMAT_RGB;
	else if(Png.color_type == PNG_TRUECOLOR_ALPHA) // ignore_convention
		pImg->m_Format = CImageInfo::FORMAT_RGBA;
	pImg->m_pData = pBuffer;
	return 1;
}

void CGraphics_3DS::ScreenshotDirect(const char *pFilename)
{
	
}

void CGraphics_3DS::TextureSet(int TextureID)
{
	dbg_assert(m_Drawing == 0, "called Graphics()->TextureSet within begin");

	if(TextureID == -1)
		currShader = &shaders[0];
	else
	{
		currShader = &shaders[1];
		C3D_TexBind(0, &m_aTextures[TextureID].m_Tex);
	}

	C3D_BindProgram(&currShader->program);

	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 4); // v1=color
	if (TextureID != -1) AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 2); // v2=texcoord0

	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	if (TextureID == -1)
		BufInfo_Add(bufInfo, m_aVertices, sizeof(CVertex), 2, 0x10);
	else
		BufInfo_Add(bufInfo, m_aVertices, sizeof(CVertex), 3, 0x210);

	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	if (TextureID == -1)
	{
		// Configure the first fragment shading substage to just pass through the vertex color
		// See https://www.opengl.org/sdk/docs/man2/xhtml/glTexEnv.xml for more insight
		C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}
	else
	{
		// Configure the first fragment shading substage to blend the texture color with
		// the vertex color (calculated by the vertex shader using a lighting algorithm)
		// See https://www.opengl.org/sdk/docs/man2/xhtml/glTexEnv.xml for more insight
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
	}
}

void CGraphics_3DS::Clear(float r, float g, float b)
{
	u32 color = ((u8)(r*255) << 24) | ((u8)(g*255) << 16) | ((u8)(b*255) << 8) | 0xFF;
	C3D_RenderTargetClear(bottomTarget, C3D_CLEAR_ALL, color, 0);
}

void CGraphics_3DS::QuadsBegin()
{
	dbg_assert(m_Drawing == 0, "called Graphics()->QuadsBegin twice");
	m_Drawing = DRAWING_QUADS;

	QuadsSetSubset(0,0,1,1);
	QuadsSetRotation(0);
	SetColor(1,1,1,1);
}

void CGraphics_3DS::QuadsEnd()
{
	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsEnd without begin");
	Flush();
	m_Drawing = 0;
}

void CGraphics_3DS::QuadsSetRotation(float Angle)
{
	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsSetRotation without begin");
	m_Rotation = Angle;
}

void CGraphics_3DS::SetColorVertex(const CColorVertex *pArray, int Num)
{
	dbg_assert(m_Drawing != 0, "called Graphics()->SetColorVertex without begin");

	for(int i = 0; i < Num; ++i)
	{
		m_aColor[pArray[i].m_Index].r = pArray[i].m_R;
		m_aColor[pArray[i].m_Index].g = pArray[i].m_G;
		m_aColor[pArray[i].m_Index].b = pArray[i].m_B;
		m_aColor[pArray[i].m_Index].a = pArray[i].m_A;
	}
}

void CGraphics_3DS::SetColor(float r, float g, float b, float a)
{
	dbg_assert(m_Drawing != 0, "called Graphics()->SetColor without begin");
	CColorVertex Array[4] = {
		CColorVertex(0, r, g, b, a),
		CColorVertex(1, r, g, b, a),
		CColorVertex(2, r, g, b, a),
		CColorVertex(3, r, g, b, a)};
	SetColorVertex(Array, 4);
}

void CGraphics_3DS::QuadsSetSubset(float TlU, float TlV, float BrU, float BrV)
{
	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsSetSubset without begin");

	m_aTexture[0].u = TlU;	m_aTexture[1].u = BrU;
	m_aTexture[0].v = TlV;	m_aTexture[1].v = TlV;

	m_aTexture[3].u = TlU;	m_aTexture[2].u = BrU;
	m_aTexture[3].v = BrV;	m_aTexture[2].v = BrV;
}

void CGraphics_3DS::QuadsSetSubsetFree(
	float x0, float y0, float x1, float y1,
	float x2, float y2, float x3, float y3)
{
	m_aTexture[0].u = x0; m_aTexture[0].v = y0;
	m_aTexture[1].u = x1; m_aTexture[1].v = y1;
	m_aTexture[2].u = x2; m_aTexture[2].v = y2;
	m_aTexture[3].u = x3; m_aTexture[3].v = y3;
}

void CGraphics_3DS::QuadsDraw(CQuadItem *pArray, int Num)
{
	for(int i = 0; i < Num; ++i)
	{
		pArray[i].m_X -= pArray[i].m_Width/2;
		pArray[i].m_Y -= pArray[i].m_Height/2;
	}

	QuadsDrawTL(pArray, Num);
}

void CGraphics_3DS::QuadsDrawTL(const CQuadItem *pArray, int Num)
{
	CPoint Center;
	Center.z = 0;

	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsDrawTL without begin");

	if(g_Config.m_GfxQuadAsTriangle)
	{
		for(int i = 0; i < Num; ++i)
		{
			// first triangle
			m_aVertices[m_NumVertices + 6*i].m_Pos.x = pArray[i].m_X;
			m_aVertices[m_NumVertices + 6*i].m_Pos.y = pArray[i].m_Y;
			m_aVertices[m_NumVertices + 6*i].m_Tex = m_aTexture[0];
			m_aVertices[m_NumVertices + 6*i].m_Color = m_aColor[0];

			m_aVertices[m_NumVertices + 6*i + 1].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
			m_aVertices[m_NumVertices + 6*i + 1].m_Pos.y = pArray[i].m_Y;
			m_aVertices[m_NumVertices + 6*i + 1].m_Tex = m_aTexture[1];
			m_aVertices[m_NumVertices + 6*i + 1].m_Color = m_aColor[1];

			m_aVertices[m_NumVertices + 6*i + 2].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
			m_aVertices[m_NumVertices + 6*i + 2].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
			m_aVertices[m_NumVertices + 6*i + 2].m_Tex = m_aTexture[2];
			m_aVertices[m_NumVertices + 6*i + 2].m_Color = m_aColor[2];

			// second triangle
			m_aVertices[m_NumVertices + 6*i + 3].m_Pos.x = pArray[i].m_X;
			m_aVertices[m_NumVertices + 6*i + 3].m_Pos.y = pArray[i].m_Y;
			m_aVertices[m_NumVertices + 6*i + 3].m_Tex = m_aTexture[0];
			m_aVertices[m_NumVertices + 6*i + 3].m_Color = m_aColor[0];

			m_aVertices[m_NumVertices + 6*i + 4].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
			m_aVertices[m_NumVertices + 6*i + 4].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
			m_aVertices[m_NumVertices + 6*i + 4].m_Tex = m_aTexture[2];
			m_aVertices[m_NumVertices + 6*i + 4].m_Color = m_aColor[2];

			m_aVertices[m_NumVertices + 6*i + 5].m_Pos.x = pArray[i].m_X;
			m_aVertices[m_NumVertices + 6*i + 5].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
			m_aVertices[m_NumVertices + 6*i + 5].m_Tex = m_aTexture[3];
			m_aVertices[m_NumVertices + 6*i + 5].m_Color = m_aColor[3];

			if(m_Rotation != 0)
			{
				Center.x = pArray[i].m_X + pArray[i].m_Width/2;
				Center.y = pArray[i].m_Y + pArray[i].m_Height/2;

				Rotate(Center, &m_aVertices[m_NumVertices + 6*i], 6);
			}
		}

		AddVertices(3*2*Num);
	}
	else
	{
		for(int i = 0; i < Num; ++i)
		{
			m_aVertices[m_NumVertices + 4*i].m_Pos.x = pArray[i].m_X;
			m_aVertices[m_NumVertices + 4*i].m_Pos.y = pArray[i].m_Y;
			m_aVertices[m_NumVertices + 4*i].m_Tex = m_aTexture[0];
			m_aVertices[m_NumVertices + 4*i].m_Color = m_aColor[0];

			m_aVertices[m_NumVertices + 4*i + 1].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
			m_aVertices[m_NumVertices + 4*i + 1].m_Pos.y = pArray[i].m_Y;
			m_aVertices[m_NumVertices + 4*i + 1].m_Tex = m_aTexture[1];
			m_aVertices[m_NumVertices + 4*i + 1].m_Color = m_aColor[1];

			m_aVertices[m_NumVertices + 4*i + 2].m_Pos.x = pArray[i].m_X + pArray[i].m_Width;
			m_aVertices[m_NumVertices + 4*i + 2].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
			m_aVertices[m_NumVertices + 4*i + 2].m_Tex = m_aTexture[2];
			m_aVertices[m_NumVertices + 4*i + 2].m_Color = m_aColor[2];

			m_aVertices[m_NumVertices + 4*i + 3].m_Pos.x = pArray[i].m_X;
			m_aVertices[m_NumVertices + 4*i + 3].m_Pos.y = pArray[i].m_Y + pArray[i].m_Height;
			m_aVertices[m_NumVertices + 4*i + 3].m_Tex = m_aTexture[3];
			m_aVertices[m_NumVertices + 4*i + 3].m_Color = m_aColor[3];

			if(m_Rotation != 0)
			{
				Center.x = pArray[i].m_X + pArray[i].m_Width/2;
				Center.y = pArray[i].m_Y + pArray[i].m_Height/2;

				Rotate(Center, &m_aVertices[m_NumVertices + 4*i], 4);
			}
		}

		AddVertices(4*Num);
	}
}

void CGraphics_3DS::QuadsDrawFreeform(const CFreeformItem *pArray, int Num)
{
	dbg_assert(m_Drawing == DRAWING_QUADS, "called Graphics()->QuadsDrawFreeform without begin");

	if(g_Config.m_GfxQuadAsTriangle)
	{
		for(int i = 0; i < Num; ++i)
		{
			m_aVertices[m_NumVertices + 6*i].m_Pos.x = pArray[i].m_X0;
			m_aVertices[m_NumVertices + 6*i].m_Pos.y = pArray[i].m_Y0;
			m_aVertices[m_NumVertices + 6*i].m_Tex = m_aTexture[0];
			m_aVertices[m_NumVertices + 6*i].m_Color = m_aColor[0];

			m_aVertices[m_NumVertices + 6*i + 1].m_Pos.x = pArray[i].m_X1;
			m_aVertices[m_NumVertices + 6*i + 1].m_Pos.y = pArray[i].m_Y1;
			m_aVertices[m_NumVertices + 6*i + 1].m_Tex = m_aTexture[1];
			m_aVertices[m_NumVertices + 6*i + 1].m_Color = m_aColor[1];

			m_aVertices[m_NumVertices + 6*i + 2].m_Pos.x = pArray[i].m_X3;
			m_aVertices[m_NumVertices + 6*i + 2].m_Pos.y = pArray[i].m_Y3;
			m_aVertices[m_NumVertices + 6*i + 2].m_Tex = m_aTexture[3];
			m_aVertices[m_NumVertices + 6*i + 2].m_Color = m_aColor[3];

			m_aVertices[m_NumVertices + 6*i + 3].m_Pos.x = pArray[i].m_X0;
			m_aVertices[m_NumVertices + 6*i + 3].m_Pos.y = pArray[i].m_Y0;
			m_aVertices[m_NumVertices + 6*i + 3].m_Tex = m_aTexture[0];
			m_aVertices[m_NumVertices + 6*i + 3].m_Color = m_aColor[0];

			m_aVertices[m_NumVertices + 6*i + 4].m_Pos.x = pArray[i].m_X3;
			m_aVertices[m_NumVertices + 6*i + 4].m_Pos.y = pArray[i].m_Y3;
			m_aVertices[m_NumVertices + 6*i + 4].m_Tex = m_aTexture[3];
			m_aVertices[m_NumVertices + 6*i + 4].m_Color = m_aColor[3];

			m_aVertices[m_NumVertices + 6*i + 5].m_Pos.x = pArray[i].m_X2;
			m_aVertices[m_NumVertices + 6*i + 5].m_Pos.y = pArray[i].m_Y2;
			m_aVertices[m_NumVertices + 6*i + 5].m_Tex = m_aTexture[2];
			m_aVertices[m_NumVertices + 6*i + 5].m_Color = m_aColor[2];
		}

		AddVertices(3*2*Num);
	}
	else
	{
		for(int i = 0; i < Num; ++i)
		{
			m_aVertices[m_NumVertices + 4*i].m_Pos.x = pArray[i].m_X0;
			m_aVertices[m_NumVertices + 4*i].m_Pos.y = pArray[i].m_Y0;
			m_aVertices[m_NumVertices + 4*i].m_Tex = m_aTexture[0];
			m_aVertices[m_NumVertices + 4*i].m_Color = m_aColor[0];

			m_aVertices[m_NumVertices + 4*i + 1].m_Pos.x = pArray[i].m_X1;
			m_aVertices[m_NumVertices + 4*i + 1].m_Pos.y = pArray[i].m_Y1;
			m_aVertices[m_NumVertices + 4*i + 1].m_Tex = m_aTexture[1];
			m_aVertices[m_NumVertices + 4*i + 1].m_Color = m_aColor[1];

			m_aVertices[m_NumVertices + 4*i + 2].m_Pos.x = pArray[i].m_X3;
			m_aVertices[m_NumVertices + 4*i + 2].m_Pos.y = pArray[i].m_Y3;
			m_aVertices[m_NumVertices + 4*i + 2].m_Tex = m_aTexture[3];
			m_aVertices[m_NumVertices + 4*i + 2].m_Color = m_aColor[3];

			m_aVertices[m_NumVertices + 4*i + 3].m_Pos.x = pArray[i].m_X2;
			m_aVertices[m_NumVertices + 4*i + 3].m_Pos.y = pArray[i].m_Y2;
			m_aVertices[m_NumVertices + 4*i + 3].m_Tex = m_aTexture[2];
			m_aVertices[m_NumVertices + 4*i + 3].m_Color = m_aColor[2];
		}

		AddVertices(4*Num);
	}
}

void CGraphics_3DS::QuadsText(float x, float y, float Size, const char *pText)
{
	float StartX = x;

	while(*pText)
	{
		char c = *pText;
		pText++;

		if(c == '\n')
		{
			x = StartX;
			y += Size;
		}
		else
		{
			QuadsSetSubset(
				(c%16)/16.0f,
				(c/16)/16.0f,
				(c%16)/16.0f+1.0f/16.0f,
				(c/16)/16.0f+1.0f/16.0f);

			CQuadItem QuadItem(x, y, Size, Size);
			QuadsDrawTL(&QuadItem, 1);
			x += Size/2;
		}
	}
}

int CGraphics_3DS::Init()
{
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	// Initialize graphics
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	consoleInit(GFX_TOP, NULL);

	// Initialize the render target
	bottomTarget = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(bottomTarget, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	m_ScreenWidth = g_Config.m_GfxScreenWidth = 320;
	m_ScreenHeight = g_Config.m_GfxScreenHeight = 240;

	m_aVertices = (CVertex*)linearAlloc(sizeof(CVertex)*MAX_VERTICES);

	for (int i=0; i<2; i++)
	{
		u32* bin = (!i) ? (u32*)colored_shbin : (u32*)textured_shbin;
		u32 binsize = (!i) ? colored_shbin_size : textured_shbin_size;

		// Load the vertex shader, create a shader program and bind it
		shaders[i].dvlb = DVLB_ParseFile(bin, binsize);
		shaderProgramInit(&shaders[i].program);
		shaderProgramSetVsh(&shaders[i].program, &shaders[i].dvlb->DVLE[0]);
		C3D_BindProgram(&shaders[i].program);

		// Get the location of the uniform
		shaders[i].uf_projection = shaderInstanceGetUniformLocation(shaders[i].program.vertexShader, "projection");
	}

	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 4); // v1=color

	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, m_aVertices, sizeof(CVertex), 2, 0x10);

	// Configure the first fragment shading substage to just pass through the vertex color
	// See https://www.opengl.org/sdk/docs/man2/xhtml/glTexEnv.xml for more insight
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

	// Use untextured shader by default
	currShader = &shaders[0];
	C3D_BindProgram(&currShader->program);

	// Set all z to -5.0f
	for(int i = 0; i < MAX_VERTICES; i++)
		m_aVertices[i].m_Pos.z = -5.0f;

	// init textures
	m_FirstFreeTexture = 0;
	for(int i = 0; i < MAX_TEXTURES; i++)
		m_aTextures[i].m_Next = i+1;
	m_aTextures[MAX_TEXTURES-1].m_Next = -1;

	// set some default settings
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_AlphaTest(true, GPU_GREATER, 0);
	C3D_BlendingColor(0);

	// create null texture, will get id=0
	static const unsigned char aNullTextureData[] = {
		0xff,0x00,0x00,0xff, 0xff,0x00,0x00,0xff, 0x00,0xff,0x00,0xff, 0x00,0xff,0x00,0xff,
		0xff,0x00,0x00,0xff, 0xff,0x00,0x00,0xff, 0x00,0xff,0x00,0xff, 0x00,0xff,0x00,0xff,
		0x00,0x00,0xff,0xff, 0x00,0x00,0xff,0xff, 0xff,0xff,0x00,0xff, 0xff,0xff,0x00,0xff,
		0x00,0x00,0xff,0xff, 0x00,0x00,0xff,0xff, 0xff,0xff,0x00,0xff, 0xff,0xff,0x00,0xff,
	};

	m_InvalidTexture = LoadTextureRaw(4,4,CImageInfo::FORMAT_RGBA,aNullTextureData,CImageInfo::FORMAT_RGBA,TEXLOAD_NORESAMPLE);

	C3D_FrameBegin((g_Config.m_GfxVsync) ? C3D_FRAME_SYNCDRAW : 0);
	C3D_FrameDrawOn(bottomTarget);

	return 0;
}

void CGraphics_3DS::Shutdown()
{
	for (int i=0; i<2; i++)
	{
		shaderProgramFree(&shaders[i].program);
		DVLB_Free(shaders[i].dvlb);
	}

	linearFree(m_aVertices);
	C3D_Fini();
	gfxExit();
}

void CGraphics_3DS::Minimize()
{
	
}

void CGraphics_3DS::Maximize()
{
	
}

int CGraphics_3DS::WindowActive()
{
	return 1;
}

int CGraphics_3DS::WindowOpen()
{
	return 1;
}

void CGraphics_3DS::NotifyWindow()
{
	
}

void CGraphics_3DS::TakeScreenshot(const char *pFilename)
{
	
}

void CGraphics_3DS::TakeCustomScreenshot(const char *pFilename)
{
	
}


void CGraphics_3DS::Swap()
{
	C3D_FrameEnd(0);
	m_StartVertex = 0;
	m_NumVertices = 0;

	C3D_FrameBegin((g_Config.m_GfxVsync) ? C3D_FRAME_SYNCDRAW : 0);
	C3D_FrameDrawOn(bottomTarget);
}


int CGraphics_3DS::GetVideoModes(CVideoMode *pModes, int MaxModes)
{
	pModes[0].m_Width = 320;
	pModes[0].m_Height = 240;
	pModes[0].m_Red = 8;
	pModes[0].m_Green = 8;
	pModes[0].m_Blue = 8;
	return 1;
}

// syncronization
void CGraphics_3DS::InsertSignal(semaphore *pSemaphore)
{
	//pSemaphore->signal();
}

bool CGraphics_3DS::IsIdle()
{
	return true;
}

void CGraphics_3DS::WaitForIdle()
{
}

extern IEngineGraphics *CreateEngineGraphics() { return new CGraphics_3DS(); }
