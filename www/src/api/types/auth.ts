export interface AuthPrincipal {
    user_name: string;
    session_id: string;
    role: string;
    must_change_password?: boolean;
}

export interface AuthState {
    authenticated: boolean;
    mustChangePassword: boolean;
    principal?: AuthPrincipal;
}
