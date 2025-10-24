// tex2dds.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>
#include <future>

#include <wrl\client.h>

#include "TexUtils.hpp"
#include "CTex2DDS.hpp"

using namespace DirectX;

HRESULT LoadTextures( CTex2DDS &spec, bool verbose = false )
{
    std::wcout << spec.GetOutFile() << std::endl;
    return spec.LoadTextures( verbose );
}

HRESULT ProcessTextures( ID3D11Device *pDevice, CTex2DDS &spec, bool verbose = false )
{
    HRESULT hr;

    std::wcout << spec.GetOutFile() << std::endl;

    // Load all textures
    hr = spec.LoadTextures( verbose );
    if ( FAILED( hr ) ) {
        std::cerr << "Failed loading textures!" << std::endl;
        return hr;
    }

    auto channels = spec.GetChannelCount();
    auto formatOut = spec.GetOutputFormat();


    // Split into red, green, blue and alpha
    std::cout << "Extracting channels..." << std::endl;
    std::vector<std::unique_ptr<ScratchImage>> slices;
    slices.reserve( channels );
    for ( size_t i = 0; i < channels; ++i ) {
        auto &slice = slices.emplace_back( std::make_unique<ScratchImage>() );
        hr = ExtractChannel( spec.GetTexture( i ), spec.GetSwizzle( i ), slice );
        if ( FAILED( hr ) ) {
            std::cerr << "Failed to split channels!" << std::endl;
            return hr;
        }
    }
    // Split channels done


    // Get slices and combine image
    std::cout << "Combining channels..." << std::endl;
    auto pCombinerImage = std::make_unique<ScratchImage>();
    hr = CombineChannelSlices( slices, formatOut, pCombinerImage, verbose );
    if ( FAILED( hr ) ) {
        std::cerr << "Failed to combine channel slices!" << std::endl;
        return hr;
    }
    // Combine done


    // Here is where we'd do PMA
    //
    //


    // Generate mipmaps
    std::cout << "Generating mips..." << std::endl;
    auto pMipMapImage = std::make_unique<ScratchImage>();
    hr = GenerateMipMapChain( formatOut, pCombinerImage, pMipMapImage, verbose );
    if ( FAILED( hr ) ) {
        std::cerr << "Failed to create mipmaps!" << std::endl;
        return hr;
    }
    // Generate mipmaps done


    // Compress image
    std::cout << "Compressing texture..." << std::endl;
    auto pCompressedImage = std::make_unique<ScratchImage>();
    hr = CompressImage( pDevice, formatOut, pMipMapImage, pCompressedImage, verbose );
    if FAILED( hr ) {
        std::cerr << "Failed to compress texture!" << std::endl;
        return hr;
    }
    // Compress done

    if ( verbose ) {
        // Decompression sanity check
        auto pDecompressedImage = std::make_unique<ScratchImage>();
        hr = Decompress( pCompressedImage->GetImages(), pCompressedImage->GetImageCount(), pCompressedImage->GetMetadata(), pMipMapImage->GetMetadata().format, *pDecompressedImage.get() );
        if FAILED( hr ) {
            std::cerr << "Failed to decompress texture for mip testing!" << std::endl;
            return hr;
        }

        auto mips = pDecompressedImage->GetMetadata().mipLevels;

        std::cout << "Last decompressed MIP channel values:";
        for ( int i = 0; i < channels; ++i ) {
            std::cout << " " << int( pDecompressedImage->GetImage( mips - 1, 0, 0 )->pixels[i] );
        }
        std::cout << std::endl;

        PrintDebugMetadata( "Final", pCompressedImage->GetMetadata() );

        float mse;
        ComputeMSE( *pCompressedImage->GetImage( 0, 0, 0 ), *pMipMapImage->GetImage( 0, 0, 0 ), mse, nullptr );
        std::cout << "RMSE = " << std::sqrt( mse / spec.GetChannelCount() ) << std::endl;

    }

    std::cout << "Saving texture..." << std::endl;
    hr = SaveToDDSFile( pCompressedImage->GetImages(), pCompressedImage->GetImageCount(), pCompressedImage->GetMetadata(), DDS_FLAGS_NONE, spec.GetOutFile().c_str() );
    if FAILED( hr ) {
        std::cerr << "Failed to save file!" << std::endl;
        return hr;
    }

    if ( verbose ) std::cout << std::endl;


    return 0;
}

HRESULT ParseFromJSON( nlohmann::json &data, ID3D11Device *pDevice, bool verbose )
{
    HRESULT hr;

    CTex2DDS spec( data );

    hr = LoadTextures( spec, verbose );
    if ( FAILED( hr ) ) {
        std::cerr << "Failed loading textures!" << std::endl;
        return hr;
    }

    hr = ProcessTextures( pDevice, spec, verbose );
    if ( FAILED( hr ) ) {
        std::cerr << "Failed processing textures!" << std::endl;
        return hr;
    }

    return 0;
}

HRESULT ParseFromJSONArray( nlohmann::json &data, ID3D11Device *pDevice, bool verbose )
{
    HRESULT hr;

    auto data_arr = data.get<std::vector<nlohmann::json>>();
    int len = int( data_arr.size() );

    auto tex2dds_arr = std::vector<std::unique_ptr<CTex2DDS>>();
    tex2dds_arr.reserve( len );

    // Parse all JSON objects as Tex2DDS instances
    int i = 0;
    for ( const auto &row : data_arr ) {
        std::cerr << "\rParsing " << ++i << "/" << len << " ";
        tex2dds_arr.emplace_back( std::make_unique<CTex2DDS>( row ) );
    }
    std::cerr << std::endl;

    // Load and resize images in SERIAL when verbose=true
    if ( verbose ) {
        i = 0;
        for ( const auto &spec : tex2dds_arr ) {
            std::cerr << "\rLoading " << ++i << "/" << len << " ";

            hr = LoadTextures( *spec.get(), verbose );
            if ( FAILED( hr ) ) {
                std::cerr << "Failed loading textures!" << std::endl;
                return hr;
            }
        }
    }

    // Load and resize images in PARALLEL when verbose=false
    else {
        std::vector<std::future<HRESULT>> futures;
        futures.reserve( len );

        for ( const auto &spec : tex2dds_arr ) {
            futures.emplace_back( std::async( std::launch::async, [&] {
                return LoadTextures( *spec.get(), verbose );
            } ) );
        }

        i = 0;
        for ( auto &f : futures ) {
            hr = f.get();
            if ( FAILED( hr ) ) {
                std::cerr << "Failed loading textures!" << std::endl;
                return hr;
            }
            std::cerr << "\rLoaded " << ++i << "/" << len << " ";
        }
    }
    std::cerr << std::endl;

    // Perform actual processing
    i = 0;
    for ( const auto &spec : tex2dds_arr ) {
        std::cerr << "\rProcessing " << ++i << "/" << len << " ";

        hr = ProcessTextures( pDevice, *spec.get(), verbose );
        if ( FAILED( hr ) ) {
            std::cerr << "Failed processing textures!" << std::endl;
            return hr;
        }
    }
    std::cerr << std::endl;

    return 0;
}

int main( int argc, char *argv[] )
{
    bool verbose = false;

    std::vector<std::string> arguments;
    arguments.reserve( argc );
    for ( int i = 1; i < argc; ++i ) {
        arguments.emplace_back( argv[i] );
    }

    for ( int i = 0; i < arguments.size(); ++i ) {
        if ( arguments[i] == "-v" || arguments[i] == "--verbose" ) {
            verbose = true;
            std::cerr << "WARNING! Verbose logging enabled, disabling parallel processing!" << std::endl;
        }
    }

    HRESULT hr = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
    if ( FAILED( hr ) ) {
        std::cerr << "Failed to init COM library!" << std::endl;
        return hr;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> pDevice;
    CreateDevice( 0, pDevice.GetAddressOf() );

    std::istreambuf_iterator<char> begin( std::cin ), end;
    std::string input( begin, end );

    try {
        auto data = nlohmann::json::parse( input );

        if ( data.is_object() ) {
            return ParseFromJSON( data, pDevice.Get(), verbose );
        }

        else if ( data.is_array() ) {
            return ParseFromJSONArray( data, pDevice.Get(), verbose );
        }
    }
    catch ( const std::exception &e ) {
        std::cerr << "Error parsing json: " << e.what() << std::endl;
        exit( -1 );
    }
    
}