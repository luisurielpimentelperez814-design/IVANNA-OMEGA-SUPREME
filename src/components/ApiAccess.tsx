import { useState } from 'react';
import { setDashboardToken } from '../lib/apiAccess';

export function ApiAccess() {
  const [value, setValue] = useState('');
  const [status, setStatus] = useState('Sin token de acceso.');
  return <details className="rounded-xl border border-slate-600 bg-slate-950 p-4 text-sm text-slate-200">
    <summary className="cursor-pointer">Acceso a servicios de IA</summary>
    <p className="my-3">Si el servidor exige autenticación, introduce el token de acceso del dashboard.
      No introduzcas una clave de Gemini/OpenAI. Se conserva solo hasta recargar esta página.</p>
    <form className="flex flex-wrap gap-3" onSubmit={event => {
      event.preventDefault();
      try { setDashboardToken(value); setValue(''); setStatus('Acceso actualizado para esta sesión.'); }
      catch (error) { setStatus(error instanceof Error ? error.message : 'Token inválido.'); }
    }}>
      <label className="min-w-0 flex-1">Token del dashboard
        <input type="password" autoComplete="off" value={value} maxLength={2048}
          onChange={event => setValue(event.target.value)}
          className="mt-1 block w-full rounded border border-slate-500 bg-slate-900 p-2" />
      </label>
      <button className="rounded border border-cyan-400 px-3" type="submit">Usar token</button>
      <button className="rounded border border-slate-500 px-3" type="button" onClick={() => {
        setDashboardToken(''); setValue(''); setStatus('Token eliminado de la sesión.');
      }}>Olvidar token</button>
    </form>
    <p className="mt-2" role="status">{status}</p>
  </details>;
}
