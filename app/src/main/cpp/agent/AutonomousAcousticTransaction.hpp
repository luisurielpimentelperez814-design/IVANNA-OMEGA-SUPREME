// AutonomousAcousticTransaction.hpp — transactional control policy for the
// measurement -> bounded proposal -> apply -> remeasure -> rollback loop.
// All callbacks run on an orchestration thread, never the real-time audio thread.
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace ivanna::agent {
struct AcousticSnapshot {
    std::array<float,10> levelDb{};
    std::array<float,10> snrDb{};
    float confidence=0.f;
    bool valid=false;
};
struct DecisionRecord {
    std::uint64_t transactionId=0;
    std::string decision;
    std::string reason;
    float beforeErrorDb=0.f, afterErrorDb=0.f;
    bool applied=false, rolledBack=false, committed=false;
    std::uint32_t elapsedMs=0;
};
struct AgentLimits {
    float maxCutDb=9.f;
    float maxBoostDb=3.f;
    float minConfidence=0.65f;
    float minImprovementDb=0.5f;
    std::uint32_t maxTransactionMs=8000;
    std::uint32_t maxIterations=1; // One bounded proposal per transaction; no self-looping.
};
struct TransactionResult { bool committed=false, rolledBack=false; DecisionRecord record{}; };

class AutonomousAcousticTransaction {
public:
    using Measure = bool(*)(void*, AcousticSnapshot&);
    using Propose = bool(*)(void*, const AcousticSnapshot&, std::array<float,10>&);
    using Apply = bool(*)(void*, const std::array<float,10>&);
    using Audit = void(*)(void*, const DecisionRecord&);

    explicit AutonomousAcousticTransaction(AgentLimits limits={}) : limits_(limits) {
        limits_.maxCutDb=std::clamp(std::isfinite(limits_.maxCutDb)?limits_.maxCutDb:9.f,0.f,9.f);
        limits_.maxBoostDb=std::clamp(std::isfinite(limits_.maxBoostDb)?limits_.maxBoostDb:3.f,0.f,3.f);
        limits_.minConfidence=std::clamp(std::isfinite(limits_.minConfidence)?limits_.minConfidence:0.65f,0.f,1.f);
        limits_.minImprovementDb=std::clamp(std::isfinite(limits_.minImprovementDb)?limits_.minImprovementDb:0.5f,0.1f,6.f);
        limits_.maxTransactionMs=std::clamp(limits_.maxTransactionMs,100u,8000u);
        limits_.maxIterations=std::clamp(limits_.maxIterations,1u,1u);
    }

    TransactionResult execute(void* context, const std::array<float,10>& current,
                              Measure measure, Propose propose, Apply apply, Audit audit,
                              std::uint64_t transactionId) const {
        TransactionResult out{};
        auto& r=out.record; r.transactionId=transactionId; r.decision="NO_CHANGE";
        const auto start=Clock::now();
        auto finish=[&]() {
            r.elapsedMs=static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count());
            if(audit) audit(context,r);
            return out;
        };
        if(!measure||!propose||!apply){r.reason="missing_callback";return finish();}
        AcousticSnapshot before{};
        if(!measure(context,before)||!usable(before)){r.reason="baseline_measurement_invalid";return finish();}
        if(before.confidence<limits_.minConfidence){r.reason="baseline_confidence_below_threshold";return finish();}
        r.beforeErrorDb=error(before);
        std::array<float,10> target{};
        if(!propose(context,before,target)){r.reason="proposal_unavailable";return finish();}
        for(float& v:target){
            if(!std::isfinite(v)){r.reason="non_finite_proposal";return finish();}
            v=std::clamp(v,-limits_.maxCutDb,limits_.maxBoostDb);
        }
        if(expired(start)){r.reason="deadline_before_apply";return finish();}
        if(!apply(context,target)){r.reason="apply_failed";return finish();}
        r.applied=true;r.decision="VERIFYING";
        AcousticSnapshot after{};
        const bool measured=measure(context,after)&&usable(after)&&after.confidence>=limits_.minConfidence;
        if(!measured||expired(start)||error(after)>r.beforeErrorDb-limits_.minImprovementDb){
            const bool restored=apply(context,current);
            r.rolledBack=restored;r.decision=restored?"ROLLED_BACK":"ROLLBACK_FAILED";
            r.reason=!measured?"verification_measurement_invalid":(expired(start)?"transaction_deadline_exceeded":"insufficient_improvement");
            out.rolledBack=restored;
            return finish();
        }
        r.afterErrorDb=error(after);r.decision="COMMITTED";r.reason="verified_improvement";
        r.committed=true;out.committed=true;return finish();
    }
private:
    using Clock=std::chrono::steady_clock;
    AgentLimits limits_;
    bool expired(Clock::time_point start) const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count()>limits_.maxTransactionMs;
    }
    static bool usable(const AcousticSnapshot& s) {
        if(!s.valid||!std::isfinite(s.confidence))return false;
        int n=0;for(std::size_t i=0;i<s.levelDb.size();++i)
            if(std::isfinite(s.levelDb[i])&&std::isfinite(s.snrDb[i])&&s.snrDb[i]>=10.f)++n;
        return n>=3;
    }
    static float error(const AcousticSnapshot& s) {
        double sum=0.;int n=0;
        for(std::size_t i=0;i<s.levelDb.size();++i)if(std::isfinite(s.levelDb[i])&&std::isfinite(s.snrDb[i])&&s.snrDb[i]>=10.f){
            sum+=static_cast<double>(s.levelDb[i])*s.levelDb[i];++n;
        }
        return n?static_cast<float>(std::sqrt(sum/n)):std::numeric_limits<float>::infinity();
    }
};
} // namespace ivanna::agent
