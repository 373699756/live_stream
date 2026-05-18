import { createContext, useCallback, useContext, useState } from 'react';
import { hasToken, login as apiLogin, logout as apiLogout } from '../api/client';

interface AuthContextValue {
  authenticated: boolean;
  login: (userName: string, password: string) => Promise<boolean>;
  logout: () => void;
}

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [authenticated, setAuthenticated] = useState(hasToken);

  const login = useCallback(async (userName: string, password: string) => {
    const ok = await apiLogin(userName, password);
    if (ok) {
      setAuthenticated(true);
    }
    return ok;
  }, []);

  const logout = useCallback(() => {
    apiLogout();
    setAuthenticated(false);
  }, []);

  return (
    <AuthContext.Provider value={{ authenticated, login, logout }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth(): AuthContextValue {
  const ctx = useContext(AuthContext);
  if (!ctx) {
    throw new Error('useAuth must be used inside AuthProvider');
  }
  return ctx;
}
