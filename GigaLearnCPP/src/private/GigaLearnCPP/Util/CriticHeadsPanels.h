#pragma once
// CRITIC HEADS panels (docs/GCO_CRITIC_HEADS_FRESH.md). MEASUREMENT ONLY: reads CPU tensors, writes report values, trains nothing.
// Inputs are the heads' predictions on this iteration's rows computed BEFORE the update (out of sample), the event labels, each row's steps to
// episode end, and the live critic's value and the scaled MC return in the same units.
#include <GigaLearnCPP/PPO/PPOLearnerConfig.h>
#include <GigaLearnCPP/Util/Report.h>
#include <torch/torch.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace GGL {
	namespace HeadsPanels {
		// rank-based AUC of scores against 0/1 labels (-1 if one class is missing)
		inline float Auc(const std::vector<float>& s, const std::vector<uint8_t>& y) {
			const size_t n = s.size();
			std::vector<size_t> idx(n);
			std::iota(idx.begin(), idx.end(), 0);
			std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return s[a] < s[b]; });
			double rankSumPos = 0; int64_t pos = 0;
			for (size_t r = 0; r < n;) { // tied scores get the average rank (a constant score must read AUC 0.5, not an arbitrary sort order)
				size_t e = r;
				while (e + 1 < n && s[idx[e + 1]] == s[idx[r]]) e++;
				double avgRank = 0.5 * ((double)r + 1.0 + (double)e + 1.0);
				for (size_t q = r; q <= e; q++)
					if (y[idx[q]]) { rankSumPos += avgRank; pos++; }
				r = e + 1;
			}
			int64_t neg = (int64_t)n - pos;
			if (pos == 0 || neg == 0) return -1.f;
			return (float)((rankSumPos - 0.5 * pos * (pos + 1)) / ((double)pos * (double)neg));
		}
		inline float Corr(const std::vector<float>& a, const std::vector<float>& b) {
			const size_t n = a.size();
			if (n < 50) return NAN;
			double ma = 0, mb = 0;
			for (size_t i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
			ma /= n; mb /= n;
			double sab = 0, saa = 0, sbb = 0;
			for (size_t i = 0; i < n; i++) { double da = a[i] - ma, db = b[i] - mb; sab += da * db; saa += da * da; sbb += db * db; }
			return (saa > 0 && sbb > 0) ? (float)(sab / std::sqrt(saa * sbb)) : NAN;
		}

		// out [N, HEAD_OUT_DIMS], labels [N, HEAD_LABEL_DIMS], tteSteps[i] = steps from row i to its episode end, vLive and ret [N] (critic units)
		inline void Compute(GGL::Report& report, const std::string& prefix, const torch::Tensor& outT, const torch::Tensor& labT,
			const std::vector<int>& tteSteps, float stepsPerSec, const torch::Tensor& vLiveT, const torch::Tensor& retT) {
			const int64_t n = outT.size(0);
			if (n < 1000) return;
			torch::Tensor outC = outT.contiguous(), labC = labT.contiguous(), vC = vLiveT.contiguous(), rC = retT.contiguous();
			const float* out = outC.const_data_ptr<float>();
			const float* lab = labC.const_data_ptr<float>();
			const float* vLive = vC.const_data_ptr<float>();
			const float* ret = rC.const_data_ptr<float>();

			// time-to-end bins in seconds: 0-2, 2-5, 5-10, >10 ; bin 4 = pooled
			const char* binNames[5] = { "0-2s", "2-5s", "5-10s", ">10s", "All" };
			std::vector<std::vector<int64_t>> rows(5);
			for (int64_t i = 0; i < n; i++) {
				float sec = tteSteps[i] / stepsPerSec;
				int b = sec <= 2.f ? 0 : sec <= 5.f ? 1 : sec <= 10.f ? 2 : 3;
				rows[b].push_back(i);
				rows[4].push_back(i);
			}

			auto fnSoftmaxLogProb = [&](const float* z, int cls) {
				float mx = z[0];
				for (int c = 1; c < HEAD_CLASSES; c++) mx = std::max(mx, z[c]);
				double sum = 0;
				for (int c = 0; c < HEAD_CLASSES; c++) sum += std::exp((double)z[c] - mx);
				return (float)((double)z[cls] - mx - std::log(sum));
			};
			auto fnProb = [&](const float* z, int cls) { return std::exp(fnSoftmaxLogProb(z, cls)); };

			const char* hName[HEAD_HORIZONS] = { "H5", "H15", "Ep" };
			for (int b = 0; b < 5; b++) {
				const auto& R = rows[b];
				if (R.size() < 300) continue;
				const std::string bn = binNames[b];
				// --- event heads: log-loss skill against the same-window climatology, for the head and the shuffled-label control ---
				for (int k = 0; k < HEAD_HORIZONS; k++) {
					if (b != 4 && k != 1) continue; // per-bin detail only for the 15 s horizon; pooled for all three
					double cnt[HEAD_CLASSES] = {};
					for (int64_t i : R) cnt[(int)lab[i * HEAD_LABEL_DIMS + k]]++;
					double llClim = 0;
					for (int c = 0; c < HEAD_CLASSES; c++)
						if (cnt[c] > 0) llClim -= cnt[c] * std::log(cnt[c] / (double)R.size());
					llClim /= (double)R.size();
					double llEv = 0, llCtl = 0;
					for (int64_t i : R) {
						int y = (int)lab[i * HEAD_LABEL_DIMS + k];
						llEv -= fnSoftmaxLogProb(out + i * HEAD_OUT_DIMS + k * HEAD_CLASSES, y);
						llCtl -= fnSoftmaxLogProb(out + i * HEAD_OUT_DIMS + HEAD_EV_OUT + k * HEAD_CLASSES, y);
					}
					llEv /= (double)R.size(); llCtl /= (double)R.size();
					if (llClim > 1e-9) {
						report[prefix + "E Skill " + hName[k] + " " + bn] = (float)(1.0 - llEv / llClim);
						if (b == 4) report[prefix + "E Ctrl Skill " + hName[k]] = (float)(1.0 - llCtl / llClim);
					}
					if (b == 4) report[prefix + "E LogLoss " + hName[k]] = (float)llEv;
				}
				// --- AUC of P(score within 15 s) and P(concede within 15 s) ---
				for (int cls = 1; cls <= 2; cls++) {
					std::vector<float> sc; std::vector<uint8_t> y;
					sc.reserve(R.size()); y.reserve(R.size());
					for (int64_t i : R) {
						sc.push_back(fnProb(out + i * HEAD_OUT_DIMS + 1 * HEAD_CLASSES, cls));
						y.push_back((int)lab[i * HEAD_LABEL_DIMS + 1] == cls);
					}
					float a = Auc(sc, y);
					if (a >= 0) report[prefix + (cls == 1 ? "E AUC Score15 " : "E AUC Concede15 ") + bn] = a;
				}
				// --- touch within 5 s: AUC and Brier, pooled and per bin ---
				{
					std::vector<float> sc; std::vector<uint8_t> y;
					double brier = 0;
					for (int64_t i : R) {
						float p = 1.f / (1.f + std::exp(-out[i * HEAD_OUT_DIMS + HEAD_EV_OUT - 1]));
						float t = lab[i * HEAD_LABEL_DIMS + HEAD_HORIZONS];
						sc.push_back(p); y.push_back(t > 0.5f);
						brier += (p - t) * (p - t);
					}
					float a = Auc(sc, y);
					if (a >= 0) report[prefix + "E Touch5 AUC " + bn] = a;
					if (b == 4) report[prefix + "E Touch5 Brier"] = (float)(brier / R.size());
				}
				// --- expected calibration error of P(score within 15 s), pooled ---
				if (b == 4) {
					double binP[10] = {}, binY[10] = {}; int64_t binN[10] = {};
					for (int64_t i : R) {
						float p = fnProb(out + i * HEAD_OUT_DIMS + 1 * HEAD_CLASSES, 1);
						int bi = std::min(9, (int)(p * 10.f));
						binP[bi] += p; binY[bi] += ((int)lab[i * HEAD_LABEL_DIMS + 1] == 1); binN[bi]++;
					}
					double ece = 0;
					for (int j = 0; j < 10; j++)
						if (binN[j] > 0) ece += (binN[j] / (double)R.size()) * std::fabs(binP[j] / binN[j] - binY[j] / binN[j]);
					report[prefix + "E ECE Score15"] = (float)ece;
				}
				// --- uncertainty: D = (V - V_b)^2 (epistemic proxy), predicted error, realised squared error of the live critic ---
				{
					std::vector<float> D, UE, ERR;
					D.reserve(R.size()); UE.reserve(R.size()); ERR.reserve(R.size());
					for (int64_t i : R) {
						float vb = out[i * HEAD_OUT_DIMS + 2 * HEAD_EV_OUT];
						float ue = out[i * HEAD_OUT_DIMS + 2 * HEAD_EV_OUT + 1];
						D.push_back((vLive[i] - vb) * (vLive[i] - vb));
						UE.push_back(ue);
						ERR.push_back((ret[i] - vLive[i]) * (ret[i] - vLive[i]));
					}
					double mD = 0, mU = 0, mE = 0;
					for (size_t j = 0; j < D.size(); j++) { mD += D[j]; mU += UE[j]; mE += ERR[j]; }
					report[prefix + "U D Mean " + bn] = (float)(mD / D.size());
					report[prefix + "U Pred Err Mean " + bn] = (float)(mU / D.size());
					report[prefix + "U Err Mean " + bn] = (float)(mE / D.size());
					float cD = Corr(D, ERR), cU = Corr(UE, ERR);
					if (std::isfinite(cD)) report[prefix + "U Corr D Err " + bn] = cD;
					if (std::isfinite(cU)) report[prefix + "U Corr Pred Err " + bn] = cU;
					// decile ratio: mean realised error in the top decile of the score over the bottom decile (1 = the score does not rank error)
					auto fnDecile = [&](const std::vector<float>& sc, const char* key) {
						std::vector<size_t> idx(sc.size());
						std::iota(idx.begin(), idx.end(), 0);
						std::sort(idx.begin(), idx.end(), [&](size_t a, size_t c) { return sc[a] < sc[c]; });
						size_t d = idx.size() / 10;
						if (d < 30) return;
						double lo = 0, hi = 0;
						for (size_t j = 0; j < d; j++) { lo += ERR[idx[j]]; hi += ERR[idx[idx.size() - 1 - j]]; }
						if (lo > 1e-9) report[prefix + key + bn] = (float)(hi / lo);
					};
					fnDecile(D, "U Decile Ratio D ");
					fnDecile(UE, "U Decile Ratio Pred ");
				}
			}
		}
	}
}
