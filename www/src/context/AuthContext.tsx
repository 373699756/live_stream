import { createContext, useCallback, useContext, useEffect, useState } from 'react';
import {
  changePassword as apiChangePassword,
  hasToken,
  login as apiLogin,
  logout as apiLogout,
  onAuthInvalid,
  onMustChangePassword,
  validateSession,
} from '../api/client';

interface AuthContextValue {
  authenticated: boolean;
  mustChangePassword: boolean;
  ready: boolean;
  login: (userName: string, password: string) => Promise<boolean>;
  changePassword: (oldPassword: string, newPassword: string) => Promise<boolean>;
  logout: () => void;
}

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [authenticated, setAuthenticated] = useState(hasToken);
  const [mustChangePassword, setMustChangePassword] = useState(false);
  const [ready, setReady] = useState(!hasToken());

  useEffect(() => {
    let mounted = true;
    if (!hasToken()) {
      setReady(true);
      return () => {
        mounted = false;
      };
    }
    void validateSession().then((state) => {
      if (mounted) {
        setAuthenticated(state.authenticated);
        setMustChangePassword(state.mustChangePassword);
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
      setMustChangePassword(false);
      setReady(true);
    });
  }, []);

  useEffect(() => {
    return onMustChangePassword(() => {
      setAuthenticated(true);
      setMustChangePassword(true);
      setReady(true);
    });
  }, []);

  const login = useCallback(async (userName: string, password: string) => {
    const state = await apiLogin(userName, password);
    if (state.authenticated) {
      setAuthenticated(true);
      setMustChangePassword(state.mustChangePassword);
    }
    return state.authenticated;
  }, []);

  const changePassword = useCallback(async (oldPassword: string, newPassword: string) => {
    const ok = await apiChangePassword(oldPassword, newPassword);
    if (ok) {
      setMustChangePassword(false);
    }
    return ok;
  }, []);

  const logout = useCallback(() => {
    void apiLogout();
    setAuthenticated(false);
    setMustChangePassword(false);
  }, []);

  return (
    <AuthContext.Provider
      value={{
        authenticated,
        mustChangePassword,
        ready,
        login,
        changePassword,
        logout,
      }}
    >
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
