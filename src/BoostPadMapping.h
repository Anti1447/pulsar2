#pragma once
// Maps RLBot's boost-pad indices to the order the observation was trained in (RLGC::CommonValues::BOOST_LOCATIONS).
//
// The client used to copy packet->boost_pads()[i] straight into slot i, which is only right if RLBot lists the pads in
// RocketSim's order. Nothing checked that, and a wrong order feeds the policy another pad's availability and timer on
// every step. The same approach GalactusV2's client takes: match each trained pad to the packet pad at the same (x, y)
// from FieldInfo once at start-up, and refuse to run if the field does not match one-to-one.
#include <RLGymCPP/Gamestates/GameState.h>
#include <rlbot/Bot.h>

#include <array>
#include <cmath>
#include <string>

class BoostPadMapping {
public:
	static constexpr int COUNT = RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT;

	// Call from Bot::initialize. False = the field is not the trained Soccar field; Error() says why.
	bool Build(const rlbot::flat::FieldInfo* field) {
		ready = false;
		error.clear();
		const auto* pads = field ? field->boost_pads() : nullptr;
		if (!pads || (int)pads->size() != COUNT)
			return Fail("FieldInfo must contain exactly " + std::to_string(COUNT) + " boost pads");

		std::array<bool, COUNT> used{};
		for (int t = 0; t < COUNT; t++) {
			const auto& want = RLGC::CommonValues::BOOST_LOCATIONS[t];
			int match = -1;
			for (int k = 0; k < COUNT; k++) {
				const auto* loc = pads->Get(k) ? pads->Get(k)->location() : nullptr;
				if (!loc || !std::isfinite(loc->x()) || !std::isfinite(loc->y()))
					return Fail("FieldInfo has a boost pad with no valid location");
				float dx = loc->x() - want.x, dy = loc->y() - want.y;
				if (dx * dx + dy * dy > 25.f) // within 5 uu
					continue;
				if (match != -1)
					return Fail("two FieldInfo pads sit on trained pad " + std::to_string(t));
				match = k;
			}
			if (match < 0 || used[match])
				return Fail("no unique FieldInfo pad for trained pad " + std::to_string(t));
			// Big pads are the z = 73 ones in BOOST_LOCATIONS
			if (pads->Get(match)->is_full_boost() != (want.z > 72.f))
				return Fail("pad size differs from training at trained pad " + std::to_string(t));
			used[match] = true;
			packetIndex[t] = match;
		}
		ready = true;
		return true;
	}

	// Fills gs.boostPads/Timers (and the Inv copies). False = not built, wrong pad count, or a non-finite timer.
	bool Apply(const rlbot::flat::GamePacket* packet, RLGC::GameState& gs) const {
		const auto* pads = packet ? packet->boost_pads() : nullptr;
		if (!ready || !pads || (int)pads->size() != COUNT)
			return false;
		for (int i = 0; i < COUNT; i++) {
			const auto* pad = pads->Get(packetIndex[i]);
			if (!pad || !std::isfinite(pad->timer()))
				return false;
		}
		for (int i = 0; i < COUNT; i++) {
			const auto* pad = pads->Get(packetIndex[i]);
			const int inv = COUNT - 1 - i;
			gs.boostPads[i] = pad->is_active();
			gs.boostPadTimers[i] = pad->timer();
			gs.boostPadsInv[inv] = gs.boostPads[i];
			gs.boostPadTimersInv[inv] = gs.boostPadTimers[i];
		}
		return true;
	}

	bool Ready() const { return ready; }
	const std::string& Error() const { return error; }
	int PacketIndex(int trainedIndex) const { return packetIndex.at(trainedIndex); }

private:
	bool Fail(std::string msg) { error = std::move(msg); return false; }
	bool ready = false;
	std::array<int, COUNT> packetIndex{};
	std::string error;
};
