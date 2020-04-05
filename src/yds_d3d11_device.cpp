#include "../include/yds_d3d11_device.h"

#include "../include/yds_d3d11_context.h"
#include "../include/yds_d3d11_render_target.h"
#include "../include/yds_d3d11_gpu_buffer.h"
#include "../include/yds_d3d11_shader.h"
#include "../include/yds_d3d11_shader_program.h"
#include "../include/yds_d3d11_input_layout.h"
#include "../include/yds_d3d11_texture.h"

#include "../include/yds_windows_window.h"

#include <d3d11.h>

#pragma warning(push, 0)
#include <d3dx11async.h>
#include <d3dx11tex.h>
#include <dxgi1_3.h>
#include <initguid.h>
#include <dxgidebug.h>
#pragma warning(pop)

ysD3D11Device::ysD3D11Device() : ysDevice(DIRECTX11) {
    m_device = nullptr;
    m_deviceContext = nullptr;
    m_DXGIFactory = nullptr;

    m_multisampleCount = 0;
    m_multisampleQuality = 0;

    // TEMP
    m_rasterizerState = nullptr;

    m_depthStencilDisabledState = nullptr;
    m_depthStencilEnabledState = nullptr;

    m_blendState = nullptr;
}

ysD3D11Device::~ysD3D11Device() {
    /* void */
}

ysError ysD3D11Device::InitializeDevice() {
    YDS_ERROR_DECLARE("InitializeDevice");

    HRESULT result;
    D3D_FEATURE_LEVEL highestFeatureLevel;

    result = D3D11CreateDevice(nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_DEBUG,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &m_device,
        &highestFeatureLevel,
        &m_deviceContext);

    if (FAILED(result)) {
        m_device = nullptr;
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_GRAPHICS_DEVICE);
    }

    result = CreateDXGIFactory(IID_IDXGIFactory, (void **)(&m_DXGIFactory));
    if (FAILED(result)) {
        m_device->Release();
        m_device = nullptr;

        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_GRAPHICS_DEVICE);
    }

    // TEMP
    // Temporary location for this initialization
    GetImmediateContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    InitializeTextureSlots(D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

#include <wrl.h>

typedef HRESULT(WINAPI *DXGIGetDebugInterface_proc) (const IID &riid, void **ppDebug);

ysError ysD3D11Device::DestroyDevice() {
    YDS_ERROR_DECLARE("DestroyDevice");

    if (m_deviceContext != nullptr)	m_deviceContext->Release();
    if (m_device != nullptr) m_device->Release();
    if (m_DXGIFactory != nullptr) m_DXGIFactory->Release();

    if (m_depthStencilDisabledState != nullptr) m_depthStencilDisabledState->Release();
    if (m_depthStencilEnabledState != nullptr) m_depthStencilEnabledState->Release();

    if (m_blendState != nullptr) m_blendState->Release();
    if (m_samplerState != nullptr) m_samplerState->Release();

    if (m_rasterizerState != nullptr) m_rasterizerState->Release();

    Microsoft::WRL::ComPtr<IDXGIDebug> dxgiDebug;

    DXGIGetDebugInterface_proc proc = (DXGIGetDebugInterface_proc)GetProcAddress(GetModuleHandle(TEXT("Dxgidebug.dll")), "DXGIGetDebugInterface");
    const IID &pD = DXGI_DEBUG_ALL;
    HRESULT r = proc(IID_PPV_ARGS(dxgiDebug.GetAddressOf()));
    dxgiDebug.Get()->ReportLiveObjects(pD, DXGI_DEBUG_RLO_ALL);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

bool ysD3D11Device::CheckSupport() {
    // TEMP
    return true;
}

ysError ysD3D11Device::CreateRenderingContext(ysRenderingContext **context, ysWindow *window) {
    YDS_ERROR_DECLARE("CreateRenderingContext");

    if (window->GetPlatform() != ysWindowSystem::Platform::WINDOWS) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);
    if (context == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (m_device == nullptr) return YDS_ERROR_RETURN(ysError::YDS_NO_DEVICE);
    *context = nullptr;

    ysWindowsWindow *windowsWindow = static_cast<ysWindowsWindow *>(window);

    IDXGIDevice *pDXGIDevice = nullptr;
    GetDXGIDevice(&pDXGIDevice);
    if (pDXGIDevice == nullptr) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_OBTAIN_DEVICE);
    }

    ysD3D11Context *newContext = m_renderingContexts.NewGeneric<ysD3D11Context>();
    newContext->m_targetWindow = window;

    // Get max quality
    UINT multisamplesPerPixel = 8;
    UINT maxQuality;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    HRESULT result = m_device->CheckMultisampleQualityLevels(format, multisamplesPerPixel, &maxQuality);

    m_multisampleCount = (int)multisamplesPerPixel;
    m_multisampleQuality = (int)maxQuality - 1;

    // Create the swap chain
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    ZeroMemory(&swapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));
    swapChainDesc.BufferDesc.Width = window->GetScreenWidth();
    swapChainDesc.BufferDesc.Height = window->GetScreenHeight();
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferDesc.Format = format;
    swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    swapChainDesc.SampleDesc.Count = m_multisampleCount;
    swapChainDesc.SampleDesc.Quality = m_multisampleQuality;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.OutputWindow = windowsWindow->GetWindowHandle();
    swapChainDesc.Windowed = window->GetStyle() != ysWindow::WindowStyle::FULLSCREEN;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    IDXGIFactory *factory = GetDXGIFactory();

    result = factory->CreateSwapChain(pDXGIDevice, &swapChainDesc, &newContext->m_swapChain);
    pDXGIDevice->Release();

    if (FAILED(result)) {
        m_renderingContexts.Delete(newContext->GetIndex());
        *context = nullptr;

        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_SWAP_CHAIN);
    }

    *context = static_cast<ysRenderingContext *>(newContext);

    // TEMP

    if (m_rasterizerState == nullptr) {
        D3D11_RASTERIZER_DESC rasterizerDescription;
        ZeroMemory(&rasterizerDescription, sizeof(D3D11_RASTERIZER_DESC));
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_FRONT;
        rasterizerDescription.FrontCounterClockwise = FALSE;
        rasterizerDescription.DepthBias = FALSE;
        rasterizerDescription.DepthBiasClamp = 0;
        rasterizerDescription.SlopeScaledDepthBias = 0;
        rasterizerDescription.DepthClipEnable = FALSE;
        rasterizerDescription.ScissorEnable = FALSE;
        rasterizerDescription.MultisampleEnable = TRUE;
        rasterizerDescription.AntialiasedLineEnable = TRUE;

        m_device->CreateRasterizerState(&rasterizerDescription, &m_rasterizerState);
        GetImmediateContext()->RSSetState(m_rasterizerState);

        // TEMPORARY ALPHA ENABLING
        D3D11_BLEND_DESC BlendState;
        ZeroMemory(&BlendState, sizeof(D3D11_BLEND_DESC));
        BlendState.RenderTarget[0].BlendEnable = TRUE;
        BlendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        BlendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        BlendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        BlendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        BlendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_DEST_ALPHA;
        BlendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        BlendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        m_device->CreateBlendState(&BlendState, &m_blendState);

        float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UINT sampleMask = 0xffffffff;

        GetImmediateContext()->OMSetBlendState(m_blendState, blendFactor, sampleMask);

        // END TEMPORARY ALPHA BLENDING
    }

    // END TEMP

    // Create standard depth stencil states
    D3D11_DEPTH_STENCIL_DESC dsDesc;

    // Depth test parameters
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;

    // Stencil test parameters
    dsDesc.StencilEnable = true;
    dsDesc.StencilReadMask = 0xFF;
    dsDesc.StencilWriteMask = 0xFF;

    // Stencil operations if pixel is front-facing
    dsDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
    dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    // Stencil operations if pixel is back-facing
    dsDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    dsDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    // Create depth stencil state
    m_device->CreateDepthStencilState(&dsDesc, &m_depthStencilEnabledState);

    dsDesc.DepthEnable = false;
    m_device->CreateDepthStencilState(&dsDesc, &m_depthStencilDisabledState);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::UpdateRenderingContext(ysRenderingContext *context) {
    YDS_ERROR_DECLARE("UpdateRenderingContext");

    if (context == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (m_device == nullptr) return YDS_ERROR_RETURN(ysError::YDS_NO_DEVICE);

    // Check the window
    if (!context->GetWindow()->IsVisible()) {
        return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
    }

    ysD3D11Context *d3d11Context = static_cast<ysD3D11Context *>(context);

    int width = context->GetWindow()->GetScreenWidth();
    int height = context->GetWindow()->GetScreenHeight();

    ysD3D11RenderTarget *attachedTarget = static_cast<ysD3D11RenderTarget *>(context->GetAttachedRenderTarget());

    if (width == 0 || height == 0) {
        // Don't do anything in this case
        return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
    }

    // Destroy render target first

    if (attachedTarget != nullptr) {
        YDS_NESTED_ERROR_CALL(DestroyD3D11RenderTarget(attachedTarget));
    }

    HRESULT result = d3d11Context->m_swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_API_ERROR);
    }

    if (context->GetAttachedRenderTarget() != nullptr) {
        YDS_NESTED_ERROR_CALL(ResizeRenderTarget(context->GetAttachedRenderTarget(), width, height));
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyRenderingContext(ysRenderingContext *&context) {
    YDS_ERROR_DECLARE("DestroyRenderingContext");

    if (context != nullptr) {
        YDS_NESTED_ERROR_CALL(SetContextMode(context, ysRenderingContext::ContextMode::WINDOWED));

        ysD3D11Context *d3d11Context = static_cast<ysD3D11Context *>(context);
        if (d3d11Context->m_swapChain) d3d11Context->m_swapChain->Release();
    }

    YDS_NESTED_ERROR_CALL(ysDevice::DestroyRenderingContext(context));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::SetContextMode(ysRenderingContext *context, ysRenderingContext::ContextMode mode) {
    YDS_ERROR_DECLARE("SetContextMode");

    if (context == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (!CheckCompatibility(context)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    ysD3D11Context *d3d11Context = static_cast<ysD3D11Context *>(context);
    ysWindow *window = context->GetWindow();
    ysMonitor *monitor = window->GetMonitor();

    HRESULT result;

    if (mode == ysRenderingContext::ContextMode::FULLSCREEN) {
        window->SetWindowStyle(ysWindow::WindowStyle::FULLSCREEN);
        result = d3d11Context->m_swapChain->SetFullscreenState(TRUE, nullptr);

        if (FAILED(result)) {
            return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_ENTER_FULLSCREEN);
        }
    }

    else if (mode == ysRenderingContext::ContextMode::WINDOWED) {
        window->SetWindowStyle(ysWindow::WindowStyle::WINDOWED);
        result = d3d11Context->m_swapChain->SetFullscreenState(FALSE, nullptr);

        if (FAILED(result)) {
            return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_EXIT_FULLSCREEN);
        }
    }

    YDS_NESTED_ERROR_CALL(ysDevice::SetContextMode(context, mode));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateOnScreenRenderTarget(ysRenderTarget **newTarget, ysRenderingContext *context, bool depthBuffer) {
    YDS_ERROR_DECLARE("CreateOnScreenRenderTarget");

    if (newTarget == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newTarget = nullptr;

    if (context == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (context->GetAttachedRenderTarget() != nullptr) return YDS_ERROR_RETURN(ysError::YDS_CONTEXT_ALREADY_HAS_RENDER_TARGET);

    ysD3D11RenderTarget *newRenderTarget = m_renderTargets.NewGeneric<ysD3D11RenderTarget>();

    ysError result = CreateD3D11OnScreenRenderTarget(newRenderTarget, context, depthBuffer);

    if (result != ysError::YDS_NO_ERROR) {
        m_renderTargets.Delete(newRenderTarget->GetIndex());
        return YDS_ERROR_RETURN(result);
    }

    *newTarget = static_cast<ysRenderTarget *>(newRenderTarget);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateOffScreenRenderTarget(ysRenderTarget **newTarget, int width, int height, ysRenderTarget::Format format, int sampleCount, bool depthBuffer) {
    YDS_ERROR_DECLARE("CreateOffScreenRenderTarget");

    if (newTarget == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newTarget = nullptr;

    ysD3D11RenderTarget *d3d11Target = m_renderTargets.NewGeneric<ysD3D11RenderTarget>();

    ysError result = CreateD3D11OffScreenRenderTarget(d3d11Target, width, height, format, sampleCount, depthBuffer);

    if (result != ysError::YDS_NO_ERROR) {
        m_renderTargets.Delete(d3d11Target->GetIndex());
        return YDS_ERROR_RETURN(result);
    }

    *newTarget = static_cast<ysRenderTarget *>(d3d11Target);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateSubRenderTarget(ysRenderTarget **newTarget, ysRenderTarget *parent, int x, int y, int width, int height) {
    YDS_ERROR_DECLARE("CreateSubRenderTarget");

    if (newTarget == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (parent->GetType() == ysRenderTarget::Type::Subdivision) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    ysD3D11RenderTarget *newRenderTarget = m_renderTargets.NewGeneric<ysD3D11RenderTarget>();

    newRenderTarget->m_type = ysRenderTarget::Type::Subdivision;
    newRenderTarget->m_posX = x;
    newRenderTarget->m_posY = y;
    newRenderTarget->m_width = width;
    newRenderTarget->m_height = height;
    newRenderTarget->m_format = ysRenderTarget::Format::RTF_R8G8B8A8_UNORM;
    newRenderTarget->m_hasDepthBuffer = parent->HasDepthBuffer();
    newRenderTarget->m_associatedContext = parent->GetAssociatedContext();
    newRenderTarget->m_parent = parent;

    newRenderTarget->m_renderTargetView = nullptr;
    newRenderTarget->m_depthStencilView = nullptr;

    *newTarget = static_cast<ysRenderTarget *>(newRenderTarget);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::SetRenderTarget(ysRenderTarget *target) {
    YDS_ERROR_DECLARE("SetRenderTarget");

    if (target != nullptr) {
        ysD3D11RenderTarget *d3d11Target = static_cast<ysD3D11RenderTarget *>(target);
        ysD3D11RenderTarget *realTarget = (target->GetType() == ysRenderTarget::Type::Subdivision) ?
            static_cast<ysD3D11RenderTarget *>(target->GetParent()) : d3d11Target;

        if (target->IsDepthTestEnabled()) {
            m_deviceContext->OMSetDepthStencilState(m_depthStencilEnabledState, 1);
        }
        else {
            m_deviceContext->OMSetDepthStencilState(m_depthStencilDisabledState, 1);
        }

        if (realTarget != m_activeRenderTarget) {
            GetImmediateContext()->OMSetRenderTargets(1, &d3d11Target->m_renderTargetView, d3d11Target->m_depthStencilView);
        }

        if (target->GetAssociatedContext()) {
            m_activeContext = target->GetAssociatedContext();
        }

        D3D11_VIEWPORT vp;
        vp.Width = (FLOAT)target->GetWidth();
        vp.Height = (FLOAT)target->GetHeight();
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = (FLOAT)target->GetPosX();
        vp.TopLeftY = (FLOAT)target->GetPosY();
        GetImmediateContext()->RSSetViewports(1, &vp);
    }
    else {
        GetImmediateContext()->OMSetRenderTargets(0, nullptr, nullptr);
        m_activeContext = nullptr;
    }

    YDS_NESTED_ERROR_CALL(ysDevice::SetRenderTarget(target));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::SetDepthTestEnabled(ysRenderTarget *target, bool enable) {
    YDS_ERROR_DECLARE("SetDepthTestEnabled");

    bool previousState = target->IsDepthTestEnabled();
    YDS_NESTED_ERROR_CALL(ysDevice::SetDepthTestEnabled(target, enable));

    if (target == GetActiveRenderTarget() && previousState != enable) {
        SetRenderTarget(target);
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::ResizeRenderTarget(ysRenderTarget *target, int width, int height) {
    YDS_ERROR_DECLARE("ResizeRenderTarget");

    YDS_NESTED_ERROR_CALL(ysDevice::ResizeRenderTarget(target, width, height));

    ysD3D11RenderTarget *d3d11Target = static_cast<ysD3D11RenderTarget *>(target);

    ysRenderTarget *prevTarget = m_activeRenderTarget;

    // Disable the target if it is active
    if (target == m_activeRenderTarget) {
        SetRenderTarget(nullptr);
    }

    if (target->GetType() == ysRenderTarget::Type::OnScreen) {
        YDS_NESTED_ERROR_CALL(CreateD3D11OnScreenRenderTarget(target, target->GetAssociatedContext(), target->HasDepthBuffer()));
    }
    else if (target->GetType() == ysRenderTarget::Type::OffScreen) {
        YDS_NESTED_ERROR_CALL(CreateD3D11OffScreenRenderTarget(target, width, height, target->GetFormat(), target->GetSampleCount(), target->HasDepthBuffer()));
    }
    else if (target->GetType() == ysRenderTarget::Type::Subdivision) {
        // Nothing needs to be done
    }

    // Re-enable the target if it was active
    if (target == prevTarget) {
        SetRenderTarget(target);
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyRenderTarget(ysRenderTarget *&target) {
    YDS_ERROR_DECLARE("DestroyRenderTarget");

    if (target == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    if (target == m_activeRenderTarget) {
        YDS_NESTED_ERROR_CALL(SetRenderTarget(nullptr));
    }

    YDS_NESTED_ERROR_CALL(DestroyD3D11RenderTarget(target));
    YDS_NESTED_ERROR_CALL(ysDevice::DestroyRenderTarget(target));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::ClearBuffers(const float *clearColor) {
    YDS_ERROR_DECLARE("ClearBuffers");

    if (GetDevice() == nullptr) return YDS_ERROR_RETURN(ysError::YDS_NO_DEVICE);

    if (m_activeRenderTarget != nullptr) {
        ysD3D11RenderTarget *renderTarget = static_cast<ysD3D11RenderTarget *>(m_activeRenderTarget);
        GetImmediateContext()->ClearRenderTargetView(renderTarget->m_renderTargetView, clearColor);
        if (renderTarget->m_hasDepthBuffer) GetImmediateContext()->ClearDepthStencilView(renderTarget->m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

        return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_RENDER_TARGET);
}

ysError ysD3D11Device::Present() {
    YDS_ERROR_DECLARE("Present");

    if (m_activeContext == nullptr) return YDS_ERROR_RETURN(ysError::YDS_NO_CONTEXT);
    if (m_activeRenderTarget->GetType() == ysRenderTarget::Type::Subdivision) return YDS_ERROR_RETURN(ysError::YDS_INVALID_OPERATION);

    ysD3D11Context *context = static_cast<ysD3D11Context *>(m_activeContext);
    if (context->m_swapChain == nullptr) return YDS_ERROR_RETURN(ysError::YDS_NO_CONTEXT);

    context->m_swapChain->Present(1, 0);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

// Vertex Buffers

ysError ysD3D11Device::CreateVertexBuffer(ysGPUBuffer **newBuffer, int size, char *data, bool mirrorToRam) {
    YDS_ERROR_DECLARE("CreateVertexBuffer");

    if (newBuffer == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newBuffer = nullptr;

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = size;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData, *pInitData;
    ZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = data;

    if (data) pInitData = &InitData;
    else pInitData = nullptr;

    ID3D11Buffer *buffer;
    HRESULT result;
    if (data == nullptr) result = m_device->CreateBuffer(&bd, nullptr, &buffer);
    else result = result = m_device->CreateBuffer(&bd, pInitData, &buffer);
    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_GPU_BUFFER);
    }

    ysD3D11GPUBuffer *newD3D11Buffer = m_gpuBuffers.NewGeneric<ysD3D11GPUBuffer>();

    newD3D11Buffer->m_size = size;
    newD3D11Buffer->m_mirrorToRAM = mirrorToRam;
    newD3D11Buffer->m_bufferType = ysGPUBuffer::GPU_DATA_BUFFER;
    newD3D11Buffer->m_buffer = buffer;

    if (mirrorToRam) {
        newD3D11Buffer->m_RAMMirror = new char[size];

        if (data != nullptr) {
            memcpy(newD3D11Buffer->m_RAMMirror, data, size);
        }
    }

    *newBuffer = static_cast<ysGPUBuffer *>(newD3D11Buffer);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateIndexBuffer(ysGPUBuffer **newBuffer, int size, char *data, bool mirrorToRam) {
    YDS_ERROR_DECLARE("CreateIndexBuffer");

    if (newBuffer == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newBuffer = nullptr;

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = size;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData, *pInitData;
    ZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = data;

    if (data != nullptr) pInitData = &InitData;
    else pInitData = nullptr;

    ID3D11Buffer *buffer;
    HRESULT result = m_device->CreateBuffer(&bd, pInitData, &buffer);
    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_GPU_BUFFER);
    }

    ysD3D11GPUBuffer *newD3D11Buffer = m_gpuBuffers.NewGeneric<ysD3D11GPUBuffer>();

    newD3D11Buffer->m_size = size;
    newD3D11Buffer->m_mirrorToRAM = mirrorToRam;
    newD3D11Buffer->m_bufferType = ysGPUBuffer::GPU_INDEX_BUFFER;
    newD3D11Buffer->m_buffer = buffer;

    if (mirrorToRam) {
        newD3D11Buffer->m_RAMMirror = new char[size];
        memcpy(newD3D11Buffer->m_RAMMirror, data, size);
    }

    *newBuffer = static_cast<ysGPUBuffer *>(newD3D11Buffer);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateConstantBuffer(ysGPUBuffer **newBuffer, int size, char *data, bool mirrorToRam) {
    YDS_ERROR_DECLARE("CreateConstantBuffer");

    if (newBuffer == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newBuffer = nullptr;

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = size;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData, *pInitData;
    ZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = data;

    if (data) pInitData = &InitData;
    else pInitData = nullptr;

    HRESULT result;
    ID3D11Buffer *buffer;
    result = m_device->CreateBuffer(&bd, pInitData, &buffer);
    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_GPU_BUFFER);
    }

    ysD3D11GPUBuffer *newD3D11Buffer = m_gpuBuffers.NewGeneric<ysD3D11GPUBuffer>();

    newD3D11Buffer->m_size = size;
    newD3D11Buffer->m_mirrorToRAM = mirrorToRam;
    newD3D11Buffer->m_bufferType = ysGPUBuffer::GPU_CONSTANT_BUFFER;
    newD3D11Buffer->m_buffer = buffer;

    *newBuffer = static_cast<ysGPUBuffer *>(newD3D11Buffer);

    if (mirrorToRam) {
        newD3D11Buffer->m_RAMMirror = new char[size];
        memcpy(newD3D11Buffer->m_RAMMirror, data, size);
    }

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::UseVertexBuffer(ysGPUBuffer *buffer, int stride, int offset) {
    YDS_ERROR_DECLARE("UseVertexBuffer");

    if (!CheckCompatibility(buffer)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    if (buffer) {
        UINT uoffset = (UINT)offset;
        UINT ustride = (UINT)stride;

        ysD3D11GPUBuffer *d3d11Buffer = static_cast<ysD3D11GPUBuffer *>(buffer);

        if (d3d11Buffer->m_bufferType == ysGPUBuffer::GPU_DATA_BUFFER && (buffer != m_activeVertexBuffer || stride != d3d11Buffer->m_currentStride)) {
            GetImmediateContext()->IASetVertexBuffers(0, 1, &d3d11Buffer->m_buffer, &ustride, &uoffset);
        }
    }
    else {
        GetImmediateContext()->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    }

    YDS_NESTED_ERROR_CALL(ysDevice::UseVertexBuffer(buffer, stride, offset));

    return YDS_ERROR_RETURN(ysDevice::UseVertexBuffer(buffer, stride, offset));
}

ysError ysD3D11Device::UseIndexBuffer(ysGPUBuffer *buffer, int offset) {
    YDS_ERROR_DECLARE("UseIndexBuffer");

    if (!CheckCompatibility(buffer)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    if (buffer) {
        UINT uoffset = (UINT)offset;

        ysD3D11GPUBuffer *d3d11Buffer = static_cast<ysD3D11GPUBuffer *>(buffer);

        if (d3d11Buffer->m_bufferType == ysGPUBuffer::GPU_INDEX_BUFFER && buffer != m_activeIndexBuffer) {
            GetImmediateContext()->IASetIndexBuffer(d3d11Buffer->m_buffer, DXGI_FORMAT_R16_UINT, uoffset);
        }
    }
    else {
        GetImmediateContext()->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    }

    YDS_NESTED_ERROR_CALL(ysDevice::UseIndexBuffer(buffer, offset));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::UseConstantBuffer(ysGPUBuffer *buffer, int slot) {
    YDS_ERROR_DECLARE("UseConstantBuffer");

    if (!CheckCompatibility(buffer)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    if (buffer) {
        ysD3D11GPUBuffer *d3d11Buffer = static_cast<ysD3D11GPUBuffer *>(buffer);

        GetImmediateContext()->VSSetConstantBuffers(slot, 1, &d3d11Buffer->m_buffer);
        GetImmediateContext()->PSSetConstantBuffers(slot, 1, &d3d11Buffer->m_buffer);
    }
    else {
        GetImmediateContext()->VSSetConstantBuffers(slot, 0, nullptr);
        GetImmediateContext()->PSSetConstantBuffers(slot, 0, nullptr);
    }

    YDS_NESTED_ERROR_CALL(ysDevice::UseConstantBuffer(buffer, slot));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::EditBufferDataRange(ysGPUBuffer *buffer, char *data, int size, int offset) {
    YDS_ERROR_DECLARE("EditBufferDataRange");

    if (!CheckCompatibility(buffer))			return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);
    if (buffer == nullptr || data == nullptr)			return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if ((size + offset) > buffer->GetSize())	return YDS_ERROR_RETURN(ysError::YDS_OUT_OF_BOUNDS);
    if (size < 0 || offset < 0)					return YDS_ERROR_RETURN(ysError::YDS_OUT_OF_BOUNDS);

    ysD3D11GPUBuffer *d3d11Buffer = static_cast<ysD3D11GPUBuffer *>(buffer);

    D3D11_BOX box;
    ZeroMemory(&box, sizeof(box));
    box.left = offset;
    box.right = offset + size;
    box.bottom = 1;
    box.back = 1;

    GetImmediateContext()->UpdateSubresource(d3d11Buffer->m_buffer, 0, &box, data, buffer->GetSize(), 0);

    YDS_NESTED_ERROR_CALL(ysDevice::EditBufferDataRange(buffer, data, size, offset));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::EditBufferData(ysGPUBuffer *buffer, char *data) {
    YDS_ERROR_DECLARE("EditBufferData");

    if (!CheckCompatibility(buffer))		return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);
    if (buffer == nullptr || data == nullptr)		return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    ysD3D11GPUBuffer *d3d11Buffer = static_cast<ysD3D11GPUBuffer *>(buffer);

    GetImmediateContext()->UpdateSubresource(d3d11Buffer->m_buffer, 0, nullptr, data, 0, 0);

    YDS_NESTED_ERROR_CALL(ysDevice::EditBufferData(buffer, data));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyGPUBuffer(ysGPUBuffer *&buffer) {
    YDS_ERROR_DECLARE("DestroyGPUBuffer");

    if (!CheckCompatibility(buffer))	return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);
    if (buffer == nullptr)					return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    ysD3D11GPUBuffer *d3d11Buffer = static_cast<ysD3D11GPUBuffer *>(buffer);

    switch (buffer->GetType()) {
        case ysGPUBuffer::GPU_CONSTANT_BUFFER:
        {
            if (buffer == GetActiveBuffer(ysGPUBuffer::GPU_CONSTANT_BUFFER)) {
                YDS_NESTED_ERROR_CALL(UseConstantBuffer(nullptr, 0));
            }

            break;
        }
        case ysGPUBuffer::GPU_DATA_BUFFER:
        {
            if (buffer == GetActiveBuffer(ysGPUBuffer::GPU_DATA_BUFFER)) {
                YDS_NESTED_ERROR_CALL(UseVertexBuffer(nullptr, 0, 0));
            }

            break;
        }
        case ysGPUBuffer::GPU_INDEX_BUFFER:
        {
            if (buffer == GetActiveBuffer(ysGPUBuffer::GPU_INDEX_BUFFER)) {
                YDS_NESTED_ERROR_CALL(UseIndexBuffer(nullptr, 0));
            }

            break;
        }
    }

    d3d11Buffer->m_buffer->Release();

    YDS_NESTED_ERROR_CALL(ysDevice::DestroyGPUBuffer(buffer));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

// Shaders
ysError ysD3D11Device::CreateVertexShader(ysShader **newShader, const char *shaderFilename, const char *shaderName) {
    YDS_ERROR_DECLARE("CreateVertexShader");

    if (newShader == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newShader = nullptr;

    if (shaderFilename == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (shaderName == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    ID3D11VertexShader *vertexShader;
    ID3D10Blob *error;
    ID3D10Blob *shaderBlob;

    HRESULT result;
    result = D3DX11CompileFromFile(shaderFilename, nullptr, nullptr, shaderName, "vs_4_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, nullptr, &shaderBlob, &error, nullptr);

    if (FAILED(result)) {
        return YDS_ERROR_RETURN_MSG(ysError::YDS_VERTEX_SHADER_COMPILATION_ERROR, (char *)error->GetBufferPointer());
    }

    result = m_device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &vertexShader);
    if (FAILED(result)) {
        shaderBlob->Release();

        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_SHADER);
    }

    ysD3D11Shader *newD3D11Shader = m_shaders.NewGeneric<ysD3D11Shader>();
    newD3D11Shader->m_vertexShader = vertexShader;
    newD3D11Shader->m_shaderBlob = shaderBlob;

    strcpy_s(newD3D11Shader->m_filename, 256, shaderFilename);
    strcpy_s(newD3D11Shader->m_shaderName, 64, shaderName);
    newD3D11Shader->m_shaderType = ysShader::SHADER_TYPE_VERTEX;

    *newShader = static_cast<ysShader *>(newD3D11Shader);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreatePixelShader(ysShader **newShader, const char *shaderFilename, const char *shaderName) {
    YDS_ERROR_DECLARE("CreatePixelShader");

    if (newShader == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newShader = nullptr;

    if (shaderFilename == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (shaderName == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    ID3D11PixelShader *pixelShader;
    ID3D10Blob *error;
    ID3D10Blob *shaderBlob;

    HRESULT result;
    result = D3DX11CompileFromFile(shaderFilename, nullptr, nullptr, shaderName, "ps_4_0", D3D10_SHADER_ENABLE_STRICTNESS, 0, nullptr, &shaderBlob, &error, nullptr);

    if (FAILED(result)) {
        return YDS_ERROR_RETURN_MSG(ysError::YDS_FRAGMENT_SHADER_COMPILATION_ERROR, (char *)error->GetBufferPointer());
    }

    result = m_device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &pixelShader);
    if (FAILED(result)) {
        shaderBlob->Release();
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_SHADER);
    }

    ysD3D11Shader *newD3D11Shader = m_shaders.NewGeneric<ysD3D11Shader>();
    newD3D11Shader->m_shaderBlob = shaderBlob;
    newD3D11Shader->m_pixelShader = pixelShader;

    strcpy_s(newD3D11Shader->m_filename, 256, shaderFilename);
    strcpy_s(newD3D11Shader->m_shaderName, 64, shaderName);
    newD3D11Shader->m_shaderType = ysShader::SHADER_TYPE_PIXEL;

    *newShader = newD3D11Shader;

    // TEMP ----------------------------------------------------

    GetImmediateContext()->PSSetShader(pixelShader, 0, 0);

    // Create a sampler state (testing purposes)
    D3D11_SAMPLER_DESC desc;
    ZeroMemory(&desc, sizeof(D3D11_SAMPLER_DESC));
    desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.Filter = D3D11_FILTER_ANISOTROPIC;
    desc.MinLOD = 0.0f;
    desc.MaxLOD = FLT_MAX;
    desc.MaxAnisotropy = 16;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    HRESULT err = m_device->CreateSamplerState(&desc, &m_samplerState);
    GetImmediateContext()->PSSetSamplers(0, 1, &m_samplerState);

    // END TEMP ----------------------------------------------------

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyShader(ysShader *&shader) {
    YDS_ERROR_DECLARE("DestroyShader");

    if (!CheckCompatibility(shader)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);
    if (shader == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    ysD3D11Shader *d3d11Shader = static_cast<ysD3D11Shader *>(shader);
    bool active = (m_activeShaderProgram != nullptr) && (m_activeShaderProgram->GetShader(shader->GetShaderType()) == shader);

    d3d11Shader->m_shaderBlob->Release();

    switch (shader->GetShaderType()) {
        case ysShader::SHADER_TYPE_VERTEX:
        {
            if (active) GetImmediateContext()->VSSetShader(nullptr, nullptr, 0);
            d3d11Shader->m_vertexShader->Release();
            break;
        }
        case ysShader::SHADER_TYPE_PIXEL:
        {
            if (active) GetImmediateContext()->PSSetShader(nullptr, nullptr, 0);
            d3d11Shader->m_pixelShader->Release();
            break;
        }
    }

    YDS_NESTED_ERROR_CALL(ysDevice::DestroyShader(shader));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyShaderProgram(ysShaderProgram *&program, bool destroyShaders) {
    YDS_ERROR_DECLARE("DestroyShaderProgram");

    YDS_NESTED_ERROR_CALL(ysDevice::DestroyShaderProgram(program, destroyShaders));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateShaderProgram(ysShaderProgram **newProgram) {
    YDS_ERROR_DECLARE("CreateShaderProgram");

    if (newProgram == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newProgram = nullptr;

    ysD3D11ShaderProgram *newD3D11Program = m_shaderPrograms.NewGeneric<ysD3D11ShaderProgram>();
    *newProgram = static_cast<ysShaderProgram *>(newD3D11Program);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::AttachShader(ysShaderProgram *program, ysShader *shader) {
    YDS_ERROR_DECLARE("AttachShader");

    YDS_NESTED_ERROR_CALL(ysDevice::AttachShader(program, shader));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::LinkProgram(ysShaderProgram *program) {
    YDS_ERROR_DECLARE("LinkProgram");

    YDS_NESTED_ERROR_CALL(ysDevice::LinkProgram(program));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::UseShaderProgram(ysShaderProgram *program) {
    YDS_ERROR_DECLARE("UseShaderProgram");

    if (!CheckCompatibility(program)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    ysD3D11ShaderProgram *d3d11Program = static_cast<ysD3D11ShaderProgram *>(program);
    ysD3D11ShaderProgram *currentProgram = static_cast<ysD3D11ShaderProgram *>(m_activeShaderProgram);
    ysD3D11Shader *vertexShader = (d3d11Program) ? d3d11Program->GetShader(ysShader::SHADER_TYPE_VERTEX) : nullptr;
    ysD3D11Shader *fragmentShader = (d3d11Program) ? d3d11Program->GetShader(ysShader::SHADER_TYPE_PIXEL) : nullptr;

    if (d3d11Program == currentProgram) return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);

    ysD3D11Shader *currentVertexShader = (currentProgram) ? currentProgram->GetShader(ysShader::SHADER_TYPE_VERTEX) : nullptr;
    ysD3D11Shader *currentPixelShader = (currentProgram) ? currentProgram->GetShader(ysShader::SHADER_TYPE_PIXEL) : nullptr;

    if (vertexShader != currentVertexShader) {
        GetImmediateContext()->VSSetShader((vertexShader) ? vertexShader->m_vertexShader : nullptr, nullptr, 0);
    }

    if (fragmentShader != currentPixelShader) {
        GetImmediateContext()->PSSetShader((fragmentShader) ? fragmentShader->m_pixelShader : nullptr, nullptr, 0);
    }

    YDS_NESTED_ERROR_CALL(ysDevice::UseShaderProgram(program));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateInputLayout(ysInputLayout **newInputLayout, ysShader *shader, ysRenderGeometryFormat *format) {
    YDS_ERROR_DECLARE("CreateInputLayout");

    if (newInputLayout == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newInputLayout = nullptr;

    if (shader == nullptr || format == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (!CheckCompatibility(shader)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    ysD3D11Shader *d3d11Shader = static_cast<ysD3D11Shader *>(shader);

    int nChannels = format->GetChannelCount();
    D3D11_INPUT_ELEMENT_DESC descArray[64];

    for (int i = 0; i < nChannels; i++) {
        const ysRenderGeometryChannel *channel = format->GetChannel(i);

        descArray[i].SemanticName = channel->GetName();
        descArray[i].SemanticIndex = 0;
        descArray[i].Format = ConvertInputLayoutFormat(channel->GetFormat());
        descArray[i].InputSlot = 0;
        descArray[i].AlignedByteOffset = channel->GetOffset();
        descArray[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        descArray[i].InstanceDataStepRate = 0;
    }

    HRESULT result;
    ID3D11InputLayout *layout;

    result = m_device->CreateInputLayout(
        descArray,
        nChannels,
        d3d11Shader->m_shaderBlob->GetBufferPointer(),
        d3d11Shader->m_shaderBlob->GetBufferSize(),
        &layout);

    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_INPUT_FORMAT);
    }

    ysD3D11InputLayout *newD3D11Layout = m_inputLayouts.NewGeneric<ysD3D11InputLayout>();
    newD3D11Layout->m_layout = layout;

    *newInputLayout = static_cast<ysInputLayout *>(newD3D11Layout);

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::UseInputLayout(ysInputLayout *layout) {
    YDS_ERROR_DECLARE("UseInputLayout");

    if (!CheckCompatibility(layout)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    ysD3D11InputLayout *d3d11Layout = static_cast<ysD3D11InputLayout *>(layout);

    if (layout != nullptr) {
        if (layout != m_activeInputLayout) {
            GetImmediateContext()->IASetInputLayout(d3d11Layout->m_layout);
        }
    }
    else {
        GetImmediateContext()->IASetInputLayout(nullptr);
    }

    YDS_NESTED_ERROR_CALL(ysDevice::UseInputLayout(layout));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyInputLayout(ysInputLayout *&layout) {
    YDS_ERROR_DECLARE("DestroyInputLayout");

    if (layout == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (!CheckCompatibility(layout)) return YDS_ERROR_RETURN(ysError::YDS_INCOMPATIBLE_PLATFORMS);

    if (layout == m_activeInputLayout) {
        UseInputLayout(nullptr);
    }

    ysD3D11InputLayout *d3d11Layout = static_cast<ysD3D11InputLayout *>(layout);
    d3d11Layout->m_layout->Release();

    YDS_NESTED_ERROR_CALL(ysDevice::DestroyInputLayout(layout));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

// Textures
ysError ysD3D11Device::CreateTexture(ysTexture **newTexture, const char *fname) {
    YDS_ERROR_DECLARE("CreateTexture");

    if (newTexture == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    if (fname == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    *newTexture = nullptr;

    ID3D11Texture2D *newD3DTexture = nullptr;
    ID3D11ShaderResourceView *resourceView;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D11_TEXTURE2D_DESC desc;
    HRESULT result;

    D3DX11_IMAGE_LOAD_INFO info;
    memset((void *)&info, 0, sizeof(D3DX11_IMAGE_LOAD_INFO));
    //info.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    result = D3DX11CreateTextureFromFile(m_device, fname, nullptr, nullptr, (ID3D11Resource **)&newD3DTexture, nullptr);
    if (FAILED(result)) {
        return YDS_ERROR_RETURN_MSG(ysError::YDS_COULD_NOT_OPEN_TEXTURE, fname);
    }

    newD3DTexture->GetDesc(&desc);
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;

    result = m_device->CreateShaderResourceView(newD3DTexture, &srvDesc, &resourceView);
    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_MAKE_SHADER_RESOURCE_VIEW);
    }

    ysD3D11Texture *newD3D11Texture = m_textures.NewGeneric<ysD3D11Texture>();
    newD3D11Texture->m_resourceView = resourceView;
    newD3D11Texture->m_width = desc.Width;
    newD3D11Texture->m_height = desc.Height;
    *newTexture = newD3D11Texture;

    newD3DTexture->Release();

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::UseTexture(ysTexture *texture, int slot) {
    YDS_ERROR_DECLARE("UseTexture");

    if (slot >= m_maxTextureSlots) return YDS_ERROR_RETURN(ysError::YDS_OUT_OF_BOUNDS);

    ysD3D11Texture *d3d11Texture = static_cast<ysD3D11Texture *>(texture);

    if (texture != m_activeTextures[slot].Texture) {
        ID3D11ShaderResourceView *nullView = nullptr;
        GetImmediateContext()->PSSetShaderResources(slot, 1, (texture) ? &d3d11Texture->m_resourceView : &nullView);
    }

    YDS_NESTED_ERROR_CALL(ysDevice::UseTexture(texture, slot));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::UseRenderTargetAsTexture(ysRenderTarget *texture, int slot) {
    YDS_ERROR_DECLARE("UseTexture");

    if (slot >= m_maxTextureSlots) return YDS_ERROR_RETURN(ysError::YDS_OUT_OF_BOUNDS);

    ysD3D11RenderTarget *d3d11Texture = static_cast<ysD3D11RenderTarget *>(texture);

    if (texture != m_activeTextures[slot].RenderTarget) {
        ID3D11ShaderResourceView *nullView = nullptr;
        GetImmediateContext()->PSSetShaderResources(slot, 1, (texture) ? &d3d11Texture->m_resourceView : &nullView);
    }

    YDS_NESTED_ERROR_CALL(ysDevice::UseRenderTargetAsTexture(texture, slot));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyTexture(ysTexture *&texture) {
    YDS_ERROR_DECLARE("DestroyTexture");

    if (texture == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    ysD3D11Texture *d3d11Texture = static_cast<ysD3D11Texture *>(texture);

    for (int i = 0; i < m_maxTextureSlots; i++) {
        if (m_activeTextures[i].Texture == texture) {
            YDS_NESTED_ERROR_CALL(UseTexture((ysTexture *)nullptr, i));
        }
    }

    if (d3d11Texture->m_renderTargetView != nullptr) d3d11Texture->m_renderTargetView->Release();
    if (d3d11Texture->m_resourceView != nullptr) d3d11Texture->m_resourceView->Release();

    YDS_NESTED_ERROR_CALL(ysDevice::DestroyTexture(texture));

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

void ysD3D11Device::Draw(int numFaces, int indexOffset, int vertexOffset) {
    GetImmediateContext()->DrawIndexed(numFaces * 3, indexOffset, vertexOffset);
}

// Non-standard interface

void ysD3D11Device::GetDXGIDevice(IDXGIDevice **device) {
    if (m_device == nullptr) *device = nullptr;
    else {
        HRESULT hr = m_device->QueryInterface(IID_IDXGIDevice, (void **)device);
        if (FAILED(hr)) (*device) = nullptr;
    }
}

DXGI_FORMAT ysD3D11Device::ConvertInputLayoutFormat(ysRenderGeometryChannel::CHANNEL_FORMAT format) {
    switch (format) {
    case ysRenderGeometryChannel::CHANNEL_FORMAT_R32G32B32A32_FLOAT:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case ysRenderGeometryChannel::CHANNEL_FORMAT_R32G32B32_FLOAT:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case ysRenderGeometryChannel::CHANNEL_FORMAT_R32G32_FLOAT:
        return DXGI_FORMAT_R32G32_FLOAT;
    case ysRenderGeometryChannel::CHANNEL_FORMAT_R32G32B32A32_UINT:
        return DXGI_FORMAT_R32G32B32A32_UINT;
    case ysRenderGeometryChannel::CHANNEL_FORMAT_R32G32B32_UINT:
        return DXGI_FORMAT_R32G32B32_UINT;
    case ysRenderGeometryChannel::CHANNEL_FORMAT_UNDEFINED:
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

ysError ysD3D11Device::CreateD3D11DepthStencilView(ID3D11DepthStencilView **newDepthStencil, int width, int height, int count, int quality) {
    YDS_ERROR_DECLARE("CreateD3D11DepthBuffer");

    if (newDepthStencil == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);
    *newDepthStencil = nullptr;

    HRESULT result;

    ID3D11Texture2D *depthBuffer;

    D3D11_TEXTURE2D_DESC descDepth;
    ZeroMemory(&descDepth, sizeof(descDepth));
    descDepth.Width = width;
    descDepth.Height = height;
    descDepth.MipLevels = 1;
    descDepth.ArraySize = 1;
    descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    descDepth.SampleDesc.Count = count;
    descDepth.SampleDesc.Quality = quality;
    descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    descDepth.CPUAccessFlags = 0;
    descDepth.MiscFlags = 0;
    result = m_device->CreateTexture2D(&descDepth, nullptr, &depthBuffer);

    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_DEPTH_BUFFER);
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
    descDSV.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    descDSV.Texture2D.MipSlice = 0;
    result = m_device->CreateDepthStencilView(depthBuffer, nullptr, newDepthStencil);

    if (FAILED(result)) {
        depthBuffer->Release();
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_DEPTH_BUFFER);
    }

    depthBuffer->Release();

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyD3D11DepthStencilView(ID3D11DepthStencilView *&depthStencil) {
    YDS_ERROR_DECLARE("DestroyD3D11DepthBuffer");

    if (depthStencil == nullptr) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

    depthStencil->Release();
    depthStencil = nullptr;

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateD3D11OnScreenRenderTarget(ysRenderTarget *newTarget, ysRenderingContext *context, bool depthBuffer) {
    YDS_ERROR_DECLARE("CreateD3D11OnScreenRenderTarget");

    ysD3D11Context *d3d11Context = static_cast<ysD3D11Context *>(context);

    ID3D11Texture2D *backBuffer;
    ID3D11RenderTargetView *newRenderTargetView;
    ID3D11DepthStencilView *newDepthStencilView = nullptr;
    HRESULT result;

    result = d3d11Context->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID *)(&backBuffer));
    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_GET_BACK_BUFFER);
    }

    result = m_device->CreateRenderTargetView(backBuffer, nullptr, &newRenderTargetView);
    backBuffer->Release();

    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_RENDER_TARGET);
    }

    // Create Depth Buffer
    if (depthBuffer) {
        ysError depthResult
            = CreateD3D11DepthStencilView(
                &newDepthStencilView, context->GetWindow()->GetScreenWidth(), context->GetWindow()->GetScreenHeight(),
                m_multisampleCount, m_multisampleQuality);

        if (depthResult != ysError::YDS_NO_ERROR) {
            newRenderTargetView->Release();
            return YDS_ERROR_RETURN(depthResult);
        }
    }

    ysD3D11RenderTarget *newRenderTarget = static_cast<ysD3D11RenderTarget *>(newTarget);
    d3d11Context->m_attachedRenderTarget = newRenderTarget;

    newRenderTarget->m_type = ysRenderTarget::Type::OnScreen;
    newRenderTarget->m_posX = 0;
    newRenderTarget->m_posY = 0;
    newRenderTarget->m_width = context->GetWindow()->GetScreenWidth();
    newRenderTarget->m_height = context->GetWindow()->GetScreenHeight();
    newRenderTarget->m_format = ysRenderTarget::Format::RTF_R8G8B8A8_UNORM;
    newRenderTarget->m_hasDepthBuffer = depthBuffer;
    newRenderTarget->m_associatedContext = context;

    newRenderTarget->m_renderTargetView = newRenderTargetView;
    newRenderTarget->m_depthStencilView = newDepthStencilView;
    newRenderTarget->m_resourceView = nullptr;

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::CreateD3D11OffScreenRenderTarget(
    ysRenderTarget *target, int width, int height, ysRenderTarget::Format format, int sampleCount, bool depthBuffer) 
{
    YDS_ERROR_DECLARE("CreateD3D11OffScreenRenderTarget");

    HRESULT result;

    ID3D11Texture2D *renderTarget;
    ID3D11RenderTargetView *newRenderTargetView = nullptr;
    ID3D11ShaderResourceView *shaderResourceView = nullptr;
    ID3D11DepthStencilView *newDepthStencil = nullptr;

    // Create the texture
    D3D11_TEXTURE2D_DESC descBuffer;
    ZeroMemory(&descBuffer, sizeof(descBuffer));
    descBuffer.Width = width;
    descBuffer.Height = height;
    descBuffer.MipLevels = 1;
    descBuffer.ArraySize = 1;

    if (format == ysRenderTarget::Format::RTF_R32G32B32_FLOAT)
        descBuffer.Format = DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT;
    else if (format == ysRenderTarget::Format::RTF_R8G8B8A8_UNORM)
        descBuffer.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;

    descBuffer.SampleDesc.Count = 1;
    descBuffer.SampleDesc.Quality = 0;
    descBuffer.Usage = D3D11_USAGE_DEFAULT;
    descBuffer.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    descBuffer.CPUAccessFlags = 0;
    descBuffer.MiscFlags = 0;
    result = m_device->CreateTexture2D(&descBuffer, nullptr, &renderTarget);

    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_RENDER_TARGET);
    }

    // Create the render target view
    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    ZeroMemory(&rtDesc, sizeof(rtDesc));
    rtDesc.Format = descBuffer.Format;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;

    result = m_device->CreateRenderTargetView(renderTarget, &rtDesc, &newRenderTargetView);

    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_RENDER_TARGET);
    }

    // Create the shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
    ZeroMemory(&srDesc, sizeof(srDesc));
    srDesc.Format = descBuffer.Format;
    srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srDesc.Texture2D.MostDetailedMip = 0;
    srDesc.Texture2D.MipLevels = 1;

    result = m_device->CreateShaderResourceView(renderTarget, &srDesc, &shaderResourceView);

    if (FAILED(result)) {
        return YDS_ERROR_RETURN(ysError::YDS_COULD_NOT_CREATE_RENDER_TARGET);
    }

    renderTarget->Release();

    // Create Depth Buffer
    if (depthBuffer) {
        ysError depthResult = CreateD3D11DepthStencilView(&newDepthStencil, width, height, 1, 0);

        if (depthResult != ysError::YDS_NO_ERROR) {
            newRenderTargetView->Release();
            return YDS_ERROR_RETURN(depthResult);
        }
    }

    // Create the render target

    ysD3D11RenderTarget *newRenderTarget = static_cast<ysD3D11RenderTarget *>(target);

    newRenderTarget->m_type = ysRenderTarget::Type::OffScreen;
    newRenderTarget->m_posX = 0;
    newRenderTarget->m_posY = 0;
    newRenderTarget->m_width = width;
    newRenderTarget->m_height = height;
    newRenderTarget->m_format = ysRenderTarget::Format::RTF_R8G8B8A8_UNORM;
    newRenderTarget->m_hasDepthBuffer = depthBuffer;
    newRenderTarget->m_associatedContext = nullptr;

    newRenderTarget->m_renderTargetView = newRenderTargetView;
    newRenderTarget->m_depthStencilView = newDepthStencil;
    newRenderTarget->m_resourceView = shaderResourceView;

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

ysError ysD3D11Device::DestroyD3D11RenderTarget(ysRenderTarget *target) {
    YDS_ERROR_DECLARE("DestroyD3D11RenderTarget");

    ysD3D11RenderTarget *d3d11Target = static_cast<ysD3D11RenderTarget *>(target);
    if (d3d11Target->m_renderTargetView != nullptr) d3d11Target->m_renderTargetView->Release();
    if (d3d11Target->m_depthStencilView != nullptr) d3d11Target->m_depthStencilView->Release();
    if (d3d11Target->m_resourceView != nullptr) d3d11Target->m_resourceView->Release();

    d3d11Target->m_renderTargetView = nullptr;
    d3d11Target->m_depthStencilView = nullptr;
    d3d11Target->m_resourceView = nullptr;

    return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}
