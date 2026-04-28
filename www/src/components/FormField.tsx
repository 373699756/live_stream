import type { ReactNode } from 'react';

interface FormFieldProps {
  label: string;
  children: ReactNode;
}

export function FormField({ label, children }: FormFieldProps) {
  return (
    <label className="form-field">
      <span className="form-label">{label}</span>
      <span className="form-control">{children}</span>
    </label>
  );
}
