#include "Library.h"
#include <stdio.h>
#include "d3dx12.h"
#include "imgui/imgui.h"
#include "EAStdC/EASprintf.h"
#include "EAStdC/EATextUtil.h"
#include "EAStdC/EABitTricks.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")


void CreateHeaps(FGraphicsContext& Gfx);

void* operator new[](size_t Size, const char* /*Name*/, int /*Flags*/, unsigned /*DebugFlags*/, const char* /*File*/, int /*Line*/)
{
	return malloc(Size);
}

void* operator new[](size_t Size, size_t Alignment, size_t AlignmentOffset, const char* /*Name*/, int /*Flags*/, unsigned /*DebugFlags*/, const char* /*File*/, int /*Line*/)
{
	return _aligned_offset_malloc(Size, Alignment, AlignmentOffset);
}

void CreateGraphicsContext(HWND Window, bool bShouldCreateDepthBuffer, FGraphicsContext& Gfx)
{
	IDXGIFactory4* Factory;
#ifdef _DEBUG
	VHR(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&Factory)));
#else
	VHR(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory)));
#endif
#ifdef _DEBUG
	{
		ID3D12Debug* Dbg;
		D3D12GetDebugInterface(IID_PPV_ARGS(&Dbg));
		if (Dbg)
		{
			Dbg->EnableDebugLayer();
			ID3D12Debug1* Dbg1;
			Dbg->QueryInterface(IID_PPV_ARGS(&Dbg1));
			if (Dbg1)
			{
				Dbg1->SetEnableGPUBasedValidation(TRUE);
			}
			SAFE_RELEASE(Dbg);
			SAFE_RELEASE(Dbg1);
		}
	}
#endif
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_1, IID_PPV_ARGS(&Gfx.Device))))
	{
		MessageBox(Window, "This application requires Windows 10 1709 (RS3) or newer.", "D3D12CreateDevice failed", MB_OK | MB_ICONERROR);
		return;
	}

	Gfx.Window = Window;

	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	CmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	VHR(Gfx.Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&Gfx.CmdQueue)));

	DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
	SwapChainDesc.BufferCount = 4;
	SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.OutputWindow = Window;
	SwapChainDesc.SampleDesc.Count = 1;
	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapChainDesc.Windowed = TRUE;

	IDXGISwapChain* TempSwapChain;
	VHR(Factory->CreateSwapChain(Gfx.CmdQueue, &SwapChainDesc, &TempSwapChain));
	VHR(TempSwapChain->QueryInterface(IID_PPV_ARGS(&Gfx.SwapChain)));
	SAFE_RELEASE(TempSwapChain);
	SAFE_RELEASE(Factory);

	RECT Rect;
	GetClientRect(Window, &Rect);
	Gfx.Resolution[0] = (uint32_t)Rect.right;
	Gfx.Resolution[1] = (uint32_t)Rect.bottom;

	for (uint32_t Idx = 0; Idx < 2; ++Idx)
	{
		VHR(Gfx.Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Gfx.CmdAlloc[Idx])));
	}

	Gfx.DescriptorSize = Gfx.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Gfx.DescriptorSizeRTV = Gfx.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CreateHeaps(Gfx);

	// Swap-buffer render targets.
	{
		D3D12_CPU_DESCRIPTOR_HANDLE Handle = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4);

		for (uint32_t Idx = 0; Idx < 4; ++Idx)
		{
			VHR(Gfx.SwapChain->GetBuffer(Idx, IID_PPV_ARGS(&Gfx.SwapBuffers[Idx])));
			Gfx.Device->CreateRenderTargetView(Gfx.SwapBuffers[Idx], nullptr, Handle);
			Handle.ptr += Gfx.DescriptorSizeRTV;
		}
	}
	// Depth-stencil target.
	if (bShouldCreateDepthBuffer)
	{
		auto ImageDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, Gfx.Resolution[0], Gfx.Resolution[1], 1, 1);
		ImageDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &ImageDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0), IID_PPV_ARGS(&Gfx.DepthStencilBuffer)));

		D3D12_CPU_DESCRIPTOR_HANDLE Handle = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

		D3D12_DEPTH_STENCIL_VIEW_DESC ViewDesc = {};
		ViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		ViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		ViewDesc.Flags = D3D12_DSV_FLAG_NONE;
		Gfx.Device->CreateDepthStencilView(Gfx.DepthStencilBuffer, &ViewDesc, Handle);
	}

	VHR(Gfx.Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Gfx.CmdAlloc[0], nullptr, IID_PPV_ARGS(&Gfx.CmdList)));
	VHR(Gfx.CmdList->Close());

	VHR(Gfx.Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Gfx.FrameFence)));
	Gfx.FrameFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

	GetAndInitCommandList(Gfx);
}

void DestroyGraphicsContext(FGraphicsContext& Gfx)
{
	CloseHandle(Gfx.FrameFenceEvent);
	SAFE_RELEASE(Gfx.CmdList);
	SAFE_RELEASE(Gfx.RTVHeap.Heap);
	SAFE_RELEASE(Gfx.DSVHeap.Heap);
	for (uint32_t Idx = 0; Idx < 4; ++Idx)
	{
		SAFE_RELEASE(Gfx.SwapBuffers[Idx]);
	}
	for (uint32_t Idx = 0; Idx < 2; ++Idx)
	{
		SAFE_RELEASE(Gfx.CmdAlloc[Idx]);
		SAFE_RELEASE(Gfx.GPUDescriptorHeaps[Idx].Heap);
		SAFE_RELEASE(Gfx.GPUUploadMemoryHeaps[Idx].Heap);
	}
	SAFE_RELEASE(Gfx.CPUDescriptorHeap.Heap);
	SAFE_RELEASE(Gfx.DepthStencilBuffer);
	SAFE_RELEASE(Gfx.FrameFence);
	SAFE_RELEASE(Gfx.SwapChain);
	SAFE_RELEASE(Gfx.CmdQueue);
	SAFE_RELEASE(Gfx.Device);
}

void PresentFrame(FGraphicsContext& Gfx, uint32_t SwapInterval)
{
	Gfx.SwapChain->Present(SwapInterval, 0);
	Gfx.CmdQueue->Signal(Gfx.FrameFence, ++Gfx.FrameCount);

	const uint64_t GPUFrameCount = Gfx.FrameFence->GetCompletedValue();

	if ((Gfx.FrameCount - GPUFrameCount) >= 2)
	{
		Gfx.FrameFence->SetEventOnCompletion(GPUFrameCount + 1, Gfx.FrameFenceEvent);
		WaitForSingleObject(Gfx.FrameFenceEvent, INFINITE);
	}

	Gfx.FrameIndex = !Gfx.FrameIndex;
	Gfx.BackBufferIndex = Gfx.SwapChain->GetCurrentBackBufferIndex();
	Gfx.GPUDescriptorHeaps[Gfx.FrameIndex].Size = 0;
	Gfx.GPUUploadMemoryHeaps[Gfx.FrameIndex].Size = 0;
}

void WaitForGPU(FGraphicsContext& Gfx)
{
	Gfx.CmdQueue->Signal(Gfx.FrameFence, ++Gfx.FrameCount);
	Gfx.FrameFence->SetEventOnCompletion(Gfx.FrameCount, Gfx.FrameFenceEvent);
	WaitForSingleObject(Gfx.FrameFenceEvent, INFINITE);

	Gfx.GPUDescriptorHeaps[Gfx.FrameIndex].Size = 0;
	Gfx.GPUUploadMemoryHeaps[Gfx.FrameIndex].Size = 0;
}

FDescriptorHeap& GetDescriptorHeap(FGraphicsContext& Gfx, D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags, uint32_t& OutDescriptorSize)
{
	if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
	{
		OutDescriptorSize = Gfx.DescriptorSizeRTV;
		return Gfx.RTVHeap;
	}
	else if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
	{
		OutDescriptorSize = Gfx.DescriptorSizeRTV;
		return Gfx.DSVHeap;
	}
	else if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		OutDescriptorSize = Gfx.DescriptorSize;
		if (Flags == D3D12_DESCRIPTOR_HEAP_FLAG_NONE)
		{
			return Gfx.CPUDescriptorHeap;
		}
		else if (Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		{
			return Gfx.GPUDescriptorHeaps[Gfx.FrameIndex];
		}
	}
	EA_ASSERT(0);
	OutDescriptorSize = 0;
	return Gfx.CPUDescriptorHeap;
}

static void CreateHeaps(FGraphicsContext& Gfx)
{
	// Render target descriptor heap (RTV).
	{
		Gfx.RTVHeap.Capacity = 16;

		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
		HeapDesc.NumDescriptors = Gfx.RTVHeap.Capacity;
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(Gfx.Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Gfx.RTVHeap.Heap)));
		Gfx.RTVHeap.CPUStart = Gfx.RTVHeap.Heap->GetCPUDescriptorHandleForHeapStart();
	}
	// Depth-stencil descriptor heap (DSV).
	{
		Gfx.DSVHeap.Capacity = 8;

		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
		HeapDesc.NumDescriptors = Gfx.DSVHeap.Capacity;
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(Gfx.Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Gfx.DSVHeap.Heap)));
		Gfx.DSVHeap.CPUStart = Gfx.DSVHeap.Heap->GetCPUDescriptorHandleForHeapStart();
	}
	// Non-shader visible descriptor heap (CBV, SRV, UAV).
	{
		Gfx.CPUDescriptorHeap.Capacity = 10000;

		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
		HeapDesc.NumDescriptors = Gfx.CPUDescriptorHeap.Capacity;
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(Gfx.Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Gfx.CPUDescriptorHeap.Heap)));
		Gfx.CPUDescriptorHeap.CPUStart = Gfx.CPUDescriptorHeap.Heap->GetCPUDescriptorHandleForHeapStart();
	}
	// Shader visible descriptor heaps (CBV, SRV, UAV).
	{
		for (uint32_t Idx = 0; Idx < 2; ++Idx)
		{
			FDescriptorHeap& Heap = Gfx.GPUDescriptorHeaps[Idx];
			Heap.Capacity = 10000;

			D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
			HeapDesc.NumDescriptors = Heap.Capacity;
			HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			VHR(Gfx.Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap.Heap)));

			Heap.CPUStart = Heap.Heap->GetCPUDescriptorHandleForHeapStart();
			Heap.GPUStart = Heap.Heap->GetGPUDescriptorHandleForHeapStart();
		}
	}
	// Upload Memory Heaps.
	{
		for (uint32_t Index = 0; Index < 2; ++Index)
		{
			FGPUMemoryHeap& UploadHeap = Gfx.GPUUploadMemoryHeaps[Index];
			UploadHeap.Size = 0;
			UploadHeap.Capacity = 8 * 1024 * 1024;
			UploadHeap.CPUStart = nullptr;
			UploadHeap.GPUStart = 0;

			VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(UploadHeap.Capacity), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&UploadHeap.Heap)));

			VHR(UploadHeap.Heap->Map(0, &CD3DX12_RANGE(0, 0), (void**)& UploadHeap.CPUStart));
			UploadHeap.GPUStart = UploadHeap.Heap->GetGPUVirtualAddress();
		}
	}
}

void CreateUIContext(FGraphicsContext& Gfx, uint32_t NumSamples, FUIContext& UI, eastl::vector<ID3D12Resource*>& OutStagingResources)
{
	ImGuiIO& IO = ImGui::GetIO();
	IO.KeyMap[ImGuiKey_Tab] = VK_TAB;
	IO.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
	IO.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
	IO.KeyMap[ImGuiKey_UpArrow] = VK_UP;
	IO.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
	IO.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
	IO.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
	IO.KeyMap[ImGuiKey_Home] = VK_HOME;
	IO.KeyMap[ImGuiKey_End] = VK_END;
	IO.KeyMap[ImGuiKey_Delete] = VK_DELETE;
	IO.KeyMap[ImGuiKey_Backspace] = VK_BACK;
	IO.KeyMap[ImGuiKey_Enter] = VK_RETURN;
	IO.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
	IO.KeyMap[ImGuiKey_A] = 'A';
	IO.KeyMap[ImGuiKey_C] = 'C';
	IO.KeyMap[ImGuiKey_V] = 'V';
	IO.KeyMap[ImGuiKey_X] = 'X';
	IO.KeyMap[ImGuiKey_Y] = 'Y';
	IO.KeyMap[ImGuiKey_Z] = 'Z';
	IO.ImeWindowHandle = Gfx.Window;
	IO.RenderDrawListsFn = nullptr;
	IO.DisplaySize = ImVec2((float)Gfx.Resolution[0], (float)Gfx.Resolution[1]);
	ImGui::GetStyle().WindowRounding = 0.0f;


	uint8_t* Pixels;
	int32_t Width, Height;
	ImGui::GetIO().Fonts->AddFontFromFileTTF("Data/Roboto-Medium.ttf", 18.0f);
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

	const auto TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, Width, Height, 1, 1);
	VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &TextureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&UI.Font)));

	{
		ID3D12Resource* StagingBuffer = nullptr;
		uint64_t BufferSize;
		Gfx.Device->GetCopyableFootprints(&TextureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &BufferSize);

		const auto BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&StagingBuffer)));
		OutStagingResources.push_back(StagingBuffer);

		D3D12_SUBRESOURCE_DATA TextureData = { Pixels, (LONG_PTR)Width * 4 };
		UpdateSubresources<1>(Gfx.CmdList, UI.Font, StagingBuffer, 0, 0, 1, &TextureData);

		Gfx.CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(UI.Font, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MipLevels = 1;

	UI.FontSRV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	Gfx.Device->CreateShaderResourceView(UI.Font, &SRVDesc, UI.FontSRV);


	D3D12_INPUT_ELEMENT_DESC InputElements[] =
	{
		{ "_Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "_Texcoords", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "_Color", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	eastl::vector<uint8_t> VSBytecode = LoadFile("Data/Shaders/UserInterface.vs.cso");
	eastl::vector<uint8_t> PSBytecode = LoadFile("Data/Shaders/UserInterface.ps.cso");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
	PSODesc.InputLayout = { InputElements, (uint32_t)eastl::size(InputElements) };
	PSODesc.VS = { VSBytecode.data(), VSBytecode.size() };
	PSODesc.PS = { PSBytecode.data(), PSBytecode.size() };
	PSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	PSODesc.RasterizerState.MultisampleEnable = NumSamples > 1 ? TRUE : FALSE;
	PSODesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	PSODesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	PSODesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	PSODesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	PSODesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	PSODesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	PSODesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	PSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	PSODesc.SampleMask = UINT_MAX;
	PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	PSODesc.NumRenderTargets = 1;
	PSODesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	PSODesc.SampleDesc.Count = NumSamples;

	VHR(Gfx.Device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&UI.PipelineState)));
	VHR(Gfx.Device->CreateRootSignature(0, VSBytecode.data(), VSBytecode.size(), IID_PPV_ARGS(&UI.RootSignature)));
}

void DestroyUIContext(FUIContext& UI)
{
	SAFE_RELEASE(UI.RootSignature);
	SAFE_RELEASE(UI.PipelineState);
	SAFE_RELEASE(UI.Font);
	for (uint32_t Idx = 0; Idx < 2; ++Idx)
	{
		SAFE_RELEASE(UI.Frames[Idx].VertexBuffer);
		SAFE_RELEASE(UI.Frames[Idx].IndexBuffer);
	}
}

void UpdateUI(float DeltaTime)
{
	ImGuiIO& IO = ImGui::GetIO();
	IO.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	IO.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	IO.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	IO.DeltaTime = DeltaTime;

	ImGui::NewFrame();
}

void DrawUI(FGraphicsContext& Gfx, FUIContext& UI)
{
	ImGui::Render();

	ImDrawData* DrawData = ImGui::GetDrawData();
	if (!DrawData || DrawData->TotalVtxCount == 0)
	{
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();
	FUIContext::FFrame& Frame = UI.Frames[Gfx.FrameIndex];

	const auto ViewportWidth = (int32_t)(IO.DisplaySize.x * IO.DisplayFramebufferScale.x);
	const auto ViewportHeight = (int32_t)(IO.DisplaySize.y * IO.DisplayFramebufferScale.y);
	DrawData->ScaleClipRects(IO.DisplayFramebufferScale);

	// Create or resize vertex buffer if needed.
	if (Frame.VertexBufferSize == 0 || Frame.VertexBufferSize < DrawData->TotalVtxCount * sizeof(ImDrawVert))
	{
		SAFE_RELEASE(Frame.VertexBuffer);
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(DrawData->TotalVtxCount * sizeof(ImDrawVert)), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&Frame.VertexBuffer)));

		VHR(Frame.VertexBuffer->Map(0, &CD3DX12_RANGE(0, 0), &Frame.VertexBufferCPUAddress));

		Frame.VertexBufferSize = DrawData->TotalVtxCount * sizeof(ImDrawVert);

		Frame.VertexBufferView.BufferLocation = Frame.VertexBuffer->GetGPUVirtualAddress();
		Frame.VertexBufferView.StrideInBytes = sizeof(ImDrawVert);
		Frame.VertexBufferView.SizeInBytes = DrawData->TotalVtxCount * sizeof(ImDrawVert);
	}

	// Create or resize index buffer if needed.
	if (Frame.IndexBufferSize == 0 || Frame.IndexBufferSize < DrawData->TotalIdxCount * sizeof(ImDrawIdx))
	{
		SAFE_RELEASE(Frame.IndexBuffer);
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(DrawData->TotalIdxCount * sizeof(ImDrawIdx)), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&Frame.IndexBuffer)));

		VHR(Frame.IndexBuffer->Map(0, &CD3DX12_RANGE(0, 0), &Frame.IndexBufferCPUAddress));

		Frame.IndexBufferSize = DrawData->TotalIdxCount * sizeof(ImDrawIdx);

		Frame.IndexBufferView.BufferLocation = Frame.IndexBuffer->GetGPUVirtualAddress();
		Frame.IndexBufferView.Format = sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
		Frame.IndexBufferView.SizeInBytes = DrawData->TotalIdxCount * sizeof(ImDrawIdx);
	}

	// Update vertex and index buffers.
	{
		ImDrawVert* VertexPtr = (ImDrawVert*)Frame.VertexBufferCPUAddress;
		ImDrawIdx* IndexPtr = (ImDrawIdx*)Frame.IndexBufferCPUAddress;

		for (uint32_t CmdListIdx = 0; CmdListIdx < (uint32_t)DrawData->CmdListsCount; ++CmdListIdx)
		{
			ImDrawList* DrawList = DrawData->CmdLists[CmdListIdx];
			memcpy(VertexPtr, &DrawList->VtxBuffer[0], DrawList->VtxBuffer.size() * sizeof(ImDrawVert));
			memcpy(IndexPtr, &DrawList->IdxBuffer[0], DrawList->IdxBuffer.size() * sizeof(ImDrawIdx));
			VertexPtr += DrawList->VtxBuffer.size();
			IndexPtr += DrawList->IdxBuffer.size();
		}
	}

	D3D12_GPU_VIRTUAL_ADDRESS ConstantBufferGPUAddress;
	auto ConstantBufferCPUAddress = (XMFLOAT4X4*)AllocateGPUMemory(Gfx, 64, ConstantBufferGPUAddress);

	// Update constant buffer.
	{
		const XMMATRIX M = XMMatrixTranspose(XMMatrixOrthographicOffCenterLH(0.0f, (float)ViewportWidth, (float)ViewportHeight, 0.0f, 0.0f, 1.0f));
		XMStoreFloat4x4(ConstantBufferCPUAddress, M);
	}

	ID3D12GraphicsCommandList2* CmdList = Gfx.CmdList;

	CmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, (float)ViewportWidth, (float)ViewportHeight));

	CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	CmdList->SetPipelineState(UI.PipelineState);

	CmdList->SetGraphicsRootSignature(UI.RootSignature);
	CmdList->SetGraphicsRootConstantBufferView(0, ConstantBufferGPUAddress);
	CmdList->SetGraphicsRootDescriptorTable(1, CopyDescriptorsToGPUHeap(Gfx, 1, UI.FontSRV));

	CmdList->IASetVertexBuffers(0, 1, &Frame.VertexBufferView);
	CmdList->IASetIndexBuffer(&Frame.IndexBufferView);


	int32_t VertexOffset = 0;
	uint32_t IndexOffset = 0;
	for (uint32_t CmdListIdx = 0; CmdListIdx < (uint32_t)DrawData->CmdListsCount; ++CmdListIdx)
	{
		ImDrawList* DrawList = DrawData->CmdLists[CmdListIdx];

		for (uint32_t CmdIndex = 0; CmdIndex < (uint32_t)DrawList->CmdBuffer.size(); ++CmdIndex)
		{
			ImDrawCmd* Cmd = &DrawList->CmdBuffer[CmdIndex];

			if (Cmd->UserCallback)
			{
				Cmd->UserCallback(DrawList, Cmd);
			}
			else
			{
				const D3D12_RECT R = CD3DX12_RECT((LONG)Cmd->ClipRect.x, (LONG)Cmd->ClipRect.y, (LONG)Cmd->ClipRect.z, (LONG)Cmd->ClipRect.w);
				CmdList->RSSetScissorRects(1, &R);
				CmdList->DrawIndexedInstanced(Cmd->ElemCount, 1, IndexOffset, VertexOffset, 0);
			}
			IndexOffset += Cmd->ElemCount;
		}
		VertexOffset += DrawList->VtxBuffer.size();
	}
}

void CreateMipmapGenerator(FGraphicsContext& Gfx, DXGI_FORMAT Format, FMipmapGenerator& OutGenerator)
{
	// We will support textures up to 2048x2048 for now.

	uint32_t Width = 2048 / 2;
	uint32_t Height = 2048 / 2;

	for (uint32_t Idx = 0; Idx < 4; ++Idx)
	{
		auto TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(Format, Width, Height, 1, 1);
		TextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &TextureDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&OutGenerator.ScratchTextures[Idx])));

		Width /= 2;
		Height /= 2;
	}

	OutGenerator.ScratchTexturesBaseUAV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);

	D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = OutGenerator.ScratchTexturesBaseUAV;
	for (uint32_t Idx = 0; Idx < 4; ++Idx)
	{
		Gfx.Device->CreateUnorderedAccessView(OutGenerator.ScratchTextures[Idx], nullptr, nullptr, CPUHandle);
		CPUHandle.ptr += Gfx.DescriptorSize;
	}

	{
		eastl::vector<uint8_t> CSBytecode = LoadFile("Data/Shaders/GenerateMipmaps.cs.cso");

		D3D12_COMPUTE_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.CS = { CSBytecode.data(), CSBytecode.size() };

		VHR(Gfx.Device->CreateComputePipelineState(&PSODesc, IID_PPV_ARGS(&OutGenerator.ComputePipeline)));
		VHR(Gfx.Device->CreateRootSignature(0, CSBytecode.data(), CSBytecode.size(), IID_PPV_ARGS(&OutGenerator.RootSignature)));
	}
}

void DestroyMipmapGenerator(FMipmapGenerator& Generator)
{
	SAFE_RELEASE(Generator.ComputePipeline);
	SAFE_RELEASE(Generator.RootSignature);
	for (uint32_t Idx = 0; Idx < 4; ++Idx)
	{
		SAFE_RELEASE(Generator.ScratchTextures[Idx]);
	}
}

void GenerateMipmaps(FGraphicsContext& Gfx, FMipmapGenerator& Generator, ID3D12Resource* Texture)
{
	EA_ASSERT(Texture);

	const D3D12_RESOURCE_DESC TextureDesc = Texture->GetDesc();

	EA_ASSERT(TextureDesc.Width <= 2048 && TextureDesc.Height <= 2048);
	EA_ASSERT(TextureDesc.Width == TextureDesc.Height);
	EA_ASSERT(EA::StdC::IsPowerOf2(TextureDesc.Width) && EA::StdC::IsPowerOf2(TextureDesc.Height));
	EA_ASSERT(TextureDesc.MipLevels > 1);

	FDescriptorHeap SavedHeapState = GetDescriptorHeapState(Gfx);

	ID3D12GraphicsCommandList2* CmdList = Gfx.CmdList;


	for (uint32_t ArraySliceIdx = 0; ArraySliceIdx < TextureDesc.DepthOrArraySize; ++ArraySliceIdx)
	{
		const D3D12_CPU_DESCRIPTOR_HANDLE TextureSRV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		SRVDesc.Texture2DArray.MostDetailedMip = 0;
		SRVDesc.Texture2DArray.MipLevels = TextureDesc.MipLevels;
		SRVDesc.Texture2DArray.ArraySize = 1;
		SRVDesc.Texture2DArray.FirstArraySlice = ArraySliceIdx;
		Gfx.Device->CreateShaderResourceView(Texture, &SRVDesc, TextureSRV);

		const D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = CopyDescriptorsToGPUHeap(Gfx, 1, TextureSRV);
		CopyDescriptorsToGPUHeap(Gfx, 4, Generator.ScratchTexturesBaseUAV);

		CmdList->SetPipelineState(Generator.ComputePipeline);
		CmdList->SetComputeRootSignature(Generator.RootSignature);

		uint32_t TotalNumMipsToGen = (uint32_t)(TextureDesc.MipLevels - 1);
		uint32_t CurrentSrcMipLevel = 0;

		for (;;)
		{
			const uint32_t NumMipsInDispatch = TotalNumMipsToGen >= 4 ? 4 : TotalNumMipsToGen;

			CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

			CmdList->SetComputeRoot32BitConstant(0, CurrentSrcMipLevel, 0);
			CmdList->SetComputeRoot32BitConstant(0, NumMipsInDispatch, 1);
			CmdList->SetComputeRootDescriptorTable(1, GPUHandle);
			CmdList->Dispatch((UINT)(TextureDesc.Width >> (4 + CurrentSrcMipLevel)), TextureDesc.Height >> (4 + CurrentSrcMipLevel), 1);

			{
				const CD3DX12_RESOURCE_BARRIER Barriers[5] =
				{
					CD3DX12_RESOURCE_BARRIER::Transition(Generator.ScratchTextures[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(Generator.ScratchTextures[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(Generator.ScratchTextures[2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(Generator.ScratchTextures[3], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(Texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
				};
				CmdList->ResourceBarrier((UINT)eastl::size(Barriers), Barriers);
			}

			for (uint32_t MipmapIdx = 0; MipmapIdx < NumMipsInDispatch; ++MipmapIdx)
			{
				const auto Dest = CD3DX12_TEXTURE_COPY_LOCATION(Texture, MipmapIdx + 1 + CurrentSrcMipLevel + ArraySliceIdx * TextureDesc.MipLevels);
				const auto Src = CD3DX12_TEXTURE_COPY_LOCATION(Generator.ScratchTextures[MipmapIdx], 0);
				const auto Box = CD3DX12_BOX(0, 0, 0, (UINT)(TextureDesc.Width >> (MipmapIdx + 1 + CurrentSrcMipLevel)), TextureDesc.Height >> (MipmapIdx + 1 + CurrentSrcMipLevel), 1);
				CmdList->CopyTextureRegion(&Dest, 0, 0, 0, &Src, &Box);
			}

			{
				const CD3DX12_RESOURCE_BARRIER Barriers[5] =
				{
					CD3DX12_RESOURCE_BARRIER::Transition(Generator.ScratchTextures[0], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(Generator.ScratchTextures[1], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(Generator.ScratchTextures[2], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(Generator.ScratchTextures[3], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(Texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
				};
				CmdList->ResourceBarrier((UINT)eastl::size(Barriers), Barriers);
			}

			if ((TotalNumMipsToGen -= NumMipsInDispatch) == 0)
			{
				break;
			}
			CurrentSrcMipLevel += NumMipsInDispatch;
		}
	}

	RestoreDescriptorHeapState(Gfx, SavedHeapState);
}

eastl::vector<uint8_t> LoadFile(const char* Name)
{
	FILE* File = fopen(Name, "rb");
	EA_ASSERT(File);
	fseek(File, 0, SEEK_END);
	long Size = ftell(File);
	if (Size <= 0)
	{
		EA_ASSERT(0);
		return eastl::vector<uint8_t>();
	}
	eastl::vector<uint8_t> Content(Size);
	fseek(File, 0, SEEK_SET);
	fread(Content.data(), 1, Content.size(), File);
	fclose(File);
	return Content;
}

void UpdateFrameStats(HWND Window, const char* Name, double& OutTime, float& OutDeltaTime)
{
	static double PreviousTime = -1.0;
	static double HeaderRefreshTime = 0.0;
	static uint32_t FrameCount = 0;

	if (PreviousTime < 0.0)
	{
		PreviousTime = GetTime();
		HeaderRefreshTime = PreviousTime;
	}

	OutTime = GetTime();
	OutDeltaTime = (float)(OutTime - PreviousTime);
	PreviousTime = OutTime;

	if ((OutTime - HeaderRefreshTime) >= 1.0)
	{
		const double FPS = FrameCount / (OutTime - HeaderRefreshTime);
		const double MS = (1.0 / FPS) * 1000.0;
		char Header[256];
		EA::StdC::Snprintf(Header, sizeof(Header), "[%.1f fps  %.3f ms] %s", FPS, MS, Name);
		SetWindowText(Window, Header);
		HeaderRefreshTime = OutTime;
		FrameCount = 0;
	}
	FrameCount++;
}

double GetTime()
{
	static LARGE_INTEGER StartCounter;
	static LARGE_INTEGER Frequency;
	if (StartCounter.QuadPart == 0)
	{
		QueryPerformanceFrequency(&Frequency);
		QueryPerformanceCounter(&StartCounter);
	}
	LARGE_INTEGER Counter;
	QueryPerformanceCounter(&Counter);
	return (Counter.QuadPart - StartCounter.QuadPart) / (double)Frequency.QuadPart;
}

static LRESULT CALLBACK ProcessWindowMessage(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	ImGuiIO& IO = ImGui::GetIO();

	switch (Message)
	{
	case WM_LBUTTONDOWN:
		IO.MouseDown[0] = true;
		return 0;
	case WM_LBUTTONUP:
		IO.MouseDown[0] = false;
		return 0;
	case WM_RBUTTONDOWN:
		IO.MouseDown[1] = true;
		return 0;
	case WM_RBUTTONUP:
		IO.MouseDown[1] = false;
		return 0;
	case WM_MBUTTONDOWN:
		IO.MouseDown[2] = true;
		return 0;
	case WM_MBUTTONUP:
		IO.MouseDown[2] = false;
		return 0;
	case WM_MOUSEWHEEL:
		IO.MouseWheel += GET_WHEEL_DELTA_WPARAM(WParam) > 0 ? 1.0f : -1.0f;
		return 0;
	case WM_MOUSEMOVE:
		IO.MousePos.x = (signed short)LParam;
		IO.MousePos.y = (signed short)(LParam >> 16);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_KEYDOWN:
	{
		if (WParam < 256)
		{
			IO.KeysDown[WParam] = true;
			if (WParam == VK_ESCAPE)
				PostQuitMessage(0);
			return 0;
		}
	}
	break;
	case WM_KEYUP:
	{
		if (WParam < 256)
		{
			IO.KeysDown[WParam] = false;
			return 0;
		}
	}
	break;
	case WM_CHAR:
	{
		if (WParam > 0 && WParam < 0x10000)
		{
			IO.AddInputCharacter((unsigned short)WParam);
			return 0;
		}
	}
	break;
	}
	return DefWindowProc(Window, Message, WParam, LParam);
}

HWND CreateSimpleWindow(const char* Name, uint32_t Width, uint32_t Height)
{
	WNDCLASS WinClass = {};
	WinClass.lpfnWndProc = ProcessWindowMessage;
	WinClass.hInstance = GetModuleHandle(nullptr);
	WinClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	WinClass.lpszClassName = Name;
	if (!RegisterClass(&WinClass))
	{
		EA_ASSERT(0);
	}

	RECT Rect = { 0, 0, (LONG)Width, (LONG)Height };
	if (!AdjustWindowRect(&Rect, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX, 0))
	{
		EA_ASSERT(0);
	}

	HWND Window = CreateWindowEx(0, Name, Name, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, Rect.right - Rect.left, Rect.bottom - Rect.top, nullptr, nullptr, nullptr, 0);
	EA_ASSERT(Window);
	return Window;
}

void LoadPLYFile(const char* FileName, eastl::vector<XMFLOAT3>& InOutPositions, eastl::vector<XMFLOAT3>& InOutNormals, eastl::vector<XMFLOAT2>& InOutTexcoords, eastl::vector<uint32_t>& InOutTriangles)
{
	using namespace EA::StdC;
	FILE* File = fopen(FileName, "r");
	EA_ASSERT(File);

	char LineBuffer[1024];
	char Token[64];
	uint32_t NumVertices = UINT32_MAX;
	uint32_t NumTriangles = UINT32_MAX;

	struct FProperty
	{
		const char* Name;
		bool bIsPresent;
	} Properties[] =
	{
		{ "x\n", false }, { "y\n", false }, { "z\n", false },
		{ "nx\n", false }, { "ny\n", false }, { "nz\n", false },
		{ "s\n", false }, { "t\n", false },
	};
	bool bHasPositions = false;
	bool bHasNormals = false;
	bool bHasTexcoords = false;

	while (fgets(LineBuffer, sizeof(LineBuffer), File))
	{
		const char* Line = LineBuffer;
		while (SplitTokenSeparated(Line, kLengthNull, ' ', Token, sizeof(Token), &Line))
		{
			if (Strcmp(Token, "comment") == 0 || Strcmp(Token, "format") == 0)
			{
				break; // Skip the line.
			}
			else if (Strcmp(Token, "vertex") == 0)
			{
				NumVertices = AtoU32(Line);
			}
			else if (Strcmp(Token, "face") == 0)
			{
				NumTriangles = AtoU32(Line);
			}
			else if (Strcmp(Token, "float") == 0)
			{
				for (uint32_t Idx = 0; Idx < eastl::size(Properties); ++Idx)
				{
					if (Strcmp(Line, Properties[Idx].Name) == 0)
					{
						Properties[Idx].bIsPresent = true;
						break;
					}
				}
			}
			else if (Strcmp(Token, "end_header\n") == 0)
			{
				bHasPositions = Properties[0].bIsPresent && Properties[1].bIsPresent && Properties[2].bIsPresent;
				bHasNormals = Properties[3].bIsPresent && Properties[4].bIsPresent && Properties[5].bIsPresent;
				bHasTexcoords = Properties[6].bIsPresent && Properties[7].bIsPresent;
				goto HeaderIsDone;
			}
		}
	}
HeaderIsDone:

	EA_ASSERT(bHasPositions);
	EA_ASSERT(NumVertices != UINT32_MAX);
	EA_ASSERT(NumTriangles != UINT32_MAX);

	InOutPositions.reserve(InOutPositions.size() + NumVertices);
	if (bHasNormals)
	{
		InOutNormals.reserve(InOutNormals.size() + NumVertices);
	}
	if (bHasTexcoords)
	{
		InOutTexcoords.reserve(InOutTexcoords.size() + NumVertices);
	}

	for (uint32_t LineIdx = 0; LineIdx < NumVertices; ++LineIdx)
	{
		char* Line = fgets(LineBuffer, sizeof(LineBuffer), File);
		EA_ASSERT(Line);

		XMFLOAT3 Position;
		Position.x = StrtoF32(Line, &Line);
		Position.y = StrtoF32(Line, &Line);
		Position.z = StrtoF32(Line, &Line);
		InOutPositions.push_back(Position);

		if (bHasNormals)
		{
			XMFLOAT3 Normal;
			Normal.x = StrtoF32(Line, &Line);
			Normal.y = StrtoF32(Line, &Line);
			Normal.z = StrtoF32(Line, &Line);
			InOutNormals.push_back(Normal);
		}
		if (bHasTexcoords)
		{
			XMFLOAT2 Texcoord;
			Texcoord.x = StrtoF32(Line, &Line);
			Texcoord.y = StrtoF32(Line, &Line);
			InOutTexcoords.push_back(Texcoord);
		}
	}

	InOutTriangles.reserve(InOutTriangles.size() + NumTriangles * 3);

	for (uint32_t LineIdx = 0; LineIdx < NumTriangles; ++LineIdx)
	{
		char* Line = fgets(LineBuffer, sizeof(LineBuffer), File);
		EA_ASSERT(Line);

		uint32_t NumIndices = StrtoU32(Line, &Line, 10);
		EA_ASSERT(NumIndices == 3);

		uint32_t Triangle[3];
		Triangle[0] = StrtoU32(Line, &Line, 10);
		Triangle[1] = StrtoU32(Line, &Line, 10);
		Triangle[2] = StrtoU32(Line, &Line, 10);

		InOutTriangles.push_back(Triangle[0]);
		InOutTriangles.push_back(Triangle[1]);
		InOutTriangles.push_back(Triangle[2]);
	}

	fclose(File);
}
