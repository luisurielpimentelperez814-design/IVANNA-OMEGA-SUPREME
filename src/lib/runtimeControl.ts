export const RUNTIME_CONTROLS = {
  spatialWidth: [0, 1.5], spatialAngleDeg: [-180, 180], antiDolbyIntensity: [0, 1],
  realityIntensity: [0, 1], realityEnabled: [0, 1],
} as const;

export interface RuntimeControlBridge { applyRuntimeControl(name: string, value: number): string }
export type ControlReceipt = { accepted: boolean; message: string; readback?: number };

export function applyRuntimeControl(bridge: RuntimeControlBridge | undefined, name: string, value: unknown): ControlReceipt {
  const bounds = RUNTIME_CONTROLS[name as keyof typeof RUNTIME_CONTROLS];
  if (!bounds) return { accepted: false, message: `${name}: guardado localmente; control nativo no disponible.` };
  if ((name === 'realityEnabled' && value !== 0 && value !== 1) || typeof value !== 'number' || !Number.isFinite(value) || value < bounds[0] || value > bounds[1]) {
    return { accepted: false, message: `${name}: valor fuera del contrato nativo.` };
  }
  if (!bridge) return { accepted: false, message: `${name}: guardado localmente; sin puente Android.` };
  try {
    const result = JSON.parse(bridge.applyRuntimeControl(name, value));
    if (result.accepted === true && typeof result.readback === 'number' &&
        Number.isFinite(result.readback) && Math.abs(result.readback - value) < 0.001) {
      return { accepted: true, readback: result.readback,
        message: `${name}: ${result.readback} confirmado por lectura del control nativo; procesamiento audible pendiente de medición.` };
    }
  } catch { /* Failed transport must never become an applied state. */ }
  return { accepted: false, message: `${name}: el motor no confirmó el valor solicitado.` };
}
