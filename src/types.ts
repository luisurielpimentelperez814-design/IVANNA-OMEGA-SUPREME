export interface DspParameters {
  goldenEarEnabled: boolean;
  goldenEarDrive: number;
  goldenEarMix: number;
  fatigueIndex: number;
  iirAlpha: number;
  crosstalkGain: number;
  hrtfDelayMs: number;
  eqMutationRate: number;
  sampleRate: number;
  blockSize: number;
}

export interface BenchmarkMetrics {
  blockLatencyMicroseconds: number;
  sampleLatencyNanoseconds: number;
  simdEfficiencyPercent: number;
  heapAllocationsInAudioThread: number;
  l1CacheHitRatePercent: number;
  gflopsThroughput: number;
  registersActiveCount: number;
}

export interface CppFile {
  filename: string;
  category: 'header' | 'source' | 'build' | 'script';
  content: string;
  description: string;
}
