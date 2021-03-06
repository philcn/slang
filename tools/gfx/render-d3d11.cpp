// render-d3d11.cpp

#define _CRT_SECURE_NO_WARNINGS

#include "render-d3d11.h"

//WORKING: #include "options.h"
#include "render.h"
#include "d3d-util.h"

#include "surface.h"

// In order to use the Slang API, we need to include its header

//#include <slang.h>

#include "../../slang-com-ptr.h"
#include "flag-combiner.h"

// We will be rendering with Direct3D 11, so we need to include
// the Windows and D3D11 headers

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NOMINMAX

#include <d3d11_2.h>
#include <d3dcompiler.h>

// We will use the C standard library just for printing error messages.
#include <stdio.h>

#ifdef _MSC_VER
#include <stddef.h>
#if (_MSC_VER < 1900)
#define snprintf sprintf_s
#endif
#endif
//
using namespace Slang;

namespace gfx {

class D3D11Renderer : public Renderer
{
public:
    enum
    {
        kMaxUAVs = 64,
        kMaxRTVs = 8,
    };

    // Renderer    implementation
    virtual SlangResult initialize(const Desc& desc, void* inWindowHandle) override;
    virtual void setClearColor(const float color[4]) override;
    virtual void clearFrame() override;
    virtual void presentFrame() override;
    TextureResource::Desc getSwapChainTextureDesc() override;

    Result createTextureResource(Resource::Usage initialUsage, const TextureResource::Desc& desc, const TextureResource::Data* initData, TextureResource** outResource) override;
    Result createBufferResource(Resource::Usage initialUsage, const BufferResource::Desc& desc, const void* initData, BufferResource** outResource) override;
    Result createSamplerState(SamplerState::Desc const& desc, SamplerState** outSampler) override;

    Result createTextureView(TextureResource* texture, ResourceView::Desc const& desc, ResourceView** outView) override;
    Result createBufferView(BufferResource* buffer, ResourceView::Desc const& desc, ResourceView** outView) override;

    Result createInputLayout(const InputElementDesc* inputElements, UInt inputElementCount, InputLayout** outLayout) override;

    Result createDescriptorSetLayout(const DescriptorSetLayout::Desc& desc, DescriptorSetLayout** outLayout) override;
    Result createPipelineLayout(const PipelineLayout::Desc& desc, PipelineLayout** outLayout) override;
    Result createDescriptorSet(DescriptorSetLayout* layout, DescriptorSet** outDescriptorSet) override;

    Result createProgram(const ShaderProgram::Desc& desc, ShaderProgram** outProgram) override;
    Result createGraphicsPipelineState(const GraphicsPipelineStateDesc& desc, PipelineState** outState) override;
    Result createComputePipelineState(const ComputePipelineStateDesc& desc, PipelineState** outState) override;

    virtual SlangResult captureScreenSurface(Surface& surfaceOut) override;

    virtual void* map(BufferResource* buffer, MapFlavor flavor) override;
    virtual void unmap(BufferResource* buffer) override;
    virtual void setPrimitiveTopology(PrimitiveTopology topology) override;

    virtual void setDescriptorSet(PipelineType pipelineType, PipelineLayout* layout, UInt index, DescriptorSet* descriptorSet) override;

    virtual void setVertexBuffers(UInt startSlot, UInt slotCount, BufferResource*const* buffers, const UInt* strides,  const UInt* offsets) override;
    virtual void setIndexBuffer(BufferResource* buffer, Format indexFormat, UInt offset) override;
    virtual void setDepthStencilTarget(ResourceView* depthStencilView) override;
    void setViewports(UInt count, Viewport const* viewports) override;
    void setScissorRects(UInt count, ScissorRect const* rects) override;
    virtual void setPipelineState(PipelineType pipelineType, PipelineState* state) override;
    virtual void draw(UInt vertexCount, UInt startVertex) override;
    virtual void drawIndexed(UInt indexCount, UInt startIndex, UInt baseVertex) override;
    virtual void dispatchCompute(int x, int y, int z) override;
    virtual void submitGpuWork() override {}
    virtual void waitForGpu() override {}
    virtual RendererType getRendererType() const override { return RendererType::DirectX11; }

    ~D3D11Renderer() {}

    protected:

#if 0
    struct BindingDetail
    {
        ComPtr<ID3D11ShaderResourceView>    m_srv;
        ComPtr<ID3D11UnorderedAccessView>   m_uav;
        ComPtr<ID3D11SamplerState>          m_samplerState;
    };

    class BindingStateImpl: public BindingState
    {
		public:
        typedef BindingState Parent;

            /// Ctor
        BindingStateImpl(const Desc& desc):
            Parent(desc)
        {}

        List<BindingDetail> m_bindingDetails;
    };
#endif

    enum class D3D11DescriptorSlotType
    {
        ConstantBuffer,
        ShaderResourceView,
        UnorderedAccessView,
        Sampler,

        CombinedTextureSampler,

        CountOf,
    };

    class DescriptorSetLayoutImpl : public DescriptorSetLayout
    {
    public:
        struct RangeInfo
        {
            D3D11DescriptorSlotType type;
            UInt                    arrayIndex;
            UInt                    pairedSamplerArrayIndex;
        };
        List<RangeInfo> m_ranges;

        UInt m_counts[int(D3D11DescriptorSlotType::CountOf)];
    };

    class PipelineLayoutImpl : public PipelineLayout
    {
    public:
        struct DescriptorSetInfo
        {
            RefPtr<DescriptorSetLayoutImpl> layout;
            UInt                            baseIndices[int(D3D11DescriptorSlotType::CountOf)];
        };

        List<DescriptorSetInfo>     m_descriptorSets;
        UINT                        m_uavCount;
    };

    class DescriptorSetImpl : public DescriptorSet
    {
    public:
        virtual void setConstantBuffer(UInt range, UInt index, BufferResource* buffer) override;
        virtual void setResource(UInt range, UInt index, ResourceView* view) override;
        virtual void setSampler(UInt range, UInt index, SamplerState* sampler) override;
        virtual void setCombinedTextureSampler(
            UInt range,
            UInt index,
            ResourceView*   textureView,
            SamplerState*   sampler) override;

        RefPtr<DescriptorSetLayoutImpl>         m_layout;

        List<ComPtr<ID3D11Buffer>>              m_cbs;
        List<ComPtr<ID3D11ShaderResourceView>>  m_srvs;
        List<ComPtr<ID3D11UnorderedAccessView>> m_uavs;
        List<ComPtr<ID3D11SamplerState>>        m_samplers;
    };

    class ShaderProgramImpl: public ShaderProgram
    {
    public:
        ComPtr<ID3D11VertexShader> m_vertexShader;
        ComPtr<ID3D11PixelShader> m_pixelShader;
        ComPtr<ID3D11ComputeShader> m_computeShader;
    };

    class BufferResourceImpl: public BufferResource
    {
		public:
        typedef BufferResource Parent;

        BufferResourceImpl(const Desc& desc, Usage initialUsage):
            Parent(desc),
            m_initialUsage(initialUsage)
        {
        }

        MapFlavor m_mapFlavor;
        Usage m_initialUsage;
        ComPtr<ID3D11Buffer> m_buffer;
        ComPtr<ID3D11Buffer> m_staging;
    };
    class TextureResourceImpl : public TextureResource
    {
    public:
        typedef TextureResource Parent;

        TextureResourceImpl(const Desc& desc, Usage initialUsage) :
            Parent(desc),
            m_initialUsage(initialUsage)
        {
        }
        Usage m_initialUsage;
        ComPtr<ID3D11Resource> m_resource;

    };

    class SamplerStateImpl : public SamplerState
    {
    public:
        ComPtr<ID3D11SamplerState> m_sampler;
    };


    class ResourceViewImpl : public ResourceView
    {
    public:
        enum class Type
        {
            SRV,
            UAV,
            DSV,
            RTV,
        };
        Type m_type;
    };

    class ShaderResourceViewImpl : public ResourceViewImpl
    {
    public:
        ComPtr<ID3D11ShaderResourceView>    m_srv;
    };

    class UnorderedAccessViewImpl : public ResourceViewImpl
    {
    public:
        ComPtr<ID3D11UnorderedAccessView>   m_uav;
    };

    class DepthStencilViewImpl : public ResourceViewImpl
    {
    public:
        ComPtr<ID3D11DepthStencilView>      m_dsv;
    };

    class RenderTargetViewImpl : public ResourceViewImpl
    {
    public:
        ComPtr<ID3D11RenderTargetView>      m_rtv;
    };

    class InputLayoutImpl: public InputLayout
	{
		public:
		ComPtr<ID3D11InputLayout> m_layout;
	};

    class PipelineStateImpl : public PipelineState
    {
    public:
        RefPtr<ShaderProgramImpl>   m_program;
        RefPtr<PipelineLayoutImpl>  m_pipelineLayout;
    };


    class GraphicsPipelineStateImpl : public PipelineStateImpl
    {
    public:
        UINT                            m_rtvCount;

        RefPtr<InputLayoutImpl>         m_inputLayout;
        ComPtr<ID3D11DepthStencilState> m_depthStencilState;
        ComPtr<ID3D11RasterizerState>   m_rasterizerState;
        ComPtr<ID3D11BlendState>        m_blendState;

        UINT                            m_stencilRef;
        float                           m_blendColor[4];
        UINT                            m_sampleMask;
    };

    class ComputePipelineStateImpl : public PipelineStateImpl
    {
    public:
    };

        /// Capture a texture to a file
    static HRESULT captureTextureToSurface(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, Surface& surfaceOut);

    void _flushGraphicsState();
    void _flushComputeState();

    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_immediateContext;
    ComPtr<ID3D11Texture2D> m_backBufferTexture;

    RefPtr<TextureResourceImpl>     m_primaryRenderTargetTexture;
    RefPtr<RenderTargetViewImpl>    m_primaryRenderTargetView;

//    List<ComPtr<ID3D11RenderTargetView> > m_renderTargetViews;
//    List<ComPtr<ID3D11Texture2D> > m_renderTargetTextures;

    bool m_renderTargetBindingsDirty = false;

    RefPtr<GraphicsPipelineStateImpl>   m_currentGraphicsState;
    RefPtr<ComputePipelineStateImpl>    m_currentComputeState;

    ComPtr<ID3D11RenderTargetView>      m_rtvBindings[kMaxRTVs];
    ComPtr<ID3D11DepthStencilView>      m_dsvBinding;
    ComPtr<ID3D11UnorderedAccessView>   m_uavBindings[int(PipelineType::CountOf)][kMaxUAVs];
    bool m_targetBindingsDirty[int(PipelineType::CountOf)];

    Desc m_desc;

    float m_clearColor[4] = { 0, 0, 0, 0 };
};

Renderer* createD3D11Renderer()
{
    return new D3D11Renderer();
}

/* static */HRESULT D3D11Renderer::captureTextureToSurface(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, Surface& surfaceOut)
{
    if (!context) return E_INVALIDARG;
    if (!texture) return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC textureDesc;
    texture->GetDesc(&textureDesc);

    // Don't bother supporting MSAA for right now
    if (textureDesc.SampleDesc.Count > 1)
    {
        fprintf(stderr, "ERROR: cannot capture multi-sample texture\n");
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    ComPtr<ID3D11Texture2D> stagingTexture;

    if (textureDesc.Usage == D3D11_USAGE_STAGING && (textureDesc.CPUAccessFlags & D3D11_CPU_ACCESS_READ))
    {
        stagingTexture = texture;
    }
    else
    {
        // Modify the descriptor to give us a staging texture
        textureDesc.BindFlags = 0;
        textureDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_TEXTURECUBE;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        textureDesc.Usage = D3D11_USAGE_STAGING;

        hr = device->CreateTexture2D(&textureDesc, 0, stagingTexture.writeRef());
        if (FAILED(hr))
        {
            fprintf(stderr, "ERROR: failed to create staging texture\n");
            return hr;
        }

        context->CopyResource(stagingTexture, texture);
    }

    // Now just read back texels from the staging textures
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        SLANG_RETURN_ON_FAIL(context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource));

        Result res = surfaceOut.set(textureDesc.Width, textureDesc.Height, Format::RGBA_Unorm_UInt8, mappedResource.RowPitch, mappedResource.pData, SurfaceAllocator::getMallocAllocator());

        // Make sure to unmap
        context->Unmap(stagingTexture, 0);
        return res;
    }
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!! Renderer interface !!!!!!!!!!!!!!!!!!!!!!!!!!

SlangResult D3D11Renderer::initialize(const Desc& desc, void* inWindowHandle)
{
    auto windowHandle = (HWND)inWindowHandle;
    m_desc = desc;

    // Rather than statically link against D3D, we load it dynamically.
    HMODULE d3dModule = LoadLibraryA("d3d11.dll");
    if (!d3dModule)
    {
        fprintf(stderr, "error: failed load 'd3d11.dll'\n");
        return SLANG_FAIL;
    }

    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN D3D11CreateDeviceAndSwapChain_ =
        (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)GetProcAddress(d3dModule, "D3D11CreateDeviceAndSwapChain");
    if (!D3D11CreateDeviceAndSwapChain_)
    {
        fprintf(stderr,
            "error: failed load symbol 'D3D11CreateDeviceAndSwapChain'\n");
        return SLANG_FAIL;
    }

    // Our swap chain uses RGBA8 with sRGB, with double buffering.
    DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    // Note(tfoley): Disabling sRGB for DX back buffer for now, so that we
    // can get consistent output with OpenGL, where setting up sRGB will
    // probably be more involved.
    // swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.OutputWindow = windowHandle;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapChainDesc.Flags = 0;

    // We will ask for the highest feature level that can be supported.
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    const int totalNumFeatureLevels = SLANG_COUNT_OF(featureLevels);

    {
        // On a machine that does not have an up-to-date version of D3D installed,
        // the `D3D11CreateDeviceAndSwapChain` call will fail with `E_INVALIDARG`
        // if you ask for feature level 11_1 (DeviceCheckFlag::UseFullFeatureLevel).
        // The workaround is to call `D3D11CreateDeviceAndSwapChain` the first time
        // with 11_1 and then back off to 11_0 if that fails.

        FlagCombiner combiner;
        // TODO: we should probably provide a command-line option
        // to override UseDebug of default rather than leave it
        // up to each back-end to specify.

#if _DEBUG
        combiner.add(DeviceCheckFlag::UseDebug, ChangeType::OnOff);                 ///< First try debug then non debug
#else
        combiner.add(DeviceCheckFlag::UseDebug, ChangeType::Off);                   ///< Don't bother with debug
#endif
        combiner.add(DeviceCheckFlag::UseHardwareDevice, ChangeType::OnOff);        ///< First try hardware, then reference
        combiner.add(DeviceCheckFlag::UseFullFeatureLevel, ChangeType::OnOff);      ///< First try fully featured, then degrade features

        const int numCombinations = combiner.getNumCombinations();
        Result res = SLANG_FAIL;
        for (int i = 0; i < numCombinations; ++i)
        {
            const auto deviceCheckFlags = combiner.getCombination(i);
            const D3D_DRIVER_TYPE driverType = (deviceCheckFlags & DeviceCheckFlag::UseHardwareDevice) ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_REFERENCE;
            const int startFeatureIndex = (deviceCheckFlags & DeviceCheckFlag::UseFullFeatureLevel) ? 0 : 1; 
            const UINT deviceFlags = (deviceCheckFlags & DeviceCheckFlag::UseDebug) ? D3D11_CREATE_DEVICE_DEBUG : 0;

            res = D3D11CreateDeviceAndSwapChain_(
                nullptr,                    // adapter (use default)
                driverType,
                nullptr,                    // software
                deviceFlags,
                &featureLevels[startFeatureIndex],
                totalNumFeatureLevels - startFeatureIndex,
                D3D11_SDK_VERSION,
                &swapChainDesc,
                m_swapChain.writeRef(),
                m_device.writeRef(),
                &featureLevel,
                m_immediateContext.writeRef());

            // Check if successfully constructed - if so we are done. 
            if (SLANG_SUCCEEDED(res))
            {
                break;
            }
        }
        // If res is failure, means all styles have have failed, and so initialization fails.
        if (SLANG_FAILED(res))
        {
            return res;
        }
        // Check we have a swap chain, context and device
        SLANG_ASSERT(m_immediateContext && m_swapChain && m_device);
    }

    // TODO: Add support for debugging to help detect leaks:
    //
    //      ComPtr<ID3D11Debug> gDebug;
    //      m_device->QueryInterface(IID_PPV_ARGS(gDebug.writeRef()));
    //

    // After we've created the swap chain, we can request a pointer to the
    // back buffer as a D3D11 texture, and create a render-target view from it.

    static const IID kIID_ID3D11Texture2D = {
        0x6f15aaf2, 0xd208, 0x4e89, 0x9a, 0xb4, 0x48,
        0x95, 0x35, 0xd3, 0x4f, 0x9c };

    SLANG_RETURN_ON_FAIL(m_swapChain->GetBuffer(0, kIID_ID3D11Texture2D, (void**)m_backBufferTexture.writeRef()));

//    for (int i = 0; i < 8; i++)
    {
        ComPtr<ID3D11Texture2D> texture;
        D3D11_TEXTURE2D_DESC textureDesc;
        m_backBufferTexture->GetDesc(&textureDesc);
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture2D(&textureDesc, nullptr, texture.writeRef()));

        ComPtr<ID3D11RenderTargetView> rtv;
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        SLANG_RETURN_ON_FAIL(m_device->CreateRenderTargetView(texture, &rtvDesc, rtv.writeRef()));

        TextureResource::Desc resourceDesc;
        resourceDesc.init2D(Resource::Type::Texture2D, Format::RGBA_Unorm_UInt8, textureDesc.Width, textureDesc.Height, 1);

        RefPtr<TextureResource> primaryRenderTargetTexture;
        SLANG_RETURN_ON_FAIL(createTextureResource(Resource::Usage::RenderTarget, resourceDesc, nullptr, primaryRenderTargetTexture.writeRef()));

        ResourceView::Desc viewDesc;
        viewDesc.format = resourceDesc.format;
        viewDesc.type = ResourceView::Type::RenderTarget;
        RefPtr<ResourceView> primaryRenderTargetView;
        SLANG_RETURN_ON_FAIL(createTextureView(primaryRenderTargetTexture, viewDesc, primaryRenderTargetView.writeRef()));

        m_primaryRenderTargetTexture = (TextureResourceImpl*) primaryRenderTargetTexture.Ptr();
        m_primaryRenderTargetView = (RenderTargetViewImpl*) primaryRenderTargetView.Ptr();
    }

//    m_immediateContext->OMSetRenderTargets(1, m_primaryRenderTargetView->m_rtv.readRef(), nullptr);
    m_rtvBindings[0] = m_primaryRenderTargetView->m_rtv;
    m_targetBindingsDirty[int(PipelineType::Graphics)] = true;

    // Similarly, we are going to set up a viewport once, and then never
    // switch, since this is a simple test app.
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (float)desc.width;
    viewport.Height = (float)desc.height;
    viewport.MaxDepth = 1; // TODO(tfoley): use reversed depth
    viewport.MinDepth = 0;
    m_immediateContext->RSSetViewports(1, &viewport);

    return SLANG_OK;
}

void D3D11Renderer::setClearColor(const float color[4])
{
    memcpy(m_clearColor, color, sizeof(m_clearColor));
}

void D3D11Renderer::clearFrame()
{
    m_immediateContext->ClearRenderTargetView(m_primaryRenderTargetView->m_rtv, m_clearColor);

    if(m_dsvBinding)
    {
        m_immediateContext->ClearDepthStencilView(m_dsvBinding, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
}

void D3D11Renderer::presentFrame()
{
    m_immediateContext->CopyResource(m_backBufferTexture, m_primaryRenderTargetTexture->m_resource);
    m_swapChain->Present(0, 0);
}

TextureResource::Desc D3D11Renderer::getSwapChainTextureDesc()
{
    D3D11_TEXTURE2D_DESC dxDesc;
    ((ID3D11Texture2D*)m_primaryRenderTargetTexture->m_resource.get())->GetDesc(&dxDesc);

    TextureResource::Desc desc;
    desc.init2D(Resource::Type::Texture2D, Format::Unknown, dxDesc.Width, dxDesc.Height, 1);

    return desc;
}

SlangResult D3D11Renderer::captureScreenSurface(Surface& surfaceOut)
{
    return captureTextureToSurface(m_device, m_immediateContext, (ID3D11Texture2D*) m_primaryRenderTargetTexture->m_resource.get(), surfaceOut);
}

static D3D11_BIND_FLAG _calcResourceFlag(Resource::BindFlag::Enum bindFlag)
{
    typedef Resource::BindFlag BindFlag;
    switch (bindFlag)
    {
        case BindFlag::VertexBuffer:            return D3D11_BIND_VERTEX_BUFFER;
        case BindFlag::IndexBuffer:             return D3D11_BIND_INDEX_BUFFER;
        case BindFlag::ConstantBuffer:          return D3D11_BIND_CONSTANT_BUFFER;
        case BindFlag::StreamOutput:            return D3D11_BIND_STREAM_OUTPUT;
        case BindFlag::RenderTarget:            return D3D11_BIND_RENDER_TARGET;
        case BindFlag::DepthStencil:            return D3D11_BIND_DEPTH_STENCIL;
        case BindFlag::UnorderedAccess:         return D3D11_BIND_UNORDERED_ACCESS;
        case BindFlag::PixelShaderResource:     return D3D11_BIND_SHADER_RESOURCE;
        case BindFlag::NonPixelShaderResource:  return D3D11_BIND_SHADER_RESOURCE;
        default:                                return D3D11_BIND_FLAG(0);
    }
}

static int _calcResourceBindFlags(int bindFlags)
{
    int dstFlags = 0;
    while (bindFlags)
    {
        int lsb = bindFlags & -bindFlags;

        dstFlags |= _calcResourceFlag(Resource::BindFlag::Enum(lsb));
        bindFlags &= ~lsb;
    }
    return dstFlags;
}

static int _calcResourceAccessFlags(int accessFlags)
{
    switch (accessFlags)
    {
        case 0:         return 0;
        case Resource::AccessFlag::Read:            return D3D11_CPU_ACCESS_READ;
        case Resource::AccessFlag::Write:           return D3D11_CPU_ACCESS_WRITE;
        case Resource::AccessFlag::Read |
             Resource::AccessFlag::Write:           return D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        default: assert(!"Invalid flags"); return 0;
    }
}

Result D3D11Renderer::createTextureResource(Resource::Usage initialUsage, const TextureResource::Desc& descIn, const TextureResource::Data* initData, TextureResource** outResource)
{
    TextureResource::Desc srcDesc(descIn);
    srcDesc.setDefaults(initialUsage);

    const int effectiveArraySize = srcDesc.calcEffectiveArraySize();

    if(initData)
    {
        assert(initData->numSubResources == srcDesc.numMipLevels * effectiveArraySize * srcDesc.size.depth);
    }

    const DXGI_FORMAT format = D3DUtil::getMapFormat(srcDesc.format);
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    const int bindFlags = _calcResourceBindFlags(srcDesc.bindFlags);

    // Set up the initialize data
    List<D3D11_SUBRESOURCE_DATA> subRes;
    D3D11_SUBRESOURCE_DATA* subResourcesPtr = nullptr;
    if(initData)
    {
        subRes.SetSize(srcDesc.numMipLevels * effectiveArraySize);
        {
            int subResourceIndex = 0;
            for (int i = 0; i < effectiveArraySize; i++)
            {
                for (int j = 0; j < srcDesc.numMipLevels; j++)
                {
                    const int mipHeight = TextureResource::calcMipSize(srcDesc.size.height, j);

                    D3D11_SUBRESOURCE_DATA& data = subRes[subResourceIndex];

                    data.pSysMem = initData->subResources[subResourceIndex];

                    data.SysMemPitch = UINT(initData->mipRowStrides[j]);
                    data.SysMemSlicePitch = UINT(initData->mipRowStrides[j] * mipHeight);

                    subResourceIndex++;
                }
            }
        }
        subResourcesPtr = subRes.Buffer();
    }

    const int accessFlags = _calcResourceAccessFlags(srcDesc.cpuAccessFlags);

    RefPtr<TextureResourceImpl> texture(new TextureResourceImpl(srcDesc, initialUsage));

    switch (srcDesc.type)
    {
        case Resource::Type::Texture1D:
        {
            D3D11_TEXTURE1D_DESC desc = { 0 };
            desc.BindFlags = bindFlags;
            desc.CPUAccessFlags = accessFlags;
            desc.Format = format;
            desc.MiscFlags = 0;
            desc.MipLevels = srcDesc.numMipLevels;
            desc.ArraySize = effectiveArraySize;
            desc.Width = srcDesc.size.width;
            desc.Usage = D3D11_USAGE_DEFAULT;

            ComPtr<ID3D11Texture1D> texture1D;
            SLANG_RETURN_ON_FAIL(m_device->CreateTexture1D(&desc, subResourcesPtr, texture1D.writeRef()));

            texture->m_resource = texture1D;
            break;
        }
        case Resource::Type::TextureCube:
        case Resource::Type::Texture2D:
        {
            D3D11_TEXTURE2D_DESC desc = { 0 };
            desc.BindFlags = bindFlags;
            desc.CPUAccessFlags = accessFlags;
            desc.Format = format;
            desc.MiscFlags = 0;
            desc.MipLevels = srcDesc.numMipLevels;
            desc.ArraySize = effectiveArraySize;

            desc.Width = srcDesc.size.width;
            desc.Height = srcDesc.size.height;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.SampleDesc.Count = srcDesc.sampleDesc.numSamples;
            desc.SampleDesc.Quality = srcDesc.sampleDesc.quality;

            if (srcDesc.type == Resource::Type::TextureCube)
            {
                desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
            }

            ComPtr<ID3D11Texture2D> texture2D;
            SLANG_RETURN_ON_FAIL(m_device->CreateTexture2D(&desc, subResourcesPtr, texture2D.writeRef()));

            texture->m_resource = texture2D;
            break;
        }
        case Resource::Type::Texture3D:
        {
            D3D11_TEXTURE3D_DESC desc = { 0 };
            desc.BindFlags = bindFlags;
            desc.CPUAccessFlags = accessFlags;
            desc.Format = format;
            desc.MiscFlags = 0;
            desc.MipLevels = srcDesc.numMipLevels;
            desc.Width = srcDesc.size.width;
            desc.Height = srcDesc.size.height;
            desc.Depth = srcDesc.size.depth;
            desc.Usage = D3D11_USAGE_DEFAULT;

            ComPtr<ID3D11Texture3D> texture3D;
            SLANG_RETURN_ON_FAIL(m_device->CreateTexture3D(&desc, subResourcesPtr, texture3D.writeRef()));

            texture->m_resource = texture3D;
            break;
        }
        default:
            return SLANG_FAIL;
    }

    *outResource = texture.detach();
    return SLANG_OK;
}

Result D3D11Renderer::createBufferResource(Resource::Usage initialUsage, const BufferResource::Desc& descIn, const void* initData, BufferResource** outResource)
{
    BufferResource::Desc srcDesc(descIn);
    srcDesc.setDefaults(initialUsage);

    auto d3dBindFlags = _calcResourceBindFlags(srcDesc.bindFlags);

    size_t alignedSizeInBytes = srcDesc.sizeInBytes;

    if(d3dBindFlags & D3D11_BIND_CONSTANT_BUFFER)
    {
        // Make aligned to 256 bytes... not sure why, but if you remove this the tests do fail.
        alignedSizeInBytes = D3DUtil::calcAligned(alignedSizeInBytes, 256);
    }

    // Hack to make the initialization never read from out of bounds memory, by copying into a buffer
    List<uint8_t> initDataBuffer;
    if (initData && alignedSizeInBytes > srcDesc.sizeInBytes)
    {
        initDataBuffer.SetSize(alignedSizeInBytes);
        ::memcpy(initDataBuffer.Buffer(), initData, srcDesc.sizeInBytes);
        initData = initDataBuffer.Buffer();
    }

    D3D11_BUFFER_DESC bufferDesc = { 0 };
    bufferDesc.ByteWidth = UINT(alignedSizeInBytes);
    bufferDesc.BindFlags = d3dBindFlags;
    // For read we'll need to do some staging
    bufferDesc.CPUAccessFlags = _calcResourceAccessFlags(descIn.cpuAccessFlags & Resource::AccessFlag::Write);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;

    // If written by CPU, make it dynamic
    if (descIn.cpuAccessFlags & Resource::AccessFlag::Write)
    {
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    }

    switch (initialUsage)
    {
        case Resource::Usage::ConstantBuffer:
        {
            // We'll just assume ConstantBuffers are dynamic for now
            bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
            break;
        }
        default: break;
    }

    if (bufferDesc.BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE))
    {
        //desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        if (srcDesc.elementSize != 0)
        {
            bufferDesc.StructureByteStride = srcDesc.elementSize;
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        }
        else
        {
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        }
    }

    D3D11_SUBRESOURCE_DATA subResourceData = { 0 };
    subResourceData.pSysMem = initData;

    RefPtr<BufferResourceImpl> buffer(new BufferResourceImpl(srcDesc, initialUsage));

    SLANG_RETURN_ON_FAIL(m_device->CreateBuffer(&bufferDesc, initData ? &subResourceData : nullptr, buffer->m_buffer.writeRef()));

    if (srcDesc.cpuAccessFlags & Resource::AccessFlag::Read)
    {
        D3D11_BUFFER_DESC bufDesc = {};
        bufDesc.BindFlags = 0;
        bufDesc.ByteWidth = (UINT)alignedSizeInBytes;
        bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        bufDesc.Usage = D3D11_USAGE_STAGING;

        SLANG_RETURN_ON_FAIL(m_device->CreateBuffer(&bufDesc, nullptr, buffer->m_staging.writeRef()));
    }

    *outResource = buffer.detach();
    return SLANG_OK;
}

D3D11_FILTER_TYPE translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    default:
        return D3D11_FILTER_TYPE(0);

#define CASE(SRC, DST) \
    case TextureFilteringMode::SRC: return D3D11_FILTER_TYPE_##DST

        CASE(Point, POINT);
        CASE(Linear, LINEAR);

#undef CASE
    }
}

D3D11_FILTER_REDUCTION_TYPE translateFilterReduction(TextureReductionOp op)
{
    switch (op)
    {
    default:
        return D3D11_FILTER_REDUCTION_TYPE(0);

#define CASE(SRC, DST) \
    case TextureReductionOp::SRC: return D3D11_FILTER_REDUCTION_TYPE_##DST

        CASE(Average, STANDARD);
        CASE(Comparison, COMPARISON);
        CASE(Minimum, MINIMUM);
        CASE(Maximum, MAXIMUM);

#undef CASE
    }
}

D3D11_TEXTURE_ADDRESS_MODE translateAddressingMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    default:
        return D3D11_TEXTURE_ADDRESS_MODE(0);

#define CASE(SRC, DST) \
    case TextureAddressingMode::SRC: return D3D11_TEXTURE_ADDRESS_##DST

    CASE(Wrap,          WRAP);
    CASE(ClampToEdge,   CLAMP);
    CASE(ClampToBorder, BORDER);
    CASE(MirrorRepeat,  MIRROR);
    CASE(MirrorOnce,    MIRROR_ONCE);

#undef CASE
    }
}

static D3D11_COMPARISON_FUNC translateComparisonFunc(ComparisonFunc func)
{
    switch (func)
    {
    default:
        // TODO: need to report failures
        return D3D11_COMPARISON_ALWAYS;

#define CASE(FROM, TO) \
    case ComparisonFunc::FROM: return D3D11_COMPARISON_##TO

        CASE(Never, NEVER);
        CASE(Less, LESS);
        CASE(Equal, EQUAL);
        CASE(LessEqual, LESS_EQUAL);
        CASE(Greater, GREATER);
        CASE(NotEqual, NOT_EQUAL);
        CASE(GreaterEqual, GREATER_EQUAL);
        CASE(Always, ALWAYS);
#undef CASE
    }
}

Result D3D11Renderer::createSamplerState(SamplerState::Desc const& desc, SamplerState** outSampler)
{
    D3D11_FILTER_REDUCTION_TYPE dxReduction = translateFilterReduction(desc.reductionOp);
    D3D11_FILTER dxFilter;
    if (desc.maxAnisotropy > 1)
    {
        dxFilter = D3D11_ENCODE_ANISOTROPIC_FILTER(dxReduction);
    }
    else
    {
        D3D11_FILTER_TYPE dxMin = translateFilterMode(desc.minFilter);
        D3D11_FILTER_TYPE dxMag = translateFilterMode(desc.magFilter);
        D3D11_FILTER_TYPE dxMip = translateFilterMode(desc.mipFilter);

        dxFilter = D3D11_ENCODE_BASIC_FILTER(dxMin, dxMag, dxMip, dxReduction);
    }

    D3D11_SAMPLER_DESC dxDesc = {};
    dxDesc.Filter = dxFilter;
    dxDesc.AddressU = translateAddressingMode(desc.addressU);
    dxDesc.AddressV = translateAddressingMode(desc.addressV);
    dxDesc.AddressW = translateAddressingMode(desc.addressW);
    dxDesc.MipLODBias = desc.mipLODBias;
    dxDesc.MaxAnisotropy = desc.maxAnisotropy;
    dxDesc.ComparisonFunc = translateComparisonFunc(desc.comparisonFunc);
    for (int ii = 0; ii < 4; ++ii)
        dxDesc.BorderColor[ii] = desc.borderColor[ii];
    dxDesc.MinLOD = desc.minLOD;
    dxDesc.MaxLOD = desc.maxLOD;

    ComPtr<ID3D11SamplerState> sampler;
    SLANG_RETURN_ON_FAIL(m_device->CreateSamplerState(
        &dxDesc,
        sampler.writeRef()));

    RefPtr<SamplerStateImpl> samplerImpl = new SamplerStateImpl();
    samplerImpl->m_sampler = sampler;
    *outSampler = samplerImpl.detach();
    return SLANG_OK;
}

Result D3D11Renderer::createTextureView(TextureResource* texture, ResourceView::Desc const& desc, ResourceView** outView)
{
    auto resourceImpl = (TextureResourceImpl*) texture;

    switch (desc.type)
    {
    default:
        return SLANG_FAIL;

    case ResourceView::Type::RenderTarget:
        {
            ComPtr<ID3D11RenderTargetView> rtv;
            SLANG_RETURN_ON_FAIL(m_device->CreateRenderTargetView(resourceImpl->m_resource, nullptr, rtv.writeRef()));

            RefPtr<RenderTargetViewImpl> viewImpl = new RenderTargetViewImpl();
            viewImpl->m_type = ResourceViewImpl::Type::RTV;
            viewImpl->m_rtv = rtv;
            *outView = viewImpl.detach();
            return SLANG_OK;
        }
        break;

    case ResourceView::Type::DepthStencil:
        {
            ComPtr<ID3D11DepthStencilView> dsv;
            SLANG_RETURN_ON_FAIL(m_device->CreateDepthStencilView(resourceImpl->m_resource, nullptr, dsv.writeRef()));

            RefPtr<DepthStencilViewImpl> viewImpl = new DepthStencilViewImpl();
            viewImpl->m_type = ResourceViewImpl::Type::DSV;
            viewImpl->m_dsv = dsv;
            *outView = viewImpl.detach();
            return SLANG_OK;
        }
        break;

    case ResourceView::Type::UnorderedAccess:
        {
            ComPtr<ID3D11UnorderedAccessView> uav;
            SLANG_RETURN_ON_FAIL(m_device->CreateUnorderedAccessView(resourceImpl->m_resource, nullptr, uav.writeRef()));

            RefPtr<UnorderedAccessViewImpl> viewImpl = new UnorderedAccessViewImpl();
            viewImpl->m_type = ResourceViewImpl::Type::UAV;
            viewImpl->m_uav = uav;
            *outView = viewImpl.detach();
            return SLANG_OK;
        }
        break;

    case ResourceView::Type::ShaderResource:
        {
            ComPtr<ID3D11ShaderResourceView> srv;
            SLANG_RETURN_ON_FAIL(m_device->CreateShaderResourceView(resourceImpl->m_resource, nullptr, srv.writeRef()));

            RefPtr<ShaderResourceViewImpl> viewImpl = new ShaderResourceViewImpl();
            viewImpl->m_type = ResourceViewImpl::Type::SRV;
            viewImpl->m_srv = srv;
            *outView = viewImpl.detach();
            return SLANG_OK;
        }
        break;
    }
}

Result D3D11Renderer::createBufferView(BufferResource* buffer, ResourceView::Desc const& desc, ResourceView** outView)
{
    auto resourceImpl = (BufferResourceImpl*) buffer;
    auto resourceDesc = resourceImpl->getDesc();

    switch (desc.type)
    {
    default:
        return SLANG_FAIL;

    case ResourceView::Type::UnorderedAccess:
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Format = D3DUtil::getMapFormat(desc.format);
            uavDesc.Buffer.FirstElement = 0;

            if(resourceDesc.elementSize)
            {
                uavDesc.Buffer.NumElements = UINT(resourceDesc.sizeInBytes / resourceDesc.elementSize);
            }
            else if(desc.format == Format::Unknown)
            {
                uavDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
                uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                uavDesc.Buffer.NumElements = UINT(resourceDesc.sizeInBytes / 4);
            }
            else
            {
                uavDesc.Buffer.NumElements = UINT(resourceDesc.sizeInBytes / RendererUtil::getFormatSize(desc.format));
            }

            ComPtr<ID3D11UnorderedAccessView> uav;
            SLANG_RETURN_ON_FAIL(m_device->CreateUnorderedAccessView(resourceImpl->m_buffer, &uavDesc, uav.writeRef()));

            RefPtr<UnorderedAccessViewImpl> viewImpl = new UnorderedAccessViewImpl();
            viewImpl->m_type = ResourceViewImpl::Type::UAV;
            viewImpl->m_uav = uav;
            *outView = viewImpl.detach();
            return SLANG_OK;
        }
        break;

    case ResourceView::Type::ShaderResource:
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Format = D3DUtil::getMapFormat(desc.format);
            srvDesc.Buffer.FirstElement = 0;

            if(resourceDesc.elementSize)
            {
                srvDesc.Buffer.NumElements = UINT(resourceDesc.sizeInBytes / resourceDesc.elementSize);
            }
            else if(desc.format == Format::Unknown)
            {
                // We need to switch to a different member of the `union`,
                // so that we can set the `BufferEx.Flags` member.
                //
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;

                // Because we've switched, we need to re-set the `FirstElement`
                // field to be valid, since we can't count on all compilers
                // to respect that `Buffer.FirstElement` and `BufferEx.FirstElement`
                // alias in memory.
                //
                srvDesc.BufferEx.FirstElement = 0;

                srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
                srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                srvDesc.BufferEx.NumElements = UINT(resourceDesc.sizeInBytes / 4);
            }
            else
            {
                srvDesc.Buffer.NumElements = UINT(resourceDesc.sizeInBytes / RendererUtil::getFormatSize(desc.format));
            }

            ComPtr<ID3D11ShaderResourceView> srv;
            SLANG_RETURN_ON_FAIL(m_device->CreateShaderResourceView(resourceImpl->m_buffer, &srvDesc, srv.writeRef()));

            RefPtr<ShaderResourceViewImpl> viewImpl = new ShaderResourceViewImpl();
            viewImpl->m_type = ResourceViewImpl::Type::SRV;
            viewImpl->m_srv = srv;
            *outView = viewImpl.detach();
            return SLANG_OK;
        }
        break;
    }
}

Result D3D11Renderer::createInputLayout(const InputElementDesc* inputElementsIn, UInt inputElementCount, InputLayout** outLayout)
{
    D3D11_INPUT_ELEMENT_DESC inputElements[16] = {};

    char hlslBuffer[1024];
    char* hlslCursor = &hlslBuffer[0];

    hlslCursor += sprintf(hlslCursor, "float4 main(\n");

    for (UInt ii = 0; ii < inputElementCount; ++ii)
    {
        inputElements[ii].SemanticName = inputElementsIn[ii].semanticName;
        inputElements[ii].SemanticIndex = (UINT)inputElementsIn[ii].semanticIndex;
        inputElements[ii].Format = D3DUtil::getMapFormat(inputElementsIn[ii].format);
        inputElements[ii].InputSlot = 0;
        inputElements[ii].AlignedByteOffset = (UINT)inputElementsIn[ii].offset;
        inputElements[ii].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        inputElements[ii].InstanceDataStepRate = 0;

        if (ii != 0)
        {
            hlslCursor += sprintf(hlslCursor, ",\n");
        }

        char const* typeName = "Unknown";
        switch (inputElementsIn[ii].format)
        {
            case Format::RGBA_Float32:
            case Format::RGBA_Unorm_UInt8:
                typeName = "float4";
                break;
            case Format::RGB_Float32:
                typeName = "float3";
                break;
            case Format::RG_Float32:
                typeName = "float2";
                break;
            case Format::R_Float32:
                typeName = "float";
                break;
            default:
                return SLANG_FAIL;
        }

        hlslCursor += sprintf(hlslCursor, "%s a%d : %s%d",
            typeName,
            (int)ii,
            inputElementsIn[ii].semanticName,
            (int)inputElementsIn[ii].semanticIndex);
    }

    hlslCursor += sprintf(hlslCursor, "\n) : SV_Position { return 0; }");

    ComPtr<ID3DBlob> vertexShaderBlob;
    SLANG_RETURN_ON_FAIL(D3DUtil::compileHLSLShader("inputLayout", hlslBuffer, "main", "vs_5_0", vertexShaderBlob));

    ComPtr<ID3D11InputLayout> inputLayout;
    SLANG_RETURN_ON_FAIL(m_device->CreateInputLayout(&inputElements[0], (UINT)inputElementCount, vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(),
        inputLayout.writeRef()));

    RefPtr<InputLayoutImpl> impl = new InputLayoutImpl;
    impl->m_layout.swap(inputLayout);

    *outLayout = impl.detach();
    return SLANG_OK;
}

void* D3D11Renderer::map(BufferResource* bufferIn, MapFlavor flavor)
{
    BufferResourceImpl* bufferResource = static_cast<BufferResourceImpl*>(bufferIn);

    D3D11_MAP mapType;
    ID3D11Buffer* buffer = bufferResource->m_buffer;

    switch (flavor)
    {
        case MapFlavor::WriteDiscard:
            mapType = D3D11_MAP_WRITE_DISCARD;
            break;
        case MapFlavor::HostWrite:
            mapType = D3D11_MAP_WRITE;
            break;
        case MapFlavor::HostRead:
            mapType = D3D11_MAP_READ;

            buffer = bufferResource->m_staging;
            if (!buffer)
            {
                return nullptr;
            }

            // Okay copy the data over
            m_immediateContext->CopyResource(buffer, bufferResource->m_buffer);

            break;
        default:
            return nullptr;
    }

    // We update our constant buffer per-frame, just for the purposes
    // of the example, but we don't actually load different data
    // per-frame (we always use an identity projection).
    D3D11_MAPPED_SUBRESOURCE mappedSub;
    SLANG_RETURN_NULL_ON_FAIL(m_immediateContext->Map(buffer, 0, mapType, 0, &mappedSub));

    bufferResource->m_mapFlavor = flavor;

    return mappedSub.pData;
}

void D3D11Renderer::unmap(BufferResource* bufferIn)
{
    BufferResourceImpl* bufferResource = static_cast<BufferResourceImpl*>(bufferIn);
    ID3D11Buffer* buffer = (bufferResource->m_mapFlavor == MapFlavor::HostRead) ? bufferResource->m_staging : bufferResource->m_buffer;
    m_immediateContext->Unmap(buffer, 0);
}

#if 0
void D3D11Renderer::setInputLayout(InputLayout* inputLayoutIn)
{
    auto inputLayout = static_cast<InputLayoutImpl*>(inputLayoutIn);
    m_immediateContext->IASetInputLayout(inputLayout->m_layout);
}
#endif

void D3D11Renderer::setPrimitiveTopology(PrimitiveTopology topology)
{
    m_immediateContext->IASetPrimitiveTopology(D3DUtil::getPrimitiveTopology(topology));
}

void D3D11Renderer::setVertexBuffers(UInt startSlot, UInt slotCount, BufferResource*const* buffersIn, const UInt* stridesIn, const UInt* offsetsIn)
{
    static const int kMaxVertexBuffers = 16;
	assert(slotCount <= kMaxVertexBuffers);

    UINT vertexStrides[kMaxVertexBuffers];
    UINT vertexOffsets[kMaxVertexBuffers];
	ID3D11Buffer* dxBuffers[kMaxVertexBuffers];

	auto buffers = (BufferResourceImpl*const*)buffersIn;

    for (UInt ii = 0; ii < slotCount; ++ii)
    {
        vertexStrides[ii] = (UINT)stridesIn[ii];
        vertexOffsets[ii] = (UINT)offsetsIn[ii];
		dxBuffers[ii] = buffers[ii]->m_buffer;
	}

    m_immediateContext->IASetVertexBuffers((UINT)startSlot, (UINT)slotCount, dxBuffers, &vertexStrides[0], &vertexOffsets[0]);
}

void D3D11Renderer::setIndexBuffer(BufferResource* buffer, Format indexFormat, UInt offset)
{
    DXGI_FORMAT dxFormat = D3DUtil::getMapFormat(indexFormat);
    m_immediateContext->IASetIndexBuffer(((BufferResourceImpl*)buffer)->m_buffer, dxFormat, UINT(offset));
}

void D3D11Renderer::setDepthStencilTarget(ResourceView* depthStencilView)
{
    m_dsvBinding = ((DepthStencilViewImpl*) depthStencilView)->m_dsv;
    m_targetBindingsDirty[int(PipelineType::Graphics)] = true;
}

void D3D11Renderer::setViewports(UInt count, Viewport const* viewports)
{
    static const int kMaxViewports = D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1;
    assert(count <= kMaxViewports);

    D3D11_VIEWPORT dxViewports[kMaxViewports];
    for(UInt ii = 0; ii < count; ++ii)
    {
        auto& inViewport = viewports[ii];
        auto& dxViewport = dxViewports[ii];

        dxViewport.TopLeftX = inViewport.originX;
        dxViewport.TopLeftY = inViewport.originY;
        dxViewport.Width    = inViewport.extentX;
        dxViewport.Height   = inViewport.extentY;
        dxViewport.MinDepth = inViewport.minZ;
        dxViewport.MaxDepth = inViewport.maxZ;
    }

    m_immediateContext->RSSetViewports(UINT(count), dxViewports);
}

void D3D11Renderer::setScissorRects(UInt count, ScissorRect const* rects)
{
    static const int kMaxScissorRects = D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1;
    assert(count <= kMaxScissorRects);

    D3D11_RECT dxRects[kMaxScissorRects];
    for(UInt ii = 0; ii < count; ++ii)
    {
        auto& inRect = rects[ii];
        auto& dxRect = dxRects[ii];

        dxRect.left     = LONG(inRect.minX);
        dxRect.top      = LONG(inRect.minY);
        dxRect.right    = LONG(inRect.maxX);
        dxRect.bottom   = LONG(inRect.maxY);
    }

    m_immediateContext->RSSetScissorRects(UINT(count), dxRects);
}


void D3D11Renderer::setPipelineState(PipelineType pipelineType, PipelineState* state)
{
    switch(pipelineType)
    {
    default:
        break;

    case PipelineType::Graphics:
        {
            auto stateImpl = (GraphicsPipelineStateImpl*) state;
            auto programImpl = stateImpl->m_program;

            // TODO: We could conceivably do some lightweight state
            // differencing here (e.g., check if `programImpl` is the
            // same as the program that is currently bound).
            //
            // It isn't clear how much that would pay off given that
            // the D3D11 runtime seems to do its own state diffing.

            // IA

            m_immediateContext->IASetInputLayout(stateImpl->m_inputLayout->m_layout);

            // VS

            m_immediateContext->VSSetShader(programImpl->m_vertexShader, nullptr, 0);

            // HS

            // DS

            // GS

            // RS

            m_immediateContext->RSSetState(stateImpl->m_rasterizerState);

            // PS

            m_immediateContext->PSSetShader(programImpl->m_pixelShader, nullptr, 0);

            // OM

            m_immediateContext->OMSetBlendState(stateImpl->m_blendState, stateImpl->m_blendColor, stateImpl->m_sampleMask);
            m_immediateContext->OMSetDepthStencilState(stateImpl->m_depthStencilState, stateImpl->m_stencilRef);

            m_currentGraphicsState = stateImpl;
        }
        break;

    case PipelineType::Compute:
        {
            auto stateImpl = (ComputePipelineStateImpl*) state;
            auto programImpl = stateImpl->m_program;

            // CS

            m_immediateContext->CSSetShader(programImpl->m_computeShader, nullptr, 0);

            m_currentComputeState = stateImpl;
        }
        break;
    }

    /// ...
}

void D3D11Renderer::draw(UInt vertexCount, UInt startVertex)
{
    _flushGraphicsState();
    m_immediateContext->Draw((UINT)vertexCount, (UINT)startVertex);
}

void D3D11Renderer::drawIndexed(UInt indexCount, UInt startIndex, UInt baseVertex)
{
    _flushGraphicsState();
    m_immediateContext->DrawIndexed((UINT)indexCount, (UINT)startIndex, (INT)baseVertex);
}

Result D3D11Renderer::createProgram(const ShaderProgram::Desc& desc, ShaderProgram** outProgram)
{
    if (desc.pipelineType == PipelineType::Compute)
    {
        auto computeKernel = desc.findKernel(StageType::Compute);

        ComPtr<ID3D11ComputeShader> computeShader;
        SLANG_RETURN_ON_FAIL(m_device->CreateComputeShader(computeKernel->codeBegin, computeKernel->getCodeSize(), nullptr, computeShader.writeRef()));

        RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl();
        shaderProgram->m_computeShader.swap(computeShader);

        *outProgram = shaderProgram.detach();
        return SLANG_OK;
    }
    else
    {
        auto vertexKernel = desc.findKernel(StageType::Vertex);
        auto fragmentKernel = desc.findKernel(StageType::Fragment);

        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11PixelShader> pixelShader;

        SLANG_RETURN_ON_FAIL(m_device->CreateVertexShader(vertexKernel->codeBegin, vertexKernel->getCodeSize(), nullptr, vertexShader.writeRef()));
        SLANG_RETURN_ON_FAIL(m_device->CreatePixelShader(fragmentKernel->codeBegin, fragmentKernel->getCodeSize(), nullptr, pixelShader.writeRef()));

        RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl();
        shaderProgram->m_vertexShader.swap(vertexShader);
        shaderProgram->m_pixelShader.swap(pixelShader);

        *outProgram = shaderProgram.detach();
        return SLANG_OK;
    }
}

static D3D11_STENCIL_OP translateStencilOp(StencilOp op)
{
    switch(op)
    {
    default:
        // TODO: need to report failures
        return D3D11_STENCIL_OP_KEEP;

#define CASE(FROM, TO) \
    case StencilOp::FROM: return D3D11_STENCIL_OP_##TO

    CASE(Keep,              KEEP);
    CASE(Zero,              ZERO);
    CASE(Replace,           REPLACE);
    CASE(IncrementSaturate, INCR_SAT);
    CASE(DecrementSaturate, DECR_SAT);
    CASE(Invert,            INVERT);
    CASE(IncrementWrap,     INCR);
    CASE(DecrementWrap,     DECR);
#undef CASE

    }
}

static D3D11_FILL_MODE translateFillMode(FillMode mode)
{
    switch(mode)
    {
    default:
        // TODO: need to report failures
        return D3D11_FILL_SOLID;

    case FillMode::Solid:       return D3D11_FILL_SOLID;
    case FillMode::Wireframe:   return D3D11_FILL_WIREFRAME;
    }
}

static D3D11_CULL_MODE translateCullMode(CullMode mode)
{
    switch(mode)
    {
    default:
        // TODO: need to report failures
        return D3D11_CULL_NONE;

    case CullMode::None:    return D3D11_CULL_NONE;
    case CullMode::Back:    return D3D11_CULL_BACK;
    case CullMode::Front:   return D3D11_CULL_FRONT;
    }
}

bool isBlendDisabled(AspectBlendDesc const& desc)
{
    return desc.op == BlendOp::Add
        && desc.srcFactor == BlendFactor::One
        && desc.dstFactor == BlendFactor::Zero;
}


bool isBlendDisabled(TargetBlendDesc const& desc)
{
    return isBlendDisabled(desc.color)
        && isBlendDisabled(desc.alpha);
}

D3D11_BLEND_OP translateBlendOp(BlendOp op)
{
    switch(op)
    {
    default:
        assert(!"unimplemented");
        return (D3D11_BLEND_OP) -1;

#define CASE(FROM, TO) case BlendOp::FROM: return D3D11_BLEND_OP_##TO
    CASE(Add,               ADD);
    CASE(Subtract,          SUBTRACT);
    CASE(ReverseSubtract,   REV_SUBTRACT);
    CASE(Min,               MIN);
    CASE(Max,               MAX);
#undef CASE
    }
}

D3D11_BLEND translateBlendFactor(BlendFactor factor)
{
    switch(factor)
    {
    default:
        assert(!"unimplemented");
        return (D3D11_BLEND) -1;

#define CASE(FROM, TO) case BlendFactor::FROM: return D3D11_BLEND_##TO
    CASE(Zero,                  ZERO);
    CASE(One,                   ONE);
    CASE(SrcColor,              SRC_COLOR);
    CASE(InvSrcColor,           INV_SRC_COLOR);
    CASE(SrcAlpha,              SRC_ALPHA);
    CASE(InvSrcAlpha,           INV_SRC_ALPHA);
    CASE(DestAlpha,             DEST_ALPHA);
    CASE(InvDestAlpha,          INV_DEST_ALPHA);
    CASE(DestColor,             DEST_COLOR);
    CASE(InvDestColor,          INV_DEST_ALPHA);
    CASE(SrcAlphaSaturate,      SRC_ALPHA_SAT);
    CASE(BlendColor,            BLEND_FACTOR);
    CASE(InvBlendColor,         INV_BLEND_FACTOR);
    CASE(SecondarySrcColor,     SRC1_COLOR);
    CASE(InvSecondarySrcColor,  INV_SRC1_COLOR);
    CASE(SecondarySrcAlpha,     SRC1_ALPHA);
    CASE(InvSecondarySrcAlpha,  INV_SRC1_ALPHA);
#undef CASE
    }
}

D3D11_COLOR_WRITE_ENABLE translateRenderTargetWriteMask(RenderTargetWriteMaskT mask)
{
    UINT result = 0;
#define CASE(FROM, TO) if(mask & RenderTargetWriteMask::Enable##FROM) result |= D3D11_COLOR_WRITE_ENABLE_##TO

    CASE(Red,   RED);
    CASE(Green, GREEN);
    CASE(Blue,  BLUE);
    CASE(Alpha, ALPHA);

#undef CASE
    return D3D11_COLOR_WRITE_ENABLE(result);
}

Result D3D11Renderer::createGraphicsPipelineState(const GraphicsPipelineStateDesc& desc, PipelineState** outState)
{
    auto programImpl = (ShaderProgramImpl*) desc.program;

    ComPtr<ID3D11DepthStencilState> depthStencilState;
    {
        D3D11_DEPTH_STENCIL_DESC dsDesc;
        dsDesc.DepthEnable      = desc.depthStencil.depthTestEnable;
        dsDesc.DepthWriteMask   = desc.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        dsDesc.DepthFunc        = translateComparisonFunc(desc.depthStencil.depthFunc);
        dsDesc.StencilEnable    = desc.depthStencil.stencilEnable;
        dsDesc.StencilReadMask  = desc.depthStencil.stencilReadMask;
        dsDesc.StencilWriteMask = desc.depthStencil.stencilWriteMask;

    #define FACE(DST, SRC) \
        dsDesc.DST.StencilFailOp      = translateStencilOp(     desc.depthStencil.SRC.stencilFailOp);       \
        dsDesc.DST.StencilDepthFailOp = translateStencilOp(     desc.depthStencil.SRC.stencilDepthFailOp);  \
        dsDesc.DST.StencilPassOp      = translateStencilOp(     desc.depthStencil.SRC.stencilPassOp);       \
        dsDesc.DST.StencilFunc        = translateComparisonFunc(desc.depthStencil.SRC.stencilFunc);         \
        /* end */

        FACE(FrontFace, frontFace);
        FACE(BackFace,  backFace);

        SLANG_RETURN_ON_FAIL(m_device->CreateDepthStencilState(
            &dsDesc,
            depthStencilState.writeRef()));
    }

    ComPtr<ID3D11RasterizerState> rasterizerState;
    {
        D3D11_RASTERIZER_DESC rsDesc;
        rsDesc.FillMode                 = translateFillMode(desc.rasterizer.fillMode);
        rsDesc.CullMode                 = translateCullMode(desc.rasterizer.cullMode);
        rsDesc.FrontCounterClockwise    = desc.rasterizer.frontFace == FrontFaceMode::Clockwise;
        rsDesc.DepthBias                = desc.rasterizer.depthBias;
        rsDesc.DepthBiasClamp           = desc.rasterizer.depthBiasClamp;
        rsDesc.SlopeScaledDepthBias     = desc.rasterizer.slopeScaledDepthBias;
        rsDesc.DepthClipEnable          = desc.rasterizer.depthClipEnable;
        rsDesc.ScissorEnable            = desc.rasterizer.scissorEnable;
        rsDesc.MultisampleEnable        = desc.rasterizer.multisampleEnable;
        rsDesc.AntialiasedLineEnable    = desc.rasterizer.antialiasedLineEnable;

        SLANG_RETURN_ON_FAIL(m_device->CreateRasterizerState(
            &rsDesc,
            rasterizerState.writeRef()));

    }

    ComPtr<ID3D11BlendState> blendState;
    {
        auto& srcDesc = desc.blend;
        D3D11_BLEND_DESC dstDesc = {};

        TargetBlendDesc defaultTargetBlendDesc;

        static const UInt kMaxTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        if(srcDesc.targetCount > kMaxTargets) return SLANG_FAIL;

        for(UInt ii = 0; ii < kMaxTargets; ++ii)
        {
            TargetBlendDesc const* srcTargetBlendDescPtr = nullptr;
            if(ii < srcDesc.targetCount)
            {
                srcTargetBlendDescPtr = &srcDesc.targets[ii];
            }
            else if(srcDesc.targetCount == 0)
            {
                srcTargetBlendDescPtr = &defaultTargetBlendDesc;
            }
            else
            {
                srcTargetBlendDescPtr = &srcDesc.targets[srcDesc.targetCount-1];
            }

            auto& srcTargetBlendDesc = *srcTargetBlendDescPtr;
            auto& dstTargetBlendDesc = dstDesc.RenderTarget[ii];

            if(isBlendDisabled(srcTargetBlendDesc))
            {
                dstTargetBlendDesc.BlendEnable      = false;
                dstTargetBlendDesc.BlendOp          = D3D11_BLEND_OP_ADD;
                dstTargetBlendDesc.BlendOpAlpha     = D3D11_BLEND_OP_ADD;
                dstTargetBlendDesc.SrcBlend         = D3D11_BLEND_ONE;
                dstTargetBlendDesc.SrcBlendAlpha    = D3D11_BLEND_ONE;
                dstTargetBlendDesc.DestBlend        = D3D11_BLEND_ZERO;
                dstTargetBlendDesc.DestBlendAlpha   = D3D11_BLEND_ZERO;
            }
            else
            {
                dstTargetBlendDesc.BlendEnable = true;
                dstTargetBlendDesc.BlendOp          = translateBlendOp(srcTargetBlendDesc.color.op);
                dstTargetBlendDesc.BlendOpAlpha     = translateBlendOp(srcTargetBlendDesc.alpha.op);
                dstTargetBlendDesc.SrcBlend         = translateBlendFactor(srcTargetBlendDesc.color.srcFactor);
                dstTargetBlendDesc.SrcBlendAlpha    = translateBlendFactor(srcTargetBlendDesc.alpha.srcFactor);
                dstTargetBlendDesc.DestBlend        = translateBlendFactor(srcTargetBlendDesc.color.dstFactor);
                dstTargetBlendDesc.DestBlendAlpha   = translateBlendFactor(srcTargetBlendDesc.alpha.dstFactor);
            }

            dstTargetBlendDesc.RenderTargetWriteMask = translateRenderTargetWriteMask(srcTargetBlendDesc.writeMask);
        }

        dstDesc.IndependentBlendEnable = srcDesc.targetCount > 1;
        dstDesc.AlphaToCoverageEnable = srcDesc.alphaToCoverateEnable;

        SLANG_RETURN_ON_FAIL(m_device->CreateBlendState(
            &dstDesc,
            blendState.writeRef()));
    }

    RefPtr<GraphicsPipelineStateImpl> state = new GraphicsPipelineStateImpl();
    state->m_program = programImpl;
    state->m_stencilRef = desc.depthStencil.stencilRef;
    state->m_depthStencilState = depthStencilState;
    state->m_rasterizerState = rasterizerState;
    state->m_blendState = blendState;
    state->m_pipelineLayout = (PipelineLayoutImpl*) desc.pipelineLayout;
    state->m_inputLayout = (InputLayoutImpl*) desc.inputLayout;
    state->m_rtvCount = UINT(desc.renderTargetCount);
    state->m_blendColor[0] = 0;
    state->m_blendColor[1] = 0;
    state->m_blendColor[2] = 0;
    state->m_blendColor[3] = 0;
    state->m_sampleMask = 0xFFFFFFFF;

    *outState = state.detach();
    return SLANG_OK;
}

Result D3D11Renderer::createComputePipelineState(const ComputePipelineStateDesc& desc, PipelineState** outState)
{
    auto programImpl = (ShaderProgramImpl*) desc.program;
    auto pipelineLayoutImpl = (PipelineLayoutImpl*) desc.pipelineLayout;

    RefPtr<ComputePipelineStateImpl> state = new ComputePipelineStateImpl();
    state->m_program = programImpl;
    state->m_pipelineLayout = pipelineLayoutImpl;

    *outState = state.detach();
    return SLANG_OK;
}

void D3D11Renderer::dispatchCompute(int x, int y, int z)
{
    _flushComputeState();
    m_immediateContext->Dispatch(x, y, z);
}

Result D3D11Renderer::createDescriptorSetLayout(const DescriptorSetLayout::Desc& desc, DescriptorSetLayout** outLayout)
{
    RefPtr<DescriptorSetLayoutImpl> descriptorSetLayoutImpl = new DescriptorSetLayoutImpl();

    UInt counts[int(D3D11DescriptorSlotType::CountOf)] = { 0, };

    UInt rangeCount = desc.slotRangeCount;
    for(UInt rr = 0; rr < rangeCount; ++rr)
    {
        auto rangeDesc = desc.slotRanges[rr];

        DescriptorSetLayoutImpl::RangeInfo rangeInfo;

        switch(rangeDesc.type)
        {
        default:
            assert(!"invalid slot type");
            return SLANG_FAIL;

        case DescriptorSlotType::Sampler:
            rangeInfo.type = D3D11DescriptorSlotType::Sampler;
            break;

        case DescriptorSlotType::CombinedImageSampler:
            rangeInfo.type = D3D11DescriptorSlotType::CombinedTextureSampler;
            break;

        case DescriptorSlotType::UniformBuffer:
        case DescriptorSlotType::DynamicUniformBuffer:
            rangeInfo.type = D3D11DescriptorSlotType::ConstantBuffer;
            break;

        case DescriptorSlotType::SampledImage:
        case DescriptorSlotType::UniformTexelBuffer:
        case DescriptorSlotType::InputAttachment:
            rangeInfo.type = D3D11DescriptorSlotType::ShaderResourceView;
            break;

        case DescriptorSlotType::StorageImage:
        case DescriptorSlotType::StorageTexelBuffer:
        case DescriptorSlotType::StorageBuffer:
        case DescriptorSlotType::DynamicStorageBuffer:
            rangeInfo.type = D3D11DescriptorSlotType::UnorderedAccessView;
            break;
        }

        if(rangeInfo.type == D3D11DescriptorSlotType::CombinedTextureSampler)
        {
            auto srvTypeIndex = int(D3D11DescriptorSlotType::ShaderResourceView);
            auto samplerTypeIndex = int(D3D11DescriptorSlotType::Sampler);

            rangeInfo.arrayIndex = counts[srvTypeIndex];
            rangeInfo.pairedSamplerArrayIndex = counts[samplerTypeIndex];

            counts[srvTypeIndex] += rangeDesc.count;
            counts[samplerTypeIndex] += rangeDesc.count;
        }
        else
        {
            auto typeIndex = int(rangeInfo.type);

            rangeInfo.arrayIndex = counts[typeIndex];
            counts[typeIndex] += rangeDesc.count;
        }
        descriptorSetLayoutImpl->m_ranges.Add(rangeInfo);
    }

    for(int ii = 0; ii < int(D3D11DescriptorSlotType::CountOf); ++ii)
    {
        descriptorSetLayoutImpl->m_counts[ii] = counts[ii];
    }

    *outLayout = descriptorSetLayoutImpl.detach();
    return SLANG_OK;
}

Result D3D11Renderer::createPipelineLayout(const PipelineLayout::Desc& desc, PipelineLayout** outLayout)
{
    RefPtr<PipelineLayoutImpl> pipelineLayoutImpl = new PipelineLayoutImpl();

    UInt counts[int(D3D11DescriptorSlotType::CountOf)] = { 0, };

    UInt setCount = desc.descriptorSetCount;
    for(UInt ii = 0; ii < setCount; ++ii)
    {
        auto setDesc = desc.descriptorSets[ii];
        PipelineLayoutImpl::DescriptorSetInfo setInfo;

        setInfo.layout = (DescriptorSetLayoutImpl*) setDesc.layout;

        for(int jj = 0; jj < int(D3D11DescriptorSlotType::CountOf); ++jj)
        {
            setInfo.baseIndices[jj] = counts[jj];
            counts[jj] += setInfo.layout->m_counts[jj];
        }

        pipelineLayoutImpl->m_descriptorSets.Add(setInfo);
    }

    pipelineLayoutImpl->m_uavCount = UINT(counts[int(D3D11DescriptorSlotType::UnorderedAccessView)]);

    *outLayout = pipelineLayoutImpl.detach();
    return SLANG_OK;
}

Result D3D11Renderer::createDescriptorSet(DescriptorSetLayout* layout, DescriptorSet** outDescriptorSet)
{
    auto layoutImpl = (DescriptorSetLayoutImpl*)layout;

    RefPtr<DescriptorSetImpl> descriptorSetImpl = new DescriptorSetImpl();

    descriptorSetImpl->m_layout = layoutImpl;
    descriptorSetImpl->m_cbs     .SetSize(layoutImpl->m_counts[int(D3D11DescriptorSlotType::ConstantBuffer)]);
    descriptorSetImpl->m_srvs    .SetSize(layoutImpl->m_counts[int(D3D11DescriptorSlotType::ShaderResourceView)]);
    descriptorSetImpl->m_uavs    .SetSize(layoutImpl->m_counts[int(D3D11DescriptorSlotType::UnorderedAccessView)]);
    descriptorSetImpl->m_samplers.SetSize(layoutImpl->m_counts[int(D3D11DescriptorSlotType::Sampler)]);

    *outDescriptorSet = descriptorSetImpl.detach();
    return SLANG_OK;
}


#if 0
BindingState* D3D11Renderer::createBindingState(const BindingState::Desc& bindingStateDesc)
{
    RefPtr<BindingStateImpl> bindingState(new BindingStateImpl(bindingStateDesc));

    const auto& srcBindings = bindingStateDesc.m_bindings;
    const int numBindings = int(srcBindings.Count());

    auto& dstDetails = bindingState->m_bindingDetails;
    dstDetails.SetSize(numBindings);

    for (int i = 0; i < numBindings; ++i)
    {
        auto& dstDetail = dstDetails[i];
        const auto& srcBinding = srcBindings[i];

        assert(srcBinding.registerRange.isSingle());

        switch (srcBinding.bindingType)
        {
            case BindingType::Buffer:
            {
                assert(srcBinding.resource && srcBinding.resource->isBuffer());

                BufferResourceImpl* buffer = static_cast<BufferResourceImpl*>(srcBinding.resource.Ptr());
                const BufferResource::Desc& desc = buffer->getDesc();

                const int elemSize = bufferDesc.elementSize <= 0 ? 1 : bufferDesc.elementSize;

                if (bufferDesc.bindFlags & Resource::BindFlag::UnorderedAccess)
                {
                    D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc;
                    memset(&viewDesc, 0, sizeof(viewDesc));
                    viewDesc.Buffer.FirstElement = 0;
                    viewDesc.Buffer.NumElements = (UINT)(bufferDesc.sizeInBytes / elemSize);
                    viewDesc.Buffer.Flags = 0;
                    viewDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
                    viewDesc.Format = D3DUtil::getMapFormat(bufferDesc.format);

                    if (bufferDesc.elementSize == 0 && bufferDesc.format == Format::Unknown)
                    {
                        viewDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
                        viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                    }

                    SLANG_RETURN_NULL_ON_FAIL(m_device->CreateUnorderedAccessView(buffer->m_buffer, &viewDesc, dstDetail.m_uav.writeRef()));
                }
                if (bufferDesc.bindFlags & (Resource::BindFlag::NonPixelShaderResource | Resource::BindFlag::PixelShaderResource))
                {
                    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
                    memset(&viewDesc, 0, sizeof(viewDesc));
                    viewDesc.Buffer.FirstElement = 0;
                    viewDesc.Buffer.ElementWidth = elemSize;
                    viewDesc.Buffer.NumElements = (UINT)(bufferDesc.sizeInBytes / elemSize);
                    viewDesc.Buffer.ElementOffset = 0;
                    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                    viewDesc.Format = DXGI_FORMAT_UNKNOWN;

                    if (bufferDesc.elementSize == 0)
                    {
                        viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
                    }

                    SLANG_RETURN_NULL_ON_FAIL(m_device->CreateShaderResourceView(buffer->m_buffer, &viewDesc, dstDetail.m_srv.writeRef()));
                }
                break;
            }
            case BindingType::Texture:
            case BindingType::CombinedTextureSampler:
            {
                assert(srcBinding.resource && srcBinding.resource->isTexture());

                TextureResourceImpl* texture = static_cast<TextureResourceImpl*>(srcBinding.resource.Ptr());

                const TextureResource::Desc& textureDesc = texture->getDesc();

                D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
                viewDesc.Format = D3DUtil::getMapFormat(textureDesc.format);

                switch (texture->getType())
                {
                    case Resource::Type::Texture1D:
                    {
                        if (textureDesc.arraySize <= 0)
                        {
                            viewDesc.ViewDimension =  D3D11_SRV_DIMENSION_TEXTURE1D;
                            viewDesc.Texture1D.MipLevels = textureDesc.numMipLevels;
                            viewDesc.Texture1D.MostDetailedMip = 0;
                        }
                        else
                        {
                            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
                            viewDesc.Texture1DArray.ArraySize = textureDesc.arraySize;
                            viewDesc.Texture1DArray.FirstArraySlice = 0;
                            viewDesc.Texture1DArray.MipLevels = textureDesc.numMipLevels;
                            viewDesc.Texture1DArray.MostDetailedMip = 0;
                        }
                        break;
                    }
                    case Resource::Type::Texture2D:
                    {
                        if (textureDesc.arraySize <= 0)
                        {
                            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                            viewDesc.Texture2D.MipLevels = textureDesc.numMipLevels;
                            viewDesc.Texture2D.MostDetailedMip = 0;
                        }
                        else
                        {
                            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                            viewDesc.Texture2DArray.ArraySize = textureDesc.arraySize;
                            viewDesc.Texture2DArray.FirstArraySlice = 0;
                            viewDesc.Texture2DArray.MipLevels = textureDesc.numMipLevels;
                            viewDesc.Texture2DArray.MostDetailedMip = 0;
                        }
                        break;
                    }
                    case Resource::Type::TextureCube:
                    {
                        if (textureDesc.arraySize <= 0)
                        {
                            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                            viewDesc.TextureCube.MipLevels = textureDesc.numMipLevels;
                            viewDesc.TextureCube.MostDetailedMip = 0;
                        }
                        else
                        {
                            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                            viewDesc.TextureCubeArray.MipLevels = textureDesc.numMipLevels;
                            viewDesc.TextureCubeArray.MostDetailedMip = 0;
                            viewDesc.TextureCubeArray.First2DArrayFace = 0;
                            viewDesc.TextureCubeArray.NumCubes = textureDesc.arraySize;
                        }
                        break;
                    }
                    case Resource::Type::Texture3D:
                    {
                        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                        viewDesc.Texture3D.MipLevels = textureDesc.numMipLevels;            // Old code fixed as one
                        viewDesc.Texture3D.MostDetailedMip = 0;
                        break;
                    }
                    default:
                    {
                        assert(!"Unhandled type");
                        return nullptr;
                    }
                }

                SLANG_RETURN_NULL_ON_FAIL(m_device->CreateShaderResourceView(texture->m_resource, &viewDesc, dstDetail.m_srv.writeRef()));
                break;
            }
            case BindingType::Sampler:
            {
                const BindingState::SamplerDesc& samplerDesc = bindingStateDesc.m_samplerDescs[srcBinding.descIndex];

                D3D11_SAMPLER_DESC desc = {};
                desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

                if (samplerDesc.isCompareSampler)
                {
                    desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
                    desc.Filter = D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
                    desc.MinLOD = desc.MaxLOD = 0.0f;
                }
                else
                {
                    desc.Filter = D3D11_FILTER_ANISOTROPIC;
                    desc.MaxAnisotropy = 8;
                    desc.MinLOD = 0.0f;
                    desc.MaxLOD = 100.0f;
                }
                SLANG_RETURN_NULL_ON_FAIL(m_device->CreateSamplerState(&desc, dstDetail.m_samplerState.writeRef()));
                break;
            }
            default:
            {
                assert(!"Unhandled type");
                return nullptr;
            }
        }
    }

    // Done
    return bindingState.detach();
}

void D3D11Renderer::_applyBindingState(bool isCompute)
{
    auto context = m_immediateContext.get();

    const auto& details = m_currentBindings->m_bindingDetails;
    const auto& bindings = m_currentBindings->getDesc().m_bindings;

    const int numBindings = int(bindings.Count());

    for (int i = 0; i < numBindings; ++i)
    {
        const auto& binding = bindings[i];
        const auto& detail = details[i];

        const int bindingIndex = binding.registerRange.getSingleIndex();

        switch (binding.bindingType)
        {
            case BindingType::Buffer:
            {
                assert(binding.resource && binding.resource->isBuffer());
                if (binding.resource->canBind(Resource::BindFlag::ConstantBuffer))
                {
                    ID3D11Buffer* buffer = static_cast<BufferResourceImpl*>(binding.resource.Ptr())->m_buffer;
                    if (isCompute)
                        context->CSSetConstantBuffers(bindingIndex, 1, &buffer);
                    else
                    {
                        context->VSSetConstantBuffers(bindingIndex, 1, &buffer);
                        context->PSSetConstantBuffers(bindingIndex, 1, &buffer);
                    }
                }
                else if (detail.m_uav)
                {
                    if (isCompute)
                        context->CSSetUnorderedAccessViews(bindingIndex, 1, detail.m_uav.readRef(), nullptr);
                    else
                        context->OMSetRenderTargetsAndUnorderedAccessViews(
                            m_currentBindings->getDesc().m_numRenderTargets,
                            m_renderTargetViews.Buffer()->readRef(),
                            m_depthStencilView,
                            bindingIndex,
                            1,
                            detail.m_uav.readRef(),
                            nullptr);
                }
                else
                {
                    if (isCompute)
                        context->CSSetShaderResources(bindingIndex, 1, detail.m_srv.readRef());
                    else
                    {
                        context->PSSetShaderResources(bindingIndex, 1, detail.m_srv.readRef());
                        context->VSSetShaderResources(bindingIndex, 1, detail.m_srv.readRef());
                    }
                }
                break;
            }
            case BindingType::Texture:
            {
                if (detail.m_uav)
                {
                    if (isCompute)
                        context->CSSetUnorderedAccessViews(bindingIndex, 1, detail.m_uav.readRef(), nullptr);
                    else
                        context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
                            nullptr, nullptr, bindingIndex, 1, detail.m_uav.readRef(), nullptr);
                }
                else
                {
                    if (isCompute)
                        context->CSSetShaderResources(bindingIndex, 1, detail.m_srv.readRef());
                    else
                    {
                        context->PSSetShaderResources(bindingIndex, 1, detail.m_srv.readRef());
                        context->VSSetShaderResources(bindingIndex, 1, detail.m_srv.readRef());
                    }
                }
                break;
            }
            case BindingType::Sampler:
            {
                if (isCompute)
                    context->CSSetSamplers(bindingIndex, 1, detail.m_samplerState.readRef());
                else
                {
                    context->PSSetSamplers(bindingIndex, 1, detail.m_samplerState.readRef());
                    context->VSSetSamplers(bindingIndex, 1, detail.m_samplerState.readRef());
                }
                break;
            }
            default:
            {
                assert(!"Not implemented");
                return;
            }
        }
    }
}

void D3D11Renderer::setBindingState(BindingState* state)
{
    m_currentBindings = static_cast<BindingStateImpl*>(state);
}
#endif

void D3D11Renderer::_flushGraphicsState()
{
    auto pipelineType = int(PipelineType::Graphics);
    if(m_targetBindingsDirty[pipelineType])
    {
        m_targetBindingsDirty[pipelineType] = false;

        auto pipelineState = m_currentGraphicsState.Ptr();

        auto rtvCount = pipelineState->m_rtvCount;
        auto uavCount = pipelineState->m_pipelineLayout->m_uavCount;

        m_immediateContext->OMSetRenderTargetsAndUnorderedAccessViews(
            rtvCount,
            m_rtvBindings[0].readRef(),
            m_dsvBinding,
            rtvCount,
            uavCount,
            m_uavBindings[pipelineType][0].readRef(),
            nullptr);
    }
}

void D3D11Renderer::_flushComputeState()
{
    auto pipelineType = int(PipelineType::Compute);
    if(m_targetBindingsDirty[pipelineType])
    {
        m_targetBindingsDirty[pipelineType] = false;

        auto pipelineState = m_currentComputeState.Ptr();

        auto uavCount = pipelineState->m_pipelineLayout->m_uavCount;

        m_immediateContext->CSSetUnorderedAccessViews(
            0,
            uavCount,
            m_uavBindings[pipelineType][0].readRef(),
            nullptr);
    }
}

void D3D11Renderer::DescriptorSetImpl::setConstantBuffer(UInt range, UInt index, BufferResource* buffer)
{
    auto bufferImpl = (BufferResourceImpl*) buffer;
    auto& rangeInfo = m_layout->m_ranges[range];

    assert(rangeInfo.type == D3D11DescriptorSlotType::ConstantBuffer);

    m_cbs[rangeInfo.arrayIndex + index] = bufferImpl->m_buffer;
}

void D3D11Renderer::DescriptorSetImpl::setResource(UInt range, UInt index, ResourceView* view)
{
    auto viewImpl = (ResourceViewImpl*)view;
    auto& rangeInfo = m_layout->m_ranges[range];

    switch (rangeInfo.type)
    {
    case D3D11DescriptorSlotType::ShaderResourceView:
        {
            assert(viewImpl->m_type == ResourceViewImpl::Type::SRV);
            auto srvImpl = (ShaderResourceViewImpl*)viewImpl;
            m_srvs[rangeInfo.arrayIndex + index] = srvImpl->m_srv;
        }
        break;

    case D3D11DescriptorSlotType::UnorderedAccessView:
        {
            assert(viewImpl->m_type == ResourceViewImpl::Type::UAV);
            auto uavImpl = (UnorderedAccessViewImpl*)viewImpl;
            m_uavs[rangeInfo.arrayIndex + index] = uavImpl->m_uav;
        }
        break;

    default:
        assert(!"invalid to bind a resource view to this descriptor range");
        break;
    }
}

void D3D11Renderer::DescriptorSetImpl::setSampler(UInt range, UInt index, SamplerState* sampler)
{
    auto samplerImpl = (SamplerStateImpl*) sampler;
    auto& rangeInfo = m_layout->m_ranges[range];

    assert(rangeInfo.type == D3D11DescriptorSlotType::Sampler);

    m_samplers[rangeInfo.arrayIndex + index] = samplerImpl->m_sampler;
}

void D3D11Renderer::DescriptorSetImpl::setCombinedTextureSampler(
    UInt            range,
    UInt            index,
    ResourceView*   textureView,
    SamplerState*   sampler)
{
    auto viewImpl = (ResourceViewImpl*) textureView;
    auto samplerImpl = (SamplerStateImpl*)sampler;

    auto& rangeInfo = m_layout->m_ranges[range];
    assert(rangeInfo.type == D3D11DescriptorSlotType::CombinedTextureSampler);

    assert(viewImpl->m_type == ResourceViewImpl::Type::SRV);
    auto srvImpl = (ShaderResourceViewImpl*)viewImpl;
    m_srvs[rangeInfo.arrayIndex + index] = srvImpl->m_srv;

    m_samplers[rangeInfo.arrayIndex + index] = samplerImpl->m_sampler;

    // TODO: need a place to bind the matching sampler...
    m_srvs[rangeInfo.pairedSamplerArrayIndex + index] = srvImpl->m_srv;
}

void D3D11Renderer::setDescriptorSet(PipelineType pipelineType, PipelineLayout* layout, UInt index, DescriptorSet* descriptorSet)
{
    auto pipelineLayoutImpl = (PipelineLayoutImpl*)layout;
    auto descriptorSetImpl = (DescriptorSetImpl*) descriptorSet;

    auto descriptorSetLayoutImpl = descriptorSetImpl->m_layout;
    auto& setInfo = pipelineLayoutImpl->m_descriptorSets[index];

    // Note: `setInfo->layout` and `descriptorSetLayoutImpl` need to be compatible

    // TODO: If/when we add per-stage visibility masks, it would be best to organize
    // this as a loop over stages, so that we only do the binding that is required
    // for each stage.

    {
        const int slotType = int(D3D11DescriptorSlotType::ConstantBuffer);
        const UINT slotCount = UINT(setInfo.layout->m_counts[slotType]);
        if(slotCount)
        {
            const UINT startSlot = UINT(setInfo.baseIndices[slotType]);

            auto cbs = descriptorSetImpl->m_cbs[0].readRef();

            m_immediateContext->VSSetConstantBuffers(startSlot, slotCount, cbs);
            // ...
            m_immediateContext->PSSetConstantBuffers(startSlot, slotCount, cbs);

            m_immediateContext->CSSetConstantBuffers(startSlot, slotCount, cbs);
        }
    }

    {
        const int slotType = int(D3D11DescriptorSlotType::ShaderResourceView);
        const UINT slotCount = UINT(setInfo.layout->m_counts[slotType]);
        if(slotCount)
        {
            const UINT startSlot = UINT(setInfo.baseIndices[slotType]);

            auto srvs = descriptorSetImpl->m_srvs[0].readRef();

            m_immediateContext->VSSetShaderResources(startSlot, slotCount, srvs);
            // ...
            m_immediateContext->PSSetShaderResources(startSlot, slotCount, srvs);

            m_immediateContext->CSSetShaderResources(startSlot, slotCount, srvs);
        }
    }

    {
        const int slotType = int(D3D11DescriptorSlotType::Sampler);
        const UINT slotCount = UINT(setInfo.layout->m_counts[slotType]);
        if(slotCount)
        {
            const UINT startSlot = UINT(setInfo.baseIndices[slotType]);

            auto samplers = descriptorSetImpl->m_samplers[0].readRef();

            m_immediateContext->VSSetSamplers(startSlot, slotCount, samplers);
            // ...
            m_immediateContext->PSSetSamplers(startSlot, slotCount, samplers);

            m_immediateContext->CSSetSamplers(startSlot, slotCount, samplers);
        }
    }

    {
        // Note: UAVs are handled differently from other bindings, because
        // D3D11 requires all UAVs to be set with a single call, rather
        // than allowing incremental updates. We will therefore shadow
        // the UAV bindings with `m_uavBindings` and then flush them
        // as needed right before a draw/dispatch.
        //
        const int slotType = int(D3D11DescriptorSlotType::UnorderedAccessView);
        const UInt slotCount = setInfo.layout->m_counts[slotType];
        if(slotCount)
        {
            UInt startSlot = setInfo.baseIndices[slotType];

            auto uavs = descriptorSetImpl->m_uavs[0].readRef();

            for(UINT ii = 0; ii < slotCount; ++ii)
            {
                m_uavBindings[int(pipelineType)][startSlot + ii] = uavs[ii];
            }
            m_targetBindingsDirty[int(pipelineType)] = true;
        }
    }


}

} // renderer_test
