#pragma once
// ============================================================================
// RewardStreamTap.h - stream a render run's reward trace, read-only. 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md section 10 (P4-3)
//
// A StreamTapReward wraps one reward of a stack. It forwards everything to the reward, records what the reward returned, and the tap of the LAST reward
// in the stack assembles the step's trace and sends one datagram: a Lab frame (players, ball, actions, the whole StackStepTrace) as JSON. The reward is
// called exactly as before (same calls, same order, same arguments), and its return value is passed through untouched, so a tapped stack pays exactly
// what the bare stack pays (tests/test_reward_lab_stream.cpp: bit-identical outputs, unchanged GetName()).
//
// WHERE IT MAY BE USED: render mode only (one arena). It is not installed anywhere by this header. The future wiring is two lines in the render branch of
//   the EnvCreate function:    MaybeInstallStreamTapsFromEnv(result.rewards, TICK_SKIP);    which does nothing unless REWARD_LAB_STREAM is set. Never wrap
//   training arenas.
//   - WeightedReward::zeroSumPtr is resolved by EnvSet with dynamic_cast on the reward, so on a tapped reward it is null. That pointer is only used by the
//     per-step reward LOGGING path (saveRewards -> W&B), which render mode does not run. Another reason not to tap training arenas.
//   - WeightedReward::gated is preserved (the new WeightedReward copies it).
//
// Wire format, schema 1: UTF-8 JSON, one datagram per message, to 127.x.x.x:PORT (loopback only, refused otherwise). `seq` counts every message in the
// stream and never repeats, so a receiver sees a lost datagram as a gap. A frame too large for one datagram is NOT truncated: an error message takes its
// seq slot instead.
//   {"kind":"lab_stream_hello","schema":1,"seq":n,"episode":e,"tick_skip":8,"cars":[{"car":id,"team":0|1}],
//        "rewards":[{"name":"OffsidePenalty","wandb_key":"...","weight":0.15,"zero_sum":false}]}        (at every reset: a new episode)
//   {"kind":"lab_stream_frame","schema":1,"seq":n,"episode":e,"frame":{FrameToJson}}                     (frame i = 0 at the reset, no trace; then 1, 2, ...)
//   {"kind":"lab_stream_error","schema":1,"seq":n,"episode":e,"reason":"...","bytes":n}
// `terminal` in a streamed frame is 1 when the step was final (the tap only sees EnvSet's bool), never 2. tick_skip is GIVEN by the installer: the state's
// deltaTime is not set yet at a reset, when the hello is sent.
//
// Not thread-safe: one collector per arena. The UDP sink itself may be shared (a datagram send is atomic).
// ============================================================================
#include "Reward.h"
#include "RewardWrapper.h"
#include "RewardTrace.h"
#include "RewardTraceStack.h"
#include "ZeroSumReward.h"
#include "../Lab/FrameJson.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#define RLGC_STREAM_HAS_UDP 1
#endif

namespace RLGC {
namespace Lab {

	constexpr int STREAM_SCHEMA = 1;
	constexpr size_t STREAM_MAX_DATAGRAM = 60000;   // under the 65507-byte UDP limit, with room

	// ---- where messages go -----------------------------------------------------------------------------------------------
	// Send must not block and must not throw: a viewer that is not there must never slow down or break a trainer.
	struct StreamSink {
		virtual ~StreamSink() {}
		virtual bool Send(const std::string& message) = 0;   // false = not delivered (counted by the sink)
	};

	// For tests and in-process use: keeps every message.
	struct VectorStreamSink : StreamSink {
		std::vector<std::string> messages;
		bool Send(const std::string& m) override { messages.push_back(m); return true; }
	};

	// "127.0.0.1:9280": an IPv4 loopback literal (127.x.x.x) and a port 1024..65535. Anything else is refused so the stream can never leave the machine.
	inline bool ParseStreamTarget(const std::string& s, std::string& host, int& port, std::string& err) {
		size_t c = s.rfind(':');
		if (c == std::string::npos || c == 0 || c + 1 >= s.size()) { err = "expected HOST:PORT, e.g. 127.0.0.1:9280"; return false; }
		host = s.substr(0, c);
		const std::string p = s.substr(c + 1);
		int a = 0, b = 0, cc = 0, d = 0; char tail = 0;
		if (std::sscanf(host.c_str(), "%d.%d.%d.%d%c", &a, &b, &cc, &d, &tail) != 4 || a != 127 || b < 0 || b > 255 || cc < 0 || cc > 255 || d < 0 || d > 255) {
			err = "the host must be an IPv4 loopback address (127.x.x.x); the stream never leaves this machine"; return false;
		}
		for (char ch : p) if (ch < '0' || ch > '9') { err = "the port must be a number"; return false; }
		if (p.size() > 5) { err = "the port must be 1024..65535"; return false; }
		port = std::atoi(p.c_str());
		if (port < 1024 || port > 65535) { err = "the port must be 1024..65535"; return false; }
		return true;
	}

#ifdef RLGC_STREAM_HAS_UDP
	// Non-blocking UDP to a loopback address. Failures (nobody listening, a full buffer) are counted, never raised.
	class UdpStreamSink : public StreamSink {
	public:
		UdpStreamSink(const std::string& host, int port) {
			fd = ::socket(AF_INET, SOCK_DGRAM, 0);
			if (fd < 0) return;
			::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
			::fcntl(fd, F_SETFD, FD_CLOEXEC);
			sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
			if (::inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1 || ::connect(fd, (sockaddr*)&a, sizeof(a)) != 0) { ::close(fd); fd = -1; }
		}
		~UdpStreamSink() override { if (fd >= 0) ::close(fd); }
		UdpStreamSink(const UdpStreamSink&) = delete;
		UdpStreamSink& operator=(const UdpStreamSink&) = delete;
		bool ok() const { return fd >= 0; }
		bool Send(const std::string& m) override {
			if (fd < 0 || m.size() > STREAM_MAX_DATAGRAM) { dropped++; return false; }
			ssize_t n = ::send(fd, m.data(), m.size(), MSG_DONTWAIT);
			if (n == (ssize_t)m.size()) { sent++; return true; }
			dropped++; return false;
		}
		uint64_t sent = 0, dropped = 0;
	private:
		int fd = -1;
	};
#endif

	// ---- the collector: one per tapped stack ------------------------------------------------------------------------------
	class StreamTapCollector {
	public:
		StreamTapCollector(std::shared_ptr<StreamSink> sink, int tickSkip, size_t maxDatagram = STREAM_MAX_DATAGRAM) : sink(std::move(sink)), maxDatagram(maxDatagram), tickSkip(tickSkip) {}

		// The taps register themselves in stack order; the sink pointers are the ones the rewards emit into.
		void Configure(const std::vector<WeightedReward>& stack) {
			trace.rewards.assign(stack.size(), RewardStepTrace());
			for (size_t i = 0; i < stack.size(); i++) {
				trace.rewards[i].name = ReadableRewardName(stack[i].reward);
				trace.rewards[i].wandbKey = stack[i].reward->GetName();
				trace.rewards[i].weight = stack[i].weight;
				trace.rewards[i].zeroSum = dynamic_cast<ZeroSumReward*>(stack[i].reward) != nullptr;
			}
		}
		TraceSink* SinkFor(size_t i) { return &trace.rewards[i].sink; }
		size_t Count() const { return trace.rewards.size(); }

		// index 0 of the stack, from Reset(): a new episode
		void OnReset(const GameState& gs) {
			episode++; stepInEpisode = 0; trace.step = 0;
			nlohmann::json cars = nlohmann::json::array();
			for (auto& p : gs.players) cars.push_back({ { "car", p.carId }, { "team", (int)p.team } });
			nlohmann::json rw = nlohmann::json::array();
			for (auto& r : trace.rewards) rw.push_back({ { "name", r.name }, { "wandb_key", r.wandbKey }, { "weight", r.weight }, { "zero_sum", r.zeroSum } });
			Emit({ { "kind", "lab_stream_hello" }, { "schema", STREAM_SCHEMA }, { "seq", seq }, { "episode", episode }, { "tick_skip", tickSkip }, { "cars", cars }, { "rewards", rw } });
			Frame f0; f0.index = 0; Fill(f0, gs, false);
			EmitFrame(f0, nullptr);
		}

		// index 0 of the stack, from PreStep(): the step starts
		void BeginStep(const GameState& gs) {
			stepInEpisode++;
			trace.step = (uint64_t)(stepInEpisode - 1);
			trace.carIds.clear();
			for (auto& p : gs.players) trace.carIds.push_back(p.carId);
			trace.total.assign(gs.players.size(), 0.0f);
		}

		// each tap, from GetAllRewards(): what reward `i` returned
		void Record(size_t i, const std::vector<float>& output, const std::vector<float>* rawIfZeroSum) {
			RewardStepTrace& rt = trace.rewards[i];
			rt.wrapped = output;
			rt.weighted.resize(output.size());
			for (size_t p = 0; p < output.size(); p++) {
				rt.weighted[p] = output[p] * rt.weight;
				if (p < trace.total.size()) trace.total[p] += rt.weighted[p];
			}
			rt.raw = rawIfZeroSum ? *rawIfZeroSum : output;
		}

		// the LAST tap, from GetAllRewards(): the step is complete
		void EndStep(const GameState& gs, bool isFinal) {
			Frame f; f.index = stepInEpisode; Fill(f, gs, isFinal);
			for (auto& p : gs.players) f.actions.push_back(p.prevAction);
			EmitFrame(f, &trace);
		}

		uint64_t Seq() const { return seq; }
		int Episode() const { return episode; }
		uint64_t TooLarge() const { return tooLarge; }

	private:
		std::shared_ptr<StreamSink> sink;
		size_t maxDatagram;
		StackStepTrace trace;
		uint64_t seq = 0; int episode = 0, stepInEpisode = 0, tickSkip; uint64_t tooLarge = 0;

		static void Fill(Frame& f, const GameState& gs, bool isFinal) {
			for (auto& p : gs.players) f.players.push_back(SnapPlayer(p));
			f.ball.pos = gs.ball.pos; f.ball.vel = gs.ball.vel; f.ball.angVel = gs.ball.angVel;
			f.goalScored = gs.goalScored; f.terminal = isFinal ? 1 : 0;
		}
		void Emit(nlohmann::json j) {
			std::string m = j.dump();
			if (m.size() > maxDatagram) return;   // callers use EmitFrame for large things; a hello is small
			sink->Send(m); seq++;
		}
		void EmitFrame(const Frame& f, const StackStepTrace* tr) {
			nlohmann::json j = { { "kind", "lab_stream_frame" }, { "schema", STREAM_SCHEMA }, { "seq", seq }, { "episode", episode }, { "frame", FrameToJsonWith(f, tr) } };
			std::string m = j.dump();
			if (m.size() > maxDatagram) {
				tooLarge++;
				nlohmann::json e = { { "kind", "lab_stream_error" }, { "schema", STREAM_SCHEMA }, { "seq", seq }, { "episode", episode },
					{ "reason", "frame too large for one datagram: refused, not truncated" }, { "bytes", m.size() } };
				sink->Send(e.dump()); seq++;
				return;
			}
			sink->Send(m); seq++;
		}
	};

	// ---- the tap ----------------------------------------------------------------------------------------------------------
	class StreamTapReward : public RewardWrapper {
	public:
		// Assumes ownership of `child` (like every RewardWrapper).
		StreamTapReward(Reward* child, std::shared_ptr<StreamTapCollector> col, size_t index, bool last)
			: RewardWrapper(child), col(std::move(col)), index(index), last(last), zeroSum(dynamic_cast<ZeroSumReward*>(child)) {}

		virtual void Reset(const GameState& initialState) override {
			child->Reset(initialState);
			if (index == 0) col->OnReset(initialState);
		}
		virtual void PreStep(const GameState& state) override {
			child->PreStep(state);
			if (index == 0) col->BeginStep(state);
		}
		virtual std::vector<float> GetAllRewards(const GameState& state, bool isFinal) override {
			col->SinkFor(index)->Clear();
			std::vector<float> out = child->GetAllRewards(state, isFinal);
			col->Record(index, out, zeroSum ? &zeroSum->_lastRewards : nullptr);
			if (last) col->EndStep(state, isFinal);
			return out;
		}

	private:
		std::shared_ptr<StreamTapCollector> col;
		size_t index; bool last;
		ZeroSumReward* zeroSum;   // the child, if it is one (owned by the child's own wrapper chain)
	};

	// Replaces every reward in `rewards` by a tap around it (weights and the gated flag are kept) and points each reward's trace sink at the collector.
	// Returns the collector, or null (and sets `err`) if there is nothing to tap or it is already tapped.
	inline std::shared_ptr<StreamTapCollector> InstallStreamTaps(std::vector<WeightedReward>& rewards, std::shared_ptr<StreamSink> sink, int tickSkip, std::string* err = nullptr) {
		auto fail = [&](const char* m) { if (err) *err = m; return std::shared_ptr<StreamTapCollector>(); };
		if (rewards.empty()) return fail("no rewards to tap");
		for (auto& w : rewards) if (dynamic_cast<StreamTapReward*>(w.reward)) return fail("these rewards are already tapped");
		if (tickSkip < 1 || tickSkip > 120) return fail("tick_skip must be 1..120");
		auto col = std::make_shared<StreamTapCollector>(std::move(sink), tickSkip);
		col->Configure(rewards);
		for (size_t i = 0; i < rewards.size(); i++) {
			Reward* child = rewards[i].reward;
			child->SetTrace(col->SinkFor(i));
			rewards[i].reward = new StreamTapReward(child, col, i, i + 1 == rewards.size());
		}
		return col;
	}

	// The render-branch hook: does NOTHING unless REWARD_LAB_STREAM=127.0.0.1:PORT is set. A bad value or an unusable socket logs one line to stderr and
	// leaves the rewards untouched (a viewer problem must never stop a render run). Returns true if the rewards were tapped.
	inline bool MaybeInstallStreamTapsFromEnv(std::vector<WeightedReward>& rewards, int tickSkip) {
		const char* v = std::getenv("REWARD_LAB_STREAM");
		if (!v || !*v) return false;
#ifdef RLGC_STREAM_HAS_UDP
		std::string host, err; int port = 0;
		if (!ParseStreamTarget(v, host, port, err)) { std::fprintf(stderr, "[reward-lab] REWARD_LAB_STREAM=%s ignored: %s\n", v, err.c_str()); return false; }
		auto udp = std::make_shared<UdpStreamSink>(host, port);
		if (!udp->ok()) { std::fprintf(stderr, "[reward-lab] REWARD_LAB_STREAM=%s ignored: cannot open the UDP socket\n", v); return false; }
		if (!InstallStreamTaps(rewards, udp, tickSkip, &err)) { std::fprintf(stderr, "[reward-lab] REWARD_LAB_STREAM=%s ignored: %s\n", v, err.c_str()); return false; }
		std::fprintf(stderr, "[reward-lab] streaming reward traces to %s (read-only, loopback)\n", v);
		return true;
#else
		std::fprintf(stderr, "[reward-lab] REWARD_LAB_STREAM ignored: UDP streaming is not built for this platform\n");
		return false;
#endif
	}

} // namespace Lab
} // namespace RLGC
