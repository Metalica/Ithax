#include <cstdio>
#include <cstdlib>

#include "carbongrpc/module/module.h"

namespace
{

constexpr int FAILURE_EXIT_CODE = 1;

}  // namespace

int main()
{
    if (!monolith_grpc::module::PublisherFinalize())
    {
        std::fprintf(stderr,
                     "{\"event\":\"grpc_native_smoke\","
                     "\"status\":\"fail\","
                     "\"error\":\"publisher finalization failed\"}\n");
        return FAILURE_EXIT_CODE;
    }

    std::puts("{\"event\":\"grpc_native_smoke\",\"status\":\"pass\"}");
    return EXIT_SUCCESS;
}
