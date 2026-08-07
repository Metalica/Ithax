// Copyright © 2026 Ithax contributors.

// Stage 5.7: Vulkan render graph compilation tests.
// Exercises the CPU-only graph compiler (pass order, dead-pass culling,
// resource lifetimes, image layouts, synchronization2 barriers) without
// creating a Vulkan device or recording commands. Exits 0 on success.

#include "vulkan/Tr2RenderGraphALVulkan.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using TrinityALImpl::Tr2RenderGraphAL;

class TestError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(bool condition, const char *message) {
  if (!condition) {
    throw TestError(message);
  }
}

void RequireSuccess(const ALResult &result, const char *message) {
  if (FAILED(result)) {
    throw TestError(message);
  }
}

void RequireFailed(const ALResult &result, const char *message) {
  if (SUCCEEDED(result)) {
    throw TestError(message);
  }
}

Tr2RenderGraphAL::ImageDesc MakeColorImage() {
  Tr2RenderGraphAL::ImageDesc desc = {};
  desc.width = 800;
  desc.height = 600;
  desc.format = VK_FORMAT_B8G8R8A8_UNORM;
  return desc;
}

Tr2RenderGraphAL::ImageDesc MakeDepthImage() {
  Tr2RenderGraphAL::ImageDesc desc = {};
  desc.width = 800;
  desc.height = 600;
  desc.format = VK_FORMAT_D32_SFLOAT;
  desc.isDepth = true;
  return desc;
}

Tr2RenderGraphAL::BufferDesc MakeBuffer(uint32_t sizeBytes) {
  Tr2RenderGraphAL::BufferDesc desc = {};
  desc.sizeBytes = sizeBytes;
  return desc;
}

int TestPassOrderAndBarriers() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId backbuffer = graph.AddImage("backbuffer", MakeColorImage());
  const Tr2RenderGraphAL::ResourceId vertexData = graph.AddBuffer("vertexData", MakeBuffer(4096));

  const Tr2RenderGraphAL::PassId upload = graph.AddPass("upload", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId geometry = graph.AddPass("geometry", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesBuffer(upload, vertexData,
                                        Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE),
                 "upload write buffer");
  RequireSuccess(graph.PassReadsBuffer(geometry, vertexData,
                                       Tr2RenderGraphAL::BufferAccess::VERTEX_READ),
                 "geometry read buffer");
  RequireSuccess(graph.PassWritesImage(geometry, backbuffer,
                                       Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "geometry write color");
  RequireSuccess(graph.MarkPresented(backbuffer), "mark presented");

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireSuccess(graph.Compile(compiled, errorMessage), "compile");
  Require(errorMessage.empty(), "error message empty on success");

  Require(compiled.passes.size() == 2, "two passes compiled");
  Require(compiled.passes[upload].orderIndex < compiled.passes[geometry].orderIndex,
          "upload ordered before geometry");
  Require(!compiled.passes[upload].culled && !compiled.passes[geometry].culled,
          "no pass culled");

  bool hasVertexBarrier = false;
  bool hasPresentBarrier = false;
  bool hasFirstUseBarrier = false;
  for (const auto &barrier : compiled.imageBarriers) {
    if (barrier.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
      hasPresentBarrier = true;
      Require(barrier.beforePass == Tr2RenderGraphAL::INVALID_PASS,
              "present barrier is last");
    }
    if (barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        barrier.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      hasFirstUseBarrier = true;
      Require(barrier.beforePass == geometry, "first-use barrier before geometry");
      Require(barrier.srcAccess == 0, "undefined layout implies no src access");
      Require(barrier.dstStage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
              "dst stage is color attachment output");
    }
  }
  Require(hasPresentBarrier, "present transition emitted");
  Require(hasFirstUseBarrier, "first-use transition emitted");
  Require(compiled.imageBarriers.size() == 2, "exactly two image barriers");

  Require(compiled.bufferBarriers.size() == 1, "one buffer barrier");
  const auto &bufferBarrier = compiled.bufferBarriers[0];
  Require(bufferBarrier.beforePass == geometry, "buffer barrier before geometry");
  Require(bufferBarrier.srcStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT,
          "buffer barrier src stage is transfer");
  Require(bufferBarrier.dstStage == VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
          "buffer barrier dst stage is vertex input");

  return 0;
}

int TestReadReadNeedsNoBarrier() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId texture = graph.AddImage("texture", MakeColorImage());
  const Tr2RenderGraphAL::ResourceId backbuffer = graph.AddImage("backbuffer", MakeColorImage());

  const Tr2RenderGraphAL::PassId upload = graph.AddPass("upload", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId first = graph.AddPass("first", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId second = graph.AddPass("second", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesImage(upload, texture,
                                       Tr2RenderGraphAL::ImageAccess::TRANSFER_WRITE),
                 "upload writes texture");
  RequireSuccess(graph.PassReadsImage(first, texture,
                                      Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                 "first reads texture");
  RequireSuccess(graph.PassReadsImage(second, texture,
                                      Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                 "second reads texture");
  RequireSuccess(graph.PassWritesImage(second, backbuffer,
                                       Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "second writes backbuffer");
  RequireSuccess(graph.MarkPresented(backbuffer), "mark presented");

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireSuccess(graph.Compile(compiled, errorMessage), "compile");
  Require(compiled.passes[upload].orderIndex < compiled.passes[first].orderIndex,
          "upload ordered before first reader");
  Require(compiled.passes[first].orderIndex < compiled.passes[second].orderIndex,
          "no ordering edge between consecutive read-read uses");

  int textureBarriers = 0;
  for (const auto &barrier : compiled.imageBarriers) {
    if (barrier.resource == texture) {
      ++textureBarriers;
    }
  }
  // First use (UNDEFINED->TRANSFER_DST) and the transfer->shader-read
  // transition are one barrier each; the read-read pair needs none.
  Require(textureBarriers == 2, "no barrier between read-read uses");

  return 0;
}

int TestCycleDetection() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId first = graph.AddImage("first", MakeColorImage());
  const Tr2RenderGraphAL::ResourceId second = graph.AddImage("second", MakeColorImage());

  const Tr2RenderGraphAL::PassId a = graph.AddPass("a", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId b = graph.AddPass("b", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesImage(a, first, Tr2RenderGraphAL::ImageAccess::TRANSFER_WRITE),
                 "a writes first");
  RequireSuccess(graph.PassWritesImage(b, second, Tr2RenderGraphAL::ImageAccess::TRANSFER_WRITE),
                 "b writes second");
  RequireSuccess(graph.PassReadsImage(a, second, Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                 "a reads second");
  RequireSuccess(graph.PassReadsImage(b, first, Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                 "b reads first");

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireFailed(graph.Compile(compiled, errorMessage), "cycle rejected");
  Require(errorMessage.find("cycle") != std::string::npos,
          "cycle error message names the problem");

  return 0;
}

int TestSelfHazardDetection() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId image = graph.AddImage("image", MakeColorImage());
  const Tr2RenderGraphAL::PassId pass = graph.AddPass("pass", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesImage(pass, image, Tr2RenderGraphAL::ImageAccess::TRANSFER_WRITE),
                 "pass writes");
  RequireSuccess(graph.PassReadsImage(pass, image, Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                 "pass reads");

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireFailed(graph.Compile(compiled, errorMessage), "self-hazard rejected");

  return 0;
}

int TestDeadPassCulling() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId backbuffer = graph.AddImage("backbuffer", MakeColorImage());
  const Tr2RenderGraphAL::ResourceId scratch = graph.AddImage("scratch", MakeColorImage());

  const Tr2RenderGraphAL::PassId render = graph.AddPass("render", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId debug = graph.AddPass("debug", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesImage(render, backbuffer,
                                       Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "render writes backbuffer");
  RequireSuccess(graph.PassWritesImage(debug, scratch,
                                       Tr2RenderGraphAL::ImageAccess::TRANSFER_WRITE),
                 "debug writes scratch");
  RequireSuccess(graph.MarkPresented(backbuffer), "mark presented");
  graph.MarkPassCullable(debug);

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireSuccess(graph.Compile(compiled, errorMessage), "compile");
  Require(!compiled.passes[render].culled, "render survives");
  Require(compiled.passes[debug].culled, "orphan debug pass culled");

  return 0;
}

int TestCullableWriterOfConsumedResourceSurvives() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId backbuffer = graph.AddImage("backbuffer", MakeColorImage());
  const Tr2RenderGraphAL::ResourceId lighting = graph.AddImage("lighting", MakeColorImage());

  const Tr2RenderGraphAL::PassId bake = graph.AddPass("bake", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId compose = graph.AddPass("compose", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesImage(bake, lighting,
                                       Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "bake writes lighting");
  RequireSuccess(graph.PassReadsImage(compose, lighting,
                                      Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                 "compose reads lighting");
  RequireSuccess(graph.PassWritesImage(compose, backbuffer,
                                       Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "compose writes backbuffer");
  RequireSuccess(graph.MarkPresented(backbuffer), "mark presented");
  graph.MarkPassCullable(bake);

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireSuccess(graph.Compile(compiled, errorMessage), "compile");
  Require(!compiled.passes[bake].culled, "bake survives because compose consumes it");
  Require(compiled.passes[bake].orderIndex < compiled.passes[compose].orderIndex,
          "bake ordered before compose");

  return 0;
}

int TestCullableWriterOfPresentedImageSurvives() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId backbuffer = graph.AddImage("backbuffer", MakeColorImage());
  const Tr2RenderGraphAL::PassId pass = graph.AddPass("pass", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesImage(pass, backbuffer,
                                       Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "pass writes backbuffer");
  RequireSuccess(graph.MarkPresented(backbuffer), "mark presented");
  graph.MarkPassCullable(pass);

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireSuccess(graph.Compile(compiled, errorMessage), "compile");
  Require(!compiled.passes[pass].culled, "presented writer never culled");

  return 0;
}

int TestResourceLifetimes() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId color = graph.AddImage("color", MakeColorImage());
  const Tr2RenderGraphAL::ResourceId unused = graph.AddImage("unused", MakeColorImage());
  const Tr2RenderGraphAL::ResourceId vertexData = graph.AddBuffer("vertexData", MakeBuffer(1024));

  const Tr2RenderGraphAL::PassId p0 = graph.AddPass("p0", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId p1 = graph.AddPass("p1", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId p2 = graph.AddPass("p2", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesBuffer(p0, vertexData,
                                        Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE),
                 "p0 writes buffer");
  RequireSuccess(graph.PassWritesImage(p0, color, Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "p0 writes color");
  RequireSuccess(graph.PassReadsBuffer(p1, vertexData,
                                       Tr2RenderGraphAL::BufferAccess::VERTEX_READ),
                 "p1 reads buffer");
  RequireSuccess(graph.PassReadsImage(p1, color, Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                 "p1 reads color");
  RequireSuccess(graph.PassWritesImage(p2, color, Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "p2 writes color");

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireSuccess(graph.Compile(compiled, errorMessage), "compile");
  Require(compiled.lifetimes.size() == 3, "one lifetime per resource");

  const Tr2RenderGraphAL::ResourceLifetime *colorLifetime = nullptr;
  const Tr2RenderGraphAL::ResourceLifetime *unusedLifetime = nullptr;
  const Tr2RenderGraphAL::ResourceLifetime *bufferLifetime = nullptr;
  for (const auto &lifetime : compiled.lifetimes) {
    if (lifetime.resource == color) {
      colorLifetime = &lifetime;
    } else if (lifetime.resource == unused) {
      unusedLifetime = &lifetime;
    } else if (lifetime.resource == vertexData) {
      bufferLifetime = &lifetime;
    }
  }
  Require(colorLifetime != nullptr && unusedLifetime != nullptr &&
              bufferLifetime != nullptr,
          "all resources reported");
  Require(colorLifetime->used && colorLifetime->firstPass == p0 &&
              colorLifetime->lastPass == p2,
          "color lifetime spans p0..p2");
  Require(!unusedLifetime->used, "unused resource reported unused");
  Require(bufferLifetime->used && bufferLifetime->firstPass == p0 &&
              bufferLifetime->lastPass == p1,
          "buffer lifetime spans p0..p1");

  return 0;
}

int TestMilestoneStyleGraph() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId backbuffer = graph.AddImage("backbuffer", MakeColorImage());
  const Tr2RenderGraphAL::ResourceId depth = graph.AddImage("depth", MakeDepthImage());
  const Tr2RenderGraphAL::ResourceId vertexData = graph.AddBuffer("vertexData", MakeBuffer(4096));
  const Tr2RenderGraphAL::ResourceId indexData = graph.AddBuffer("indexData", MakeBuffer(2048));

  const Tr2RenderGraphAL::PassId upload = graph.AddPass("upload", Tr2RenderGraphAL::Queue::GRAPHICS);
  const Tr2RenderGraphAL::PassId scene = graph.AddPass("scene", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassWritesBuffer(upload, vertexData,
                                        Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE),
                 "upload writes vertexData");
  RequireSuccess(graph.PassWritesBuffer(upload, indexData,
                                        Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE),
                 "upload writes indexData");
  RequireSuccess(graph.PassReadsBuffer(scene, vertexData,
                                       Tr2RenderGraphAL::BufferAccess::VERTEX_READ),
                 "scene reads vertexData");
  RequireSuccess(graph.PassReadsBuffer(scene, indexData,
                                       Tr2RenderGraphAL::BufferAccess::INDEX_READ),
                 "scene reads indexData");
  RequireSuccess(graph.PassWritesImage(scene, backbuffer,
                                       Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT),
                 "scene writes backbuffer");
  RequireSuccess(graph.PassWritesImage(scene, depth,
                                       Tr2RenderGraphAL::ImageAccess::DEPTH_ATTACHMENT),
                 "scene writes depth");
  RequireSuccess(graph.MarkPresented(backbuffer), "mark presented");

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireSuccess(graph.Compile(compiled, errorMessage), "compile");
  Require(compiled.passes[upload].orderIndex < compiled.passes[scene].orderIndex,
          "upload before scene");
  Require(compiled.bufferBarriers.size() == 2, "two buffer barriers");

  int depthBarrierCount = 0;
  for (const auto &barrier : compiled.imageBarriers) {
    if (barrier.resource == depth) {
      ++depthBarrierCount;
      Require(barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED,
              "depth transitions from undefined");
      Require(barrier.newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              "depth transitions to attachment optimal");
      Require(barrier.dstStage ==
                  (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT),
              "depth dst stage is fragment tests");
    }
  }
  Require(depthBarrierCount == 1, "one depth barrier");

  return 0;
}

int TestInvalidDeclarations() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId image = graph.AddImage("image", MakeColorImage());
  const Tr2RenderGraphAL::PassId pass = graph.AddPass("pass", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireFailed(graph.PassReadsImage(pass, 999, Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                "unknown resource rejected");
  RequireFailed(graph.PassReadsImage(999, image, Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                "unknown pass rejected");
  RequireFailed(graph.PassWritesBuffer(pass, image,
                                       Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE),
                "image used as buffer rejected");
  RequireFailed(graph.MarkPresented(999), "unknown presented image rejected");

  return 0;
}

int TestReadBeforeFirstWriteRejected() {
  Tr2RenderGraphAL graph;
  const Tr2RenderGraphAL::ResourceId image = graph.AddImage("image", MakeColorImage());
  const Tr2RenderGraphAL::PassId pass = graph.AddPass("pass", Tr2RenderGraphAL::Queue::GRAPHICS);

  RequireSuccess(graph.PassReadsImage(pass, image, Tr2RenderGraphAL::ImageAccess::SHADER_READ),
                 "pass reads image");
  RequireSuccess(graph.PassWritesImage(pass, image, Tr2RenderGraphAL::ImageAccess::TRANSFER_WRITE),
                 "pass writes image");

  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireFailed(graph.Compile(compiled, errorMessage), "read-before-write rejected");
  Require(errorMessage.find("read") != std::string::npos,
          "error message names the read");

  return 0;
}

int TestEmptyGraphRejected() {
  Tr2RenderGraphAL graph;
  Tr2RenderGraphAL::CompileResult compiled;
  std::string errorMessage;
  RequireFailed(graph.Compile(compiled, errorMessage), "empty graph rejected");
  Require(!errorMessage.empty(), "error message explains failure");

  return 0;
}

const char *kTestNames[] = {
    "pass_order_and_barriers",
    "read_read_needs_no_barrier",
    "cycle_detection",
    "self_hazard_detection",
    "dead_pass_culling",
    "cullable_writer_of_consumed_resource_survives",
    "cullable_writer_of_presented_image_survives",
    "resource_lifetimes",
    "milestone_style_graph",
    "invalid_declarations",
    "read_before_first_write_rejected",
    "empty_graph_rejected",
};

}  // namespace

int main() {
  using TestFn = int (*)();
  const TestFn kTestFunctions[] = {
      &TestPassOrderAndBarriers,
      &TestReadReadNeedsNoBarrier,
      &TestCycleDetection,
      &TestSelfHazardDetection,
      &TestDeadPassCulling,
      &TestCullableWriterOfConsumedResourceSurvives,
      &TestCullableWriterOfPresentedImageSurvives,
      &TestResourceLifetimes,
      &TestMilestoneStyleGraph,
      &TestInvalidDeclarations,
      &TestReadBeforeFirstWriteRejected,
      &TestEmptyGraphRejected,
  };
  constexpr uint32_t kTestCount = sizeof(kTestFunctions) / sizeof(kTestFunctions[0]);

  uint32_t failures = 0;
  for (uint32_t i = 0; i < kTestCount; ++i) {
    try {
      const int result = kTestFunctions[i]();
      if (result == 0) {
        std::printf("PASS %s\n", kTestNames[i]);
      } else {
        std::printf("FAIL %s (exit %d)\n", kTestNames[i], result);
        ++failures;
      }
    } catch (const std::exception &error) {
      std::printf("FAIL %s: %s\n", kTestNames[i], error.what());
      ++failures;
    }
  }

  if (failures == 0) {
    std::printf("Render graph compilation: %u/%u tests passed\n", kTestCount, kTestCount);
    return 0;
  }
  std::printf("Render graph compilation: %u/%u tests failed\n", failures, kTestCount);
  return 1;
}
