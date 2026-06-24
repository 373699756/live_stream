import {
    createContext,
    useCallback,
    useContext,
    useEffect,
    useState,
} from 'react';
import {
    changePassword as apiChangePassword,
    login as apiLogin,
    logout as apiLogout,
    validateSession,
} from '../api/auth';
import { onAuthInvalid, onMustChangePassword } from '../api/authSession';
import { getTimeInfo, syncBrowserTime } from '../api/time';
import type { AuthPrincipal } from '../api/types';

interface AuthContextValue {
    authenticated: boolean;
    mustChangePassword: boolean;
    principal?: AuthPrincipal;
    ready: boolean;
    login: (
        userName: string,
        password: string,
    ) => Promise<{ ok: boolean; error?: string }>;
    changePassword: (
        oldPassword: string,
        newPassword: string,
    ) => Promise<boolean>;
    logout: () => void;
}

const AuthContext = createContext<AuthContextValue | null>(null);

function syncBrowserTimeAfterAuth() {
    void getTimeInfo()
        .then((status) => {
            if (status.browser_sync_on_login && status.manual_sync_allowed) {
                return syncBrowserTime();
            }
            return undefined;
        })
        .catch(() => {
            // 浏览器校时失败不能阻断登录，系统维护页仍可手动重试。
        });
}

export function AuthProvider({ children }: { children: React.ReactNode }) {
    const [authenticated, setAuthenticated] = useState(false);
    const [mustChangePassword, setMustChangePassword] = useState(false);
    const [principal, setPrincipal] = useState<AuthPrincipal | undefined>();
    const [ready, setReady] = useState(false);

    useEffect(() => {
        let mounted = true;
        void validateSession().then((state) => {
            if (mounted) {
                setAuthenticated(state.authenticated);
                setMustChangePassword(state.mustChangePassword);
                setPrincipal(state.principal);
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
            setPrincipal(undefined);
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
            setPrincipal(state.principal);
            if (!state.mustChangePassword) {
                syncBrowserTimeAfterAuth();
            }
        }
        return { ok: state.authenticated, error: state.error };
    }, []);

    const changePassword = useCallback(
        async (oldPassword: string, newPassword: string) => {
            const ok = await apiChangePassword(oldPassword, newPassword);
            if (ok) {
                setAuthenticated(true);
                setMustChangePassword(false);
                setPrincipal((current) =>
                    current
                        ? { ...current, must_change_password: false }
                        : current,
                );
                syncBrowserTimeAfterAuth();
            }
            return ok;
        },
        [],
    );

    const logout = useCallback(() => {
        void apiLogout();
        setAuthenticated(false);
        setMustChangePassword(false);
        setPrincipal(undefined);
    }, []);

    return (
        <AuthContext.Provider
            value={{
                authenticated,
                mustChangePassword,
                principal,
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
