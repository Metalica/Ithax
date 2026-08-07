// Copyright © 2026 Ithax contributors.

// Stage 5.8: procedural scene through the TrinityAL Vulkan render graph.
// A starfield pass and a scene pass are declared in a render graph,
// compiled per frame, and recorded through the backend. The scene uses an
// orbit camera, a Carbon Mesh (CMF) ship mesh, a procedural texture, and
// an immutable per-frame packet. Exits 0 on success.

#include <TrinityAL.h>
#include <ITr2RenderContextEvents.h>

#include <cmf/bounds.h>
#include <cmf/memallocator.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <vector>

// Needed by CcpCore.
const char* g_moduleName = "ithax-stage5-milestone-scene";

namespace
{

constexpr uint32_t WINDOW_WIDTH = 800;
constexpr uint32_t WINDOW_HEIGHT = 600;
constexpr uint32_t FRAME_COUNT = 60;

double Percentile( std::vector<double>& samples, double percentile )
{
	if( samples.empty() )
	{
		return 0.0;
	}
	std::sort( samples.begin(), samples.end() );
	const size_t index = static_cast<size_t>(
		std::ceil( percentile * samples.size() ) - 1 );
	return samples[std::min( index, samples.size() - 1 )];
}

void RequireGraph( const ALResult& result, const char* message )
{
	if( FAILED( result ) )
	{
		std::fprintf( stderr, "%s: 0x%08x\n", message, unsigned( result.GetResult() ) );
		std::exit( 1 );
	}
}

constexpr uint32_t PROCTEX_WIDTH = 64;
constexpr uint32_t PROCTEX_HEIGHT = 64;

constexpr uint32_t STAR_COUNT = 200;

struct Vertex
{
	float position[3];
	float color[4];
	float uv[2];
};

const Vertex TRIANGLE_VERTICES[] = {
	{ { 0.0f, -0.6f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.5f, 1.0f } },
	{ { 0.5f, 0.4f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
	{ { -0.5f, 0.4f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
};

const uint16_t TRIANGLE_INDICES[] = { 0, 1, 2 };

// Procedural ship-like mesh: a hull (elongated pyramid) + two wings.
const Vertex SHIP_VERTICES[] = {
	// Hull
	{ { 0.0f, 0.0f, 0.6f }, { 0.8f, 0.8f, 0.9f, 1.0f }, { 0.5f, 0.5f } },
	{ { -0.3f, -0.1f, -0.4f }, { 0.6f, 0.6f, 0.7f, 1.0f }, { 0.25f, 0.75f } },
	{ { 0.3f, -0.1f, -0.4f }, { 0.6f, 0.6f, 0.7f, 1.0f }, { 0.75f, 0.75f } },
	{ { 0.0f, 0.25f, -0.2f }, { 0.9f, 0.9f, 1.0f, 1.0f }, { 0.5f, 0.25f } },
	// Left wing
	{ { -0.3f, -0.1f, -0.4f }, { 0.4f, 0.5f, 0.9f, 1.0f }, { 0.1f, 0.6f } },
	{ { -1.1f, -0.05f, -0.1f }, { 0.3f, 0.4f, 0.8f, 1.0f }, { 0.0f, 0.5f } },
	{ { -0.3f, 0.0f, 0.1f }, { 0.4f, 0.5f, 0.9f, 1.0f }, { 0.2f, 0.4f } },
	// Right wing
	{ { 0.3f, -0.1f, -0.4f }, { 0.9f, 0.5f, 0.4f, 1.0f }, { 0.9f, 0.6f } },
	{ { 1.1f, -0.05f, -0.1f }, { 0.8f, 0.4f, 0.3f, 1.0f }, { 1.0f, 0.5f } },
	{ { 0.3f, 0.0f, 0.1f }, { 0.9f, 0.5f, 0.4f, 1.0f }, { 0.8f, 0.4f } },
};

const uint16_t SHIP_INDICES[] = {
	0, 1, 2,
	0, 2, 3,
	0, 3, 1,
	1, 3, 2,
	4, 5, 6,
	7, 8, 9,
};

// ---------------------------------------------------------------------------
// Minimal row-major float4x4 math (storage layout matches the HLSL cbuffer).
// ---------------------------------------------------------------------------
struct Mat4
{
	float m[4][4]; // m[row][col]

	static Mat4 Identity()
	{
		Mat4 result = {};
		for( int i = 0; i < 4; ++i )
		{
			result.m[i][i] = 1.0f;
		}
		return result;
	}

	static Mat4 Perspective( float fovRadians, float aspect, float nearZ, float farZ )
	{
		Mat4 result = {};
		const float f = 1.0f / std::tan( fovRadians * 0.5f );
		result.m[0][0] = f / aspect;
		result.m[1][1] = f;
		result.m[2][2] = farZ / ( nearZ - farZ );
		result.m[2][3] = -1.0f;
		result.m[3][2] = ( farZ * nearZ ) / ( nearZ - farZ );
		return result;
	}

	static Mat4 LookAt( const float eye[3], const float center[3], const float up[3] )
	{
		// Right-handed, row-major (matches HLSL mul(M, v)).
		float zaxis[3] = {
			center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]
		};
		Normalize( zaxis );
		float xaxis[3] = {};
		Cross( up, zaxis, xaxis );
		Normalize( xaxis );
		float yaxis[3] = {};
		Cross( zaxis, xaxis, yaxis );

		Mat4 result = Identity();
		result.m[0][0] = xaxis[0];
		result.m[0][1] = xaxis[1];
		result.m[0][2] = xaxis[2];
		result.m[1][0] = yaxis[0];
		result.m[1][1] = yaxis[1];
		result.m[1][2] = yaxis[2];
		result.m[2][0] = -zaxis[0];
		result.m[2][1] = -zaxis[1];
		result.m[2][2] = -zaxis[2];
		result.m[3][0] = -( xaxis[0] * eye[0] + xaxis[1] * eye[1] + xaxis[2] * eye[2] );
		result.m[3][1] = -( yaxis[0] * eye[0] + yaxis[1] * eye[1] + yaxis[2] * eye[2] );
		result.m[3][2] = ( zaxis[0] * eye[0] + zaxis[1] * eye[1] + zaxis[2] * eye[2] );
		return result;
	}

	static Mat4 RotationZ( float angleRadians )
	{
		Mat4 result = Identity();
		const float c = std::cos( angleRadians );
		const float s = std::sin( angleRadians );
		result.m[0][0] = c;
		result.m[0][1] = -s;
		result.m[1][0] = s;
		result.m[1][1] = c;
		return result;
	}

	static Mat4 Translation( float x, float y, float z )
	{
		Mat4 result = Identity();
		result.m[3][0] = x;
		result.m[3][1] = y;
		result.m[3][2] = z;
		return result;
	}

	static Mat4 Mul( const Mat4& a, const Mat4& b )
	{
		Mat4 result = {};
		for( int row = 0; row < 4; ++row )
		{
			for( int col = 0; col < 4; ++col )
			{
				float sum = 0.0f;
				for( int k = 0; k < 4; ++k )
				{
					sum += a.m[row][k] * b.m[k][col];
				}
				result.m[row][col] = sum;
			}
		}
		return result;
	}

private:
	static void Normalize( float v[3] )
	{
		const float length = std::sqrt( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );
		if( length > 1e-6f )
		{
			v[0] /= length;
			v[1] /= length;
			v[2] /= length;
		}
	}

	static void Cross( const float a[3], const float b[3], float out[3] )
	{
		out[0] = a[1] * b[2] - a[2] * b[1];
		out[1] = a[2] * b[0] - a[0] * b[2];
		out[2] = a[0] * b[1] - a[1] * b[0];
	}
};

// ---------------------------------------------------------------------------
// Immutable per-frame packet: everything the frame needs, built on the CPU
// before recording.
// ---------------------------------------------------------------------------
struct FramePacket
{
	Mat4 starMvp;
	Mat4 triangleMvp;
	Mat4 shipMvp;
	uint32_t starCount;
	float clearColor[4];
};

class SceneEvents : public ITr2RenderContextEvents
{
public:
	void OnContextCreated( Tr2PrimaryRenderContextAL& renderContext ) override
	{
		(void)renderContext;
	}
};

HWND CreateWindowHandle()
{
	static const char* const windowClass = "ithax-stage5-vulkan-window";
	static bool classRegistered = false;
	if( !classRegistered )
	{
		WNDCLASSA windowClassInfo = {};
		windowClassInfo.style = CS_HREDRAW | CS_VREDRAW;
		windowClassInfo.lpfnWndProc = DefWindowProcA;
		windowClassInfo.hInstance = GetModuleHandleA( nullptr );
		windowClassInfo.lpszClassName = windowClass;
		if( RegisterClassA( &windowClassInfo ) == 0 )
		{
			return nullptr;
		}
		classRegistered = true;
	}
	return CreateWindowExA(
		0, windowClass, "ithax stage5 vulkan",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		static_cast<int>( WINDOW_WIDTH ), static_cast<int>( WINDOW_HEIGHT ),
		nullptr, nullptr, GetModuleHandleA( nullptr ), nullptr );
}

bool CreateShader( Tr2ShaderAL& shader, Tr2RenderContextEnum::ShaderType type,
	const void* bytecode, size_t size, const Tr2ShaderSignatureAL& signature,
	Tr2PrimaryRenderContextAL& renderContext )
{
	Tr2ShaderBytecodeAL code( bytecode, size );
	return SUCCEEDED( shader.Create( type, code, signature, "", renderContext ) );
}

bool CreateProgram( Tr2PrimaryRenderContextAL& renderContext, const void* vsBytecode,
	size_t vsSize, const Tr2ShaderSignatureAL& vsSignature, const void* psBytecode,
	size_t psSize, const Tr2ShaderSignatureAL& psSignature, Tr2ShaderProgramAL& program,
	Tr2ShaderAL& vertexShader, Tr2ShaderAL& pixelShader )
{
	if( !CreateShader( vertexShader, Tr2RenderContextEnum::VERTEX_SHADER,
		vsBytecode, vsSize, vsSignature, renderContext ) )
	{
		std::fprintf( stderr, "Vertex shader creation failed\n" );
		return false;
	}
	if( !CreateShader( pixelShader, Tr2RenderContextEnum::PIXEL_SHADER,
		psBytecode, psSize, psSignature, renderContext ) )
	{
		std::fprintf( stderr, "Pixel shader creation failed\n" );
		return false;
	}
	Tr2ShaderAL shaders[] = { vertexShader, pixelShader };
	if( FAILED( program.Create( shaders, 2, renderContext ) ) )
	{
		std::fprintf( stderr, "Shader program creation failed\n" );
		return false;
	}
	return true;
}

void BuildProceduralTexture( uint8_t* pixels, uint32_t width, uint32_t height )
{
	for( uint32_t y = 0; y < height; ++y )
	{
		for( uint32_t x = 0; x < width; ++x )
		{
			const bool panelEdge = ( x % 16 == 0 ) || ( y % 16 == 0 ) ||
				( x % 16 == 15 ) || ( y % 16 == 15 );
			const bool bolt = ( ( x % 16 == 4 || x % 16 == 11 ) &&
				( y % 16 == 4 || y % 16 == 11 ) );
			uint8_t r = 64, g = 72, b = 96, a = 255;
			if( panelEdge )
			{
				r = 120;
				g = 132;
				b = 168;
			}
			if( bolt )
			{
				r = 40;
				g = 44;
				b = 60;
			}
			const uint32_t index = ( y * width + x ) * 4;
			pixels[index + 0] = r;
			pixels[index + 1] = g;
			pixels[index + 2] = b;
			pixels[index + 3] = a;
		}
	}
}

// Builds the procedural ship mesh through Carbon Mesh (CMF) and copies the
// interleaved vertex/index data out of the CMF buffer manager.
bool BuildShipMeshFromCarbonMesh( std::vector<Vertex>& vertices, std::vector<uint16_t>& indices )
{
	cmf::MemoryAllocator allocator;
	cmf::BufferManager buffers( allocator );

	const uint32_t vertexStride = static_cast<uint32_t>( sizeof( Vertex ) );
	cmf::BufferView vertexBuffer = buffers.AllocateBuffer(
		SHIP_VERTICES, static_cast<uint32_t>( sizeof( SHIP_VERTICES ) ), vertexStride );
	cmf::BufferView indexBuffer = buffers.AllocateBuffer(
		SHIP_INDICES, static_cast<uint32_t>( sizeof( SHIP_INDICES ) ), sizeof( uint16_t ) );

	cmf::Mesh mesh;
	{
		auto decl = cmf::Modify( mesh.decl, allocator );
		cmf::VertexElement position = {};
		position.usage = cmf::Usage::Position;
		position.type = cmf::ElementType::Float32;
		position.elementCount = 3;
		position.offset = 0;
		decl.push_back( position );

		cmf::VertexElement color = {};
		color.usage = cmf::Usage::Color;
		color.type = cmf::ElementType::Float32;
		color.elementCount = 4;
		color.offset = 12;
		decl.push_back( color );

		cmf::VertexElement uv = {};
		uv.usage = cmf::Usage::TexCoord;
		uv.type = cmf::ElementType::Float32;
		uv.elementCount = 2;
		uv.offset = 28;
		decl.push_back( uv );
	}

	cmf::MeshLod lod;
	lod.vb = vertexBuffer;
	lod.ib = indexBuffer;
	cmf::Modify( mesh.lods, allocator ).push_back( lod );

	mesh.name = allocator.AllocateString( "ithax_ship" );

	const CcpMath::AxisAlignedBox bounds = cmf::CalculateBounds( mesh, buffers );
	std::printf( "Carbon Mesh ship '%s': %u vertices, %u indices, bounds "
		"[%.2f..%.2f x %.2f..%.2f x %.2f..%.2f]\n",
		cmf::ToStdString( mesh.name ).c_str(),
		static_cast<uint32_t>( sizeof( SHIP_VERTICES ) / vertexStride ),
		static_cast<uint32_t>( sizeof( SHIP_INDICES ) / sizeof( uint16_t ) ),
		bounds.m_min.x, bounds.m_max.x, bounds.m_min.y, bounds.m_max.y,
		bounds.m_min.z, bounds.m_max.z );

	vertices.resize( sizeof( SHIP_VERTICES ) / vertexStride );
	indices.resize( sizeof( SHIP_INDICES ) / sizeof( uint16_t ) );
	std::memcpy( vertices.data(), buffers.GetData( vertexBuffer ),
		sizeof( SHIP_VERTICES ) );
	std::memcpy( indices.data(), buffers.GetData( indexBuffer ),
		sizeof( SHIP_INDICES ) );
	return true;
}

int RunScene( HWND window, Tr2PrimaryRenderContextAL& renderContext )
{
	Tr2PresentParametersAL presentParameters = {};
	presentParameters.mode.width = WINDOW_WIDTH;
	presentParameters.mode.height = WINDOW_HEIGHT;
	presentParameters.mode.format = Tr2RenderContextEnum::PIXEL_FORMAT_B8G8R8A8_UNORM;
	presentParameters.backBufferCount = 1;
	presentParameters.msaaType = 1;
	presentParameters.msaaQuality = 0;
	presentParameters.swapEffect = Tr2RenderContextEnum::SWAP_EFFECT_DISCARD;
	presentParameters.outputWindow = window;
	presentParameters.windowed = true;
	presentParameters.software = false;
	presentParameters.presentInterval = Tr2RenderContextEnum::PRESENT_INTERVAL_IMMEDIATE;

	ALResult result = renderContext.CreateDevice( 0, window, presentParameters );
	if( FAILED( result ) )
	{
		std::fprintf( stderr, "CreateDevice failed: 0x%08x\n", unsigned( result.GetResult() ) );
		return 1;
	}

	// Shaders (SPIR-V embedded via generated headers).
#include "Textured_vs.h"
#include "Textured_ps.h"
#include "Starfield_vs.h"
#include "Starfield_ps.h"

	// Textured program: pos/color/uv vertices + procedural texture.
	Tr2ShaderSignatureAL texturedVsSignature;
	texturedVsSignature.Add( Tr2VertexDefinition::POSITION, 0, 0, Tr2ShaderPipelineInputAL::FLOAT, 3 );
	texturedVsSignature.Add( Tr2VertexDefinition::COLOR, 0, 1, Tr2ShaderPipelineInputAL::FLOAT, 4 );
	texturedVsSignature.Add( Tr2VertexDefinition::TEXCOORD, 0, 2, Tr2ShaderPipelineInputAL::FLOAT, 2 );
	texturedVsSignature.Add( Tr2ShaderRegisterAL::CONSTANT_BUFFER, 0 );

	Tr2ShaderSignatureAL texturedPsSignature;
	texturedPsSignature.Add( Tr2ShaderRegisterAL::SRV_TEXTURE2D, 0 );
	texturedPsSignature.Add( Tr2ShaderRegisterAL::SAMPLER, 0 );

	Tr2ShaderProgramAL texturedProgram;
	Tr2ShaderAL texturedVs;
	Tr2ShaderAL texturedPs;
	if( !CreateProgram( renderContext, g_Textured_vs, g_Textured_vs_size, texturedVsSignature,
		g_Textured_ps, g_Textured_ps_size, texturedPsSignature, texturedProgram,
		texturedVs, texturedPs ) )
	{
		return 1;
	}

	// Starfield program: position-only points.
	Tr2ShaderSignatureAL starVsSignature;
	starVsSignature.Add( Tr2VertexDefinition::POSITION, 0, 0, Tr2ShaderPipelineInputAL::FLOAT, 3 );
	starVsSignature.Add( Tr2ShaderRegisterAL::CONSTANT_BUFFER, 0 );

	Tr2ShaderProgramAL starProgram;
	Tr2ShaderAL starVs;
	Tr2ShaderAL starPs;
	if( !CreateProgram( renderContext, g_Starfield_vs, g_Starfield_vs_size, starVsSignature,
		g_Starfield_ps, g_Starfield_ps_size, Tr2ShaderSignatureAL(), starProgram,
		starVs, starPs ) )
	{
		return 1;
	}

	// Vertex layouts.
	Tr2VertexDefinition texturedDefinition;
	texturedDefinition.Add( Tr2VertexDefinition::FLOAT32_3, Tr2VertexDefinition::POSITION, 0, 0, 0 );
	texturedDefinition.Add( Tr2VertexDefinition::FLOAT32_4, Tr2VertexDefinition::COLOR, 0, 0, 0 );
	texturedDefinition.Add( Tr2VertexDefinition::FLOAT32_2, Tr2VertexDefinition::TEXCOORD, 0, 0, 0 );

	Tr2VertexLayoutAL texturedLayout;
	if( FAILED( texturedLayout.Create( texturedDefinition, renderContext ) ) )
	{
		std::fprintf( stderr, "Textured vertex layout creation failed\n" );
		return 1;
	}

	Tr2VertexDefinition starDefinition;
	starDefinition.Add( Tr2VertexDefinition::FLOAT32_3, Tr2VertexDefinition::POSITION, 0, 0, 0 );

	Tr2VertexLayoutAL starLayout;
	if( FAILED( starLayout.Create( starDefinition, renderContext ) ) )
	{
		std::fprintf( stderr, "Star vertex layout creation failed\n" );
		return 1;
	}

	// Geometry buffers (contents uploaded through the render graph on
	// frame 0).
	Tr2BufferAL triangleVertexBuffer;
	Tr2BufferAL triangleIndexBuffer;
	Tr2BufferAL shipVertexBuffer;
	Tr2BufferAL shipIndexBuffer;
	Tr2BufferAL starVertexBuffer;

	if( FAILED( triangleVertexBuffer.Create( sizeof( Vertex ), 3, Tr2GpuUsage::VERTEX_BUFFER,
		Tr2CpuUsage::NONE, nullptr, renderContext ) ) )
	{
		std::fprintf( stderr, "Triangle vertex buffer creation failed\n" );
		return 1;
	}
	if( FAILED( triangleIndexBuffer.Create( sizeof( uint16_t ), 3, Tr2GpuUsage::INDEX_BUFFER,
		Tr2CpuUsage::NONE, nullptr, renderContext ) ) )
	{
		std::fprintf( stderr, "Triangle index buffer creation failed\n" );
		return 1;
	}

	std::vector<Vertex> shipVertices;
	std::vector<uint16_t> shipIndices;
	if( !BuildShipMeshFromCarbonMesh( shipVertices, shipIndices ) )
	{
		return 1;
	}
	if( FAILED( shipVertexBuffer.Create( sizeof( Vertex ), static_cast<uint32_t>( shipVertices.size() ),
		Tr2GpuUsage::VERTEX_BUFFER, Tr2CpuUsage::NONE, nullptr, renderContext ) ) )
	{
		std::fprintf( stderr, "Ship vertex buffer creation failed\n" );
		return 1;
	}
	if( FAILED( shipIndexBuffer.Create( sizeof( uint16_t ), static_cast<uint32_t>( shipIndices.size() ),
		Tr2GpuUsage::INDEX_BUFFER, Tr2CpuUsage::NONE, nullptr, renderContext ) ) )
	{
		std::fprintf( stderr, "Ship index buffer creation failed\n" );
		return 1;
	}

	struct Star
	{
		float position[3];
	};
	std::vector<Star> stars( STAR_COUNT );
	{
		uint32_t state = 0x5eedu;
		for( uint32_t i = 0; i < STAR_COUNT; ++i )
		{
			auto nextRandom = [&state]() {
				state = state * 1664525u + 1013904223u;
				return static_cast<float>( ( state >> 8 ) & 0xffffff ) / 16777216.0f;
			};
			stars[i].position[0] = nextRandom() * 8.0f - 4.0f;
			stars[i].position[1] = nextRandom() * 8.0f - 4.0f;
			stars[i].position[2] = -( nextRandom() * 12.0f + 2.0f );
		}
	}
	if( FAILED( starVertexBuffer.Create( sizeof( Star ), STAR_COUNT,
		Tr2GpuUsage::VERTEX_BUFFER, Tr2CpuUsage::NONE, nullptr, renderContext ) ) )
	{
		std::fprintf( stderr, "Star vertex buffer creation failed\n" );
		return 1;
	}

	// Procedural texture (contents uploaded through the graph on frame 0).
	std::vector<uint8_t> texturePixels( PROCTEX_WIDTH * PROCTEX_HEIGHT * 4 );
	BuildProceduralTexture( texturePixels.data(), PROCTEX_WIDTH, PROCTEX_HEIGHT );

	Tr2BitmapDimensions textureDimensions(
		Tr2RenderContextEnum::TEX_TYPE_2D,
		Tr2RenderContextEnum::PIXEL_FORMAT_R8G8B8A8_UNORM,
		PROCTEX_WIDTH, PROCTEX_HEIGHT, 1, 1 );

	Tr2TextureAL proceduralTexture;
	if( FAILED( proceduralTexture.Create( textureDimensions,
		Tr2GpuUsage::SHADER_RESOURCE | Tr2GpuUsage::COPY_DESTINATION,
		Tr2CpuUsage::NONE, nullptr, renderContext ) ) )
	{
		std::fprintf( stderr, "Procedural texture creation failed\n" );
		return 1;
	}

	// Linear sampler.
	Tr2SamplerStateAL linearSampler;
	const float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	Tr2SamplerDescription samplerDescription(
		Tr2RenderContextEnum::TF_LINEAR, Tr2RenderContextEnum::TF_LINEAR,
		Tr2RenderContextEnum::TF_LINEAR, false,
		Tr2RenderContextEnum::TA_WRAP, Tr2RenderContextEnum::TA_WRAP,
		Tr2RenderContextEnum::TA_WRAP, 0.0f, 1,
		Tr2RenderContextEnum::CMP_ALWAYS, borderColor, 0.0f, 100.0f );
	if( FAILED( linearSampler.Create( samplerDescription, renderContext ) ) )
	{
		std::fprintf( stderr, "Sampler creation failed\n" );
		return 1;
	}

	// Resource sets: one per drawable, each with its own constant buffer
	// baked into the UBO descriptor at setup. Per-frame data flows through
	// the host-visible constant buffer memory, so no descriptor set is ever
	// updated while bound.
	Tr2ResourceSetDescriptionAL texturedDescription( texturedProgram );
	texturedDescription.SetSrv( Tr2RenderContextEnum::PIXEL_SHADER, 0, proceduralTexture,
		Tr2RenderContextEnum::COLOR_SPACE_LINEAR );
	texturedDescription.SetSampler( Tr2RenderContextEnum::PIXEL_SHADER, 0, linearSampler );
	Tr2ResourceSetAL triangleSet;
	Tr2ResourceSetAL shipSet;
	if( FAILED( triangleSet.Create( texturedDescription, texturedProgram, renderContext ) ) ||
		FAILED( shipSet.Create( texturedDescription, texturedProgram, renderContext ) ) )
	{
		std::fprintf( stderr, "Textured resource set creation failed\n" );
		return 1;
	}

	Tr2ResourceSetDescriptionAL starDescription( starProgram );
	Tr2ResourceSetAL starSet;
	if( FAILED( starSet.Create( starDescription, starProgram, renderContext ) ) )
	{
		std::fprintf( stderr, "Star resource set creation failed\n" );
		return 1;
	}

	// Constant buffers: one mvp per draw.
	const uint32_t mvpSize = sizeof( float ) * 16;
	Tr2ConstantBufferAL starConstants;
	Tr2ConstantBufferAL triangleConstants;
	Tr2ConstantBufferAL shipConstants;
	if( FAILED( starConstants.Create( mvpSize, Tr2ConstantUsageAL::ONE_SHOT, nullptr, renderContext ) ) ||
		FAILED( triangleConstants.Create( mvpSize, Tr2ConstantUsageAL::ONE_SHOT, nullptr, renderContext ) ) ||
		FAILED( shipConstants.Create( mvpSize, Tr2ConstantUsageAL::ONE_SHOT, nullptr, renderContext ) ) )
	{
		std::fprintf( stderr, "Constant buffer creation failed\n" );
		return 1;
	}

	// Bake each constant buffer into its resource set once, before any
	// frame is recorded; per-frame updates go through host memory.
	RequireGraph( renderContext.SetSetConstantBuffer( starSet, starConstants ),
		"bake star constants" );
	RequireGraph( renderContext.SetSetConstantBuffer( triangleSet, triangleConstants ),
		"bake triangle constants" );
	RequireGraph( renderContext.SetSetConstantBuffer( shipSet, shipConstants ),
		"bake ship constants" );

	// Depth texture wrapper for the swapchain depth image. Re-fetched each
	// frame: the wrapper is re-attached when the swapchain is recreated.
	bool textureShaderReady = false;
	std::vector<double> presentedFrameTimesMilliseconds;
	presentedFrameTimesMilliseconds.reserve( FRAME_COUNT );

	auto frameStart = std::chrono::steady_clock::now();
	for( uint32_t frame = 0; frame < FRAME_COUNT; ++frame )
	{
		renderContext.BeginScene();

		const bool suspended = renderContext.GetVulkanContext().state.swapchain.suspended;
		if( suspended || !renderContext.GetDefaultBackBuffer().IsValid() )
		{
			// Minimized, or the swapchain was recreated by a resize and the
			// next acquired image has not been attached yet. Skip graph
			// recording (nothing is submitted while suspended / the
			// backbuffer is stale) and keep the frame pipeline alive.
			renderContext.EndScene();
			renderContext.Present();
			if( frame == 40 )
			{
				std::printf( "Frame %u: minimized, skipped graph recording\n", frame );
			}
			continue;
		}

		// Build the immutable frame packet.
		FramePacket packet = {};
		packet.starCount = STAR_COUNT;
		// Same clear semantics as the legacy Clear(0xff101018) call:
		// R=0x10, G=0x10, B=0x18 (the readback probe checks B=0x18).
		packet.clearColor[0] = 0x10 / 255.0f;
		packet.clearColor[1] = 0x10 / 255.0f;
		packet.clearColor[2] = 0x18 / 255.0f;
		packet.clearColor[3] = 1.0f;

		const float aspect = static_cast<float>( WINDOW_WIDTH ) / WINDOW_HEIGHT;
		const Mat4 projection = Mat4::Perspective( 1.1f, aspect, 0.1f, 100.0f );
		const float orbitAngle = static_cast<float>( frame ) * 0.02f;
		const float eye[3] = {
			std::sin( orbitAngle ) * 4.0f,
			1.4f,
			std::cos( orbitAngle ) * 4.0f,
		};
		const float center[3] = { 0.0f, 0.0f, 0.0f };
		const float up[3] = { 0.0f, 1.0f, 0.0f };
		const Mat4 view = Mat4::LookAt( eye, center, up );
		// Row-vector convention: clip = world * view * projection.
		const Mat4 viewProjection = Mat4::Mul( view, projection );

		packet.starMvp = viewProjection;
		packet.triangleMvp = Mat4::Mul(
			Mat4::Mul( Mat4::Translation( -1.2f, 0.0f, 0.0f ), view ),
			projection );
		packet.shipMvp = Mat4::Mul(
			Mat4::Mul(
				Mat4::Mul( Mat4::RotationZ( orbitAngle * 0.5f ),
					Mat4::Translation( 1.2f, 0.0f, 0.0f ) ),
				view ),
			projection );

	// Upload the packet to the constant buffers. HLSL mul(M, v) uses the
	// row-vector convention and reads the column-major cbuffer storage as
	// the standard matrix, so the CPU row-major matrices upload as-is.
	void* mapped = nullptr;
	if( FAILED( starConstants.Lock( &mapped, renderContext ) ) )
	{
		std::fprintf( stderr, "Star constants lock failed\n" );
		return 1;
	}
	std::memcpy( mapped, packet.starMvp.m, mvpSize );
	starConstants.Unlock( renderContext );

	if( FAILED( triangleConstants.Lock( &mapped, renderContext ) ) )
	{
		std::fprintf( stderr, "Triangle constants lock failed\n" );
		return 1;
	}
	std::memcpy( mapped, packet.triangleMvp.m, mvpSize );
	triangleConstants.Unlock( renderContext );

	if( FAILED( shipConstants.Lock( &mapped, renderContext ) ) )
	{
		std::fprintf( stderr, "Ship constants lock failed\n" );
		return 1;
	}
	std::memcpy( mapped, packet.shipMvp.m, mvpSize );
	shipConstants.Unlock( renderContext );

		// -----------------------------------------------------------------
		// Render graph declarations (5.8): per-frame, compiled on the CPU.
		// -----------------------------------------------------------------
		using TrinityALImpl::Tr2RenderGraphAL;
		Tr2RenderGraphAL graph;

		const Tr2RenderGraphAL::ResourceId backbufferResource = graph.AddImage( "backbuffer",
			[&]() {
				Tr2RenderGraphAL::ImageDesc desc = {};
				desc.width = WINDOW_WIDTH;
				desc.height = WINDOW_HEIGHT;
				desc.format = VK_FORMAT_B8G8R8A8_UNORM;
				// The backbuffer wrapper is re-attached every frame with
				// UNDEFINED; the starfield pass clears it, so contents are
				// never preserved across frames.
				desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				return desc;
			}() );
		const Tr2RenderGraphAL::ResourceId depthResource = graph.AddImage( "depth",
			[&]() {
				Tr2RenderGraphAL::ImageDesc desc = {};
				desc.width = WINDOW_WIDTH;
				desc.height = WINDOW_HEIGHT;
				desc.format = VK_FORMAT_D32_SFLOAT;
				desc.isDepth = true;
				// The scene clears depth every frame; the layout is
				// re-established from UNDEFINED after swapchain recreates.
				desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				return desc;
			}() );
		const Tr2RenderGraphAL::ResourceId textureResource = graph.AddImage( "procedural_texture",
			[&]() {
				Tr2RenderGraphAL::ImageDesc desc = {};
				desc.width = PROCTEX_WIDTH;
				desc.height = PROCTEX_HEIGHT;
				desc.format = VK_FORMAT_R8G8B8A8_UNORM;
				// After the first upload the wrapper tracks the shader
				// read layout; the declared initial layout must match.
				desc.initialLayout = textureShaderReady
					? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					: VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				return desc;
			}() );
		const Tr2RenderGraphAL::ResourceId starVertexResource = graph.AddBuffer( "star_vertices",
			Tr2RenderGraphAL::BufferDesc{ static_cast<uint32_t>( stars.size() * sizeof( Star ) ) } );
		const Tr2RenderGraphAL::ResourceId triangleVertexResource = graph.AddBuffer( "triangle_vertices",
			Tr2RenderGraphAL::BufferDesc{ sizeof( TRIANGLE_VERTICES ) } );
		const Tr2RenderGraphAL::ResourceId triangleIndexResource = graph.AddBuffer( "triangle_indices",
			Tr2RenderGraphAL::BufferDesc{ sizeof( TRIANGLE_INDICES ) } );
		const Tr2RenderGraphAL::ResourceId shipVertexResource = graph.AddBuffer( "ship_vertices",
			Tr2RenderGraphAL::BufferDesc{ static_cast<uint32_t>( shipVertices.size() * sizeof( Vertex ) ) } );
		const Tr2RenderGraphAL::ResourceId shipIndexResource = graph.AddBuffer( "ship_indices",
			Tr2RenderGraphAL::BufferDesc{ static_cast<uint32_t>( shipIndices.size() * sizeof( uint16_t ) ) } );

		const bool firstFrame = ( frame == 0 );
		Tr2RenderGraphAL::PassId uploadPass = TrinityALImpl::Tr2RenderGraphAL::INVALID_PASS;
		if( firstFrame )
		{
			uploadPass = graph.AddPass( "upload", TrinityALImpl::Tr2RenderGraphAL::Queue::GRAPHICS );
			RequireGraph( graph.PassWritesImage( uploadPass, textureResource,
				TrinityALImpl::Tr2RenderGraphAL::ImageAccess::TRANSFER_WRITE ), "upload writes texture" );
			RequireGraph( graph.PassWritesBuffer( uploadPass, starVertexResource,
				TrinityALImpl::Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE ), "upload writes stars" );
			RequireGraph( graph.PassWritesBuffer( uploadPass, triangleVertexResource,
				TrinityALImpl::Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE ), "upload writes triangle vertices" );
			RequireGraph( graph.PassWritesBuffer( uploadPass, triangleIndexResource,
				TrinityALImpl::Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE ), "upload writes triangle indices" );
			RequireGraph( graph.PassWritesBuffer( uploadPass, shipVertexResource,
				TrinityALImpl::Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE ), "upload writes ship vertices" );
			RequireGraph( graph.PassWritesBuffer( uploadPass, shipIndexResource,
				TrinityALImpl::Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE ), "upload writes ship indices" );
		}

		const Tr2RenderGraphAL::PassId starfieldPass = graph.AddPass( "starfield", Tr2RenderGraphAL::Queue::GRAPHICS );
		const Tr2RenderGraphAL::PassId scenePass = graph.AddPass( "scene", Tr2RenderGraphAL::Queue::GRAPHICS );

		// Starfield: clears the backbuffer and draws the star points.
		RequireGraph( graph.PassWritesImage( starfieldPass, backbufferResource,
			Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT ), "starfield writes backbuffer" );
		VkClearValue starClear = {};
		starClear.color.float32[0] = packet.clearColor[0];
		starClear.color.float32[1] = packet.clearColor[1];
		starClear.color.float32[2] = packet.clearColor[2];
		starClear.color.float32[3] = packet.clearColor[3];
		RequireGraph( graph.SetAttachmentClear( starfieldPass, backbufferResource,
			VK_ATTACHMENT_LOAD_OP_CLEAR, starClear ), "starfield clear" );
		RequireGraph( graph.PassReadsBuffer( starfieldPass, starVertexResource,
			Tr2RenderGraphAL::BufferAccess::VERTEX_READ ), "starfield reads stars" );

		// Scene: loads the backbuffer, clears depth, draws textured meshes.
		RequireGraph( graph.PassWritesImage( scenePass, backbufferResource,
			Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT ), "scene writes backbuffer" );
		RequireGraph( graph.PassWritesImage( scenePass, depthResource,
			Tr2RenderGraphAL::ImageAccess::DEPTH_ATTACHMENT ), "scene writes depth" );
		VkClearValue depthClear = {};
		depthClear.depthStencil.depth = 1.0f;
		RequireGraph( graph.SetAttachmentClear( scenePass, depthResource,
			VK_ATTACHMENT_LOAD_OP_CLEAR, depthClear ), "scene depth clear" );
		RequireGraph( graph.PassReadsImage( scenePass, textureResource,
			Tr2RenderGraphAL::ImageAccess::SHADER_READ ), "scene reads texture" );
		RequireGraph( graph.PassReadsBuffer( scenePass, triangleVertexResource,
			Tr2RenderGraphAL::BufferAccess::VERTEX_READ ), "scene reads triangle vertices" );
		RequireGraph( graph.PassReadsBuffer( scenePass, triangleIndexResource,
			Tr2RenderGraphAL::BufferAccess::INDEX_READ ), "scene reads triangle indices" );
		RequireGraph( graph.PassReadsBuffer( scenePass, shipVertexResource,
			Tr2RenderGraphAL::BufferAccess::VERTEX_READ ), "scene reads ship vertices" );
		RequireGraph( graph.PassReadsBuffer( scenePass, shipIndexResource,
			Tr2RenderGraphAL::BufferAccess::INDEX_READ ), "scene reads ship indices" );

		RequireGraph( graph.MarkPresented( backbufferResource ), "mark presented" );

		Tr2RenderGraphAL::CompileResult compiled;
		std::string errorMessage;
		RequireGraph( graph.Compile( compiled, errorMessage ), "graph compile" );
		if( !errorMessage.empty() )
		{
			std::fprintf( stderr, "Render graph compile error: %s\n", errorMessage.c_str() );
			return 1;
		}

		// Register the graph resources with the backend.
		Tr2TextureAL depthTexture = renderContext.GetDepthTexture();
		RequireGraph( renderContext.BeginGraphFrame(), "begin graph frame" );
		RequireGraph( renderContext.SetGraphResult( compiled ), "set graph result" );
		RequireGraph( renderContext.RegisterGraphTexture( backbufferResource,
			renderContext.GetDefaultBackBuffer(), Tr2RenderContextEnum::COLOR_SPACE_LINEAR ),
			"register backbuffer" );
		RequireGraph( renderContext.RegisterGraphTexture( depthResource,
			depthTexture, Tr2RenderContextEnum::COLOR_SPACE_LINEAR ),
			"register depth" );
		RequireGraph( renderContext.RegisterGraphTexture( textureResource,
			proceduralTexture, Tr2RenderContextEnum::COLOR_SPACE_LINEAR ),
			"register texture" );
		RequireGraph( renderContext.RegisterGraphBuffer( starVertexResource, starVertexBuffer ),
			"register star buffer" );
		RequireGraph( renderContext.RegisterGraphBuffer( triangleVertexResource, triangleVertexBuffer ),
			"register triangle vertex buffer" );
		RequireGraph( renderContext.RegisterGraphBuffer( triangleIndexResource, triangleIndexBuffer ),
			"register triangle index buffer" );
		RequireGraph( renderContext.RegisterGraphBuffer( shipVertexResource, shipVertexBuffer ),
			"register ship vertex buffer" );
		RequireGraph( renderContext.RegisterGraphBuffer( shipIndexResource, shipIndexBuffer ),
			"register ship index buffer" );

		// Record the passes in compiled execution order.
		const std::vector<uint32_t> executionOrder = [&compiled]() {
			std::vector<uint32_t> order;
			for( const auto& pass : compiled.passes )
			{
				if( !pass.culled )
				{
					order.push_back( pass.passId );
				}
			}
			return order;
		}();

		for( const uint32_t passId : executionOrder )
		{
			RequireGraph( renderContext.BeginGraphPass( passId ), "begin graph pass" );

			if( passId == uploadPass )
			{
				// Upload the geometry and texture contents (frame 0 only).
				Tr2TextureSubresource textureRegion( 0 );
				textureRegion.SetRect( 0, 0, PROCTEX_WIDTH, PROCTEX_HEIGHT );
				RequireGraph( proceduralTexture.UpdateSubresource( textureRegion,
					texturePixels.data(), PROCTEX_WIDTH * 4,
					PROCTEX_WIDTH * PROCTEX_HEIGHT * 4, renderContext ),
					"upload procedural texture" );
				RequireGraph( starVertexBuffer.UpdateBuffer( 0,
					static_cast<uint32_t>( stars.size() * sizeof( Star ) ),
					stars.data(), renderContext ), "upload star vertices" );
				RequireGraph( triangleVertexBuffer.UpdateBuffer( 0,
					sizeof( TRIANGLE_VERTICES ), TRIANGLE_VERTICES, renderContext ),
					"upload triangle vertices" );
				RequireGraph( triangleIndexBuffer.UpdateBuffer( 0,
					sizeof( TRIANGLE_INDICES ), TRIANGLE_INDICES, renderContext ),
					"upload triangle indices" );
				RequireGraph( shipVertexBuffer.UpdateBuffer( 0,
					static_cast<uint32_t>( shipVertices.size() * sizeof( Vertex ) ),
					shipVertices.data(), renderContext ), "upload ship vertices" );
				RequireGraph( shipIndexBuffer.UpdateBuffer( 0,
					static_cast<uint32_t>( shipIndices.size() * sizeof( uint16_t ) ),
					shipIndices.data(), renderContext ), "upload ship indices" );
			}
			else 			if( passId == starfieldPass )
			{
				renderContext.SetViewport( Tr2Viewport( WINDOW_WIDTH, WINDOW_HEIGHT ) );
				renderContext.SetTopology( Tr2RenderContextEnum::TOP_POINTS );
				renderContext.SetVertexLayout( starLayout );
				renderContext.SetShaderProgram( starProgram );
				renderContext.SetResourceSet( starSet );
				renderContext.SetStreamSource( 0, starVertexBuffer, 0, sizeof( Star ) );
				renderContext.DrawPrimitive( 0, packet.starCount );
			}
			else if( passId == scenePass )
			{
				renderContext.SetViewport( Tr2Viewport( WINDOW_WIDTH, WINDOW_HEIGHT ) );
				renderContext.SetTopology( Tr2RenderContextEnum::TOP_TRIANGLES );
				renderContext.SetVertexLayout( texturedLayout );
				renderContext.SetShaderProgram( texturedProgram );

				// Triangle (left side).
				renderContext.SetResourceSet( triangleSet );
				renderContext.SetStreamSource( 0, triangleVertexBuffer, 0, sizeof( Vertex ) );
				renderContext.SetIndices( triangleIndexBuffer, sizeof( uint16_t ) );
				renderContext.DrawIndexedPrimitive( 3, 0, 1 );

				// Procedural ship (right side).
				renderContext.SetResourceSet( shipSet );
				renderContext.SetStreamSource( 0, shipVertexBuffer, 0, sizeof( Vertex ) );
				renderContext.SetIndices( shipIndexBuffer, sizeof( uint16_t ) );
				renderContext.DrawIndexedPrimitive(
					static_cast<uint32_t>( shipVertices.size() ), 0,
					static_cast<uint32_t>( shipIndices.size() ) / 3 );
			}

			RequireGraph( renderContext.EndGraphPass(), "end graph pass" );
		}

		RequireGraph( renderContext.EndGraphFrame(), "end graph frame" );

		textureShaderReady = true;

		renderContext.EndScene();

		// Read back the first frame: record the copy into the frame's
		// command buffer before present, then wait and verify after.
		Tr2TextureAL* backBuffer = nullptr;
		const void* pixels = nullptr;
		uint32_t pitch = 0;
		if( frame == 0 )
		{
			ALResult attachResult = renderContext.AttachLastPresentedImage();
			if( FAILED( attachResult ) )
			{
				std::fprintf( stderr, "AttachLastPresentedImage failed: 0x%08x\n",
					unsigned( attachResult.GetResult() ) );
				return 1;
			}
			backBuffer = &renderContext.GetDefaultBackBuffer();
			Tr2TextureSubresource region( 0 );
			ALResult mapResult = backBuffer->MapForReading( region, true, pixels, pitch, renderContext );
			if( FAILED( mapResult ) )
			{
				std::fprintf( stderr, "Readback map failed: 0x%08x\n", unsigned( mapResult.GetResult() ) );
				return 1;
			}
		}

		renderContext.Present();

		if( frame == 0 )
		{
			ALResult waitResult = renderContext.WaitForFrameCompletion();
			if( FAILED( waitResult ) )
			{
				std::fprintf( stderr, "Frame completion wait failed: 0x%08x\n",
					unsigned( waitResult.GetResult() ) );
				return 1;
			}
			ALResult invalidateResult = backBuffer->InvalidateReadback( renderContext );
			if( FAILED( invalidateResult ) )
			{
				std::fprintf( stderr, "Readback invalidate failed: 0x%08x\n",
					unsigned( invalidateResult.GetResult() ) );
				return 1;
			}

			const uint8_t* row = static_cast<const uint8_t*>( pixels );
			const uint32_t width = backBuffer->GetWidth();
			const uint32_t height = backBuffer->GetHeight();

			// Count pixels that differ from the clear color to confirm
			// geometry was actually drawn anywhere in the frame. Note the
			// probe reads BGRA (Vulkan B8G8R8A8), so the clear color is
			// R=0x10 G=0x10 B=0x18 in these channels.
			uint32_t nonClearPixels = 0;
			for( uint32_t y = 0; y < height; y += 4 )
			{
				for( uint32_t x = 0; x < width; x += 4 )
				{
					const uint8_t* p = row + y * pitch + x * 4;
					if( p[0] != 0x18 || p[1] != 0x10 || p[2] != 0x10 )
					{
						++nonClearPixels;
					}
				}
			}
			std::printf( "Readback scan: %u non-clear sample pixels\n", nonClearPixels );

			// Top-center is above the triangle and the ship: clear color.
			const uint8_t* clearPixel = row + ( height / 8 ) * pitch + ( width / 2 ) * 4;
			const bool clearMatches =
				clearPixel[0] == 0x18 && clearPixel[1] == 0x10 && clearPixel[2] == 0x10;

			// Probe a small box around each expected draw location so a
			// single off-by-a-few-pixels miss does not fail the check.
			auto boxHasNonClear = [row, pitch, width, height]( uint32_t cx, uint32_t cy, uint32_t radius ) {
				for( uint32_t dy = 0; dy <= radius * 2; ++dy )
				{
					for( uint32_t dx = 0; dx <= radius * 2; ++dx )
					{
						const uint32_t x = cx + dx - radius;
						const uint32_t y = cy + dy - radius;
						if( x >= width || y >= height )
						{
							continue;
						}
						const uint8_t* p = row + y * pitch + x * 4;
						if( p[0] != 0x18 || p[1] != 0x10 || p[2] != 0x10 )
						{
							return true;
						}
					}
				}
				return false;
			};

			// Triangle: left of center (world x = -1.2).
			const bool triangleDiffers = boxHasNonClear( width * 5 / 16, height * 3 / 5, 16 );
			// Ship: right of center (world x = +1.2).
			const bool shipDiffers = boxHasNonClear( width * 11 / 16, height * 11 / 20, 16 );

			backBuffer->UnmapForReading( renderContext );

			std::printf( "Readback probes: clear=%02x%02x%02x triangle=%s ship=%s\n",
				clearPixel[2], clearPixel[1], clearPixel[0],
				triangleDiffers ? "hit" : "miss",
				shipDiffers ? "hit" : "miss" );

			if( !clearMatches )
			{
				std::fprintf( stderr, "Readback mismatch: clear pixel = %02x%02x%02x (expected 181010)\n",
					clearPixel[2], clearPixel[1], clearPixel[0] );
				return 1;
			}
			if( nonClearPixels == 0 )
			{
				std::fprintf( stderr, "Readback mismatch: no geometry drawn in frame\n" );
				return 1;
			}
			if( !triangleDiffers )
			{
				std::fprintf( stderr, "Readback mismatch: triangle pixel equals clear color\n" );
				return 1;
			}
			if( !shipDiffers )
			{
				std::fprintf( stderr, "Readback mismatch: ship pixel equals clear color\n" );
				return 1;
			}
			std::printf( "Readback verified: clear color, triangle, and ship pixels correct\n" );
		}

		// Resize the swapchain mid-run (frame 20) and minimize (frame 40).
		if( frame == 20 )
		{
			presentParameters.mode.width = 640;
			presentParameters.mode.height = 480;
			ALResult resizeResult = renderContext.SetPresentParameters( 0, presentParameters );
			if( FAILED( resizeResult ) )
			{
				std::fprintf( stderr, "SetPresentParameters (resize) failed: 0x%08x\n",
					unsigned( resizeResult.GetResult() ) );
				return 1;
			}
			// The swapchain was recreated; the next frame sees an invalid
			// backbuffer wrapper and skips graph recording until Present()
			// re-attaches the newly acquired image.
		}
		if( frame == 40 )
		{
			ShowWindow( window, SW_MINIMIZE );
			// Pump the message queue so the minimize takes effect.
			MSG message = {};
			while( PeekMessageA( &message, nullptr, 0, 0, PM_REMOVE ) )
			{
				TranslateMessage( &message );
				DispatchMessageA( &message );
			}
		}
		if( frame == 50 )
		{
			ShowWindow( window, SW_RESTORE );
			MSG message = {};
			while( PeekMessageA( &message, nullptr, 0, 0, PM_REMOVE ) )
			{
				TranslateMessage( &message );
				DispatchMessageA( &message );
			}
		}

		const auto frameEnd = std::chrono::steady_clock::now();
		const double frameTime = std::chrono::duration<double, std::milli>(
			frameEnd - frameStart ).count();
		if( !suspended )
		{
			presentedFrameTimesMilliseconds.push_back( frameTime );
		}
		frameStart = frameEnd;
	}

	// Surface-loss recovery is exercised on the last frame: destroy the
	// swapchain and surface through the deferred path used on present.
	{
		ALResult recoverResult = renderContext.GetVulkanContext().RecreateSurface();
		if( FAILED( recoverResult ) )
		{
			std::fprintf( stderr, "Surface recovery failed: 0x%08x\n",
				unsigned( recoverResult.GetResult() ) );
			return 1;
		}
		std::printf( "Surface recovery verified: swapchain and surface recreated\n" );
	}

	// Performance evidence (5.9 gate): named hardware, driver, resolution,
	// present mode, and p50/p95/p99 frame times. driverVersion is not
	// guaranteed to be MAJOR/MINOR/PATCH encoded (NVIDIA packs it), so the
	// raw 32-bit value is reported.
	const VkPhysicalDeviceProperties& deviceProperties =
		renderContext.GetVulkanContext().state.properties;
	std::printf( "Stage 5 perf: hardware=\"%s\" driverVersion=0x%08x "
		"resolution=%ux%u presentMode=%d apiVersion=%u.%u.%u frames=%zu\n",
		deviceProperties.deviceName,
		deviceProperties.driverVersion,
		WINDOW_WIDTH, WINDOW_HEIGHT,
		int( renderContext.GetVulkanContext().state.presentMode ),
		VK_API_VERSION_MAJOR( deviceProperties.apiVersion ),
		VK_API_VERSION_MINOR( deviceProperties.apiVersion ),
		VK_API_VERSION_PATCH( deviceProperties.apiVersion ),
		presentedFrameTimesMilliseconds.size() );
	std::printf( "Stage 5 perf: frameTimeMs p50=%.3f p95=%.3f p99=%.3f\n",
		Percentile( presentedFrameTimesMilliseconds, 0.50 ),
		Percentile( presentedFrameTimesMilliseconds, 0.95 ),
		Percentile( presentedFrameTimesMilliseconds, 0.99 ) );

	return 0;
}

}

int main()
{
	HWND window = CreateWindowHandle();
	if( window == nullptr )
	{
		std::fprintf( stderr, "Window creation failed\n" );
		return 1;
	}

	Tr2PrimaryRenderContextAL renderContext;
	Tr2PrimaryRenderContextAL::SetPrimaryRenderContext( &renderContext );

	SceneEvents events;
	renderContext.m_events = &events;

	int exitCode = RunScene( window, renderContext );

	DestroyDeviceResources( AL_MEMORY_VIDEO | AL_MEMORY_MANAGED );
	renderContext.Destroy();
	Tr2PrimaryRenderContextAL::SetPrimaryRenderContext( nullptr );
	DestroyWindow( window );

	if( exitCode == 0 )
	{
		std::printf( "Stage 5 milestone scene: %u frames presented successfully\n", FRAME_COUNT );
	}
	return exitCode;
}
