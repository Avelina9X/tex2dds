#pragma once

#include "TexUtils.hpp"

class CTex2DDS
{
public:
    CTex2DDS( SRGB_INPUT srgb, DXGI_FORMAT format, std::vector<ChannelSwizzle> channels, int width, int height, const wchar_t *outputPath ) :
        m_srgb( srgb ),
        m_format( format ),
        m_channels( channels ),
        m_width( width ),
        m_height( height ),
        m_szOutoutPath( outputPath )
    {
    }

    CTex2DDS( nlohmann::json data );

    HRESULT LoadTextures( bool verbose = false );

    const SRGB_INPUT GetInputSRGB() { return m_srgb; }
    const DXGI_FORMAT GetOutputFormat() { return m_format; }
    const size_t GetChannelCount() { return m_channels.size(); }
    const auto &GetChannelMap() { return m_textureMap; }
    const std::wstring GetOutFile() { return m_szOutoutPath; }

    const int GetWidth() { return m_width; }
    const int GetHeight() { return m_height; }

    const std::unique_ptr<DirectX::ScratchImage> &GetTexture( size_t n ) {
        const auto &file = m_channels[n].szFile;

        if ( !file.has_value() ) {
            return m_textureMap.begin()->second;
        }

        return m_textureMap[file.value()];
    }

    const char GetSwizzle( size_t n ) {
        return m_channels[n].swizzle;
    }

protected:
    SRGB_INPUT m_srgb;
    DXGI_FORMAT m_format;
    std::vector<ChannelSwizzle> m_channels;
    std::map<std::wstring, std::unique_ptr<DirectX::ScratchImage>> m_textureMap;
    int m_width;
    int m_height;
    std::wstring m_szOutoutPath;
};