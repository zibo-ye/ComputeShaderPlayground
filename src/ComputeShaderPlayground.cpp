#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include "PixFinder.h"
#include <wrl.h>
using namespace Microsoft::WRL;


struct UAV
{
	UAV()
	{
		mProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		mProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		mProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		mProperties.CreationNodeMask = 0;
		mProperties.VisibleNodeMask = 0;

		mDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		mDesc.Alignment = 0;
		mDesc.Width = 0;
		mDesc.Height = 1;
		mDesc.DepthOrArraySize = 1;
		mDesc.MipLevels = 1;
		mDesc.Format = DXGI_FORMAT_UNKNOWN;
		mDesc.SampleDesc.Count = 1;
		mDesc.SampleDesc.Quality = 0;
		mDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		mDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		mReadbackProperties = mProperties;
		mReadbackProperties.Type = D3D12_HEAP_TYPE_READBACK;

		mReadbackDesc = mDesc;
		mReadbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	}

	D3D12_RESOURCE_DESC mDesc = {};
	D3D12_HEAP_PROPERTIES mProperties = {};
	ComPtr<ID3D12Resource> mGPUResource;

	D3D12_RESOURCE_DESC mReadbackDesc = {};
	D3D12_HEAP_PROPERTIES mReadbackProperties = {};
	ComPtr<ID3D12Resource> mReadbackResource;
};

UAV CreateUAV(const uint32_t width, ComPtr<ID3D12Device2> device)
{
	UAV uav;
	uav.mDesc.Width = width;
	uav.mReadbackDesc.Width = uav.mDesc.Width;
	device->CreateCommittedResource(&uav.mProperties, D3D12_HEAP_FLAG_NONE, &uav.mDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&uav.mGPUResource));
	device->CreateCommittedResource(&uav.mReadbackProperties, D3D12_HEAP_FLAG_NONE, &uav.mReadbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&uav.mReadbackResource));
	return uav;
}

void InitDX(ComPtr<ID3D12Device2>& device)
{
	// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
	// This may happen if the application is launched through the PIX UI. 
	if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0)
	{
		LoadLibrary(GetLatestWinPixGpuCapturerPath_Cpp17().c_str());
	}

	ComPtr<ID3D12Debug> d3d12_debug;
	D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12_debug));
	d3d12_debug->EnableDebugLayer();

	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

	ComPtr<IDXGIAdapter1> adapter;
	factory->EnumAdapters1(0, &adapter);

	DXGI_ADAPTER_DESC1 adapter_desc;
	adapter->GetDesc1(&adapter_desc);

	D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
}

HRESULT CompileComputeShader(const uint32_t kGroupSize, const uint32_t kDispatchCount, ComPtr<IDxcBlob>& shader_blob)
{
	ComPtr<IDxcLibrary> library;
	DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));

	ComPtr<IDxcCompiler> compiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

	uint32_t code_page = CP_UTF8;
	ComPtr<IDxcBlobEncoding> source_blob;
	library->CreateBlobFromFile(L"Shader.hlsl", &code_page, &source_blob);

	ComPtr<IDxcOperationResult> result;
	LPCWSTR arguments[] =
	{
		L"-O3",
		// L"-Zi",
	};

	std::wstring group_size_str = std::to_wstring(kGroupSize);
	std::wstring dispatch_count_str = std::to_wstring(kDispatchCount);
	DxcDefine defines[] =
	{
		{ L"GROUP_SIZE", group_size_str.c_str() },
		{ L"DISPATCH_COUNT", dispatch_count_str.c_str() },
	};
	HRESULT hr = compiler->Compile(source_blob.Get(), L"Shader.hlsl", L"main", L"cs_6_4", arguments, _countof(arguments), defines, _countof(defines), nullptr, &result);
	if (!SUCCEEDED(hr))
	{
		return hr;
	}

	result->GetStatus(&hr);
	if (!SUCCEEDED(hr))
	{
		printf("Shader compile failed\n\n");
		return hr;
	}

	printf("Shader compile succeed\n\n");

	ComPtr<IDxcBlobEncoding> error_blob;
	if (SUCCEEDED(result->GetErrorBuffer(&error_blob)) && error_blob)
	{
		std::string error_message((const char*)error_blob->GetBufferPointer(), error_blob->GetBufferSize());
		printf("%s", error_message.c_str());
	}

	result->GetResult(&shader_blob);
	return S_OK;
}

void GenerateComputeShaderRootSignatureAndPSO(ComPtr<ID3D12Device2> device, ComPtr<IDxcBlob> shader_blob, ComPtr<ID3D12RootSignature>& root_signature, ComPtr<ID3D12PipelineState>& pso)
{
	device->CreateRootSignature(0, shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));

	D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
	pso_desc.pRootSignature = root_signature.Get();
	pso_desc.CS.BytecodeLength = shader_blob->GetBufferSize();
	pso_desc.CS.pShaderBytecode = shader_blob->GetBufferPointer();
	device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
}

void CreateCommandListAndQueue(ComPtr<ID3D12Device2> device, ComPtr<ID3D12CommandQueue>& command_queue, ComPtr<ID3D12GraphicsCommandList>& command_list)
{
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));

	ComPtr<ID3D12CommandAllocator> command_allocator;
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));

	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list));
}

int main()
{
	const uint32_t kThreadGroupSize = 64;
	const uint32_t kDispatchCount = 1024;

	ComPtr<IDxcBlob> shader_blob;
	if (!SUCCEEDED(CompileComputeShader(kThreadGroupSize, kDispatchCount, shader_blob))) return 0;

	ComPtr<ID3D12Device2> device;
	InitDX(device);

	UAV uav0 = CreateUAV(kThreadGroupSize * kDispatchCount * sizeof(float) * 4, device);
	UAV uav1 = CreateUAV(kThreadGroupSize * kDispatchCount * sizeof(float) * 4, device);

	ComPtr<ID3D12RootSignature> root_signature;
	ComPtr<ID3D12PipelineState> pso;

	GenerateComputeShaderRootSignatureAndPSO(device, shader_blob, root_signature, pso);

	ComPtr<ID3D12GraphicsCommandList> command_list;
	ComPtr<ID3D12CommandQueue> command_queue;

	CreateCommandListAndQueue(device, command_queue, command_list);

	//////////////////////////////////////////////////////////////////////////

	command_list->SetComputeRootSignature(root_signature.Get());
	command_list->SetComputeRootUnorderedAccessView(0, uav0.mGPUResource->GetGPUVirtualAddress());
	command_list->SetComputeRootUnorderedAccessView(1, uav1.mGPUResource->GetGPUVirtualAddress());
	command_list->SetPipelineState(pso.Get());
	command_list->Dispatch(kDispatchCount, 1, 1);

	D3D12_RESOURCE_BARRIER barriers[1] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = uav1.mGPUResource.Get();
	barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	command_list->ResourceBarrier(_countof(barriers), &barriers[0]);	
	command_list->CopyResource(uav1.mReadbackResource.Get(), uav1.mGPUResource.Get());

	command_list->Close();

	ID3D12CommandList* command_lists[] = { command_list.Get() };
	command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

	ComPtr<ID3D12Fence> fence;
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	command_queue->Signal(fence.Get(), 1);

	//////////////////////////////////////////////////////////////////////////
	
	HANDLE handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	fence->SetEventOnCompletion(1, handle);
	WaitForSingleObject(handle, INFINITE);

	// float* data = nullptr;
	// D3D12_RANGE range = { 0, uav1.mReadbackDesc.Width };
	// uav1.mReadbackResource->Map(0, &range, (void**)&data);

	// for (int i = 0; i < uav1.mReadbackDesc.Width / sizeof(float) / 4; i++)
	// 	printf("uav[%d] = %.3f, %.3f, %.3f, %.3f\n", i, data[i * 4 + 0], data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]);

	// uav1.mReadbackResource->Unmap(0, nullptr);

	return 0;
}