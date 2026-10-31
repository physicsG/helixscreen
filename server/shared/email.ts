// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resend email delivery shared by the crash and telemetry workers.

export interface EmailConfig {
  apiKey: string;
  from: string;
  to: string;
}

/** Escape the HTML-significant characters of a value interpolated into an email body. */
export function escapeHtml(str: string): string {
  return str
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

export interface EmailMessage {
  subject: string;
  html: string;
  text: string;
}

/** Send one email through Resend; throws with the HTTP status and body on a non-2xx. */
export async function sendEmail(
  config: EmailConfig,
  message: EmailMessage,
  fetchImpl: typeof fetch = fetch
): Promise<void> {
  const response = await fetchImpl("https://api.resend.com/emails", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${config.apiKey}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      from: config.from,
      to: [config.to],
      subject: message.subject,
      html: message.html,
      text: message.text,
    }),
  });

  if (!response.ok) {
    const err = await response.text();
    throw new Error(`Resend API ${response.status}: ${err}`);
  }
}
