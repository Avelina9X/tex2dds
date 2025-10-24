#include "pch.h"
#include "CTex2DDS.hpp"

#include <iostream>

using namespace DirectX;

SRGB_INPUT ParseSRGB( const nlohmann::json &data, const std::string &ctx )
{
    if ( !data.is_string() ) throw std::runtime_error( "'srgb' must be a string for " + ctx );

    auto srgb = data.get<std::string>();

    if ( srgb == "FORCE_SRGB" )    return FORCE_SRGB;
    if ( srgb == "ASSUME_SRGB" )   return ASSUME_SRGB;
    if ( srgb == "ASSUME_LINEAR" ) return ASSUME_LINEAR;
    if ( srgb == "FORCE_LINEAR" )  return FORCE_LINEAR;
    throw std::runtime_error( "Unknown srgb type '" + srgb + "' for " + ctx );
}

DXGI_FORMAT ParseFormat( const nlohmann::json &data, const std::string &ctx ) {
    if ( !data.is_string() ) throw std::runtime_error( "'format' must be a string for " + ctx );

    auto format = data.get<std::string>();

    auto result = LookupByName( format.c_str(), g_pFormats );
    if ( result == DXGI_FORMAT_UNKNOWN ) {
        throw std::runtime_error( "Unknown format '" + format + "' for " + ctx );
    }
    return result;
}

std::pair<int, int> ParseResolution( const nlohmann::json &data, const std::string &ctx )
{
    if ( !data.is_array() ) throw std::runtime_error( "'resolution' must be an array for " + ctx );
    auto resolution = data.get<std::vector<int>>();
    if ( resolution.size() != 2 ) throw std::runtime_error( "'resolution' must be an array of length 2 for " + ctx );
    return { resolution[0], resolution[1] };
}

CTex2DDS::CTex2DDS( nlohmann::json data ) {
    // output_path
    if ( !data["output_path"].is_string() ) throw std::runtime_error( "'output_path' must be a string!" );
    auto outputPath = data["output_path"].get<std::string>();
    m_szOutoutPath = std::wstring( outputPath.begin(), outputPath.end() );

    m_srgb = ParseSRGB( data["srgb"], outputPath );

    m_format = ParseFormat( data["format"], outputPath );

    auto resolution = ParseResolution( data["resolution"], outputPath );
    m_width = resolution.first;
    m_height = resolution.second;


    // channels
    if ( !data["channels"].is_array() ) throw std::runtime_error( "'channels' must be an array for " + outputPath  );

    auto channels = data["channels"].get<std::vector<nlohmann::json>>();
    m_channels.reserve( channels.size() );

    for ( const auto &i : channels ) {
        if ( !i["file"].is_null() && !i["file"].is_string() ) throw std::runtime_error( "'channels.file' must be null or a string for " + outputPath  );
        if ( !i["src"].is_string() ) throw std::runtime_error( "'channels.src' must be a single character for " + outputPath );

        auto src = i["src"].get<std::string>();
        if ( src.length() != 1 ) throw std::runtime_error( "'channels.src' must be a single character for " + outputPath  );

        if ( i["file"].is_null() ) {
            m_channels.emplace_back( nullptr, src[0] );
        }
        else {
            auto temp = i["file"].get<std::string>();
            m_channels.emplace_back( std::wstring( temp.begin(), temp.end() ), src[0] );
        }
    }
}

HRESULT CTex2DDS::LoadTextures( bool verbose ) {
    for ( const auto &i : m_channels ) {
        if ( i.szFile.has_value() ) {
            std::wstring file = i.szFile.value();

            if ( !m_textureMap.contains( file ) ) {
                auto [it, inserted] = m_textureMap.emplace( file, std::make_unique<ScratchImage>() );
                auto &pInputImage = it->second;

                std::cout << "Loading image..." << std::endl;
                HRESULT hr = LoadImageWithSRGB( file.c_str(), m_srgb, m_format, pInputImage, verbose );
                if ( FAILED( hr ) ) {
                    std::wcerr << "Failed to load image: " << file << std::endl;
                    return hr;
                }

                std::cout << "Resizing image..." << std::endl;
                hr = ResizeImage( m_width, m_height, pInputImage, m_format );
                if ( FAILED( hr ) ) {
                    std::wcerr << "Failed to resize image: " << file << std::endl;
                    return hr;
                }
            }
        }
    }

    if ( m_textureMap.empty() ) return E_FAIL;

    return 0;
}