#include "Library.h"
#include "CPUAndGPUCommon.h"
#include "d3dx12.h"
#include "imgui/imgui.h"
#include "EAStdC/EAStdC.h"
#include "EAStdC/EASprintf.h"
#include "EAStdC/EABitTricks.h"
#include "stb_image.h"

enum
{
	MESH_Cube, MESH_Sphere,
};

enum
{
	PSO_Test, PSO_SimpleForward, PSO_SampleEnvMap, PSO_EquirectangularToCube, PSO_GenerateIrradianceMap, PSO_PrefilterEnvMap,
};

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
	ID3D12Resource* EnvMap;
	ID3D12Resource* IrradianceMap;
	ID3D12Resource* PrefilteredEnvMap;
	D3D12_CPU_DESCRIPTOR_HANDLE EnvMapSRV;
	D3D12_CPU_DESCRIPTOR_HANDLE IrradianceMapSRV;
	D3D12_CPU_DESCRIPTOR_HANDLE PrefilteredEnvMapSRV;
	ID3D12Resource* MSColorBuffer;
	ID3D12Resource* MSDepthBuffer;
	D3D12_CPU_DESCRIPTOR_HANDLE MSColorBufferRTV;
	D3D12_CPU_DESCRIPTOR_HANDLE MSDepthBufferDSV;
};

static void Update(FDemoRoot& Root)
{
	double Time;
	float DeltaTime;
	UpdateFrameStats(Root.Gfx.Window, "ImageBasedPBR", Time, DeltaTime);
	UpdateUI(DeltaTime);

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

	CmdList->OMSetRenderTargets(1, &Root.MSColorBufferRTV, TRUE, &Root.MSDepthBufferDSV);
	CmdList->ClearRenderTargetView(Root.MSColorBufferRTV, XMVECTORF32{ 0.0f }, 0, nullptr);
	CmdList->ClearDepthStencilView(Root.MSDepthBufferDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	CmdList->IASetVertexBuffers(0, 1, &Root.StaticVBView);
	CmdList->IASetIndexBuffer(&Root.StaticIBView);

	const XMMATRIX ViewTransform = XMMatrixLookAtLH(XMLoadFloat3(&Root.CameraPosition), XMLoadFloat3(&Root.CameraFocusPosition), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
	const XMMATRIX ProjectionTransform = XMMatrixPerspectiveFovLH(XM_PI / 3, 1.777f, 0.1f, 100.0f);

	// Draw all static mesh instances.
	{
		CmdList->SetPipelineState(Root.Pipelines[PSO_SimpleForward]);
		CmdList->SetGraphicsRootSignature(Root.RootSignatures[PSO_SimpleForward]);

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

			const XMFLOAT3 P = Root.CameraPosition;
			CPUAddress->ViewerPosition = XMFLOAT4(P.x, P.y, P.z, 1.0f);

			CD3DX12_CPU_DESCRIPTOR_HANDLE TableBaseCPU;
			CD3DX12_GPU_DESCRIPTOR_HANDLE TableBaseGPU;
			AllocateGPUDescriptors(Gfx, 2, TableBaseCPU, TableBaseGPU);

			D3D12_CONSTANT_BUFFER_VIEW_DESC CBVDesc = {};
			CBVDesc.BufferLocation = GPUAddress;
			CBVDesc.SizeInBytes = (uint32_t)sizeof(FPerFrameConstantData);

			Gfx.Device->CreateConstantBufferView(&CBVDesc, TableBaseCPU.Offset(0, Gfx.DescriptorSize));
			Gfx.Device->CopyDescriptorsSimple(1, TableBaseCPU.Offset(1, Gfx.DescriptorSize), Root.IrradianceMapSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			CmdList->SetGraphicsRootDescriptorTable(1, TableBaseGPU);
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

	// Draw EnvMap.
	{
		CmdList->SetPipelineState(Root.Pipelines[PSO_SampleEnvMap]);
		CmdList->SetGraphicsRootSignature(Root.RootSignatures[PSO_SampleEnvMap]);

		D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;
		auto* CPUAddress = (FPerDrawConstantData*)AllocateGPUMemory(Gfx, sizeof(FPerDrawConstantData), GPUAddress);

		XMMATRIX ViewTransformOrigin = ViewTransform;
		ViewTransformOrigin.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

		const XMMATRIX ObjectToClip = ViewTransformOrigin * ProjectionTransform;
		XMStoreFloat4x4(&CPUAddress->ObjectToClip, XMMatrixTranspose(ObjectToClip));

		const FStaticMesh& Mesh = Root.StaticMeshes[MESH_Cube];

		CmdList->SetGraphicsRootConstantBufferView(0, GPUAddress);
		CmdList->SetGraphicsRootDescriptorTable(1, CopyDescriptorsToGPUHeap(Gfx, 1, Root.EnvMapSRV));
		CmdList->DrawIndexedInstanced(Mesh.IndexCount, 1, Mesh.StartIndexLocation, Mesh.BaseVertexLocation, 0);
	}

	DrawUI(Gfx, Root.UI);

	// Resolve MS color buffer and copy it to back buffer.
	{
		ID3D12Resource* BackBuffer;
		D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRTV;
		GetBackBuffer(Gfx, BackBuffer, BackBufferRTV);

		D3D12_RESOURCE_BARRIER Barriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(Root.MSColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
		};
		CmdList->ResourceBarrier((UINT)eastl::size(Barriers), Barriers);

		CmdList->ResolveSubresource(BackBuffer, 0, Root.MSColorBuffer, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

		eastl::swap(Barriers[0].Transition.StateBefore, Barriers[0].Transition.StateAfter);
		eastl::swap(Barriers[1].Transition.StateBefore, Barriers[1].Transition.StateAfter);
		CmdList->ResourceBarrier((UINT)eastl::size(Barriers), Barriers);
	}

	CmdList->Close();

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

static void CreatePipelines(FGraphicsContext& Gfx, uint32_t NumSamples, eastl::vector<ID3D12PipelineState*>& OutPipelines, eastl::vector<ID3D12RootSignature*>& OutSignatures)
{
	const D3D12_INPUT_ELEMENT_DESC InPositionNormal[] =
	{
		{ "_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "_Normal", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

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
		EA_ASSERT(OutPipelines.size() == PSO_Test);
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
		EA_ASSERT(OutPipelines.size() == PSO_SimpleForward);
		AddGraphicsPipeline(Gfx, PSODesc, "SimpleForward.vs.cso", "SimpleForward.ps.cso", OutPipelines, OutSignatures);
	}
	// EnvMap pipeline.
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
		EA_ASSERT(OutPipelines.size() == PSO_SampleEnvMap);
		AddGraphicsPipeline(Gfx, PSODesc, "SampleEnvMap.vs.cso", "SampleEnvMap.ps.cso", OutPipelines, OutSignatures);
	}
	// EquirectangularToCube, GenerateIrradianceMap, PrefilterEnvMap pipelines.
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

		EA_ASSERT(OutPipelines.size() == PSO_EquirectangularToCube);
		AddGraphicsPipeline(Gfx, PSODesc, "EquirectangularToCube.vs.cso", "EquirectangularToCube.ps.cso", OutPipelines, OutSignatures);

		EA_ASSERT(OutPipelines.size() == PSO_GenerateIrradianceMap);
		AddGraphicsPipeline(Gfx, PSODesc, "GenerateIrradianceMap.vs.cso", "GenerateIrradianceMap.ps.cso", OutPipelines, OutSignatures);

		EA_ASSERT(OutPipelines.size() == PSO_PrefilterEnvMap);
		AddGraphicsPipeline(Gfx, PSODesc, "PrefilterEnvMap.vs.cso", "PrefilterEnvMap.ps.cso", OutPipelines, OutSignatures);
	}
}

static void CreateEnvMap(FGraphicsContext& Gfx, const FStaticMesh& Cube, ID3D12Resource*& OutEnvMap, D3D12_CPU_DESCRIPTOR_HANDLE& OutEnvMapSRV, eastl::vector<ID3D12Resource*>& OutTempResources)
{
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

	const uint32_t CubeMapResolution = 512;
	ID3D12Resource* TempCubeMap;
	const D3D12_CPU_DESCRIPTOR_HANDLE TempCubeMapRTVs = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6);
	{
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, CubeMapResolution, CubeMapResolution, 6);
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&OutEnvMap)));

		OutEnvMapSRV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels = -1;
		Gfx.Device->CreateShaderResourceView(OutEnvMap, &SRVDesc, OutEnvMapSRV);


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


	CmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, (float)CubeMapResolution, (float)CubeMapResolution));
	CmdList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, CubeMapResolution, CubeMapResolution));

	CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


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


	D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;
	auto* CPUAddress = (FPerDrawConstantData*)AllocateGPUMemory(Gfx, 6 * sizeof(FPerDrawConstantData), GPUAddress);

	D3D12_CPU_DESCRIPTOR_HANDLE RTV = TempCubeMapRTVs;

	for (uint32_t Idx = 0; Idx < 6; ++Idx)
	{
		CmdList->OMSetRenderTargets(1, &RTV, TRUE, nullptr);

		const XMMATRIX ObjectToClip = ViewTransforms[Idx] * ProjectionTransform;
		XMStoreFloat4x4(&CPUAddress->ObjectToClip, XMMatrixTranspose(ObjectToClip));

		CmdList->SetGraphicsRootConstantBufferView(0, GPUAddress);
		CmdList->SetGraphicsRootDescriptorTable(1, CopyDescriptorsToGPUHeap(Gfx, 1, TempHDRRectTextureSRV));
		CmdList->DrawIndexedInstanced(Cube.IndexCount, 1, Cube.StartIndexLocation, Cube.BaseVertexLocation, 0);

		RTV.ptr += Gfx.DescriptorSizeRTV;
		GPUAddress += sizeof(FPerDrawConstantData);
		CPUAddress++;
	}

	CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(TempCubeMap, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

	CmdList->CopyResource(OutEnvMap, TempCubeMap);

	Gfx.CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(OutEnvMap, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

static void CreateIrradianceMap(FGraphicsContext& Gfx, D3D12_CPU_DESCRIPTOR_HANDLE EnvMapSRV, const FStaticMesh& Cube, ID3D12Resource*& OutIrradianceMap, D3D12_CPU_DESCRIPTOR_HANDLE& OutIrradianceMapSRV, eastl::vector<ID3D12Resource*>& OutTempResources)
{
	const uint32_t CubeMapResolution = 64;
	ID3D12Resource* TempCubeMap;
	const D3D12_CPU_DESCRIPTOR_HANDLE TempCubeMapRTVs = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6);
	{
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, CubeMapResolution, CubeMapResolution, 6);
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&OutIrradianceMap)));

		OutIrradianceMapSRV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels = -1;
		Gfx.Device->CreateShaderResourceView(OutIrradianceMap, &SRVDesc, OutIrradianceMapSRV);


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

	CmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, (float)CubeMapResolution, (float)CubeMapResolution));
	CmdList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, CubeMapResolution, CubeMapResolution));

	CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


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


	D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;
	auto* CPUAddress = (FPerDrawConstantData*)AllocateGPUMemory(Gfx, 6 * sizeof(FPerDrawConstantData), GPUAddress);

	D3D12_CPU_DESCRIPTOR_HANDLE RTV = TempCubeMapRTVs;

	for (uint32_t Idx = 0; Idx < 6; ++Idx)
	{
		CmdList->OMSetRenderTargets(1, &RTV, TRUE, nullptr);

		const XMMATRIX ObjectToClip = ViewTransforms[Idx] * ProjectionTransform;
		XMStoreFloat4x4(&CPUAddress->ObjectToClip, XMMatrixTranspose(ObjectToClip));

		CmdList->SetGraphicsRootConstantBufferView(0, GPUAddress);
		CmdList->SetGraphicsRootDescriptorTable(1, CopyDescriptorsToGPUHeap(Gfx, 1, EnvMapSRV));
		CmdList->DrawIndexedInstanced(Cube.IndexCount, 1, Cube.StartIndexLocation, Cube.BaseVertexLocation, 0);

		RTV.ptr += Gfx.DescriptorSizeRTV;
		GPUAddress += sizeof(FPerDrawConstantData);
		CPUAddress++;
	}

	CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(TempCubeMap, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

	CmdList->CopyResource(OutIrradianceMap, TempCubeMap);

	Gfx.CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(OutIrradianceMap, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

static void CreatePrefilteredEnvMap(FGraphicsContext& Gfx, D3D12_CPU_DESCRIPTOR_HANDLE EnvMapSRV, const FStaticMesh& Cube, ID3D12Resource*& OutPrefilteredEnvMap, D3D12_CPU_DESCRIPTOR_HANDLE& OutPrefilteredEnvMapSRV, eastl::vector<ID3D12Resource*>& OutTempResources)
{
	const uint32_t CubeMapResolution = 256;
	const uint32_t NumMipLevelsUsed = 6;

	ID3D12Resource* TempCubeMap;
	const D3D12_CPU_DESCRIPTOR_HANDLE TempCubeMapRTVs = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6 * NumMipLevelsUsed);
	{
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, CubeMapResolution, CubeMapResolution, 6, NumMipLevelsUsed);
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&OutPrefilteredEnvMap)));

		OutPrefilteredEnvMapSRV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels = NumMipLevelsUsed;
		Gfx.Device->CreateShaderResourceView(OutPrefilteredEnvMap, &SRVDesc, OutPrefilteredEnvMapSRV);


		Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&TempCubeMap)));
		OutTempResources.push_back(TempCubeMap);

		D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = TempCubeMapRTVs;

		for (uint32_t MipSliceIdx = 0; MipSliceIdx < NumMipLevelsUsed; ++MipSliceIdx)
		{
			for (uint32_t ArraySliceIdx = 0; ArraySliceIdx < 6; ++ArraySliceIdx)
			{
				D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
				RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				RTVDesc.Texture2DArray.ArraySize = 1;
				RTVDesc.Texture2DArray.FirstArraySlice = ArraySliceIdx;
				RTVDesc.Texture2DArray.MipSlice = MipSliceIdx;
				Gfx.Device->CreateRenderTargetView(TempCubeMap, &RTVDesc, CPUHandle);

				CPUHandle.ptr += Gfx.DescriptorSizeRTV;
			}
		}
	}

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

	D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;
	auto* CPUAddress = (FPerDrawConstantData*)AllocateGPUMemory(Gfx, 6 * NumMipLevelsUsed * sizeof(FPerDrawConstantData), GPUAddress);

	ID3D12GraphicsCommandList2* CmdList = Gfx.CmdList;

	CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	uint32_t CurrentResolution = CubeMapResolution;
	D3D12_CPU_DESCRIPTOR_HANDLE RTV = TempCubeMapRTVs;

	for (uint32_t MipSliceIdx = 0; MipSliceIdx < NumMipLevelsUsed; ++MipSliceIdx)
	{
		CmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, (float)CurrentResolution, (float)CurrentResolution));
		CmdList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, CurrentResolution, CurrentResolution));

		for (uint32_t ArraySliceIdx = 0; ArraySliceIdx < 6; ++ArraySliceIdx)
		{
			CmdList->OMSetRenderTargets(1, &RTV, TRUE, nullptr);

			const XMMATRIX ObjectToClip = ViewTransforms[ArraySliceIdx] * ProjectionTransform;
			XMStoreFloat4x4(&CPUAddress->ObjectToClip, XMMatrixTranspose(ObjectToClip));
			CPUAddress->Roughness = (float)MipSliceIdx / (NumMipLevelsUsed - 1);

			CmdList->SetGraphicsRootConstantBufferView(0, GPUAddress);
			CmdList->SetGraphicsRootDescriptorTable(1, CopyDescriptorsToGPUHeap(Gfx, 1, EnvMapSRV));
			CmdList->DrawIndexedInstanced(Cube.IndexCount, 1, Cube.StartIndexLocation, Cube.BaseVertexLocation, 0);

			RTV.ptr += Gfx.DescriptorSizeRTV;
			GPUAddress += sizeof(FPerDrawConstantData);
			CPUAddress++;
		}

		CurrentResolution /= 2;
	}

	CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(TempCubeMap, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

	CmdList->CopyResource(OutPrefilteredEnvMap, TempCubeMap);

	Gfx.CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(OutPrefilteredEnvMap, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

static void Initialize(FDemoRoot& Root)
{
	FGraphicsContext& Gfx = Root.Gfx;

	eastl::vector<ID3D12Resource*> TempResources;
	eastl::vector<ID3D12Resource*> TexturesThatNeedMipmaps;

	const uint32_t NumSamples = 8;
	CreateUIContext(Gfx, NumSamples, Root.UI, TempResources);
	CreatePipelines(Gfx, NumSamples, Root.Pipelines, Root.RootSignatures);


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

			const uint32_t IndexCount = (uint32_t)(Triangles.size() - TrianglesSize);
			const uint32_t StartIndexLocation = (uint32_t)TrianglesSize;
			const uint32_t BaseVertexLocation = (uint32_t)PositionsSize;
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

	// Static geometry vertex buffer (single buffer for all static meshes).
	{
		const D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(VertexData.size() * sizeof(FVertex));

		ID3D12Resource* StagingVB;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&StagingVB)));
		TempResources.push_back(StagingVB);

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

	// Static geometry index buffer (single buffer for all static meshes).
	{
		const D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(Triangles.size() * sizeof(uint32_t));

		ID3D12Resource* StagingIB;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&StagingIB)));
		TempResources.push_back(StagingIB);

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

	Gfx.CmdList->IASetVertexBuffers(0, 1, &Root.StaticVBView);
	Gfx.CmdList->IASetIndexBuffer(&Root.StaticIBView);

	// Create EnvMap.
	Gfx.CmdList->SetPipelineState(Root.Pipelines[PSO_EquirectangularToCube]);
	Gfx.CmdList->SetGraphicsRootSignature(Root.RootSignatures[PSO_EquirectangularToCube]);
	CreateEnvMap(Root.Gfx, Root.StaticMeshes[MESH_Cube], Root.EnvMap, Root.EnvMapSRV, TempResources);
	TexturesThatNeedMipmaps.push_back(Root.EnvMap);

	// Create IrradianceMap.
	Gfx.CmdList->SetPipelineState(Root.Pipelines[PSO_GenerateIrradianceMap]);
	Gfx.CmdList->SetGraphicsRootSignature(Root.RootSignatures[PSO_GenerateIrradianceMap]);
	CreateIrradianceMap(Root.Gfx, Root.EnvMapSRV, Root.StaticMeshes[MESH_Cube], Root.IrradianceMap, Root.IrradianceMapSRV, TempResources);
	TexturesThatNeedMipmaps.push_back(Root.IrradianceMap);

	// Create PrefilteredEnvMap.
	Gfx.CmdList->SetPipelineState(Root.Pipelines[PSO_PrefilterEnvMap]);
	Gfx.CmdList->SetGraphicsRootSignature(Root.RootSignatures[PSO_PrefilterEnvMap]);
	CreatePrefilteredEnvMap(Gfx, Root.EnvMapSRV, Root.StaticMeshes[MESH_Cube], Root.PrefilteredEnvMap, Root.PrefilteredEnvMapSRV, TempResources);

	// Setup resources for MSAA.
	{
		CD3DX12_RESOURCE_DESC DescColor = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, Gfx.Resolution[0], Gfx.Resolution[1], 1, 1, NumSamples);
		DescColor.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &DescColor, D3D12_RESOURCE_STATE_RENDER_TARGET, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_R8G8B8A8_UNORM, XMVECTORF32{ 0.0f }), IID_PPV_ARGS(&Root.MSColorBuffer)));

		CD3DX12_RESOURCE_DESC DescDepth = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, Gfx.Resolution[0], Gfx.Resolution[1], 1, 1, NumSamples);
		DescDepth.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		VHR(Gfx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &DescDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0), IID_PPV_ARGS(&Root.MSDepthBuffer)));

		Root.MSColorBufferRTV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
		Root.MSDepthBufferDSV = AllocateDescriptors(Gfx, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

		Gfx.Device->CreateRenderTargetView(Root.MSColorBuffer, nullptr, Root.MSColorBufferRTV);
		Gfx.Device->CreateDepthStencilView(Root.MSDepthBuffer, nullptr, Root.MSDepthBufferDSV);
	}

	// Execute "data upload" and "data generation" GPU commands, create mipmaps, destroy temp resources when GPU is done.
	{
		const DXGI_FORMAT Formats[] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM };
		FMipmapGenerator MipmapGenerators[eastl::size(Formats)];
		for (uint32_t Idx = 0; Idx < eastl::size(Formats); ++Idx)
		{
			CreateMipmapGenerator(Root.Gfx, Formats[Idx], MipmapGenerators[Idx]);
		}

		for (ID3D12Resource* Texture : TexturesThatNeedMipmaps)
		{
			const D3D12_RESOURCE_DESC Desc = Texture->GetDesc();

			for (uint32_t Idx = 0; Idx < eastl::size(Formats); ++Idx)
			{
				if (Desc.Format == Formats[Idx])
				{
					GenerateMipmaps(Root.Gfx, MipmapGenerators[Idx], Texture);
					break;
				}
			}
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
	SAFE_RELEASE(Root.EnvMap);
	SAFE_RELEASE(Root.IrradianceMap);
	SAFE_RELEASE(Root.PrefilteredEnvMap);
	SAFE_RELEASE(Root.MSColorBuffer);
	SAFE_RELEASE(Root.MSDepthBuffer);
	DestroyUIContext(Root.UI);
}

static int32_t Run(FDemoRoot& Root)
{
	EA::StdC::Init();
	ImGui::CreateContext();

	HWND Window = CreateSimpleWindow("ImageBasedPBR", 1920, 1080);
	CreateGraphicsContext(Window, /*bShouldCreateDepthBuffer*/false, Root.Gfx);

	Initialize(Root);

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
			Update(Root);
			Draw(Root);
			PresentFrame(Root.Gfx, 0);
		}
	}

	WaitForGPU(Root.Gfx);
	Shutdown(Root);
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
