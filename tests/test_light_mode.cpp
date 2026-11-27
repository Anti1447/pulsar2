#include "TestFramework.h"

#include "../GigaLearnCPP/src/public/GigaLearnCPP/Util/LightMode.h"

#include <chrono>
#include <fstream>

using GGL::LightMode;

TEST(LightMode_share_file_is_read_and_bounded) {
	auto file = std::filesystem::temp_directory_path() / "ggl_throttle_test";
	std::filesystem::remove(file);
	CHECK_NEAR(LightMode::ReadShare(file), 1.f, 0.f);
	for (auto [text, share] : { std::pair<const char*, float>{"0.25", 0.25f}, {"0.25\n", 0.25f}, {"2", 1.f}, {"0.001", 0.05f}, {"junk", 1.f}, {"", 1.f}, {"nan", 1.f} }) {
		std::ofstream(file) << text;
		CHECK_NEAR(LightMode::ReadShare(file), share, 1e-6f);
	}
	std::filesystem::remove(file);
}

TEST(LightMode_rest_matches_the_share_and_full_speed_never_sleeps) {
	CHECK_NEAR(LightMode::Rest(0.1, 0.25f), 0.3, 1e-9);
	CHECK_NEAR(LightMode::Rest(0.1, 0.5f), 0.1, 1e-9);
	CHECK_NEAR(LightMode::Rest(0.1, 1.f), 0.0, 0.0);
	CHECK_NEAR(LightMode::Rest(50.0, 0.25f), 3.0, 1e-9); // a stall is not made up for
	auto file = std::filesystem::temp_directory_path() / "ggl_throttle_pace_test";
	std::ofstream(file) << "0.25";
	LightMode mode;
	mode.Bind(file);
	std::chrono::steady_clock::time_point mark = {};
	int finished = 0;
	auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 10; i++) {
		mode.Pace(mark, [&] { finished++; });
		std::this_thread::sleep_for(std::chrono::milliseconds(10)); // the "work"
	}
	mode.Pace(mark, [&] { finished++; });
	double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	CHECK_EQ(finished, 11);
	CHECK(total > 0.36 && total < 0.60); // 10 x (10 ms work + 30 ms rest)
	std::filesystem::remove(file);
}
