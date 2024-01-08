// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/index_buffer.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	/// Direct3D 11 implementation of an index buffer.
	class IndexBufferD3D11 : public IndexBuffer
	{
	public:
		IndexBufferD3D11(GraphicsDeviceD3D11& InDevice, size_t IndexCount, IndexBufferSize IndexSize, BufferUsage usage, const void* InitialData);

	public:
		//~Begin CGxBufferBase
		virtual void* Map(LockOptions lock) override;
		virtual void Unmap() override;
		virtual void Set(uint16 slot) override;
		//~End CGxBufferBase

	private:
		GraphicsDeviceD3D11& Device;
		ComPtr<ID3D11Buffer> Buffer;
	};
}

