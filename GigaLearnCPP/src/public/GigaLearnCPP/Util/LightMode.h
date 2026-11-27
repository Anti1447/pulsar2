#pragma once

// Light mode (2026-11-26, user-directed; the CPU-engine counterpart of the RocketGPU trial's pulsar/throttle.py): the trainer works
// for only part of the time so that a game or the desktop can use the GPU and CPU. The checkpoint folder's file THROTTLE holds the
// share of time the trainer may work (0.25 = work 1 part, rest 3); no file, or 1, is full speed. It is re-read every two seconds, so
// it can be switched while the run goes (tools/light.sh). Pace() is called between pieces of work (each rollout step, each learn
// minibatch): it sleeps in proportion to the time the piece took. What is learned does not change.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace GGL {
	class LightMode {
	public:
		static constexpr float MIN_SHARE = 0.05f;
		static constexpr double LONGEST_PIECE = 1.0; // seconds; a save, a barrier or a stall is not made up for with a long sleep

		// The share the file asks for: 1 when it is missing, unreadable or not a number
		static float ReadShare(const std::filesystem::path& file) {
			std::ifstream in(file);
			std::string word;
			if (!(in >> word))
				return 1.f;
			char* end = nullptr;
			float share = std::strtof(word.c_str(), &end);
			if (end == word.c_str() || *end != 0 || !(share == share))
				return 1.f;
			return std::clamp(share, MIN_SHARE, 1.f);
		}

		void Bind(const std::filesystem::path& throttleFile) {
			std::lock_guard<std::mutex> lock(mutex);
			file = throttleFile;
			checked = {};
		}

		float Share() {
			std::lock_guard<std::mutex> lock(mutex);
			if (file.empty())
				return 1.f;
			auto now = std::chrono::steady_clock::now();
			if (checked == std::chrono::steady_clock::time_point{} || now - checked > std::chrono::seconds(2)) {
				share = ReadShare(file);
				checked = now;
			}
			return share;
		}

		// Seconds to rest after a piece of work that took `worked` seconds
		static double Rest(double worked, float share) {
			if (share >= 1.f || worked <= 0)
				return 0;
			return std::min(worked, LONGEST_PIECE) * (1.0 / std::max(share, MIN_SHARE) - 1.0);
		}

		// `last` is the caller's own mark (one per working thread). `finishQueuedWork` runs first when pacing, so the GPU is idle
		// during the rest.
		template <typename Finish>
		void Pace(std::chrono::steady_clock::time_point& last, Finish&& finishQueuedWork) {
			const float s = Share();
			if (s >= 1.f) {
				last = {};
				return;
			}
			finishQueuedWork();
			auto now = std::chrono::steady_clock::now();
			if (last != std::chrono::steady_clock::time_point{})
				std::this_thread::sleep_for(std::chrono::duration<double>(Rest(std::chrono::duration<double>(now - last).count(), s)));
			last = std::chrono::steady_clock::now();
		}

		static LightMode& Get() {
			static LightMode instance;
			return instance;
		}

	private:
		std::mutex mutex;
		std::filesystem::path file;
		std::chrono::steady_clock::time_point checked = {};
		float share = 1.f;
	};
}
