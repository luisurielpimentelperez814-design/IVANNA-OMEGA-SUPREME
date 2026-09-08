/**
 * usePersist — hook genérico de persistencia localStorage para IVANNA-OMEGA-SUPREME
 * Cero dependencias externas, tipado estricto, SSR-safe.
 *
 * Diseño: la escritura a localStorage NO ocurre dentro del updater de setState.
 * Los updaters deben ser funciones puras — React StrictMode (activo en main.tsx)
 * los invoca dos veces en desarrollo, lo que duplicaría el side effect. En su
 * lugar, el siguiente valor se calcula fuera del updater usando una ref con el
 * valor actual, y se escribe una sola vez antes de actualizar el estado.
 */
import { useState, useRef, useCallback, Dispatch, SetStateAction } from 'react';

const PREFIX = 'ivanna_omega:';

const hasStorage = typeof localStorage !== 'undefined';

function read<T>(key: string, fallback: T): T {
  if (!hasStorage) return fallback;
  try {
    const raw = localStorage.getItem(PREFIX + key);
    if (raw === null) return fallback;
    return JSON.parse(raw) as T;
  } catch {
    // JSON corrupto guardado por una versión anterior — se descarta sin romper la app.
    return fallback;
  }
}

function write<T>(key: string, value: T): void {
  if (!hasStorage) return;
  try {
    localStorage.setItem(PREFIX + key, JSON.stringify(value));
  } catch {
    // Quota excedida o modo privado — la app sigue funcionando sin persistencia.
  }
}

/**
 * Igual que useState pero el valor sobrevive recargas.
 * @param key   clave única dentro del namespace ivanna_omega:
 * @param init  valor inicial si no hay nada guardado
 */
export function usePersist<T>(
  key: string,
  init: T
): [T, Dispatch<SetStateAction<T>>] {
  const [state, setStateRaw] = useState<T>(() => read<T>(key, init));
  const stateRef = useRef<T>(state);
  stateRef.current = state;

  const setState: Dispatch<SetStateAction<T>> = useCallback(
    (action) => {
      const prev = stateRef.current;
      const next =
        typeof action === 'function'
          ? (action as (prev: T) => T)(prev)
          : action;
      write(key, next); // side effect FUERA del updater — se ejecuta una sola vez
      setStateRaw(next);
    },
    [key]
  );

  return [state, setState];
}

/** Utilidad para limpiar todas las claves persisted de la app */
export function clearAllPersisted(): void {
  if (!hasStorage) return;
  Object.keys(localStorage)
    .filter((k) => k.startsWith(PREFIX))
    .forEach((k) => localStorage.removeItem(k));
}
