#pragma once

#include <stdint.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include "EAAssert/eaassert.h"
#include "EASTL/vector.h"
#include "DirectXMath/DirectXMath.h"

#define VHR(hr) if (FAILED(hr)) { EA_ASSERT(0); }
#define SAFE_RELEASE(obj) if ((obj)) { (obj)->Release(); (obj) = nullptr; }

struct FDescriptorHeap
{
	ID3D12DescriptorHeap* Heap;
	D3D12_CPU_DESCRIPTOR_HANDLE CPUStart;
	D3D12_GPU_DESCRIPTOR_HANDLE GPUStart;
	uint32_t Size;
	uint32_t Capacity;
};

struct FGPUMemoryHeap
{
	ID3D12Resource* Heap;
	uint8_t* CPUStart;
	D3D12_GPU_VIRTUAL_ADDRESS GPUStart;
	uint32_t Size;
	uint32_t Capacity;
};

struct FGraphicsContext
{
	ID3D12Device3* Device;
	ID3D12GraphicsCommandList2* CmdList;
	ID3D12CommandQueue* CmdQueue;
	ID3D12CommandAllocator* CmdAlloc[2];
	uint32_t Resolution[2];
	uint32_t DescriptorSize;
	uint32_t DescriptorSizeRTV;
	uint32_t FrameIndex;
	uint32_t BackBufferIndex;
	IDXGISwapChain3* SwapChain;
	ID3D12Resource* SwapBuffers[4];
	ID3D12Resource* DepthStencilBuffer;
	FDescriptorHeap RTVHeap;
	FDescriptorHeap DSVHeap;
	FDescriptorHeap CPUDescriptorHeap;
	FDescriptorHeap GPUDescriptorHeaps[2];
	FGPUMemoryHeap GPUUploadMemoryHeaps[2];
	ID3D12Fence* FrameFence;
	HANDLE FrameFenceEvent;
	uint64_t FrameCount;
	HWND Window;
};

struct FUIContext
{
	ID3D12RootSignature* RootSignature;
	ID3D12PipelineState* PipelineState;
	ID3D12Resource* Font;
	D3D12_CPU_DESCRIPTOR_HANDLE FontSRV;
	struct FFrame
	{
		ID3D12Resource* VertexBuffer;
		ID3D12Resource* IndexBuffer;
		void* VertexBufferCPUAddress;
		void* IndexBufferCPUAddress;
		uint32_t VertexBufferSize;
		uint32_t IndexBufferSize;
		D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
		D3D12_INDEX_BUFFER_VIEW IndexBufferView;
	} Frames[2];
};

struct FMipmapGenerator
{
	ID3D12RootSignature* RootSignature;
	ID3D12PipelineState* ComputePipeline;
	ID3D12Resource* ScratchTextures[4];
	D3D12_CPU_DESCRIPTOR_HANDLE ScratchTexturesBaseUAV;
};

void CreateMipmapGenerator(FGraphicsContext& Gfx, DXGI_FORMAT Format, FMipmapGenerator& Out);
void DestroyMipmapGenerator(FMipmapGenerator& Generator);
void GenerateMipmaps(FGraphicsContext& Gfx, FMipmapGenerator& Generator, ID3D12Resource* Texture);

void LoadPLYFile(const char* FileName, eastl::vector<XMFLOAT3>& InOutPositions, eastl::vector<XMFLOAT3>& InOutNormals, eastl::vector<XMFLOAT2>& InOutTexcoords, eastl::vector<uint32_t>& InOutTriangles);

void CreateGraphicsContext(HWND Window, bool bShouldCreateDepthBuffer, FGraphicsContext& Gfx);
void DestroyGraphicsContext(FGraphicsContext& Gfx);
FDescriptorHeap& GetDescriptorHeap(FGraphicsContext& Gfx, D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags, uint32_t& OutDescriptorSize);
void PresentFrame(FGraphicsContext& Gfx, uint32_t SwapInterval);
void WaitForGPU(FGraphicsContext& Gfx);

void CreateUIContext(FGraphicsContext& Gfx, uint32_t NumSamples, FUIContext& UI, eastl::vector<ID3D12Resource*>& OutStagingResources);
void DestroyUIContext(FUIContext& UI);
void UpdateUI(float DeltaTime);
void DrawUI(FGraphicsContext& Gfx, FUIContext& UI);

eastl::vector<uint8_t> LoadFile(const char* Name);
void UpdateFrameStats(HWND Window, const char* Name, double& OutTime, float& OutDeltaTime);
double GetTime();
HWND CreateSimpleWindow(const char* Name, uint32_t Width, uint32_t Height);

inline ID3D12GraphicsCommandList2* GetAndInitCommandList(FGraphicsContext& Gfx)
{
	Gfx.CmdAlloc[Gfx.FrameIndex]->Reset();
	Gfx.CmdList->Reset(Gfx.CmdAlloc[Gfx.FrameIndex], nullptr);
	Gfx.CmdList->SetDescriptorHeaps(1, &Gfx.GPUDescriptorHeaps[Gfx.FrameIndex].Heap);
	return Gfx.CmdList;
}

inline D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptors(FGraphicsContext& Gfx, D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32_t Count)
{
	uint32_t DescriptorSize;
	FDescriptorHeap& Heap = GetDescriptorHeap(Gfx, Type, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, DescriptorSize);
	EA_ASSERT((Heap.Size + Count) < Heap.Capacity);

	D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle;
	CPUHandle.ptr = Heap.CPUStart.ptr + (size_t)Heap.Size * DescriptorSize;
	Heap.Size += Count;

	return CPUHandle;
}

inline FDescriptorHeap GetDescriptorHeapState(FGraphicsContext& Gfx)
{
	return Gfx.CPUDescriptorHeap;
}

inline void RestoreDescriptorHeapState(FGraphicsContext& Gfx, FDescriptorHeap HeapState)
{
	Gfx.CPUDescriptorHeap = HeapState;
}

inline void AllocateGPUDescriptors(FGraphicsContext& Gfx, uint32_t Count, D3D12_CPU_DESCRIPTOR_HANDLE& OutCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE& OutGPUHandle)
{
	uint32_t DescriptorSize;
	FDescriptorHeap& Heap = GetDescriptorHeap(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, DescriptorSize);
	EA_ASSERT((Heap.Size + Count) < Heap.Capacity);

	OutCPUHandle.ptr = Heap.CPUStart.ptr + (size_t)Heap.Size * DescriptorSize;
	OutGPUHandle.ptr = Heap.GPUStart.ptr + (size_t)Heap.Size * DescriptorSize;

	Heap.Size += Count;
}

inline D3D12_GPU_DESCRIPTOR_HANDLE CopyDescriptorsToGPUHeap(FGraphicsContext& Gfx, uint32_t Count, D3D12_CPU_DESCRIPTOR_HANDLE SrcBaseHandle)
{
	D3D12_CPU_DESCRIPTOR_HANDLE CPUBaseHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE GPUBaseHandle;
	AllocateGPUDescriptors(Gfx, Count, CPUBaseHandle, GPUBaseHandle);
	Gfx.Device->CopyDescriptorsSimple(Count, CPUBaseHandle, SrcBaseHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return GPUBaseHandle;
}

inline void* AllocateGPUMemory(FGraphicsContext& Gfx, uint32_t Size, D3D12_GPU_VIRTUAL_ADDRESS& OutGPUAddress)
{
	EA_ASSERT(Size > 0);

	if (Size & 0xff)
	{
		// Always align to 256 bytes.
		Size = (Size + 255) & ~0xff;
	}

	FGPUMemoryHeap& UploadHeap = Gfx.GPUUploadMemoryHeaps[Gfx.FrameIndex];
	EA_ASSERT((UploadHeap.Size + Size) < UploadHeap.Capacity);

	void* CPUAddress = UploadHeap.CPUStart + UploadHeap.Size;
	OutGPUAddress = UploadHeap.GPUStart + UploadHeap.Size;

	UploadHeap.Size += Size;
	return CPUAddress;
}

inline void GetBackBuffer(FGraphicsContext& Gfx, ID3D12Resource*& OutBuffer, D3D12_CPU_DESCRIPTOR_HANDLE& OutHandle)
{
	OutBuffer = Gfx.SwapBuffers[Gfx.BackBufferIndex];
	OutHandle = Gfx.RTVHeap.CPUStart;
	OutHandle.ptr += Gfx.BackBufferIndex * (size_t)Gfx.DescriptorSizeRTV;
}

inline void GetDepthStencilBuffer(FGraphicsContext& Gfx, ID3D12Resource*& OutBuffer, D3D12_CPU_DESCRIPTOR_HANDLE& OutHandle)
{
	OutBuffer = Gfx.DepthStencilBuffer;
	OutHandle = Gfx.DSVHeap.CPUStart;
}
