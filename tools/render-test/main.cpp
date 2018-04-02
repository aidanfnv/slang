// main.cpp

#include "options.h"
#include "render.h"
#include "render-d3d11.h"
#include "render-d3d12.h"
#include "render-gl.h"
#include "render-vk.h"

#include "slang-support.h"

#include "shader-input-layout.h"
#include <stdio.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NOMINMAX

#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

namespace renderer_test {

int gWindowWidth = 1024;
int gWindowHeight = 768;

//
// For the purposes of a small example, we will define the vertex data for a
// single triangle directly in the source file. It should be easy to extend
// this example to load data from an external source, if desired.
//

struct Vertex
{
    float position[3];
    float color[3];
    float uv[2];
};

static const int kVertexCount = 3;
static const Vertex kVertexData[kVertexCount] = {
    { { 0,  0, 0.5 }, {1, 0, 0} , {0, 0} },
    { { 0,  1, 0.5 }, {0, 0, 1} , {1, 0} },
    { { 1,  0, 0.5 }, {0, 1, 0} , {1, 1} },
};

using namespace Slang;

class RenderTestApp
{
	public:

		// At initialization time, we are going to load and compile our Slang shader
		// code, and then create the API objects we need for rendering.
	Result initialize(Renderer* renderer, ShaderCompiler* shaderCompiler);	
	void runCompute();
	void renderFrame();
	void finalize();

	BindingState* getBindingState() const { return m_bindingState; }

	protected:
		/// Called in initialize
	Result initializeShaders(ShaderCompiler* shaderCompiler);

	// variables for state to be used for rendering...
	uintptr_t m_constantBufferSize, m_computeResultBufferSize;

	RefPtr<Renderer> m_renderer;

	RefPtr<Buffer>	m_constantBuffer;
	RefPtr<InputLayout> m_inputLayout;
	RefPtr<Buffer>      m_vertexBuffer;
	RefPtr<ShaderProgram> m_shaderProgram;
	RefPtr<BindingState> m_bindingState;

	ShaderInputLayout m_shaderInputLayout;

};

// Entry point name to use for vertex/fragment shader
static const char vertexEntryPointName[]    = "vertexMain";
static const char fragmentEntryPointName[]  = "fragmentMain";
static const char computeEntryPointName[]	= "computeMain";

// "Profile" to use when compiling for HLSL targets
// TODO: does this belong here?
static const char vertexProfileName[]   = "vs_5_0";
static const char fragmentProfileName[] = "ps_5_0";
static const char computeProfileName[]	= "cs_5_0";

SlangResult RenderTestApp::initialize(Renderer* renderer, ShaderCompiler* shaderCompiler)
{
    SLANG_RETURN_ON_FAIL(initializeShaders(shaderCompiler));

	m_renderer = renderer;

    m_bindingState = renderer->createBindingState(m_shaderInputLayout);

    // Do other initialization that doesn't depend on the source language.

    // TODO(tfoley): use each API's reflection interface to query the constant-buffer size needed
    m_constantBufferSize = 16 * sizeof(float);

    BufferDesc constantBufferDesc;
    constantBufferDesc.size = m_constantBufferSize;
    constantBufferDesc.flavor = BufferFlavor::Constant;

    m_constantBuffer = renderer->createBuffer(constantBufferDesc);
    if(!m_constantBuffer)
        return SLANG_FAIL;
		
    // Input Assembler (IA)

    InputElementDesc inputElements[] = {
        { "A", 0, Format::RGB_Float32, offsetof(Vertex, position) },
        { "A", 1, Format::RGB_Float32, offsetof(Vertex, color) },
        { "A", 2, Format::RG_Float32, offsetof(Vertex, uv) },
    };

    m_inputLayout = renderer->createInputLayout(&inputElements[0], sizeof(inputElements)/sizeof(inputElements[0]));
    if(!m_inputLayout)
        return SLANG_FAIL;

    BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
    vertexBufferDesc.flavor = BufferFlavor::Vertex;
    vertexBufferDesc.initData = &kVertexData[0];

    m_vertexBuffer = renderer->createBuffer(vertexBufferDesc);
    if(!m_vertexBuffer)
        return SLANG_FAIL;

    return SLANG_OK;
}

Result RenderTestApp::initializeShaders(ShaderCompiler* shaderCompiler)
{
	// Read in the source code
	char const* sourcePath = gOptions.sourcePath;
	FILE* sourceFile = fopen(sourcePath, "rb");
	if (!sourceFile)
	{
		fprintf(stderr, "error: failed to open '%s' for reading\n", sourcePath);
		return SLANG_FAIL;
	}
	fseek(sourceFile, 0, SEEK_END);
	size_t sourceSize = ftell(sourceFile);
	fseek(sourceFile, 0, SEEK_SET);
	char* sourceText = (char*)malloc(sourceSize + 1);
	if (!sourceText)
	{
		fprintf(stderr, "error: out of memory");
		return SLANG_FAIL;
	}
	fread(sourceText, sourceSize, 1, sourceFile);
	fclose(sourceFile);
	sourceText[sourceSize] = 0;

	m_shaderInputLayout.Parse(sourceText);

	ShaderCompileRequest::SourceInfo sourceInfo;
	sourceInfo.path = sourcePath;
	sourceInfo.dataBegin = sourceText;
	sourceInfo.dataEnd = sourceText + sourceSize;

	ShaderCompileRequest compileRequest;
	compileRequest.source = sourceInfo;
	if (gOptions.shaderType == ShaderProgramType::Graphics || gOptions.shaderType == ShaderProgramType::GraphicsCompute)
	{
		compileRequest.vertexShader.source = sourceInfo;
		compileRequest.vertexShader.name = vertexEntryPointName;
		compileRequest.vertexShader.profile = vertexProfileName;
		compileRequest.fragmentShader.source = sourceInfo;
		compileRequest.fragmentShader.name = fragmentEntryPointName;
		compileRequest.fragmentShader.profile = fragmentProfileName;
	}
	else
	{
		compileRequest.computeShader.source = sourceInfo;
		compileRequest.computeShader.name = computeEntryPointName;
		compileRequest.computeShader.profile = computeProfileName;
	}
	compileRequest.entryPointTypeArguments = m_shaderInputLayout.globalTypeArguments;
	m_shaderProgram = shaderCompiler->compileProgram(compileRequest);
	if (!m_shaderProgram)
	{
		return SLANG_FAIL;
	}

	return SLANG_OK;
}

void RenderTestApp::renderFrame()
{
    auto mappedData = m_renderer->map(m_constantBuffer, MapFlavor::WriteDiscard);
    if(mappedData)
    {
        static const float kIdentity[] =
        { 1, 0, 0, 0,
          0, 1, 0, 0,
          0, 0, 1, 0,
          0, 0, 0, 1 };
        memcpy(mappedData, kIdentity, sizeof(kIdentity));

		m_renderer->unmap(m_constantBuffer);
    }

    // Input Assembler (IA)

	m_renderer->setInputLayout(m_inputLayout);
	m_renderer->setPrimitiveTopology(PrimitiveTopology::TriangleList);

	m_renderer->setVertexBuffer(0, m_vertexBuffer, sizeof(Vertex));

    // Vertex Shader (VS)
    // Pixel Shader (PS)

	m_renderer->setShaderProgram(m_shaderProgram);
	m_renderer->setConstantBuffer(0, m_constantBuffer);
	m_renderer->setBindingState(m_bindingState);
    //

	m_renderer->draw(3);
}

void RenderTestApp::runCompute()
{
	m_renderer->setShaderProgram(m_shaderProgram);
	m_renderer->setBindingState(m_bindingState);
	m_renderer->dispatchCompute(1, 1, 1);
}

void RenderTestApp::finalize()
{
}


//
// We use a bare-minimum window procedure to get things up and running.
//

static LRESULT CALLBACK windowProc(
    HWND    windowHandle,
    UINT    message,
    WPARAM  wParam,
    LPARAM  lParam)
{
    switch (message)
    {
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(windowHandle, message, wParam, lParam);
}

SlangResult innerMain(int argc, char** argv)
{
	// Parse command-line options
	SLANG_RETURN_ON_FAIL(parseOptions(&argc, argv));

	// Do initial window-creation stuff here, rather than in the renderer-specific files

	HINSTANCE instance = GetModuleHandleA(0);
	int showCommand = SW_SHOW;

	// First we register a window class.

	WNDCLASSEXW windowClassDesc;
	windowClassDesc.cbSize = sizeof(windowClassDesc);
	windowClassDesc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	windowClassDesc.lpfnWndProc = &windowProc;
	windowClassDesc.cbClsExtra = 0;
	windowClassDesc.cbWndExtra = 0;
	windowClassDesc.hInstance = instance;
	windowClassDesc.hIcon = 0;
	windowClassDesc.hCursor = 0;
	windowClassDesc.hbrBackground = 0;
	windowClassDesc.lpszMenuName = 0;
	windowClassDesc.lpszClassName = L"HelloWorld";
	windowClassDesc.hIconSm = 0;
	ATOM windowClassAtom = RegisterClassExW(&windowClassDesc);
	if (!windowClassAtom)
	{
		fprintf(stderr, "error: failed to register window class\n");
		return SLANG_FAIL;
	}

	// Next, we create a window using that window class.

	// We will create a borderless window since our screen-capture logic in GL
	// seems to get thrown off by having to deal with a window frame.
	DWORD windowStyle = WS_POPUP;
	DWORD windowExtendedStyle = 0;

	RECT windowRect = { 0, 0, gWindowWidth, gWindowHeight };
	AdjustWindowRectEx(&windowRect, windowStyle, /*hasMenu=*/false, windowExtendedStyle);

	auto width = windowRect.right - windowRect.left;
	auto height = windowRect.bottom - windowRect.top;

	LPWSTR windowName = L"Slang Render Test";
	HWND windowHandle = CreateWindowExW(
		windowExtendedStyle,
		(LPWSTR)windowClassAtom,
		windowName,
		windowStyle,
		0, 0, // x, y
		width, height,
		NULL, // parent
		NULL, // menu
		instance,
		NULL);
	if (!windowHandle)
	{
		fprintf(stderr, "error: failed to create window\n");
		return SLANG_FAIL;
	}

	Slang::RefPtr<Renderer> renderer;

	SlangSourceLanguage nativeLanguage = SLANG_SOURCE_LANGUAGE_UNKNOWN;
	SlangCompileTarget slangTarget = SLANG_TARGET_NONE;
	switch (gOptions.rendererID)
	{
		case RendererID::D3D11:
			renderer = createD3D11Renderer();
			slangTarget = SLANG_HLSL;
			nativeLanguage = SLANG_SOURCE_LANGUAGE_HLSL;
			break;

		case RendererID::D3D12:
			renderer = createD3D12Renderer();
			slangTarget = SLANG_HLSL;
			nativeLanguage = SLANG_SOURCE_LANGUAGE_HLSL;
			break;

		case RendererID::GL:
			renderer = createGLRenderer();
			slangTarget = SLANG_GLSL;
			nativeLanguage = SLANG_SOURCE_LANGUAGE_GLSL;
			break;

		case RendererID::VK:
			renderer = createVKRenderer();
			slangTarget = SLANG_SPIRV;
			nativeLanguage = SLANG_SOURCE_LANGUAGE_GLSL;
			break;

		default:
			fprintf(stderr, "error: unexpected\n");
			return SLANG_FAIL;
	}

	SLANG_RETURN_ON_FAIL(renderer->initialize(windowHandle));

	auto shaderCompiler = renderer->getShaderCompiler();
	switch (gOptions.inputLanguageID)
	{
		case InputLanguageID::Slang:
			shaderCompiler = createSlangShaderCompiler(shaderCompiler, SLANG_SOURCE_LANGUAGE_SLANG, slangTarget);
			break;

		case InputLanguageID::NativeRewrite:
			shaderCompiler = createSlangShaderCompiler(shaderCompiler, nativeLanguage, slangTarget);
			break;

		default:
			break;
	}

	{
		RenderTestApp app;

		SLANG_RETURN_ON_FAIL(app.initialize(renderer, shaderCompiler));

		// Once initialization is all complete, we show the window...
		ShowWindow(windowHandle, showCommand);

		// ... and enter the event loop:
		for (;;)
		{
			MSG message;

			int result = PeekMessageW(&message, NULL, 0, 0, PM_REMOVE);
			if (result != 0)
			{
				if (message.message == WM_QUIT)
				{
					return (int)message.wParam;
				}

				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			else
			{
				// Whenever we don't have Windows events to process, we render a frame.
				if (gOptions.shaderType == ShaderProgramType::Compute)
				{
					app.runCompute();
				}
				else
				{
					static const float kClearColor[] = { 0.25, 0.25, 0.25, 1.0 };
					renderer->setClearColor(kClearColor);
					renderer->clearFrame();

					app.renderFrame();
				}
				// If we are in a mode where output is requested, we need to snapshot the back buffer here
				if (gOptions.outputPath)
				{
                    // Submit the work
                    renderer->submitGpuWork();
                    // Wait until everything is complete
                    renderer->waitForGpu();

					if (gOptions.shaderType == ShaderProgramType::Compute || gOptions.shaderType == ShaderProgramType::GraphicsCompute)
						renderer->serializeOutput(app.getBindingState(), gOptions.outputPath);
					else
						SLANG_RETURN_ON_FAIL(renderer->captureScreenShot(gOptions.outputPath));
					return SLANG_OK;
				}

				renderer->presentFrame();
			}
		}
	}

	return SLANG_OK;
}

} // renderer_test

int main(int argc, char**  argv)
{
	SlangResult res = renderer_test::innerMain(argc, argv);

	return SLANG_FAILED(res) ? 1 : 0;
}

