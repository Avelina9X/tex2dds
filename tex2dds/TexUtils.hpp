#pragma once

#define NOMINMAX
#include <d3d11.h>

DXGI_FORMAT CreateOutputFormat( DXGI_FORMAT inputFormat, size_t outChannels );
bool CreateDevice( int adapter, ID3D11Device **pDevice );

template<typename T> T TypeMax();
template<typename T> T TypeMin();

enum SRGB_INPUT
{
    FORCE_SRGB,
    ASSUME_SRGB,
    ASSUME_LINEAR,
    FORCE_LINEAR
};

struct ChannelSwizzle {
    std::optional<std::wstring> szFile;
    char swizzle;

    ChannelSwizzle( std::wstring file, char swiz )
    {
        szFile = file;
        swizzle = swiz;
    }

    ChannelSwizzle( std::nullptr_t file, char swiz ) {
        szFile = std::nullopt;
        swizzle = swiz;
    }
};

void PrintDebugMetadata( std::string name, DirectX::TexMetadata metadata );

HRESULT LoadImageWithSRGB(
    const wchar_t *szFile,
    SRGB_INPUT srgb,
    DXGI_FORMAT formatOut,
    std::unique_ptr<DirectX::ScratchImage> &pInputImage,
    bool verbose = false
);

HRESULT ResizeImage(
    int width,
    int height,
    std::unique_ptr<DirectX::ScratchImage> &pInputImage,
    DXGI_FORMAT formatOut
);

HRESULT ExtractChannel(
    const std::unique_ptr<DirectX::ScratchImage> &pInputImage,
    char swizzle,
    std::unique_ptr<DirectX::ScratchImage> &pOutputSlice
);

bool EnsureCompatibleChannelSlices( const std::vector<std::unique_ptr<DirectX::ScratchImage>> &slices );

HRESULT CombineChannelSlices(
    const std::vector<std::unique_ptr<DirectX::ScratchImage>> &slices,
    DXGI_FORMAT formatOut,
    std::unique_ptr<DirectX::ScratchImage> &pCombinerImage,
    bool verbose = false
);

HRESULT GenerateMipMapChain(
    DXGI_FORMAT format,
    std::unique_ptr<DirectX::ScratchImage> &pCombinerImage,
    std::unique_ptr<DirectX::ScratchImage> &pMipMapImage,
    bool verbose = false
);

HRESULT CompressImage(
    ID3D11Device *pDevice,
    DXGI_FORMAT format,
    std::unique_ptr<DirectX::ScratchImage> &pMipMapImage,
    std::unique_ptr<DirectX::ScratchImage> &pCompressedImage,
    bool verbose = false
);

template<typename T>
struct SValue
{
    const char *name;
    T value;
};

#define DEFFMT(fmt) { ## #fmt, DXGI_FORMAT_ ## fmt }
const SValue<DXGI_FORMAT> g_pFormats[] =
{
    // List does not include _TYPELESS or depth/stencil formats
    DEFFMT( R32G32B32A32_FLOAT ),
    DEFFMT( R32G32B32A32_UINT ),
    DEFFMT( R32G32B32A32_SINT ),
    DEFFMT( R32G32B32_FLOAT ),
    DEFFMT( R32G32B32_UINT ),
    DEFFMT( R32G32B32_SINT ),
    DEFFMT( R16G16B16A16_FLOAT ),
    DEFFMT( R16G16B16A16_UNORM ),
    DEFFMT( R16G16B16A16_UINT ),
    DEFFMT( R16G16B16A16_SNORM ),
    DEFFMT( R16G16B16A16_SINT ),
    DEFFMT( R32G32_FLOAT ),
    DEFFMT( R32G32_UINT ),
    DEFFMT( R32G32_SINT ),
    DEFFMT( R10G10B10A2_UNORM ),
    DEFFMT( R10G10B10A2_UINT ),
    DEFFMT( R11G11B10_FLOAT ),
    DEFFMT( R8G8B8A8_UNORM ),
    DEFFMT( R8G8B8A8_UNORM_SRGB ),
    DEFFMT( R8G8B8A8_UINT ),
    DEFFMT( R8G8B8A8_SNORM ),
    DEFFMT( R8G8B8A8_SINT ),
    DEFFMT( R16G16_FLOAT ),
    DEFFMT( R16G16_UNORM ),
    DEFFMT( R16G16_UINT ),
    DEFFMT( R16G16_SNORM ),
    DEFFMT( R16G16_SINT ),
    DEFFMT( R32_FLOAT ),
    DEFFMT( R32_UINT ),
    DEFFMT( R32_SINT ),
    DEFFMT( R8G8_UNORM ),
    DEFFMT( R8G8_UINT ),
    DEFFMT( R8G8_SNORM ),
    DEFFMT( R8G8_SINT ),
    DEFFMT( R16_FLOAT ),
    DEFFMT( R16_UNORM ),
    DEFFMT( R16_UINT ),
    DEFFMT( R16_SNORM ),
    DEFFMT( R16_SINT ),
    DEFFMT( R8_UNORM ),
    DEFFMT( R8_UINT ),
    DEFFMT( R8_SNORM ),
    DEFFMT( R8_SINT ),
    DEFFMT( A8_UNORM ),
    DEFFMT( R9G9B9E5_SHAREDEXP ),
    DEFFMT( R8G8_B8G8_UNORM ),
    DEFFMT( G8R8_G8B8_UNORM ),
    DEFFMT( BC1_UNORM ),
    DEFFMT( BC1_UNORM_SRGB ),
    DEFFMT( BC2_UNORM ),
    DEFFMT( BC2_UNORM_SRGB ),
    DEFFMT( BC3_UNORM ),
    DEFFMT( BC3_UNORM_SRGB ),
    DEFFMT( BC4_UNORM ),
    DEFFMT( BC4_SNORM ),
    DEFFMT( BC5_UNORM ),
    DEFFMT( BC5_SNORM ),
    DEFFMT( B5G6R5_UNORM ),
    DEFFMT( B5G5R5A1_UNORM ),

    // DXGI 1.1 formats
    DEFFMT( B8G8R8A8_UNORM ),
    DEFFMT( B8G8R8X8_UNORM ),
    DEFFMT( R10G10B10_XR_BIAS_A2_UNORM ),
    DEFFMT( B8G8R8A8_UNORM_SRGB ),
    DEFFMT( B8G8R8X8_UNORM_SRGB ),
    DEFFMT( BC6H_UF16 ),
    DEFFMT( BC6H_SF16 ),
    DEFFMT( BC7_UNORM ),
    DEFFMT( BC7_UNORM_SRGB ),

    // DXGI 1.2 formats
    DEFFMT( AYUV ),
    DEFFMT( Y410 ),
    DEFFMT( Y416 ),
    DEFFMT( YUY2 ),
    DEFFMT( Y210 ),
    DEFFMT( Y216 ),
    // No support for legacy paletted video formats (AI44, IA44, P8, A8P8)
    DEFFMT( B4G4R4A4_UNORM ),

    // D3D11on12 format
    { "A4B4G4R4_UNORM", DXGI_FORMAT( 191 ) },

    { nullptr, DXGI_FORMAT_UNKNOWN }
};

template<typename T>
T LookupByName( const char *pName, const SValue<T> *pArray )
{
    while ( pArray->name ) {
        if ( _stricmp( pName, pArray->name ) == 0 ) return pArray->value;
        pArray++;
    }

    return static_cast<T>( 0 );
}

template<typename T>
const char *LookupByValue( T value, const SValue<T> *pArray )
{
    while ( pArray->name ) {
        if ( value == pArray->value ) return pArray->name;
        pArray++;
    }

    return "";
}