//
// Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#pragma once

#include <vector_types.h>

namespace demandLoading {

struct TextureInfo;

/// Abstract base class for a mipmapped image.
class ImageReader
{
  public:
    /// The destructor is virtual to ensure that instances of derived classes are properly destroyed.
    virtual ~ImageReader() = default;

    /// Open the image and read header info, including dimensions and format.  Returns false on error.
    virtual bool open( TextureInfo* info ) = 0;

    /// Close the image.
    virtual void close() = 0;

    /// Get the image info.  Valid only after calling open().
    virtual const TextureInfo& getInfo() = 0;

    /// Read the specified tile or mip level, returning the data in dest.
    /// dest must be large enough to hold the tile.  Pixels outside
    /// the bounds of the mip level will be filled in with black.
    virtual bool readTile( char* dest, unsigned int mipLevel, unsigned int tileX, unsigned int tileY, unsigned int tileWidth, unsigned int tileHeight ) = 0;

    /// Read the specified mipLevel.  Returns true for success.
    virtual bool readMipLevel( char* dest, unsigned int mipLevel, unsigned int expectedWidth, unsigned int expectedHeight ) = 0;

    /// Read the mip tail into the given buffer, starting with the specified level.  An array
    /// containing the expected dimensions of all the miplevels is provided (starting from miplevel
    /// zero), along with the pixel size.  Returns true for success.
    virtual bool readMipTail( char* dest, unsigned int mipTailFirstLevel, unsigned int numMipLevels, const uint2* mipLevelDims, unsigned int pixelSizeInBytes );
};

}  // namespace demandLoading
