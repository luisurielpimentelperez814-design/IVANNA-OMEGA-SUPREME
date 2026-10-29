// Operator-supplied dashboard token. Memory only: never persisted or bundled.
let dashboardToken = '';

export function setDashboardToken(token: string): void {
  const value = token.trim();
  if (value.length > 2048 || /[^\x21-\x7e]/.test(value)) {
    throw new Error('El token debe contener caracteres ASCII sin espacios (máximo 2048).');
  }
  dashboardToken = value;
}

export function apiHeaders(): Record<string, string> {
  return {
    'Content-Type': 'application/json',
    ...(dashboardToken ? { Authorization: `Bearer ${dashboardToken}` } : {}),
  };
}
