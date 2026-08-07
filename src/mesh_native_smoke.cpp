#include <array>
#include <cstdio>

#include "cmf/bounds.h"
#include "cmf/memallocator.h"

namespace
{

constexpr uint32_t TRIANGLE_VERTEX_COUNT = 3;
constexpr uint32_t TRIANGLE_VERTEX_STRIDE = sizeof(Vector3);
constexpr uint8_t POSITION_COMPONENT_COUNT = 3;
constexpr int FAILURE_EXIT_CODE = 1;

bool HasExpectedBounds(const CcpMath::AxisAlignedBox& bounds)
{
    return bounds.m_min == Vector3(0.0f, 0.0f, 0.0f) &&
           bounds.m_max == Vector3(1.0f, 1.0f, 0.0f);
}

}  // namespace

int main()
{
    const std::array<Vector3, TRIANGLE_VERTEX_COUNT> vertices = {
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(1.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f),
    };
    cmf::MemoryAllocator allocator;
    cmf::BufferManager buffers(allocator);
    cmf::BufferView vertex_buffer = buffers.AllocateBuffer(
        vertices.data(), static_cast<uint32_t>(sizeof(vertices)),
        TRIANGLE_VERTEX_STRIDE);

    cmf::VertexElement position;
    position.usage = cmf::Usage::Position;
    position.elementCount = POSITION_COMPONENT_COUNT;
    cmf::MeshLod lod;
    lod.vb = vertex_buffer;
    cmf::Mesh mesh;
    cmf::Modify(mesh.decl, allocator).push_back(position);
    cmf::Modify(mesh.lods, allocator).push_back(lod);

    const CcpMath::AxisAlignedBox bounds =
        cmf::CalculateBounds(mesh, buffers);
    if (!HasExpectedBounds(bounds))
    {
        std::fprintf(stderr, "Carbon Mesh calculated unexpected bounds\n");
        return FAILURE_EXIT_CODE;
    }

    std::puts("{\"event\":\"mesh_native_smoke\",\"status\":\"pass\"}");
    return 0;
}
