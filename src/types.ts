export interface DspParameters {
  masterGain: number;
  antiDolbyIntensity: number;
  masterBypass: boolean;
  goldenEarEnabled: boolean;
  goldenEarDrive: number;
  goldenEarMix: number;
  fatigueIndex: number;
  iirAlpha: number;
  crosstalkGain: number;
  hrtfDelayMs: number;
  spatialAngleDeg: number;
  spatialWidth: number;
  spatialWetEta: number;
  eqMutationRate: number;
  sampleRate: number;
  blockSize: number;
  nhoAlpha: number;
  nhoBeta: number;
  harmonicGain: number;
  hrtfEnabled: boolean;
  adaptEnabled: boolean;
  compThresholdDb: number;
  compRatio: number;
  compAttackMs: number;
  compReleaseMs: number;
  activePreset: 'custom' | 'audiophile' | 'anti_dolby_extreme' | 'bass_head' | 'vocal_protect' | 'gaming_spatial' | 'evo_cma_es';
}

export interface TinyMlClassification {
  speech: number;
  music: number;
  transient: number;
  ambient: number;
  dominantClass: string;
  spscDepth: number;
  inferenceTimeUs: number;
}

export interface BenchmarkMetrics {
  blockLatencyMicroseconds: number;
  sampleLatencyNanoseconds: number;
  simdEfficiencyPercent: number;
  heapAllocationsInAudioThread: number;
  l1CacheHitRatePercent: number;
  gflopsThroughput: number;
  registersActiveCount: number;
  clipCount: number;
  evolutionFitness: number;
  optimalBlockSize: number;
  isCalibrating: boolean;
  calibrationProgress: number;
  calibrationLog: string[];
  lastCalibratedAt?: string;
}

export interface CppFile {
  filename: string;
  category: 'header' | 'source' | 'build' | 'script';
  content: string;
  description: string;
}
