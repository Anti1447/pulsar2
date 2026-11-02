#pragma once
// ============================================================================
// RewardTraceJson.h - the wire/file format for a step trace (JSONL: one StackStepTrace per line).
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md section 3.1 (P0).
//
// Schema 1:
//   {"schema":1,"step":N,"cars":[carId,...],"total":[...],
//    "rewards":[{"name":"...","wandb_key":"...","weight":w,"zero_sum":bool,"raw":[...],"wrapped":[...],"weighted":[...],
//                "players":[{"car":id,"components":[{"name":"..","value":v}],
//                            "gates":[{"name":"..","passed":bool,"reason":".."}],"events":["..",...],
//                            "prims":[{"kind":"line","role":"threshold","a":[x,y,z],"b":[x,y,z],"radius":r,"text":".."}],
//                            "prims_dropped":n}]}]}
// Arrays of per-player numbers are indexed like "cars". A non-finite float serialises as null (nlohmann's rule).
// Bump "schema" on any incompatible change; readers must refuse a schema they do not know.
// ============================================================================
#include "RewardTraceStack.h"
#include <nlohmann/json.hpp>

namespace RLGC {

	constexpr int REWARD_TRACE_SCHEMA = 1;

	inline nlohmann::json VecJson(const Vec& v) { return nlohmann::json::array({ v.x, v.y, v.z }); }

	inline nlohmann::json ToJson(const StackStepTrace& t) {
		using nlohmann::json;
		json j;
		j["schema"] = REWARD_TRACE_SCHEMA;
		j["step"] = t.step;
		j["cars"] = t.carIds;
		j["total"] = t.total;
		json rewards = json::array();
		for (auto& r : t.rewards) {
			json jr;
			jr["name"] = r.name; jr["wandb_key"] = r.wandbKey; jr["weight"] = r.weight; jr["zero_sum"] = r.zeroSum;
			jr["raw"] = r.raw; jr["wrapped"] = r.wrapped; jr["weighted"] = r.weighted;
			json players = json::array();
			for (auto& p : r.sink.players) {
				json jp;
				jp["car"] = p.carId;
				json comps = json::array(), gates = json::array(), prims = json::array();
				for (auto& c : p.components) comps.push_back({ { "name", c.name }, { "value", c.value } });
				for (auto& g : p.gates) gates.push_back({ { "name", g.name }, { "passed", g.passed }, { "reason", g.reason } });
				for (auto& q : p.prims)
					prims.push_back({ { "kind", TracePrimKindName(q.kind) }, { "role", TraceRoleName(q.role) },
						{ "a", VecJson(q.a) }, { "b", VecJson(q.b) }, { "radius", q.radius }, { "text", q.text } });
				jp["components"] = comps; jp["gates"] = gates; jp["events"] = p.events; jp["prims"] = prims; jp["prims_dropped"] = p.primsDropped;
				players.push_back(jp);
			}
			jr["players"] = players;
			rewards.push_back(jr);
		}
		j["rewards"] = rewards;
		return j;
	}

} // namespace RLGC
