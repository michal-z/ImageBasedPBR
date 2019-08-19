#include "Library.h"
#include "CPUAndGPUCommon.h"
#include "d3dx12.h"
#include "imgui/imgui.h"
#include "EAStdC/EAStdC.h"
#include "EAStdC/EASprintf.h"
#include "stb_image.h"


struct FVertex
{
	XMFLOAT3 Position;
	XMFLOAT3 Normal;
};

struct FStaticMesh
{
	uint32_t IndexCount;
	uint32_t StartIndexLocation;
	uint32_t BaseVertexLocation;
};

struct FStaticMeshInstance
{
	XMFLOAT3 Position;
	XMFLOAT3 Rotation;
	uint32_t MeshIndex;
	float Roughness;
	float Metallic;
};

struct FDemoRoot
{
	FGraphicsContext Gfx;
	FUIContext UI;
	eastl::vector<FStaticMesh> StaticMeshes;
	eastl::vector<FStaticMeshInstance> StaticMeshInstances;
	eastl::vector<ID3D12PipelineState*> Pipelines;
	eastl::vector<ID3D12RootSignature*> RootSignatures;
	ID3D12Resource* StaticVB;
	ID3D12Resource* StaticIB;
	D3D12_VERTEX_BUFFER_VIEW StaticVBView;
	D3D12_INDEX_BUFFER_VIEW StaticIBView;
	XMFLOAT3 CameraPosition;
	XMFLOAT3 CameraFocusPosition;
	ID3D12Resource* SkyBox;
	D3D12_CPU_DESCRIPTOR_HANDLE SkyBoxSRV;
	ID3D12Resource* MSColor;
	ID3D12Resource* MSDepth;
	D3D12_CPU_DESCRIPTOR_HANDLE MSColorRTV;
	D3D12_CPU_DESCRIPTOR_HANDLE MSDepthRTV;
};

static void Update(FDemoRoot& Root, double Time, float DeltaTime)
{
	// Update camera position.
	{
		const float Angle = XMScalarModAngle(0.25f * (float)Time);
		XMVECTOR Position = XMVectorSet(12.0f * cosf(Angle), 6.0f, 12.0f * sinf(Angle), 1.0f);
		XMStoreFloat3(&Root.CameraPosition, Position);
	}

	ImGui::ShowDemoWindow();
}

static void Draw(FDemoRoot& Root)
{
	FGraphicsContext& Gfx = Root.Gfx;
	ID3D12GraphicsCommandList2* CmdList = GetAndInitCommandList(Gfx);

	CmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, (float)Gfx.Resolution[0], (float)Gfx.Resolution[1]));
	CmdList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, (LONG)Gfx.Resolution[0], (LONG)Gfx.Resolution[1]));

	CmdList->OMSetRenderTargets(1, &Root.MSColorRTV, TRUE, &Root.MSDepthRTV);
	CmdList->ClearRenderTargetView(Root.MSColorRTV, XMVECTORF32{ 0.0f }, 0, nullptr);
	CmdList->ClearDepthStencilView(Root.MSDepthRTV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	CmdList->IASetVertexBuffers(0, 1, &Root.StaticVBView);
	CmdList->IASetIndexBuffer(&Root.StaticIBView);

	const XMMATRIX ViewTransform = XMMatrixLookAtLH(XMLoadFloat3(&Root.CameraPosition), XMLoadFloat3(&Root.CameraFocusPosition), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
	const XMMATRIX ProjectionTransform = XMMatrixPerspectiveFovLH(XM_PI / 3, 1.777f, 0.1f, 100.0f);

	// Draw all static mesh instances.
	{
		CmdList->SetPipelineState(Root.Pipelines[1]);
		CmdList->SetGraphicsRootSignature(Root.RootSignatures[1]);

		// Per-frame constant data.
		{
			D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;
			auto* CPUAddress = (FPerFrameConstantData*)AllocateGPUMemory(Gfx, sizeof(FPerFrameConstantData), GPUAddress);

			CPUAddress->LightPositions[0] = XMFLOAT4(-10.0f, 10.0f, -10.0f, 1.0f);
			CPUAddress->LightPositions[1] = XMFLOAT4(10.0f, 10.0f, -10.0f, 1.0f);
			CPUAddress->LightPositions[2] = XMFLOAT4(-10.0f, -10.0f, -10.0f, 1.0f);
			CPUAddress->LightPositions[3] = XMFLOAT4(10.0f, -10.0f, -10.0f, 1.0f);

			CPUAddress->LightColors[0] = XMFLOAT4(300.0f, 300.0f, 300.0f, 1.0f);
			CPUAddress->LightColors[1] = XMFLOAT4(300.0f, 300.0f, 300.0f, 1.0f);
			CPUAddress->LightColors[2] = XMFLOAT4(300.0f, 300.0f, 300.0f, 1.0f);
			CPUAddress->LightColors[3] = XMFLOAT4(300.0f, 300.0f, 300.0f, 1.0f);

			XMFLOAT3 P = Root.CameraPosition;
			CPUAddress->ViewerPosition = XMFLOAT4(P.x, P.y, P.z, 1.0f);

			CmdList->SetGraphicsRootConstantBufferView(1, GPUAddress);
		}

		const auto NumMeshInstances = (uint32_t)Root.StaticMeshInstances.size();

		D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;
		auto* CPUAddress = (FPerDrawConstantData*)AllocateGPUMemory(Gfx, NumMeshInstances * sizeof(FPerDrawConstantData), GPUAddress);

		const XMMATRIX WorldToClip = ViewTransform * ProjectionTransform;

		for (uint32_t MeshInstIdx = 0; MeshInstIdx < NumMeshInstances; ++MeshInstIdx)
		{
			const FStaticMeshInstance& MeshInst = Root.StaticMeshInstances[MeshInstIdx];
			const FStaticMesh& Mesh = Root.StaticMeshes[MeshInst.MeshIndex];

			const XMMATRIX ObjectToWorld =
				XMMatrixRotationRollPitchYaw(MeshInst.Rotation.x, MeshInst.Rotation.y, MeshInst.Rotation.z) *
				XMMatrixTranslation(MeshInst.Position.x, MeshInst.Position.y, MeshInst.Position.z);

			XMStoreFloat4x4(&CPUAddress->ObjectToClip, XMMatrixTranspose(ObjectToWorld * WorldToClip));
			{
				const XMMATRIX ObjectToWorldT = XMMatrixTranspose(ObjectToWorld);
				XMStoreFloat4((XMFLOAT4*)& CPUAddress->ObjectToWorld, ObjectToWorldT.r[0]);
				XMStoreFloat4((XMFLOAT4*)& CPUAddress->ObjectToWorld + 1, ObjectToWorldT.r[1]);
				XMStoreFloat4((XMFLOAT4*)& CPUAddress->ObjectToWorld + 2, ObjectToWorldT.r[2]);
			}

			CPUAddress->Albedo = XMFLOAT3(0.5f, 0.0f, 0.0f);
			CPUAddress->Metallic = MeshInst.Metallic;
			CPUAddress->Roughness = MeshInst.Roughness;
			CPUAddress->AO = 1.0f;

			CmdList->SetGraphicsRootConstantBufferView(0, GPUAddress);
			CmdList->DrawIndexedInstanced(Mesh.IndexCount, 1, Mesh.StartIndexLocation, Mesh.BaseVertexLocation, 0);

			GPUAddress += sizeof(FPerDrawConstantData);
			CPUAddress++;
		}
	}

	// Draw SkyBox.
	{
		CmdList->SetPipelineState(Root.Pipelines[2]);
		CmdList->SetGraphicsRootSignature(Root.RootSignatures[2]);

		D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;
		auto* CPUAddress = (FPerDrawConstantData*)AllocateGPUMemory(Gfx, sizeof(FPerDrawConstantData), GPUAddress);

		XMMATRIX ViewTransformOrigin = ViewTransform;
		ViewTransformOrigin.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

		const XMMATRIX ObjectToClip = ViewTransformOrigin * ProjectionTransform;
		XMStoreFloat4x4(&CPUAddress->ObjectToClip, XMMatrixTranspose(ObjectToClip));

		const FStaticMesh& Mesh = Root.StaticMeshes[0]; // Cube mesh.

		CmdList->SetGraphicsRootConstantBufferView(0, GPUAddress);
		CmdList->SetGraphicsRootDescriptorTable(1, CopyDescriptorsToGPUHeap(Gfx, 1, Root.SkyBoxSRV));
		CmdList->DrawIndexedInstanced(Mesh.IndexCount, 1, Mesh.StartIndexLocation, Mesh.BaseVertexLocation, 0);
	}

	DrawUI(Gfx, Root.UI);

	{
		ID3D12Resource* BackBuffer;
		D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRTV;
		GetBackBuffer(Gfx, BackBuffer, BackBufferRTV);

		D3D12_RESOURCE_BARRIER Barriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(Root.MSColor, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
		};
		CmdList->ResourceBarrier((UINT)eastl::size(Barriers), Barriers);

		CmdList->ResolveSubresource(BackBuffer, 0, Root.MSColor, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

		eastl::swap(Barriers[0].Transition.StateBefore, Barriers[0].Transition.StateAfter);
		eastl::swap(Barriers[1].Transition.StateBefore, Barriers[1].Transition.StateAfter);
		CmdList->ResourceBarrier((UINT)eastl::size(Barriers), Barriers);

		CmdList->Close();
	}

	Gfx.CmdQueue->ExecuteCommandLists(1, CommandListCast(&CmdList));
}

static void AddGraphicsPipeline(FGraphicsContext& Gfx, D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODesc, const char* VSName, const char* PSName, eastl::vector<ID3D12PipelineState*>& OutPipelines, eastl::vector<ID3D12RootSignature*>& OutSignatures)
{
	char Path[MAX_PATH];

	EA::StdC::Snprintf(Path, sizeof(Path), "Data/Shaders/%s", VSName);
	eastl::vector<uint8_t> VSBytecode = LoadFile(Path);

	EA::StdC::Snprintf(Path, sizeof(Path), "Data/Shaders/%s", PSName);
	eastl::vector<uint8_t> PSBytecode = LoadFile(Path);

	ID3D12RootSignature* RootSignature;
	VHR(Gfx.Device->CreateRootSignature(0, VSBytecode.data(), VSBytecode.size(), IID_PPV_ARGS(&RootSignature)));

	PSODesc.pRootSignature = RootSignature;
	PSODesc.VS = { VSBytecode.data(), VSBytecode.size() };
	PSODesc.PS = { PSBytecode.data(), PSBytecode.size() };

	ID3D12PipelineState* Pipeline;
	VHR(Gfx.Device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&Pipeline)));
	OutPipelines.push_back(Pipeline);
	OutSignatures.push_back(RootSignature);
}

static void CreatePipelines(FGraphicsContext& Gfx, eastl::vector<ID3D12PipelineState*>& OutPipelines, eastl::vector<ID3D12RootSignature*>& OutSignatures)
{
	const D3D12_INPUT_ELEMENT_DESC InPositionNormal[] =
	{
		{ "_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "_Normal", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	const uint32_t NumSamples = 8;

	// Test pipeline.
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.InputLayout = { InPositionNormal, (UINT)eastl::size(InPositionNormal) };
		PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		PSODesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		PSODesc.SampleMask = UINT32_MAX;
		PSODesc.SampleDesc.Count = 1;
		AddGraphicsPipeline(Gfx, PSODesc, "Test.vs.cso", "Test.ps.cso", OutPipelines, OutSignatures);
	}

	// SimpleForward pipeline.
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.InputLayout = { InPositionNormal, (UINT)eastl::size(InPositionNormal) };
		PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
		PSODesc.RasterizerState.MultisampleEnable = NumSamples > 1 ? TRUE : FALSE;
		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		PSODesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		PSODesc.SampleMask = UINT32_MAX;
		PSODesc.SampleDesc.Count = NumSamples;
		AddGraphicsPipeline(Gfx, PSODesc, "SimpleForward.vs.cso", "SimpleForward.ps.cso", OutPipelines, OutSignatures);
	}

	// SkyBox pipeline.
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.InputLayout = { InPositionNormal, (UINT)eastl::size(InPositionNormal) };
		PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
		PSODesc.RasterizerState.MultisampleEnable = NumSamples > 1 ? TRUE : FALSE;
		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		PSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		PSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		PSODesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		PSODesc.SampleMask = UINT32_MAX;
		PSODesc.SampleDesc.Count = 8;
		AddGraphicsPipeline(Gfx, PSODesc, "SkyBox.vs.cso", "SkyBox.ps.cso", OutPipelines, OutSignatures);
	}

	// EquirectangularToCube pipeline.
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.InputLayout = { InPositionNormal, (UINT)eastl::size(InPositionNormal) };
		PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		PSODesc.DepthStencilState.DepthEnable = FALSE;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		PSODesc.SampleMask = UINT32_MAX;
		PSODesc.SampleDesc.Count = 1;
		AddGraphicsPipeline(Gfx, PSODesc, "EquirectangularToCube.vs.cso", "EquirectangularToCube.ps.cso", OutPipelines, OutSignatures);
	}
}

static void CreateSkyBox(FDemoRoot& Root, eastl::vector<ID3D12Resource*>& OutTempResources)
{
	FGraphicsContext& Gfx = Root.Gfx;

	int Width, Height;
	D3D12_SUBRESOURCE_DATA ImageData = {};
	stbi_set_flip_vertically_on_load(1);
	ImageData.pData = stbi_loadf("Data/Textures/Newport_Loft.hdr", &Width, &Height, nullptr, 3);
	stbi_set_flip_vertically_on_load(0);
	ImageData.RowPitch = Width * sizeof(XMFLOAT3);
	EA_ASSERT(ImageData.pData);

	ID3D12Resource* TempHDRRectTexture;
	D3D12_CPU_DESCRIPTOR_HANDLE TempHDRRectTextureSRV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	{
		const auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32_FLOAT, Width, Height, 1, 1);
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&TempHDRRectTexture)));
		OutTempResources.push_back(TempHDRRectTexture);

		Gfx.Device->CreateShaderResourceView(TempHDRRectTexture, nullptr, TempHDRRectTextureSRV);
	}

	ID3D12Resource* StagingBuffer;
	{
		const auto BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(TempHDRRectTexture, 0, 1));
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&StagingBuffer)));
		OutTempResources.push_back(StagingBuffer);
	}

	ID3D12Resource* TempCubeMap;
	const D3D12_CPU_DESCRIPTOR_HANDLE TempCubeMapRTVs = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6);
	{
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, 512, 512, 6);
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&Root.SkyBox)));

		Root.SkyBoxSRV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels = -1;
		Gfx.Device->CreateShaderResourceView(Root.SkyBox, &SRVDesc, Root.SkyBoxSRV);

		
		Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&TempCubeMap)));
		OutTempResources.push_back(TempCubeMap);

		D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = TempCubeMapRTVs;

		for (uint32_t Idx = 0; Idx < 6; ++Idx)
		{
			D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
			RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			RTVDesc.Texture2DArray.ArraySize = 1;
			RTVDesc.Texture2DArray.FirstArraySlice = Idx;
			Gfx.Device->CreateRenderTargetView(TempCubeMap, &RTVDesc, CPUHandle);

			CPUHandle.ptr += Gfx.DescriptorSizeRTV;
		}
	}

	ID3D12GraphicsCommandList2* CmdList = Gfx.CmdList;

	UpdateSubresources<1>(CmdList, TempHDRRectTexture, StagingBuffer, 0, 0, 1, &ImageData);

	stbi_image_free((void*)ImageData.pData);

	CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(TempHDRRectTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));


	CmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, 512.0f, 512.0f));
	CmdList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, 512, 512));

	CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	CmdList->IASetVertexBuffers(0, 1, &Root.StaticVBView);
	CmdList->IASetIndexBuffer(&Root.StaticIBView);

	const XMMATRIX ViewTransforms[6] =
	{
		XMMatrixLookToLH(XMVectorZero(), XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
		XMMatrixLookToLH(XMVectorZero(), XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
		XMMatrixLookToLH(XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)),
		XMMatrixLookToLH(XMVectorZero(), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)),
		XMMatrixLookToLH(XMVectorZero(), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
		XMMatrixLookToLH(XMVectorZero(), XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
	};
	const XMMATRIX ProjectionTransform = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, 0.1f, 10.0f);

	CmdList->SetPipelineState(Root.Pipelines[3]);
	CmdList->SetGraphicsRootSignature(Root.RootSignatures[3]);

	D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;
	auto* CPUAddress = (FPerDrawConstantData*)AllocateGPUMemory(Gfx, 6 * sizeof(FPerDrawConstantData), GPUAddress);

	D3D12_CPU_DESCRIPTOR_HANDLE RTV = TempCubeMapRTVs;

	for (uint32_t Idx = 0; Idx < 6; ++Idx)
	{
		CmdList->OMSetRenderTargets(1, &RTV, TRUE, nullptr);

		const XMMATRIX ObjectToClip = ViewTransforms[Idx] * ProjectionTransform;
		XMStoreFloat4x4(&CPUAddress->ObjectToClip, XMMatrixTranspose(ObjectToClip));

		const FStaticMesh& Mesh = Root.StaticMeshes[0]; // Cube mesh.

		CmdList->SetGraphicsRootConstantBufferView(0, GPUAddress);
		CmdList->SetGraphicsRootDescriptorTable(1, CopyDescriptorsToGPUHeap(Gfx, 1, TempHDRRectTextureSRV));
		CmdList->DrawIndexedInstanced(Mesh.IndexCount, 1, Mesh.StartIndexLocation, Mesh.BaseVertexLocation, 0);

		RTV.ptr += Gfx.DescriptorSizeRTV;
		GPUAddress += sizeof(FPerDrawConstantData);
		CPUAddress++;
	}

	CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(TempCubeMap, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

	CmdList->CopyResource(Root.SkyBox, TempCubeMap);

	Gfx.CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Root.SkyBox, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

static void Initialize(FDemoRoot& Root, eastl::vector<ID3D12Resource*>& OutTempResources, eastl::vector<ID3D12Resource*>& OutTexturesThatNeedMipmaps)
{
	FGraphicsContext& Gfx = Root.Gfx;

	CreatePipelines(Gfx, Root.Pipelines, Root.RootSignatures);

	eastl::vector<FVertex> VertexData;
	eastl::vector<uint32_t> Triangles;

	// Scene data.
	{
		eastl::vector<XMFLOAT3> Positions;
		eastl::vector<XMFLOAT3> Normals;
		eastl::vector<XMFLOAT2> Texcoords;

		const char* MeshNames[] = { "Data/Meshes/Cube.ply", "Data/Meshes/Sphere.ply" };
		for (uint32_t MeshIdx = 0; MeshIdx < eastl::size(MeshNames); ++MeshIdx)
		{
			const size_t PositionsSize = Positions.size();
			const size_t TrianglesSize = Triangles.size();
			LoadPLYFile(MeshNames[MeshIdx], Positions, Normals, Texcoords, Triangles);

			const uint32_t IndexCount = uint32_t(Triangles.size() - TrianglesSize);
			const uint32_t StartIndexLocation = uint32_t(TrianglesSize);
			const uint32_t BaseVertexLocation = uint32_t(PositionsSize);
			Root.StaticMeshes.push_back(FStaticMesh{ IndexCount, StartIndexLocation, BaseVertexLocation });
		}

		VertexData.reserve(Positions.size());

		for (uint32_t Idx = 0; Idx < Positions.size(); ++Idx)
		{
			FVertex Vertex;
			Vertex.Position = Positions[Idx];
			Vertex.Normal = Normals[Idx];
			VertexData.push_back(Vertex);
		}

		const int32_t NumRows = 5;
		const int32_t NumColumns = 7;
		float Metallic = 0.0f;
		for (int32_t RowIdx = 0; RowIdx < NumRows; ++RowIdx)
		{
			float Roughness = 0.03f;
			for (int32_t ColumnIdx = 0; ColumnIdx < NumColumns; ++ColumnIdx)
			{
				FStaticMeshInstance Instance = {};
				float X = 2.2f * (-NumColumns * 0.5f + ColumnIdx + 0.5f);
				float Y = 2.2f * (-NumRows * 0.5f + RowIdx + 0.5f);
				Instance.Position = XMFLOAT3(X, Y, 0.0f);
				Instance.MeshIndex = 1;
				Instance.Roughness = Roughness;
				Instance.Metallic = Metallic;

				Roughness += 1.0f / NumColumns;
				Roughness = XMMin(Roughness, 1.0f);

				Root.StaticMeshInstances.push_back(Instance);
			}
			Metallic += 1.0f / (NumRows - 1);
			Metallic = XMMin(Metallic, 1.0f);
		}
	}

	// Static geometry vertex buffer.
	{
		const D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(VertexData.size() * sizeof(FVertex));

		ID3D12Resource* StagingVB;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&StagingVB)));
		OutTempResources.push_back(StagingVB);

		void* Ptr;
		VHR(StagingVB->Map(0, &CD3DX12_RANGE(0, 0), &Ptr));
		memcpy(Ptr, VertexData.data(), VertexData.size() * sizeof(FVertex));
		StagingVB->Unmap(0, nullptr);

		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&Root.StaticVB)));

		Root.StaticVBView.BufferLocation = Root.StaticVB->GetGPUVirtualAddress();
		Root.StaticVBView.StrideInBytes = sizeof(FVertex);
		Root.StaticVBView.SizeInBytes = (UINT)VertexData.size() * sizeof(FVertex);

		Gfx.CmdList->CopyResource(Root.StaticVB, StagingVB);
		Gfx.CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Root.StaticVB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
	}

	// Static geometry index buffer.
	{
		const D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(Triangles.size() * sizeof(uint32_t));

		ID3D12Resource* StagingIB;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&StagingIB)));
		OutTempResources.push_back(StagingIB);

		void* Ptr;
		VHR(StagingIB->Map(0, &CD3DX12_RANGE(0, 0), &Ptr));
		memcpy(Ptr, Triangles.data(), Triangles.size() * sizeof(uint32_t));
		StagingIB->Unmap(0, nullptr);

		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&Root.StaticIB)));

		Root.StaticIBView.BufferLocation = Root.StaticIB->GetGPUVirtualAddress();
		Root.StaticIBView.Format = DXGI_FORMAT_R32_UINT;
		Root.StaticIBView.SizeInBytes = (UINT)Triangles.size() * sizeof(uint32_t);

		Gfx.CmdList->CopyResource(Root.StaticIB, StagingIB);
		Gfx.CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Root.StaticIB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));
	}

	CreateSkyBox(Root, OutTempResources);
	OutTexturesThatNeedMipmaps.push_back(Root.SkyBox);

	// Setup resources for MSAA.
	{
		CD3DX12_RESOURCE_DESC DescColor = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, Gfx.Resolution[0], Gfx.Resolution[1], 1, 1, 8);
		DescColor.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &DescColor, D3D12_RESOURCE_STATE_RENDER_TARGET, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_R8G8B8A8_UNORM, XMVECTORF32{ 0.0f }), IID_PPV_ARGS(&Root.MSColor)));

		CD3DX12_RESOURCE_DESC DescDepth = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, Gfx.Resolution[0], Gfx.Resolution[1], 1, 1, 8);
		DescDepth.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &DescDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0), IID_PPV_ARGS(&Root.MSDepth)));

		Root.MSColorRTV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
		Root.MSDepthRTV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

		Gfx.Device->CreateRenderTargetView(Root.MSColor, nullptr, Root.MSColorRTV);
		Gfx.Device->CreateDepthStencilView(Root.MSDepth, nullptr, Root.MSDepthRTV);
	}

	Root.CameraPosition = XMFLOAT3(0.0f, 0.0f, -10.0f);
	Root.CameraFocusPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
}

static void Shutdown(FDemoRoot& Root)
{
	for (ID3D12RootSignature* Signature : Root.RootSignatures)
	{
		SAFE_RELEASE(Signature);
	}
	for (ID3D12PipelineState* Pipeline : Root.Pipelines)
	{
		SAFE_RELEASE(Pipeline);
	}
	SAFE_RELEASE(Root.StaticVB);
	SAFE_RELEASE(Root.StaticIB);
	SAFE_RELEASE(Root.SkyBox);
	SAFE_RELEASE(Root.MSColor);
	SAFE_RELEASE(Root.MSDepth);
}

static int32_t Run(FDemoRoot& Root)
{
	EA::StdC::Init();
	ImGui::CreateContext();

	HWND Window = CreateSimpleWindow("ImageBasedPBR", 1920, 1080);
	CreateGraphicsContext(Window, false, Root.Gfx);

	eastl::vector<ID3D12Resource*> TempResources;
	eastl::vector<ID3D12Resource*> TexturesThatNeedMipmaps;

	CreateUIContext(Root.Gfx, 8, Root.UI, TempResources);
	Initialize(Root, TempResources, TexturesThatNeedMipmaps);

	// Generate mipmaps.
	FMipmapGenerator MipmapGenerators[2];
	CreateMipmapGenerator(Root.Gfx, DXGI_FORMAT_R16G16B16A16_FLOAT, MipmapGenerators[0]);
	CreateMipmapGenerator(Root.Gfx, DXGI_FORMAT_R8G8B8A8_UNORM, MipmapGenerators[1]);

	for (ID3D12Resource* Texture : TexturesThatNeedMipmaps)
	{
		uint32_t GeneratorIdx = 0xffff;
		const D3D12_RESOURCE_DESC Desc = Texture->GetDesc();
		if (Desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
		{
			GeneratorIdx = 0;
		}
		else if (Desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			GeneratorIdx = 1;
		}
		EA_ASSERT(GeneratorIdx < eastl::size(MipmapGenerators));

		GenerateMipmaps(Root.Gfx, MipmapGenerators[GeneratorIdx], Texture);
	}

	Root.Gfx.CmdList->Close();
	Root.Gfx.CmdQueue->ExecuteCommandLists(1, CommandListCast(&Root.Gfx.CmdList));
	WaitForGPU(Root.Gfx);

	for (ID3D12Resource* Resource : TempResources)
	{
		SAFE_RELEASE(Resource);
	}
	for (uint32_t Idx = 0; Idx < eastl::size(MipmapGenerators); ++Idx)
	{
		DestroyMipmapGenerator(MipmapGenerators[Idx]);
	}

	for (;;)
	{
		MSG Message = {};
		if (PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
		{
			DispatchMessage(&Message);
			if (Message.message == WM_QUIT)
			{
				break;
			}
		}
		else
		{
			double Time;
			float DeltaTime;
			UpdateFrameStats(Window, "ImageBasedPBR", Time, DeltaTime);
			UpdateUI(DeltaTime);
			Update(Root, Time, DeltaTime);
			Draw(Root);
			PresentFrame(Root.Gfx, 0);
		}
	}

	WaitForGPU(Root.Gfx);
	Shutdown(Root);
	DestroyUIContext(Root.UI);
	DestroyGraphicsContext(Root.Gfx);
	ImGui::DestroyContext();
	EA::StdC::Shutdown();

	return 0;
}

int32_t CALLBACK WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int32_t)
{
	SetProcessDPIAware();
	FDemoRoot Root = {};
	return Run(Root);
}
