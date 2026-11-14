// REFERENCE side, network half (checklist 1.2). Compiled against the ORIGINAL bot's own GigaLearnCPP model sources
// (Models.h, InferenceModels.cpp from GalactusV2 / Chronos at the pinned commits) and the layer configuration from that
// bot's own RLBotMain.cpp. Loads the bot's SHARED_HEAD.lt + POLICY.lt and, for each observation + mask row in the
// given reference file (written by ref_obs), writes the policy logits and the deterministic pick:
//   ./ref_model_galactus <ref.json> <model folder> <out.json>
//   ./ref_model_chronos  <ref.json> <model folder> <out.json>
#include <GigaLearnCPP/InferenceModels.h>
#include <GigaLearnCPP/Models.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

using json = nlohmann::json;

int main(int argc, char** argv) {
	if (argc < 4) {
		std::cerr << "usage: ref_model <ref.json> <model folder> <out.json>\n";
		return 2;
	}
	torch::NoGradGuard noGrad;

	GGL::InferPartialModelConfig sharedHeadCfg, policyCfg;
	sharedHeadCfg.addLayerNorm = true;
	sharedHeadCfg.addOutputLayer = false;
	policyCfg.addLayerNorm = true;
	policyCfg.addOutputLayer = true;
	int obsSize;
#ifdef REF_GALACTUS
	obsSize = 258;
	sharedHeadCfg.layerSizes = { 1024, 1024 };
	policyCfg.layerSizes = { 1024, 1024, 1024, 1024 };
	sharedHeadCfg.activationType = policyCfg.activationType = GGL::ModelActivationType::LEAKY_RELU;
#elif defined(REF_DICE)
	obsSize = 167;
	sharedHeadCfg.layerSizes = { 900, 900 };
	policyCfg.layerSizes = { 900, 900, 900 };
	sharedHeadCfg.activationType = policyCfg.activationType = GGL::ModelActivationType::LEAKY_RELU;
#else
	obsSize = 225;
	sharedHeadCfg.layerSizes = { 960, 960 };
	policyCfg.layerSizes = { 960, 800, 640, 640 };
	sharedHeadCfg.activationType = policyCfg.activationType = GGL::ModelActivationType::RELU;
#endif
	const int numActions = 90;

	GGL::ModelSet models;
	GGL::Infer::MakeInferenceModels(obsSize, numActions, sharedHeadCfg, policyCfg, torch::kCPU, models);
	models.Load(argv[2], false, false);

	json in = json::parse(std::ifstream(argv[1]));
	std::vector<float> obs;
	std::vector<uint8_t> masks;
	int rows = 0;
	for (auto& snap : in["snapshots"])
		for (auto& p : snap) {
			auto o = p["obs"].get<std::vector<float>>();
			auto m = p["mask"].get<std::vector<uint8_t>>();
			obs.insert(obs.end(), o.begin(), o.end());
			masks.insert(masks.end(), m.begin(), m.end());
			rows++;
		}

	auto tObs = torch::from_blob(obs.data(), { rows, obsSize }, torch::kFloat32).clone();
	auto tMasks = torch::from_blob(masks.data(), { rows, numActions }, torch::kUInt8).clone();

	torch::Tensor feats = models["shared_head"]->Forward(tObs, false);
	torch::Tensor logits = models["policy"]->Forward(feats, false).contiguous();
	torch::Tensor picks;
	GGL::Infer::InferActions(models, tObs, tMasks, true, 1.f, false, &picks, nullptr);

	json out;
	out["rows"] = rows;
	out["logits"] = std::vector<float>(logits.data_ptr<float>(), logits.data_ptr<float>() + (size_t)rows * numActions);
	picks = picks.to(torch::kInt64).contiguous();
	out["picks"] = std::vector<int64_t>(picks.data_ptr<int64_t>(), picks.data_ptr<int64_t>() + rows);
	std::ofstream(argv[3]) << out.dump();
	std::cout << "wrote " << rows << " rows" << std::endl;
	return 0;
}
