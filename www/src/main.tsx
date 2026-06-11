import React from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import { AuthProvider } from './context/AuthContext';
import './styles/base.css';
import './styles/layout.css';
import './styles/preview.css';
import './styles/ai.css';
import './styles/system.css';
import './styles/forms.css';

createRoot(document.getElementById('root') as HTMLElement).render(
    <React.StrictMode>
        <AuthProvider>
            <App />
        </AuthProvider>
    </React.StrictMode>,
);
