#include "pch.h"

#include <iostream>
#include <wrl\client.h>

#include "TexUtils.hpp"


using namespace DirectX;
using Microsoft::WRL::ComPtr;

template<> uint8_t TypeMax() { return -1; }
template<> uint16_t TypeMax() { return -1; }

template<> uint8_t TypeMin() { return 0; }
template<> uint16_t TypeMin() { return 0; }

void PrintDebugMetadata( std::string name, TexMetadata metadata )
{
    std::cout << name << ":";
    std::cout << " SRGB=" << IsSRGB( metadata.format );
    std::cout << " BGR=" << IsBGR( metadata.format );
    std::cout << " Alpha=" << HasAlpha( metadata.format );
    std::cout << " Dtype=" << FormatDataType( metadata.format );
    std::cout << " Format=" << LookupByValue( metadata.format, g_pFormats );
    std::cout << std::endl;
}

HRESULT LoadImageWithSRGB( const wchar_t *szFile, SRGB_INPUT srgb, DXGI_FORMAT formatOut, std::unique_ptr<ScratchImage> &pInputImage, bool verbose )
{
    bool srgbOut = IsSRGB( formatOut );

    auto ext = std::filesystem::path( szFile ).extension().string();
    if ( ext == ".tga" || ext == ".TGA" ) {
        TGA_FLAGS tgaFlags = TGA_FLAGS_NONE;
        switch ( srgb ) {
            case FORCE_SRGB: tgaFlags |= TGA_FLAGS_DEFAULT_SRGB; break;
            case ASSUME_SRGB: tgaFlags |= TGA_FLAGS_DEFAULT_SRGB; break;
            case ASSUME_LINEAR: tgaFlags |= TGA_FLAGS_NONE; break;
            case FORCE_LINEAR: tgaFlags |= TGA_FLAGS_IGNORE_SRGB; break;
        }

        HRESULT hr = LoadFromTGAFile( szFile, tgaFlags, nullptr, *pInputImage.get() );
        if ( FAILED( hr ) ) {
            std::cerr << "Failed to load TGA image!" << std::endl;
            return hr;
        }
    }
    else {
        WIC_FLAGS wicFlags = WIC_FLAGS_FORCE_RGB;
        switch ( srgb ) {
            case FORCE_SRGB: wicFlags |= WIC_FLAGS_DEFAULT_SRGB; break;
            case ASSUME_SRGB: wicFlags |= WIC_FLAGS_DEFAULT_SRGB; break;
            case ASSUME_LINEAR: wicFlags |= WIC_FLAGS_NONE; break;
            case FORCE_LINEAR: wicFlags |= WIC_FLAGS_IGNORE_SRGB; break;
        }

        HRESULT hr = LoadFromWICFile( szFile, wicFlags, nullptr, *pInputImage.get() );
        if ( FAILED( hr ) ) {
            std::cerr << "Failed to load WIC image!" << std::endl;
            return hr;
        }
    }

    // Override as SRGB if force SRGB is enabled
    if ( srgb == FORCE_SRGB && !IsSRGB( pInputImage->GetMetadata().format ) ) {
        pInputImage->OverrideFormat( MakeSRGB( pInputImage->GetMetadata().format ) );
    }

    if ( verbose ) {
        PrintDebugMetadata( "Input", pInputImage->GetMetadata() );
    }


    // Actually convert image to destination SRGB type
    if ( srgbOut != IsSRGB( pInputImage->GetMetadata().format ) ) {
        auto pConvImage = std::make_unique<ScratchImage>();
        HRESULT hr = Convert(
            pInputImage->GetImages(),
            pInputImage->GetImageCount(),
            pInputImage->GetMetadata(),
            srgbOut ? MakeSRGB( pInputImage->GetMetadata().format ) : MakeLinear( pInputImage->GetMetadata().format ),
            TEX_FILTER_DEFAULT,
            TEX_THRESHOLD_DEFAULT,
            *pConvImage.get()
        );
        if ( FAILED( hr ) ) {
            std::cerr << "Failed to convert image to SRGB!" << std::endl;
            return hr;
        }

        if ( verbose ) {
            PrintDebugMetadata( "Output SRGB Conv", pConvImage->GetMetadata() );
        }

        pInputImage.swap( pConvImage );
    }

    return 0;
}

HRESULT ResizeImage( int width, int height, std::unique_ptr<ScratchImage> &pInputImage, DXGI_FORMAT formatOut )
{
    auto flags = TEX_FILTER_DEFAULT;

    if ( MakeTypeless( formatOut ) == DXGI_FORMAT_BC7_TYPELESS ) flags |= TEX_FILTER_SEPARATE_ALPHA;


    if ( width != -1 || height != -1 ) {
        auto pResizeImage = std::make_unique<ScratchImage>();
        HRESULT hr = Resize(
            pInputImage->GetImages(),
            pInputImage->GetImageCount(),
            pInputImage->GetMetadata(),
            width == -1 ? pInputImage->GetMetadata().width : width,
            height == -1 ? pInputImage->GetMetadata().height : height,
            flags,
            *pResizeImage.get()
        );
        if ( FAILED( hr ) ) {
            return hr;
        }

        pInputImage.swap( pResizeImage );
    }

    return 0;
}

template<typename T>
void _FillChannel( const Image *pOutputSlice, float fFillVal )
{
    fFillVal = fFillVal * TypeMax<T>();
    T tFillVal = static_cast<T>( std::clamp<float>( fFillVal, TypeMin<T>(), TypeMax<T>() ) );

    for ( size_t y = 0; y < pOutputSlice->height; ++y ) {
        auto row = reinterpret_cast<T *>( pOutputSlice->pixels + y * pOutputSlice->rowPitch );
        std::fill_n( row, pOutputSlice->width, tFillVal );
    }
}

HRESULT ExtractChannel( const std::unique_ptr<ScratchImage> &pInputImage, char swizzle, std::unique_ptr<ScratchImage> &pOutputSlice )
{
    auto singleChannelFormat = CreateOutputFormat( pInputImage->GetMetadata().format, 1 );

    TEX_FILTER_FLAGS channelFlags = TEX_FILTER_DEFAULT | TEX_FILTER_FORCE_NON_WIC;
    if ( IsSRGB( pInputImage->GetMetadata().format ) ) {
        channelFlags |= TEX_FILTER_SRGB;
    }

    bool fill = false;
    float fillVal;

    switch ( swizzle ) {
        case 'r': channelFlags |= TEX_FILTER_RGB_COPY_RED; break;
        case 'g': channelFlags |= TEX_FILTER_RGB_COPY_GREEN; break;
        case 'b': channelFlags |= TEX_FILTER_RGB_COPY_BLUE; break;
        case 'a': channelFlags |= TEX_FILTER_RGB_COPY_ALPHA; break;

        case '0':
            fill = true;
            fillVal = 0.0f;
            break;

        case '1':
            fill = true;
            fillVal = 1.0f;
            break;

        case 'h':
            fill = true;
            fillVal = 0.5f;
            break;

        default:
            std::cerr << "Unknown swizzle: " << swizzle << std::endl;
            return E_FAIL;
    }

    HRESULT hr = Convert(
        pInputImage->GetImages(),
        pInputImage->GetImageCount(),
        pInputImage->GetMetadata(),
        singleChannelFormat,
        channelFlags,
        TEX_THRESHOLD_DEFAULT,
        *pOutputSlice.get()
    );
    if ( FAILED( hr ) ) {
        std::cerr << "Failed to get single channel!" << std::endl;
        return hr;
    }

    if ( fill ) {
        auto bitDepth = BitsPerColor( pOutputSlice->GetMetadata().format );
        auto formatType = FormatDataType( pOutputSlice->GetMetadata().format );

        switch ( formatType ) {

            case FORMAT_TYPE_UNORM:
                if ( bitDepth == 8 ) {
                    _FillChannel<uint8_t>( pOutputSlice->GetImages(), fillVal );
                    break;
                }

                if ( bitDepth == 16 ) {
                    _FillChannel<uint16_t>( pOutputSlice->GetImages(), fillVal );
                    break;
                }

                std::cerr << "Unsupported bitdepth!" << std::endl;
                return E_FAIL;

            default:
                std::cerr << "Unsupported format!" << std::endl;
                return E_FAIL;
        }
    }

    return 0;
}

bool EnsureCompatibleChannelSlices( const std::vector<std::unique_ptr<ScratchImage>> &slices ) {
    for ( int i = 0; i < slices.size(); ++i ) {
        for ( int j = i + 1; j < slices.size(); ++j ) {
            if ( slices[i]->GetMetadata().width != slices[j]->GetMetadata().width ) {
                std::cerr << "Incompatible width!" << std::endl;
                return false;
            }
            if ( slices[i]->GetMetadata().height != slices[j]->GetMetadata().height ) {
                std::cerr << "Incompatible height!" << std::endl;
                return false;
            }
            if ( slices[i]->GetMetadata().format != slices[j]->GetMetadata().format ) {
                std::cerr << "Incompatible format!" << std::endl;
                return false;
            }
        }
    }

    return true;
}

template<typename T>
void _CombineChannels( const std::vector<std::unique_ptr<ScratchImage>> &slices, const Image *pOutputSlice )
{
    for ( size_t y = 0; y < pOutputSlice->height; ++y ) {
        auto outRow = reinterpret_cast<T *>( pOutputSlice->pixels + y * pOutputSlice->rowPitch );

        for ( size_t c = 0; c < slices.size(); ++c ) {
            auto inImage = slices[c]->GetImages();
            auto inRow = reinterpret_cast<const T *>( inImage->pixels + y * inImage->rowPitch );

            for ( size_t x = 0; x < pOutputSlice->width; ++x ) {
                outRow[x * slices.size() + c] = inRow[x];
            }
        }
    }
}

HRESULT CombineChannelSlices( const std::vector<std::unique_ptr<ScratchImage>> &slices, DXGI_FORMAT formatOut, std::unique_ptr<ScratchImage> &pCombinerImage, bool verbose )
{
    if ( !EnsureCompatibleChannelSlices( slices ) ) {
        std::cerr << "Channel slices aren't in compatible formats!" << std::endl;
        return E_FAIL;
    }

    DXGI_FORMAT combinerFormat = CreateOutputFormat( slices[0]->GetMetadata().format, slices.size() );
    if ( combinerFormat == DXGI_FORMAT_UNKNOWN ) {
        std::cerr << "Unknown input format!" << std::endl;
        return E_FAIL;
    }
    if ( IsSRGB( formatOut ) ) {
        combinerFormat = MakeSRGB( combinerFormat );
    }

    HRESULT hr = pCombinerImage->Initialize2D( combinerFormat, slices[0]->GetMetadata().width, slices[0]->GetMetadata().height, 1, 1 );
    if ( FAILED( hr ) ) {
        std::cerr << "Could not create combiner image!" << std::endl;
        return hr;
    }

    auto bitDepth = BitsPerColor( pCombinerImage->GetMetadata().format );
    switch ( bitDepth ) {
        case 8:
            _CombineChannels<uint8_t>( slices, pCombinerImage->GetImages() );
            break;

        case 16:
            _CombineChannels<uint16_t>( slices, pCombinerImage->GetImages() );
            break;

        default:
            std::cerr << "Unknown bitdepth!" << std::endl;
            return E_FAIL;
    }

    if ( verbose ) {
        PrintDebugMetadata( "Combiner", pCombinerImage->GetMetadata() );
    }

    return 0;
}

HRESULT GenerateMipMapChain( DXGI_FORMAT format, std::unique_ptr<ScratchImage> &pCombinerImage, std::unique_ptr<ScratchImage> &pMipMapImage, bool verbose )
{
    TEX_FILTER_FLAGS mipFlags = TEX_FILTER_DEFAULT | TEX_FILTER_WRAP;

    if ( MakeTypeless( format ) == DXGI_FORMAT_BC7_TYPELESS ) mipFlags |= TEX_FILTER_SEPARATE_ALPHA;
    if ( IsSRGB( format ) ) mipFlags |= TEX_FILTER_FORCE_WIC;

    HRESULT hr = GenerateMipMaps( pCombinerImage->GetImages(), pCombinerImage->GetImageCount(), pCombinerImage->GetMetadata(), mipFlags, 0, *pMipMapImage.get() );
    if ( FAILED( hr ) ) {
        return hr;
    }

    if ( verbose ) {
        auto mip = pMipMapImage->GetMetadata().mipLevels - 1;
        std::cout << "Last uncompressed MIP channel values: "
            << int( pMipMapImage->GetImage( mip, 0, 0 )->pixels[0] ) << " "
            << int( pMipMapImage->GetImage( mip, 0, 0 )->pixels[1] ) << " "
            << int( pMipMapImage->GetImage( mip, 0, 0 )->pixels[2] ) << " "
            << int( pMipMapImage->GetImage( mip, 0, 0 )->pixels[3] ) << std::endl;
    }

    return 0;
}


bool _RequiresGPU( DXGI_FORMAT format )
{
    if ( MakeTypeless( format ) == DXGI_FORMAT_BC6H_TYPELESS ) return true;
    if ( MakeTypeless( format ) == DXGI_FORMAT_BC7_TYPELESS ) return true;
    return false;
}

HRESULT CompressImage( ID3D11Device *pDevice, DXGI_FORMAT format, std::unique_ptr<ScratchImage> &pMipMapImage, std::unique_ptr<ScratchImage> &pCompressedImage, bool verbose )
{
    HRESULT hr;

    if ( _RequiresGPU( format ) ) {
        hr = Compress(
            pDevice,
            pMipMapImage->GetImages(),
            pMipMapImage->GetImageCount(),
            pMipMapImage->GetMetadata(),
            format,
            TEX_COMPRESS_PARALLEL,
            TEX_ALPHA_WEIGHT_DEFAULT,
            *pCompressedImage.get()
        );
    }
    else {
        hr = Compress(
            pMipMapImage->GetImages(),
            pMipMapImage->GetImageCount(),
            pMipMapImage->GetMetadata(),
            format,
            TEX_COMPRESS_PARALLEL,
            TEX_THRESHOLD_DEFAULT,
            *pCompressedImage.get()
        );
    }

    if ( FAILED( hr ) ) {
        return hr;
    }

    if ( verbose ) {
        PrintDebugMetadata( "Compressed", pCompressedImage->GetMetadata() );
    }

    return 0;
}

DXGI_FORMAT CreateOutputFormat( DXGI_FORMAT inputFormat, size_t outChannels ) {
	auto outFormat = DirectX::FormatDataType( inputFormat );
	auto outBitdepth = DirectX::BitsPerColor( inputFormat );

	using namespace DirectX;

	switch ( outFormat ) {
		case FORMAT_TYPE_FLOAT:
			//if ( outChannels == 1 && outBitdepth == 8 ) return DXGI_FORMAT_R8_FLOAT;
			if ( outChannels == 1 && outBitdepth == 16 ) return DXGI_FORMAT_R16_FLOAT;
			if ( outChannels == 1 && outBitdepth == 32 ) return DXGI_FORMAT_R32_FLOAT;

			//if ( outChannels == 2 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8_FLOAT;
			if ( outChannels == 2 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16_FLOAT;
			if ( outChannels == 2 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32_FLOAT;

			//if ( outChannels == 3 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8_FLOAT;
			//if ( outChannels == 3 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16_FLOAT;
			if ( outChannels == 3 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32_FLOAT;

			//if ( outChannels == 4 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8A8_FLOAT;
			if ( outChannels == 4 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16A16_FLOAT;
			if ( outChannels == 4 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32A32_FLOAT;

			break;

		case FORMAT_TYPE_UNORM:
			if ( outChannels == 1 && outBitdepth == 8 ) return DXGI_FORMAT_R8_UNORM;
			if ( outChannels == 1 && outBitdepth == 16 ) return DXGI_FORMAT_R16_UNORM;
			//if ( outChannels == 1 && outBitdepth == 32 ) return DXGI_FORMAT_R32_UNORM;

			if ( outChannels == 2 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8_UNORM;
			if ( outChannels == 2 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16_UNORM;
			//if ( outChannels == 2 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32_UNORM;

			//if ( outChannels == 3 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8_UNORM;
			//if ( outChannels == 3 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16_UNORM;
			//if ( outChannels == 3 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32_UNORM;

			if ( outChannels == 4 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8A8_UNORM;
			if ( outChannels == 4 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16A16_UNORM;
			//if ( outChannels == 4 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32A32_UNORM;

			break;

		case FORMAT_TYPE_SNORM:
			if ( outChannels == 1 && outBitdepth == 8 ) return DXGI_FORMAT_R8_SNORM;
			if ( outChannels == 1 && outBitdepth == 16 ) return DXGI_FORMAT_R16_SNORM;
			//if ( outChannels == 1 && outBitdepth == 32 ) return DXGI_FORMAT_R32_SNORM;

			if ( outChannels == 2 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8_SNORM;
			if ( outChannels == 2 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16_SNORM;
			//if ( outChannels == 2 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32_SNORM;

			//if ( outChannels == 3 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8_SNORM;
			//if ( outChannels == 3 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16_SNORM;
			//if ( outChannels == 3 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32_SNORM;

			if ( outChannels == 4 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8A8_SNORM;
			if ( outChannels == 4 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16A16_SNORM;
			//if ( outChannels == 4 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32A32_SNORM;

			break;

		case FORMAT_TYPE_UINT:
			if ( outChannels == 1 && outBitdepth == 8 ) return DXGI_FORMAT_R8_UINT;
			if ( outChannels == 1 && outBitdepth == 16 ) return DXGI_FORMAT_R16_UINT;
			if ( outChannels == 1 && outBitdepth == 32 ) return DXGI_FORMAT_R32_UINT;

			if ( outChannels == 2 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8_UINT;
			if ( outChannels == 2 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16_UINT;
			if ( outChannels == 2 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32_UINT;

			//if ( outChannels == 3 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8_UINT;
			//if ( outChannels == 3 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16_UINT;
			if ( outChannels == 3 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32_UINT;

			if ( outChannels == 4 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8A8_UINT;
			if ( outChannels == 4 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16A16_UINT;
			if ( outChannels == 4 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32A32_UINT;

			break;

		case FORMAT_TYPE_SINT:
			if ( outChannels == 1 && outBitdepth == 8 ) return DXGI_FORMAT_R8_SINT;
			if ( outChannels == 1 && outBitdepth == 16 ) return DXGI_FORMAT_R16_SINT;
			if ( outChannels == 1 && outBitdepth == 32 ) return DXGI_FORMAT_R32_SINT;

			if ( outChannels == 2 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8_SINT;
			if ( outChannels == 2 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16_SINT;
			if ( outChannels == 2 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32_SINT;

			//if ( outChannels == 3 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8_SINT;
			//if ( outChannels == 3 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16_SINT;
			if ( outChannels == 3 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32_SINT;

			if ( outChannels == 4 && outBitdepth == 8 ) return DXGI_FORMAT_R8G8B8A8_SINT;
			if ( outChannels == 4 && outBitdepth == 16 ) return DXGI_FORMAT_R16G16B16A16_SINT;
			if ( outChannels == 4 && outBitdepth == 32 ) return DXGI_FORMAT_R32G32B32A32_SINT;

			break;

		default:
			break;
	}

	return DXGI_FORMAT_UNKNOWN;
}


bool GetDXGIFactory( IDXGIFactory1 **pFactory )
{
	if ( !pFactory )
		return false;

	*pFactory = nullptr;

	typedef HRESULT( WINAPI *pfn_CreateDXGIFactory1 )( REFIID riid, _Out_ void **ppFactory );

	static pfn_CreateDXGIFactory1 s_CreateDXGIFactory1 = nullptr;

	if ( !s_CreateDXGIFactory1 )
	{
		HMODULE hModDXGI = LoadLibraryW( L"dxgi.dll" );
		if ( !hModDXGI )
			return false;

		s_CreateDXGIFactory1 = reinterpret_cast<pfn_CreateDXGIFactory1>( reinterpret_cast<void *>( GetProcAddress( hModDXGI, "CreateDXGIFactory1" ) ) );
		if ( !s_CreateDXGIFactory1 )
			return false;
	}

	return SUCCEEDED( s_CreateDXGIFactory1( IID_PPV_ARGS( pFactory ) ) );
}

bool CreateDevice( int adapter, ID3D11Device **pDevice )
{
	if ( !pDevice )
		return false;

	*pDevice = nullptr;

	static PFN_D3D11_CREATE_DEVICE s_DynamicD3D11CreateDevice = nullptr;

	if ( !s_DynamicD3D11CreateDevice )
	{
		HMODULE hModD3D11 = LoadLibraryW( L"d3d11.dll" );
		if ( !hModD3D11 )
			return false;

		s_DynamicD3D11CreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>( reinterpret_cast<void *>( GetProcAddress( hModD3D11, "D3D11CreateDevice" ) ) );
		if ( !s_DynamicD3D11CreateDevice )
			return false;
	}

	const D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	ComPtr<IDXGIAdapter> pAdapter;
	if ( adapter >= 0 )
	{
		ComPtr<IDXGIFactory1> dxgiFactory;
		if ( GetDXGIFactory( dxgiFactory.GetAddressOf() ) )
		{
			if ( FAILED( dxgiFactory->EnumAdapters( static_cast<UINT>( adapter ), pAdapter.GetAddressOf() ) ) )
			{
				wprintf( L"\nERROR: Invalid GPU adapter index (%d)!\n", adapter );
				return false;
			}
		}
	}

	D3D_FEATURE_LEVEL fl;
	HRESULT hr = s_DynamicD3D11CreateDevice( pAdapter.Get(),
		( pAdapter ) ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
		nullptr, createDeviceFlags, featureLevels, static_cast<UINT>( std::size( featureLevels ) ),
		D3D11_SDK_VERSION, pDevice, &fl, nullptr );
	if ( SUCCEEDED( hr ) )
	{
		if ( fl < D3D_FEATURE_LEVEL_11_0 )
		{
			D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS hwopts;
			hr = ( *pDevice )->CheckFeatureSupport( D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &hwopts, sizeof( hwopts ) );
			if ( FAILED( hr ) )
				memset( &hwopts, 0, sizeof( hwopts ) );

			if ( !hwopts.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x )
			{
				if ( *pDevice )
				{
					( *pDevice )->Release();
					*pDevice = nullptr;
				}
				hr = HRESULT_FROM_WIN32( ERROR_NOT_SUPPORTED );
			}
		}
	}

	if ( SUCCEEDED( hr ) )
	{
		ComPtr<IDXGIDevice> dxgiDevice;
		hr = ( *pDevice )->QueryInterface( IID_PPV_ARGS( dxgiDevice.GetAddressOf() ) );
		if ( SUCCEEDED( hr ) )
		{
			hr = dxgiDevice->GetAdapter( pAdapter.ReleaseAndGetAddressOf() );
			if ( SUCCEEDED( hr ) )
			{
				DXGI_ADAPTER_DESC desc;
				hr = pAdapter->GetDesc( &desc );
				if ( SUCCEEDED( hr ) )
				{
					wprintf( L"[Using DirectCompute %ls on \"%ls\"]\n",
						( fl >= D3D_FEATURE_LEVEL_11_0 ) ? L"5.0" : L"4.0", desc.Description );
				}
			}
		}

		return true;
	}
	else
		return false;
}