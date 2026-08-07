#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <ccpparser.h>

namespace
{

constexpr float EXPECTED_VALUE = 14.0F;
constexpr float VALUE_TOLERANCE = 0.0001F;
constexpr int FAILURE_EXIT_CODE = 1;
constexpr char EXPRESSION[] = "2 + 3 * 4";

}  // namespace

int main()
{
    CcpParser::Program program;
    const CcpParser::ParseResult result =
        CcpParser::Parse(EXPRESSION, {}, program);
    if (!result || !program.IsValid())
    {
        std::fprintf(stderr,
                     "{\"event\":\"parser_native_smoke\","
                     "\"status\":\"fail\","
                     "\"error\":\"expression parse failed\","
                     "\"type\":%d}\n",
                     static_cast<int>(result.type));
        return FAILURE_EXIT_CODE;
    }

    std::vector<std::uint8_t> temp_arena(program.GetTempArenaSize());
    const float value = program.Eval(nullptr, temp_arena.data());
    if (value < EXPECTED_VALUE - VALUE_TOLERANCE ||
        value > EXPECTED_VALUE + VALUE_TOLERANCE)
    {
        std::fprintf(stderr,
                     "{\"event\":\"parser_native_smoke\","
                     "\"status\":\"fail\","
                     "\"error\":\"unexpected evaluation\","
                     "\"value\":%.5f}\n",
                     value);
        return FAILURE_EXIT_CODE;
    }

    std::puts("{\"event\":\"parser_native_smoke\",\"status\":\"pass\"}");
    return EXIT_SUCCESS;
}
