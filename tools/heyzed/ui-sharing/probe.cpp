#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

using Microsoft::WRL::ComPtr;
constexpr UINT Width=64, Height=64, Frames=8;
constexpr GLenum MemoryDedicated=0x9581, D3D12Resource=0x958A, D3D12Fence=0x9594;
constexpr GLenum FenceValue=0x9595, GeneralLayout=0x958D, Framebuffer=0x8D40;
constexpr GLenum ColorAttachment=0x8CE0, FramebufferComplete=0x8CD5, RGBA8=0x8058;

void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void hr(HRESULT value,const char* message) {
    if(FAILED(value)) { std::fprintf(stderr,"HRESULT=0x%08lx\n",value); throw std::runtime_error(message); }
}
template<class T> T glproc(const char* name) {
    auto address=wglGetProcAddress(name);
    require(address && address!=(PROC)1 && address!=(PROC)2 && address!=(PROC)3 && address!=(PROC)-1,name);
    return reinterpret_cast<T>(address);
}
void glcheck(const char* stage) {
    GLenum error=glGetError();
    if(error) { std::fprintf(stderr,"GL error=0x%x stage=%s\n",error,stage); throw std::runtime_error(stage); }
}
std::wstring resourceName(const std::wstring& token,const wchar_t* suffix) { return L"Local\\heyzed-ui-probe-"+token+suffix; }

struct GLContext {
    HWND window=nullptr; HDC dc=nullptr; HGLRC context=nullptr;
    GLContext() {
        WNDCLASSW cls{};cls.style=CS_OWNDC;cls.lpfnWndProc=DefWindowProcW;
        cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"heyzed-ui-sharing-probe";
        require(RegisterClassW(&cls)!=0,"RegisterClass");
        window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"",WS_POPUP,0,0,Width,Height,nullptr,nullptr,cls.hInstance,nullptr);
        require(window!=nullptr,"Create hidden window");dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pixel{};pixel.nSize=sizeof(pixel);pixel.nVersion=1;
        pixel.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pixel.iPixelType=PFD_TYPE_RGBA;
        pixel.cColorBits=32;pixel.cAlphaBits=8;
        int format=ChoosePixelFormat(dc,&pixel);require(format!=0,"ChoosePixelFormat");
        require(SetPixelFormat(dc,format,&pixel)!=0,"SetPixelFormat");
        context=wglCreateContext(dc);require(context!=nullptr,"wglCreateContext");
        require(wglMakeCurrent(dc,context)!=0,"wglMakeCurrent");
    }
    ~GLContext() { wglMakeCurrent(nullptr,nullptr);if(context)wglDeleteContext(context);if(dc)ReleaseDC(window,dc);if(window)DestroyWindow(window); }
};

int producer(const std::wstring& token,const std::wstring& log) {
    FILE* output=nullptr;_wfreopen_s(&output,log.c_str(),L"w",stdout);setvbuf(stdout,nullptr,_IONBF,0);
    _wfreopen_s(&output,(log+L".err").c_str(),L"w",stderr);
    GLContext context;
    std::printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
    using Stringi=const GLubyte*(APIENTRY*)(GLenum,GLuint);
    auto getStringi=glproc<Stringi>("glGetStringi");GLint count=0;glGetIntegerv(0x821D,&count);
    auto supports=[&](const char* name) { for(GLint i=0;i<count;++i)if(std::strcmp((const char*)getStringi(GL_EXTENSIONS,i),name)==0)return true;return false; };
    for(const char* name:{"GL_EXT_memory_object","GL_EXT_memory_object_win32","GL_EXT_semaphore","GL_EXT_semaphore_win32"}) {
        bool present=supports(name);std::printf("%s=%s\n",name,present?"true":"false");require(present,name);
    }
    auto createMemory=glproc<void(APIENTRY*)(GLsizei,GLuint*)>("glCreateMemoryObjectsEXT");
    auto memoryParameter=glproc<void(APIENTRY*)(GLuint,GLenum,const GLint*)>("glMemoryObjectParameterivEXT");
    auto importMemory=glproc<void(APIENTRY*)(GLuint,unsigned long long,GLenum,const void*)>("glImportMemoryWin32NameEXT");
    auto storage=glproc<void(APIENTRY*)(GLenum,GLsizei,GLenum,GLsizei,GLsizei,GLuint,unsigned long long)>("glTexStorageMem2DEXT");
    auto genFbo=glproc<void(APIENTRY*)(GLsizei,GLuint*)>("glGenFramebuffers");
    auto bindFbo=glproc<void(APIENTRY*)(GLenum,GLuint)>("glBindFramebuffer");
    auto attach=glproc<void(APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint)>("glFramebufferTexture2D");
    auto status=glproc<GLenum(APIENTRY*)(GLenum)>("glCheckFramebufferStatus");
    auto genSemaphores=glproc<void(APIENTRY*)(GLsizei,GLuint*)>("glGenSemaphoresEXT");
    auto importSemaphore=glproc<void(APIENTRY*)(GLuint,GLenum,const void*)>("glImportSemaphoreWin32NameEXT");
    auto parameter=glproc<void(APIENTRY*)(GLuint,GLenum,const unsigned long long*)>("glSemaphoreParameterui64vEXT");
    using SemaphoreOp=void(APIENTRY*)(GLuint,GLuint,const GLuint*,GLuint,const GLuint*,const GLenum*);
    auto signal=glproc<SemaphoreOp>("glSignalSemaphoreEXT");auto wait=glproc<SemaphoreOp>("glWaitSemaphoreEXT");
    GLuint memory=0,texture=0,fbo=0,semaphore=0;createMemory(1,&memory);
    GLint dedicated=GL_TRUE;memoryParameter(memory,MemoryDedicated,&dedicated);
    auto name=resourceName(token,L"-texture");importMemory(memory,0,D3D12Resource,name.c_str());glcheck("import D3D12 texture");
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    storage(GL_TEXTURE_2D,1,RGBA8,Width,Height,memory,0);glcheck("bind imported texture storage");
    genFbo(1,&fbo);bindFbo(Framebuffer,fbo);attach(Framebuffer,ColorAttachment,GL_TEXTURE_2D,texture,0);
    require(status(Framebuffer)==FramebufferComplete,"Imported framebuffer incomplete");
    genSemaphores(1,&semaphore);name=resourceName(token,L"-fence");importSemaphore(semaphore,D3D12Fence,name.c_str());glcheck("import D3D12 fence");
    glViewport(0,0,Width,Height);GLenum layout=GeneralLayout;
    for(UINT frame=0;frame<Frames;++frame) {
        if(frame) { unsigned long long value=frame*2;parameter(semaphore,FenceValue,&value);wait(semaphore,0,nullptr,1,&texture,&layout); }
        glDisable(GL_SCISSOR_TEST);glClearColor(frame/16.f,.5f,.75f,.5f);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(0,0,Width/2,Height/2);glClearColor(1.f,frame/16.f,.25f,.25f);glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        unsigned long long value=frame*2+1;parameter(semaphore,FenceValue,&value);signal(semaphore,0,nullptr,1,&texture,&layout);glFlush();
        glcheck("publish GPU frame");
    }
    unsigned long long value=Frames*2;parameter(semaphore,FenceValue,&value);wait(semaphore,0,nullptr,1,&texture,&layout);
    glFinish();glcheck("final consumer acknowledgement");std::printf("GL_FRAMES_PUBLISHED=%u\n",Frames);return 0;
}

struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value)CloseHandle(value);}
};
struct Child {
    PROCESS_INFORMATION process{};
    ~Child() {
        if(process.hProcess) { if(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT) {TerminateProcess(process.hProcess,2);WaitForSingleObject(process.hProcess,5000);} CloseHandle(process.hProcess); }
        if(process.hThread)CloseHandle(process.hThread);
    }
};
void waitFence(ID3D12Fence* fence,UINT64 value,HANDLE event) {
    hr(fence->SetEventOnCompletion(value,event),"SetEventOnCompletion");
    require(WaitForSingleObject(event,15000)==WAIT_OBJECT_0,"GPU fence timeout");
}

int consumer(const std::wstring& directory) {
    ComPtr<IDXGIFactory4> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    for(UINT index=0;factory->EnumAdapters1(index,&adapter)!=DXGI_ERROR_NOT_FOUND;++index) {
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE){adapter.Reset();continue;}
        if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))) {std::wprintf(L"D3D12_ADAPTER=%s\n",desc.Description);break;}adapter.Reset();
    }
    require(device!=nullptr,"No hardware D3D12 device");
    D3D12_HEAP_PROPERTIES gpu{};gpu.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=Width;desc.Height=Height;
    desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> texture;hr(device->CreateCommittedResource(&gpu,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&texture)),"Create shared texture");
    ComPtr<ID3D12Fence> fence;hr(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)),"Create shared fence");
    auto token=std::to_wstring(GetCurrentProcessId());Handle textureHandle,fenceHandle,event;
    hr(device->CreateSharedHandle(texture.Get(),nullptr,GENERIC_ALL,resourceName(token,L"-texture").c_str(),&textureHandle.value),"Share texture");
    hr(device->CreateSharedHandle(fence.Get(),nullptr,GENERIC_ALL,resourceName(token,L"-fence").c_str(),&fenceHandle.value),"Share fence");
    event.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event.value!=nullptr,"Create fence event");
    D3D12_COMMAND_QUEUE_DESC queueDesc{};ComPtr<ID3D12CommandQueue> queue;hr(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"Create queue");
    ComPtr<ID3D12CommandAllocator> allocator;hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create allocator");
    ComPtr<ID3D12GraphicsCommandList> commands;hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create command list");hr(commands->Close(),"Close initial list");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&size);
    D3D12_HEAP_PROPERTIES cpu{};cpu.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=size;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;hr(device->CreateCommittedResource(&cpu,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Create verification-only readback");
    wchar_t executable[32768];require(GetModuleFileNameW(nullptr,executable,32768)>0,"Get executable path");
    std::wstring log=directory+L"/producer.log",line=L"\""+std::wstring(executable)+L"\" --producer "+token+L" \""+log+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);Child child;
    require(CreateProcessW(nullptr,line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child.process)!=0,"Launch GL producer process");
    for(UINT frame=0;frame<Frames;++frame) {
        waitFence(fence.Get(),frame*2+1,event.value);
        hr(allocator->Reset(),"Reset allocator");hr(commands->Reset(allocator.Get(),nullptr),"Reset commands");
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=texture.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;commands->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=texture.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION target{};target.pResource=readback.Get();target.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;target.PlacedFootprint=footprint;
        commands->CopyTextureRegion(&target,0,0,0,&source,nullptr);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commands->ResourceBarrier(1,&barrier);hr(commands->Close(),"Close verification commands");
        ID3D12CommandList* lists[]={commands.Get()};queue->ExecuteCommandLists(1,lists);
        ComPtr<ID3D12Fence> completed;hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completed)),"Create local verification fence");
        hr(queue->Signal(completed.Get(),1),"Signal verification fence");waitFence(completed.Get(),1,event.value);
        unsigned char* pixels=nullptr;D3D12_RANGE range{0,(SIZE_T)size};hr(readback->Map(0,&range,(void**)&pixels),"Map verification pixels");
        UINT mismatches=0;
        for(UINT y=0;y<Height;++y)for(UINT x=0;x<Width;++x) {
            bool corner=x<Width/2&&y<Height/2;
            int expected[4]={corner?255:(int)std::lround(frame*255./16),corner?(int)std::lround(frame*255./16):128,corner?64:191,corner?64:128};
            auto pixel=pixels+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;
            for(UINT c=0;c<4;++c)if(std::abs((int)pixel[c]-expected[c])>1)++mismatches;
        }
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);
        std::printf("FRAME=%u CHECKED_PIXELS=%u CHANNEL_MISMATCHES=%u\n",frame,Width*Height,mismatches);require(mismatches==0,"GL/D3D12 pixel mismatch");
        hr(queue->Signal(fence.Get(),frame*2+2),"Acknowledge reusable GPU texture");
        waitFence(fence.Get(),frame*2+2,event.value);
    }
    require(WaitForSingleObject(child.process.hProcess,15000)==WAIT_OBJECT_0,"GL producer exit timeout");DWORD exitCode=1;GetExitCodeProcess(child.process.hProcess,&exitCode);require(exitCode==0,"GL producer failed");
    std::printf("RESULT=PASS FRAMES=%u CROSS_PROCESS=true PIXEL_TRANSPORT=gpu READBACK=verification_only\n",Frames);return 0;
}

int wmain(int argc,wchar_t** argv) {
    try { if(argc==4&&std::wstring(argv[1])==L"--producer")return producer(argv[2],argv[3]);require(argc==2,"Usage: probe.exe output-directory");return consumer(argv[1]); }
    catch(const std::exception& error) { std::fprintf(stderr,"RESULT=FAIL REASON=%s\n",error.what());return 1; }
}
