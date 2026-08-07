#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "CcpDefines.h"
#include "CcpTime.h"
#include "Vector3.h"

namespace
{
constexpr uint32_t EXPECTED_PROCESS_BIT_COUNT = 64U;
constexpr uint32_t POSITION_COUNT = 3U;
constexpr float EXPECTED_RADIUS = 1.0F;
constexpr float FLOAT_TOLERANCE = 0.0001F;

bool IsNear( const float actual, const float expected )
{
	return std::abs( actual - expected ) <= FLOAT_TOLERANCE;
}

bool RunFoundationSmoke()
{
	const Vector3 positions[POSITION_COUNT] = {
		Vector3( -1.0F, 0.0F, 0.0F ),
		Vector3( 1.0F, 0.0F, 0.0F ),
		Vector3( 0.0F, 0.0F, 0.0F ),
	};
	Vector3 center;
	float radius = 0.0F;

	ComputeBoundingSphere(
		positions,
		POSITION_COUNT,
		static_cast<uint32_t>( sizeof( Vector3 ) ),
		center,
		radius );

	const bool is_valid =
		CcpGetPlatformToolset() != nullptr &&
		CcpGetProcessBitCount() == EXPECTED_PROCESS_BIT_COUNT &&
		CcpGetTimestampFrequency() > 0U &&
		center == Vector3() &&
		IsNear( radius, EXPECTED_RADIUS );

	std::cout << "{\"event\":\"foundation_smoke\",\"status\":\""
			  << ( is_valid ? "pass" : "fail" )
			  << "\",\"process_bits\":" << CcpGetProcessBitCount()
			  << "}\n";
	return is_valid;
}
}

int main()
{
	return RunFoundationSmoke() ? EXIT_SUCCESS : EXIT_FAILURE;
}
