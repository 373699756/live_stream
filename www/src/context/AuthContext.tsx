import { createContext, useCallback, useContext, useEffect, useState } from 'react';
import {
  hasToken,
  login as apiLogin,
  logout as apiLogout,
  onAuthInvalid,
  validateSession,
} from '../api/client';

interface AuthContextValue {
  authenticated: boolean;
  ready: boolean;
  login: (userName: string, password: string) => Promise<boolean>;
  logout: () => void;
}

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [authenticated, setAuthenticated] = useState(hasToken);
  const [ready, setReady] = useState(!hasToken());

  useEffect(() => {
    let mounted = true;
    if (!hasToken()) {
      setReady(true);
      return () => {
        mounted = false;
      };
    }
    void validateSession().then((valid) => {
      if (mounted) {
        setAuthenticated(valid);
        setReady(true);
      }
    });
    return () => {
      mounted = false;
    };
  }, []);

  useEffect(() => {
    return onAuthInvalid(() => {
      setAuthenticated(false);
      setReady(true);
    });
  }, []);

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
    <AuthContext.Provider value={{ authenticated, ready, login, logout }}>
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
