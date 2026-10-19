#include <gtest/gtest.h>
#include "agent/AutonomousAcousticTransaction.hpp"
using namespace ivanna::agent;
struct Fake { bool applied=false; bool failVerification=false; std::array<float,10> appliedValues{}; DecisionRecord last{}; };
static bool measure(void* p, AcousticSnapshot& s) {
    auto& f=*static_cast<Fake*>(p); s.valid=true;s.confidence=0.95f;s.snrDb.fill(20.f);
    s.levelDb.fill(f.applied&&!f.failVerification?1.f:5.f);return true;
}
static bool propose(void*,const AcousticSnapshot&,std::array<float,10>& t){t.fill(-2.f);return true;}
static bool apply(void* p,const std::array<float,10>& t){auto& f=*static_cast<Fake*>(p);f.applied=true;f.appliedValues=t;return true;}
static void audit(void* p,const DecisionRecord& r){static_cast<Fake*>(p)->last=r;}
TEST(AutonomousAcousticTransaction, CommitsOnlyAfterMeasuredImprovement) {
    Fake f;DecisionRecord log;AutonomousAcousticTransaction tx;
    auto result=tx.execute(&f,{},measure,propose,apply,audit,17);
    EXPECT_TRUE(result.committed);EXPECT_FALSE(result.rolledBack);EXPECT_EQ(result.record.transactionId,17u);
    EXPECT_EQ(result.record.decision,"COMMITTED");EXPECT_LT(result.record.afterErrorDb,result.record.beforeErrorDb);EXPECT_EQ(f.last.decision,"COMMITTED");
}
TEST(AutonomousAcousticTransaction, RollsBackWhenVerificationDoesNotImprove) {
    Fake f;f.failVerification=true;DecisionRecord log;AutonomousAcousticTransaction tx;
    auto result=tx.execute(&f,{},measure,propose,apply,audit,18);
    EXPECT_FALSE(result.committed);EXPECT_TRUE(result.rolledBack);
    EXPECT_EQ(result.record.decision,"ROLLED_BACK");EXPECT_EQ(result.record.reason,"insufficient_improvement");
}
TEST(AutonomousAcousticTransaction, RejectsLowConfidenceBaselineWithoutApplying) {
    struct Low:Fake{} f; // use a local callback to provide a low-confidence baseline
    auto low=[](void*,AcousticSnapshot& s)->bool{s.valid=true;s.confidence=.2f;s.snrDb.fill(20.f);s.levelDb.fill(3.f);return true;};
    auto noop=[](void*,const std::array<float,10>&)->bool{return true;};
    auto proposal=[](void*,const AcousticSnapshot&,std::array<float,10>& t)->bool{t.fill(0.f);return true;};
    auto result=AutonomousAcousticTransaction{}.execute(&f,{},low,proposal,noop,nullptr,19);
    EXPECT_FALSE(result.committed);EXPECT_FALSE(result.record.applied);
    EXPECT_EQ(result.record.reason,"baseline_confidence_below_threshold");
}
