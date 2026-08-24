#include "ivanna_unified_engine.hpp"
namespace ivanna {
struct UnifiedEngine::Impl { UnifiedControlFrame control; std::atomic<bool> init{false}; };
UnifiedEngine::UnifiedEngine():pImpl(new Impl()){}
UnifiedEngine::~UnifiedEngine(){delete pImpl;}
bool UnifiedEngine::initialize(){pImpl->init=true; return true;}
void UnifiedEngine::processBlock(float* d,int f,int c){ if(!pImpl->init) return; }
UnifiedControlFrame UnifiedEngine::readControlFrame() const { return pImpl->control; }
}
