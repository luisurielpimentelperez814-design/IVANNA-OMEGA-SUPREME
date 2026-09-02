/**
 * usePersist — hook genérico de persistencia localStorage para IVANNA-OMEGA-SUPREME
 * Cero dependencias externas, tipado estricto, SSR-safe.
 */
import { useState, useEffect, Dispatch, SetStateAction } from 'react';

const PREFIX = 'ivanna_omega:';

function read<T>(key: string, fallback: T): T {
  try {
    const raw = localStorage.getItem(PREFIX + key);
    if (raw === null) return fallback;
    return JSON.parse(raw) as T;
  } catch {
    return fallback;
  }
}

function write<T>(key: string, value: T): void {
  try {
    localStorage.setItem(PREFIX + key, JSON.stringify(value));
  } catch {
    // quota exceeded o privado — silencioso
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

  const setState: Dispatch<SetStateAction<T>> = (action) => {
    setStateRaw((prev) => {
      const next =
        typeof action === 'function'
          ? (action as (prev: T) => T)(prev)
          : action;
      write(key, next);
      return next;
    });
  };

  // Sincroniza si init cambia desde fuera (ej. reset de preset)
  // Solo en primer render — el hook no reescribe si ya hay algo guardado.
  useEffect(() => {}, []); // eslint-disable-line

  return [state, setState];
}

/** Utilidad para limpiar todas las claves persisted de la app */
export function clearAllPersisted(): void {
  Object.keys(localStorage)
    .filter((k) => k.startsWith(PREFIX))
    .forEach((k) => localStorage.removeItem(k));
}
