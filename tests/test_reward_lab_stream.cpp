#include "TestFramework.h"

#include <RLGymCPP/Lab/LabReport.h>
#include <RLGymCPP/Rewards/RewardStreamTap.h>

#include <chrono>
#include <iostream>
#include <netinet/in.h>

using namespace RLGC;
using namespace RLGC::Lab;
using nlohmann::json;

// P4-3 (first part) of the Reward Lab (docs/REWARD_VIEWER_SPEC.md section 10): the render-mode stream tap, tested WITHOUT the trainer by giving a real EnvSet
// tapped rewards through LabEnv. Needs build/collision_meshes (run from the repo root).

namespace {
	CarSpec MkCar(Team t, Vec pos, float yaw = 90, Vec vel = Vec(0, 0, 0), float boost = 100) {
		CarSpec c; c.team = t; c.pos = pos; c.yawDeg = yaw; c.vel = vel; c.boost = boost; return c;
	}
	Action Act(float throttle, float boost, float jump = 0, float steer = 0) {
		Action a = Action(CarControls{}); a.throttle = throttle; a.boost = boost; a.jump = jump; a.steer = steer; return a;
	}
	// 2v2: blue car 0 drives into the ball. Three rewards, one of them ZeroSum-wrapped (the tap must see through it).
	Scenario Drive(int steps = 24) {
		Scenario s; s.name = "stream_drive"; s.steps = steps;
		s.cars = { MkCar(Team::BLUE, Vec(0, -2200, 17), 90, Vec(0, 1400, 0)), MkCar(Team::BLUE, Vec(2500, 1500, 17)),
		           MkCar(Team::ORANGE, Vec(-3000, 3500, 17), 270), MkCar(Team::ORANGE, Vec(3000, 3500, 17), 270) };
		s.ball.pos = Vec(0, -1000, 93);
		s.rewards = { { "PassingSetupReward", 1.0f, -1, 0.9969f }, { "OffsidePenalty", 0.15f, -1, 0.9969f }, { "CarEnergyPotentialReward", 75.f, 0.3f, 0.9969f } };
		ActionEntry a; a.car = 0; a.from = 0; a.action = Act(1, 1); s.actions.push_back(a);
		return s;
	}
	std::vector<json> Parsed(const VectorStreamSink& s) { std::vector<json> o; for (auto& m : s.messages) o.push_back(json::parse(m)); return o; }
	std::vector<json> OfKind(const std::vector<json>& all, const std::string& k) { std::vector<json> o; for (auto& j : all) if (j["kind"] == k) o.push_back(j); return o; }

	struct Tapped {
		std::vector<WeightedReward> rewards; std::shared_ptr<VectorStreamSink> sink = std::make_shared<VectorStreamSink>();
		std::shared_ptr<StreamTapCollector> col; LabEnv* env = nullptr;
		Tapped(const Scenario& s, bool tap = true) {
			std::vector<std::string> errs; BuildRewardStack(s.rewards, rewards, errs);
			if (tap) col = InstallStreamTaps(rewards, sink, s.tickSkip);
			env = LabEnv::Create(s, rewards);
		}
		~Tapped() { delete env; FreeRewardStack(rewards); }
	};
}

// ---- equivalence with the Lab, bit for bit ---------------------------------------------------------------------------------

TEST(Stream_tap_frames_equal_the_labs_frames_exactly) {
	Scenario s = Drive();
	LabSession lab(s); CHECK(lab.ok()); lab.RunScripted();
	Tapped t(s); CHECK(t.env != nullptr); CHECK(t.col != nullptr);
	const size_t atStart = t.sink->messages.size();           // EnvSet reset the rewards once on creation: hello + frame 0
	CHECK_EQ(atStart, (size_t)2);
	for (int k = 0; k < lab.Steps(); k++) t.env->Step(lab.ActionLog()[k]);
	auto msgs = Parsed(*t.sink);
	auto frames = OfKind(msgs, "lab_stream_frame");
	CHECK_EQ(frames.size(), (size_t)(lab.Steps() + 1));
	for (int k = 0; k <= lab.Steps(); k++) {
		const json expect = FrameToJson(lab.Frames()[k]);
		CHECK(frames[k]["frame"] == expect);                   // players, ball, actions, and the whole reward trace, no tolerance
	}
	CHECK(frames[1]["frame"]["trace"]["rewards"].size() == 3);
	// the ZeroSum reward's pre-wrapper value is carried (raw != wrapped for it)
	bool sawDifferent = false;
	for (int k = 1; k <= lab.Steps(); k++) { auto& r = frames[k]["frame"]["trace"]["rewards"][2]; if (r["raw"] != r["wrapped"]) sawDifferent = true; }
	CHECK(sawDifferent);
}

TEST(Stream_tap_changes_nothing_the_rewards_pay_or_are_called) {
	Scenario s = Drive(30);
	LabSession lab(s); lab.RunScripted();
	Tapped on(s, true), off(s, false);
	std::vector<std::string> bare;
	for (auto& w : off.rewards) bare.push_back(w.reward->GetName());
	for (size_t i = 0; i < on.rewards.size(); i++) CHECK_EQ(on.rewards[i].reward->GetName(), bare[i]);   // the W&B key is unchanged
	CHECK(dynamic_cast<StreamTapReward*>(on.rewards[0].reward) != nullptr && dynamic_cast<StreamTapReward*>(off.rewards[0].reward) == nullptr);
	int nonzero = 0;
	for (int k = 0; k < lab.Steps(); k++) {
		on.env->Step(lab.ActionLog()[k]); off.env->Step(lab.ActionLog()[k]);
		for (size_t p = 0; p < on.env->env->state.rewards.size(); p++) {
			CHECK(on.env->env->state.rewards[p] == off.env->env->state.rewards[p]);      // bit-identical, tapped vs bare, inside the real EnvSet
			if (std::fabs(on.env->env->state.rewards[p]) > 1e-6f) nonzero++;
		}
	}
	CHECK(nonzero > 40);                                       // it was not comparing zeros
	for (size_t i = 0; i < on.rewards.size(); i++) CHECK_EQ(on.rewards[i].weight, off.rewards[i].weight);   // weights and the gated flag are kept
	for (size_t i = 0; i < on.rewards.size(); i++) CHECK_EQ(on.rewards[i].gated, off.rewards[i].gated);
}

// ---- the messages ----------------------------------------------------------------------------------------------------------

TEST(Stream_messages_are_ordered_counted_and_describe_the_stack) {
	Scenario s = Drive(8);
	Tapped t(s);
	LabSession lab(s); lab.RunScripted();
	for (int k = 0; k < lab.Steps(); k++) t.env->Step(lab.ActionLog()[k]);
	auto msgs = Parsed(*t.sink);
	CHECK_EQ(msgs.size(), (size_t)(2 + 8));
	for (size_t i = 0; i < msgs.size(); i++) { CHECK_EQ(msgs[i]["seq"].get<int>(), (int)i); CHECK_EQ(msgs[i]["schema"].get<int>(), 1); CHECK_EQ(msgs[i]["episode"].get<int>(), 1); }
	const json& hello = msgs[0];
	CHECK(hello["kind"] == "lab_stream_hello"); CHECK_EQ(hello["tick_skip"].get<int>(), 8);
	CHECK_EQ(hello["cars"].size(), (size_t)4); CHECK_EQ(hello["cars"][0]["team"].get<int>(), 0); CHECK_EQ(hello["cars"][2]["team"].get<int>(), 1);
	CHECK_EQ(hello["rewards"].size(), (size_t)3);
	CHECK(hello["rewards"][0]["name"] == "PassingSetupReward" && hello["rewards"][1]["name"] == "OffsidePenalty" && hello["rewards"][2]["name"] == "CarEnergyPotentialReward");
	CHECK(std::fabs(hello["rewards"][1]["weight"].get<double>() - 0.15) < 1e-6);
	CHECK(!hello["rewards"][0]["zero_sum"].get<bool>() && hello["rewards"][2]["zero_sum"].get<bool>());
	CHECK(hello["rewards"][0]["wandb_key"].get<std::string>().size() > 0);
	CHECK(msgs[1]["kind"] == "lab_stream_frame"); CHECK_EQ(msgs[1]["frame"]["i"].get<int>(), 0); CHECK(msgs[1]["frame"]["trace"].is_null());   // the reset frame has no trace
	for (int k = 1; k <= 8; k++) { CHECK_EQ(msgs[1 + k]["frame"]["i"].get<int>(), k); CHECK(!msgs[1 + k]["frame"]["trace"].is_null()); }
	CHECK_EQ(msgs[2]["frame"]["actions"].size(), (size_t)4);                  // the action each car was given
	CHECK_EQ(t.col->Seq(), (uint64_t)10);
}

TEST(Stream_hello_reports_the_tick_skip_the_installer_was_given) {
	Scenario s = Drive(4); s.tickSkip = 4;
	Tapped t(s);
	auto msgs = Parsed(*t.sink);
	CHECK(msgs[0]["kind"] == "lab_stream_hello"); CHECK_EQ(msgs[0]["tick_skip"].get<int>(), 4);
}

TEST(Stream_a_reset_starts_a_new_episode_with_a_new_hello) {
	Scenario s = Drive(8);
	Tapped t(s);
	LabSession lab(s); lab.RunScripted();
	for (int k = 0; k < 3; k++) t.env->Step(lab.ActionLog()[k]);
	t.env->env->ResetArena(0);                                                // an episode boundary
	for (int k = 0; k < 2; k++) t.env->Step(lab.ActionLog()[k]);
	auto msgs = Parsed(*t.sink);
	auto hellos = OfKind(msgs, "lab_stream_hello");
	CHECK_EQ(hellos.size(), (size_t)2); CHECK_EQ(hellos[0]["episode"].get<int>(), 1); CHECK_EQ(hellos[1]["episode"].get<int>(), 2);
	auto frames = OfKind(msgs, "lab_stream_frame");
	std::vector<int> idx1, idx2;
	for (auto& f : frames) (f["episode"].get<int>() == 1 ? idx1 : idx2).push_back(f["frame"]["i"].get<int>());
	CHECK((idx1 == std::vector<int>{ 0, 1, 2, 3 })); CHECK((idx2 == std::vector<int>{ 0, 1, 2 }));       // frame numbers restart at the reset
	for (size_t i = 0; i < msgs.size(); i++) CHECK_EQ(msgs[i]["seq"].get<int>(), (int)i);                  // seq does not
}

TEST(Stream_a_goal_marks_the_final_frame) {
	Scenario s; s.name = "goal"; s.steps = 60;
	s.cars = { MkCar(Team::BLUE, Vec(0, -3000, 17)), MkCar(Team::ORANGE, Vec(3000, 3000, 17), 270) };
	s.ball.pos = Vec(0, 4600, 93); s.ball.vel = Vec(0, 3500, 0);
	s.rewards = { { "CarEnergyPotentialReward", 1.f, -1, 0.9969f } };
	LabSession lab(s); lab.RunScripted(); CHECK(lab.Finished());
	Tapped t(s);
	for (int k = 0; k < lab.Steps(); k++) t.env->Step(lab.ActionLog()[k]);
	auto frames = OfKind(Parsed(*t.sink), "lab_stream_frame");
	const json& last = frames.back()["frame"];
	CHECK_EQ(last["terminal"].get<int>(), 1); CHECK(last["goal"].get<bool>());
	for (size_t i = 0; i + 1 < frames.size(); i++) CHECK_EQ(frames[i]["frame"]["terminal"].get<int>(), 0);
}

TEST(Stream_a_frame_too_large_for_a_datagram_is_refused_not_truncated) {
	Scenario s = Drive(4);
	std::vector<WeightedReward> rewards; std::vector<std::string> errs; BuildRewardStack(s.rewards, rewards, errs);
	auto sink = std::make_shared<VectorStreamSink>();
	auto col = std::make_shared<StreamTapCollector>(sink, s.tickSkip, /*maxDatagram=*/900);          // smaller than a 4-car frame
	col->Configure(rewards);
	for (size_t i = 0; i < rewards.size(); i++) { rewards[i].reward->SetTrace(col->SinkFor(i)); rewards[i].reward = new StreamTapReward(rewards[i].reward, col, i, i + 1 == rewards.size()); }
	LabEnv* env = LabEnv::Create(s, rewards);
	LabSession lab(s); lab.RunScripted();
	for (int k = 0; k < 4; k++) env->Step(lab.ActionLog()[k]);
	auto msgs = Parsed(*sink);
	int errors = 0, frames = 0;
	for (auto& m : msgs) { if (m["kind"] == "lab_stream_error") { errors++; CHECK(m["reason"].get<std::string>().find("not truncated") != std::string::npos); CHECK(m["bytes"].get<int>() > 900); } if (m["kind"] == "lab_stream_frame") frames++; }
	CHECK(errors >= 4); CHECK_EQ(frames, 0);
	for (auto& m : msgs) CHECK(m.dump().size() < 900 + 200);                              // nothing oversize was sent, in any form
	for (size_t i = 0; i < msgs.size(); i++) CHECK_EQ(msgs[i]["seq"].get<int>(), (int)i);   // the error took the frame's seq slot: the loss is visible, not a gap
	CHECK(col->TooLarge() >= 4);
	delete env; FreeRewardStack(rewards);
}

TEST(Stream_install_refuses_nothing_to_tap_and_double_tapping) {
	std::vector<WeightedReward> none; std::string err;
	CHECK(InstallStreamTaps(none, std::make_shared<VectorStreamSink>(), 8, &err) == nullptr); CHECK(err.find("no rewards") != std::string::npos);
	Scenario s = Drive(); std::vector<WeightedReward> rewards; std::vector<std::string> errs; BuildRewardStack(s.rewards, rewards, errs);
	auto sink = std::make_shared<VectorStreamSink>();
	CHECK(InstallStreamTaps(rewards, sink, 0, &err) == nullptr); CHECK(err.find("tick_skip") != std::string::npos);      // a bad tick skip is refused before anything is wrapped
	CHECK(InstallStreamTaps(rewards, sink, 121, &err) == nullptr);
	CHECK(InstallStreamTaps(rewards, sink, 8, &err) != nullptr);
	Reward* first = rewards[0].reward;
	CHECK(InstallStreamTaps(rewards, sink, 8, &err) == nullptr); CHECK(err.find("already tapped") != std::string::npos);
	CHECK(rewards[0].reward == first);                                                    // the refused call changed nothing
	FreeRewardStack(rewards);
}

// ---- the target and the socket --------------------------------------------------------------------------------------------

TEST(Stream_target_must_be_a_loopback_address_and_a_real_port) {
	std::string host, err; int port = 0;
	CHECK(ParseStreamTarget("127.0.0.1:9280", host, port, err)); CHECK_EQ(host, std::string("127.0.0.1")); CHECK_EQ(port, 9280);
	CHECK(ParseStreamTarget("127.5.6.7:1024", host, port, err)); CHECK_EQ(port, 1024);
	CHECK(ParseStreamTarget("127.0.0.1:65535", host, port, err));
	for (const char* bad : { "", "9280", ":9280", "127.0.0.1", "127.0.0.1:", "localhost:9280", "10.0.0.5:9280", "128.0.0.1:9280", "0.0.0.0:9280", "192.168.1.2:9280",
	                         "127.0.0.1:0", "127.0.0.1:80", "127.0.0.1:1023", "127.0.0.1:65536", "127.0.0.1:99999", "127.0.0.1:9280x", "127.0.0.1:-5", "127.0.0.1:123456",
	                         "127.0.0.256:9280", "127.0.0:9280", "127.0.0.1.5:9280", "127.0.0.1 :9280", "[::1]:9280" }) {
		std::string h, e; int p = 0;
		CHECK(!ParseStreamTarget(bad, h, p, e)); CHECK(!e.empty());
	}
}

TEST(Stream_udp_sink_delivers_to_a_loopback_receiver_and_never_blocks_or_throws) {
	int rx = ::socket(AF_INET, SOCK_DGRAM, 0); CHECK(rx >= 0);
	sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
	CHECK(::bind(rx, (sockaddr*)&a, sizeof(a)) == 0);
	socklen_t len = sizeof(a); ::getsockname(rx, (sockaddr*)&a, &len);
	const int port = ntohs(a.sin_port);
	UdpStreamSink tx("127.0.0.1", port); CHECK(tx.ok());
	for (const char* m : { "hello", "{\"a\":1}", "third" }) CHECK(tx.Send(m));
	char buf[200];
	for (const char* want : { "hello", "{\"a\":1}", "third" }) { ssize_t n = ::recv(rx, buf, sizeof(buf), 0); CHECK(n == (ssize_t)std::strlen(want)); CHECK(std::string(buf, n) == want); }
	CHECK_EQ(tx.sent, (uint64_t)3); CHECK_EQ(tx.dropped, (uint64_t)0);
	CHECK(!tx.Send(std::string(STREAM_MAX_DATAGRAM + 1, 'x'))); CHECK_EQ(tx.dropped, (uint64_t)1);       // over the limit: refused by the sink too
	// nobody listening any more: sends fail (ECONNREFUSED on a connected UDP socket) and are counted, immediately
	::close(rx);
	const auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < 200; i++) tx.Send("x");
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	CHECK(ms < 200.0); CHECK(tx.dropped >= 100);                                                      // not blocking, not throwing
	// a bad address does not construct a usable sink
	UdpStreamSink bad("not an address", 9280); CHECK(!bad.ok()); CHECK(!bad.Send("x"));
}

TEST(Stream_env_var_hook_is_inert_unless_set_and_never_breaks_a_run) {
	Scenario s = Drive(); std::vector<WeightedReward> rewards; std::vector<std::string> errs; BuildRewardStack(s.rewards, rewards, errs);
	std::vector<Reward*> before; for (auto& w : rewards) before.push_back(w.reward);
	::unsetenv("REWARD_LAB_STREAM");
	CHECK(!MaybeInstallStreamTapsFromEnv(rewards, 8));
	for (size_t i = 0; i < rewards.size(); i++) CHECK(rewards[i].reward == before[i]);                  // unset: untouched
	::setenv("REWARD_LAB_STREAM", "", 1);
	CHECK(!MaybeInstallStreamTapsFromEnv(rewards, 8)); CHECK(rewards[0].reward == before[0]);
	for (const char* bad : { "garbage", "8.8.8.8:9280", "127.0.0.1:99999", "localhost:9280" }) {
		::setenv("REWARD_LAB_STREAM", bad, 1);
		CHECK(!MaybeInstallStreamTapsFromEnv(rewards, 8));
		for (size_t i = 0; i < rewards.size(); i++) CHECK(rewards[i].reward == before[i]);               // invalid: untouched, no crash
	}
	::setenv("REWARD_LAB_STREAM", "127.0.0.1:9280", 1);
	CHECK(MaybeInstallStreamTapsFromEnv(rewards, 8));
	for (size_t i = 0; i < rewards.size(); i++) CHECK(dynamic_cast<StreamTapReward*>(rewards[i].reward) != nullptr);
	CHECK(!MaybeInstallStreamTapsFromEnv(rewards, 8));                                                      // a second call does not double-wrap
	::unsetenv("REWARD_LAB_STREAM");
	FreeRewardStack(rewards);
}

TEST(Stream_a_tapped_env_streams_over_a_real_socket_end_to_end) {
	int rx = ::socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(::bind(rx, (sockaddr*)&a, sizeof(a)) == 0);
	socklen_t len = sizeof(a); ::getsockname(rx, (sockaddr*)&a, &len);
	const std::string target = "127.0.0.1:" + std::to_string(ntohs(a.sin_port));
	::setenv("REWARD_LAB_STREAM", target.c_str(), 1);
	Scenario s = Drive(6);
	std::vector<WeightedReward> rewards; std::vector<std::string> errs; BuildRewardStack(s.rewards, rewards, errs);
	CHECK(MaybeInstallStreamTapsFromEnv(rewards, 8)); ::unsetenv("REWARD_LAB_STREAM");
	LabEnv* env = LabEnv::Create(s, rewards);
	LabSession lab(s); lab.RunScripted();
	for (int k = 0; k < 6; k++) env->Step(lab.ActionLog()[k]);
	std::vector<json> got; char buf[65536];
	for (;;) { ssize_t n = ::recv(rx, buf, sizeof(buf), MSG_DONTWAIT); if (n <= 0) break; got.push_back(json::parse(std::string(buf, n))); }
	CHECK_EQ(got.size(), (size_t)(2 + 6));
	for (size_t i = 0; i < got.size(); i++) CHECK_EQ(got[i]["seq"].get<int>(), (int)i);
	CHECK(got[0]["kind"] == "lab_stream_hello"); CHECK(got[7]["frame"] == FrameToJson(lab.Frames()[6]));   // what arrives is the Lab's frame, exactly
	::close(rx); delete env; FreeRewardStack(rewards);
}

// ---- size and cost, measured ------------------------------------------------------------------------------------------------

TEST(Stream_frame_size_and_tap_overhead_are_measured) {
	Scenario s = Drive(60);
	LabSession lab(s); lab.RunScripted();
	Tapped on(s, true), off(s, false);
	size_t maxBytes = 0, total = 0, n = 0;
	auto timeIt = [&](Tapped& t) {
		const auto t0 = std::chrono::steady_clock::now();
		for (int rep = 0; rep < 3; rep++) { t.env->env->ResetArena(0); for (int k = 0; k < lab.Steps(); k++) t.env->Step(lab.ActionLog()[k]); }
		return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (3.0 * lab.Steps());
	};
	const double usOff = timeIt(off), usOn = timeIt(on);
	for (auto& m : on.sink->messages) { maxBytes = std::max(maxBytes, m.size()); total += m.size(); n++; }
	std::cout << "    2v2, 3 rewards: largest message " << maxBytes << " B, mean " << (n ? total / n : 0) << " B; per env step " << usOff << " us untapped, " << usOn << " us tapped (+"
	          << (usOn - usOff) << " us)\n";
	CHECK(maxBytes < STREAM_MAX_DATAGRAM / 2);                       // well inside one datagram for this size
	CHECK(usOn - usOff < 2000.0);                                    // under 2 ms per step on top of the step itself (measured, printed above)
}
