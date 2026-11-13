// ivanna_servo.cpp — Theil-Sen + clamp + slew.
#include "ivanna_servo.h"

#include <algorithm>
#include <cmath>

namespace ivanna {
namespace asrcd {

TheilSenServo::TheilSenServo(size_t window, double nominal_hz, double rail_ppm)
    : cap_(window < 4 ? 4 : window), nominal_(nominal_hz), rail_(rail_ppm) {
    w_.reserve(cap_ + 1);
}

void TheilSenServo::add(int64_t t_ns, int64_t frames) {
    if (!w_.empty()) {
        const Pair& last = w_.back();
        // Frames nunca retroceden (§3.3, el binario ya lo detecta y lo loguea);
        // si retrocede, es un reinicio del contador: se limpia la ventana.
        if (frames < last.frames || t_ns <= last.t_ns) {
            if (frames < last.frames) w_.clear();
            else return;
        }
    }
    w_.push_back(Pair{t_ns, frames});
    if (w_.size() > cap_) w_.erase(w_.begin());
}

void TheilSenServo::reset() { w_.clear(); }

bool TheilSenServo::ready() const { return w_.size() >= 2; }

bool TheilSenServo::estimate(double* ppm_out) const {
    if (ppm_out == nullptr) return false;
    const size_t n = w_.size();
    if (n < 2) return false;

    std::vector<double> slopes;
    slopes.reserve((n * (n - 1)) / 2);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const double dt = static_cast<double>(w_[j].t_ns - w_[i].t_ns) * 1e-9;
            if (dt <= 0.0) continue;
            const double df = static_cast<double>(w_[j].frames - w_[i].frames);
            slopes.push_back(df / dt);
        }
    }
    if (slopes.empty()) return false;

    const size_t mid = slopes.size() / 2;
    std::nth_element(slopes.begin(), slopes.begin() + mid, slopes.end());
    double rate = slopes[mid];
    if (slopes.size() % 2 == 0 && mid > 0) {
        const double lo = *std::max_element(slopes.begin(), slopes.begin() + mid);
        rate = 0.5 * (lo + rate);
    }

    double ppm = (rate / nominal_ - 1.0) * 1e6;
    if (!std::isfinite(ppm)) return false;
    if (ppm > rail_) ppm = rail_;
    if (ppm < -rail_) ppm = -rail_;
    *ppm_out = ppm;
    return true;
}

double TheilSenServo::ratioFor(double ppm, double rail_ppm) {
    if (!std::isfinite(ppm)) return 1.0;
    double p = ppm;
    if (p > rail_ppm) p = rail_ppm;
    if (p < -rail_ppm) p = -rail_ppm;
    const double r = 1.0 / (1.0 + p * 1e-6);
    const double dev = (r - 1.0) * 1e6;
    if (std::fabs(dev) > rail_ppm) {
        return 1.0 + (dev > 0 ? rail_ppm : -rail_ppm) * 1e-6;
    }
    return r;
}

double TheilSenServo::slew(double current_ratio, double target_ratio,
                           double max_step_ppm) {
    if (!std::isfinite(current_ratio) || !std::isfinite(target_ratio))
        return 1.0;
    const double cur_ppm = (current_ratio - 1.0) * 1e6;
    const double tgt_ppm = (target_ratio - 1.0) * 1e6;
    double d = tgt_ppm - cur_ppm;
    if (d > max_step_ppm) d = max_step_ppm;
    if (d < -max_step_ppm) d = -max_step_ppm;
    return 1.0 + (cur_ppm + d) * 1e-6;
}

}  // namespace asrcd
}  // namespace ivanna
