#pragma once
#include "Reward.h"
#include "RewardTrace.h"
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"

namespace RLGC {

// ============================================================================
// OffsidePenalty (2026-10-18 Rev 1.0 | 2026-10-20 Rev 1.1 team-ratio | Rev 1.2 distance scaling)
// ============================================================================
// Penalty-only replacement for the negative half of GoalsideReward. There is NO
// positive "goalside" payout, so sitting back / net-camping earns nothing: the
// only way to lose reward is to be upfield of the ball.
//
// Y-only margin, same convention as GoalsideReward:
//   Blue offside:   player.pos.y > ball.pos.y + DEADZONE  (blue goal at Y = -5120)
//   Orange offside: player.pos.y < ball.pos.y - DEADZONE  (orange goal at Y = +5120)
//
// Per-car offsideness o in [0,1]: distance past the ball plane minus the
// 200uu dribble/flick dead zone, over an 800uu ramp (o = 1 at 1000uu upfield).
// Demoed cars count as o = 1 (out of play).
//
// Rev 1.2 (2026-10-20): distance scaling. Magnitude no longer saturates at 1.0.
// Severity s = (upfield - 200) / 800, kept linear all the way out to
// MAX_UPFIELD_UU (5000uu past the ball), where s = 6.0. So 1000uu upfield = 1.0
// (unchanged from Rev 1.0), 3000uu = 3.5, 5000uu+ = 6.0. The team factor c
// below still uses the saturating 0..1 offsideness (o), so the ratio logic is
// about HOW MANY cars are out of position, while s sets how bad it is.
//
// Penalty for an offside car = -s_me * c, where c in [0,1] is the team factor:
//   - nobody on my team is goalside of the ball (I'm exposed / last man): c = 1
//   - a teammate is goalside (covering):  c = clamp((Off_mine - Off_opp) / o_me, 0, 1)
//       Off_team = sum of o over that team's cars (incl. self for my team).
//       Dividing by o_me means a lone offside car facing all-onside opponents
//       pays its full s (same as 1v1), not a diluted amount.
//
// Rev 1.1 (2026-10-20): a covering teammate no longer fully exempts you. If my
// team has more offside cars than the opponents, the extra offside car is a
// liability even with someone back: an opponent can bump/demo the lone
// defender and the other follows up (2v2 and 3v3 alike). Examples (o = 1 or 0):
//   2v2 mine {off,on} vs opp {on,on}   -> offside car pays full  (c = 1)
//   2v2 mine {off,on} vs opp {off,on}  -> 0   (equal numbers out of position)
//   2v2 mine {off,on} vs opp {off,off} -> 0
//   3v3 mine {off,on,on} vs opp {on,on,on} -> full
//   3v3 mine {off,off,on} vs opp {off,on,on} -> full (surplus 1)
//   partial: opp car at o = 0.5 against my full surplus of 1 -> c = 0.5
// Off_opp is computed in each opponent's own frame (their upfield, not mine).
//
// Gates: demoed -> 0, kickoff (ball near center, vel < 100) -> 0.
// NOT ZeroSum: individual shaping.
//
// Raw output range is now [-6.0, 0] (was [-1, 0]); WeightedReward weight 0.15
// => worst case -0.9 per step at 5000uu+ upfield.
//
// W&B: per-reward mean for OffsidePenalty. Healthy: small negative mean
// (activation well under ~30% of steps; slightly higher than Rev 1.0 in
// team modes). Suspicious: ~0 always (never offside) or strongly negative
// (bot over-chases / team stacks upfield).
// ============================================================================

class OffsidePenalty : public Reward {
private:
    static constexpr float RAMP_UU          = 800.0f; // uu past the dead zone to reach full penalty
    static constexpr float OFFSIDE_DEADZONE = 200.0f; // uu upfield of ball before penalty starts
    static constexpr float MAX_UPFIELD_UU   = 5000.0f; // distance past ball at which the linear ramp stops growing
    static constexpr float MAX_SEVERITY     = (MAX_UPFIELD_UU - OFFSIDE_DEADZONE) / RAMP_UU; // 6.0

    // Positive = car is between ball and its own goal (Y-only).
    static float GoalsideMargin(const Player& p, const GameState& state) {
        return (p.team == Team::BLUE)
            ? (state.ball.pos.y - p.pos.y)
            : (p.pos.y - state.ball.pos.y);
    }

    // Unbounded-slope severity: (upfield - DEADZONE) / RAMP, linear, capped at MAX_SEVERITY. 0 if not offside.
    static float Severity(const Player& p, const GameState& state) {
        float upfield = -GoalsideMargin(p, state) - OFFSIDE_DEADZONE;
        if (upfield <= 0.0f) return 0.0f;
        return RS_MIN(upfield / RAMP_UU, MAX_SEVERITY);
    }

    // 0 = fine, 1 = fully offside (>= DEADZONE + RAMP upfield of ball, or demoed).
    static float Offsideness(const Player& p, const GameState& state) {
        if (p.isDemoed) return 1.0f;
        float upfield = -GoalsideMargin(p, state) - OFFSIDE_DEADZONE;
        if (upfield <= 0.0f) return 0.0f;
        return RS_MIN(upfield / RAMP_UU, 1.0f);
    }

public:
    virtual float GetReward(const Player& player, const GameState& state, bool /*isFinal*/) override {
        // Trace (Reward Lab): observation only, never changes a branch or a value.
        // The offside plane for this car's team: past it (deeper upfield) the car is offside.
        if (trace) {
            const float planeY = state.ball.pos.y + (player.team == Team::BLUE ? OFFSIDE_DEADZONE : -OFFSIDE_DEADZONE);
            trace->Line(player.carId, TraceRole::Threshold, Vec(-4096.0f, planeY, 20.0f), Vec(4096.0f, planeY, 20.0f));
        }

        if (player.isDemoed) {
            if (trace) trace->Gate(player.carId, "alive", false, "demoed");
            return 0.0f;
        }

        // Skip kickoff: ball near center with near-zero velocity
        if (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 100.0f) {
            if (trace) trace->Gate(player.carId, "not_kickoff", false, "ball near centre and slower than 100 uu/s");
            return 0.0f;
        }

        float oMe = Offsideness(player, state);
        if (trace) {
            trace->Gate(player.carId, "alive", true);
            trace->Gate(player.carId, "not_kickoff", true);
            trace->Component(player.carId, "offsideness", oMe);
            trace->Gate(player.carId, "offside", oMe > 0.0f, oMe > 0.0f ? "upfield of the ball plane + dead zone" : "goalside, or inside the dead zone");
        }
        if (oMe <= 0.0f) return 0.0f;

        float offMine = 0.0f, offOpp = 0.0f;
        bool covered = false;
        for (const Player& p : state.players) {
            float o = Offsideness(p, state);
            if (p.team == player.team) {
                offMine += o;
                if (p.carId != player.carId && !p.isDemoed && GoalsideMargin(p, state) > 0.0f)
                    covered = true; // a live teammate is between ball and our goal
            } else {
                offOpp += o;
            }
        }

        float c = covered ? RS_CLAMP((offMine - offOpp) / oMe, 0.0f, 1.0f) : 1.0f;
        if (trace) {
            trace->Gate(player.carId, "covered_by_teammate", covered, covered ? "a live teammate is goalside of the ball" : "last man: full penalty");
            trace->Component(player.carId, "off_mine", offMine);
            trace->Component(player.carId, "off_opp", offOpp);
            trace->Component(player.carId, "team_factor", c);
            trace->Component(player.carId, "severity", Severity(player, state));
        }
        return -Severity(player, state) * c;
    }
};

} // namespace RLGC
