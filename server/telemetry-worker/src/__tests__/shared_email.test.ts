// SPDX-License-Identifier: GPL-3.0-or-later

import { describe, it, expect } from "vitest";
import { escapeHtml, sendEmail } from "../../../shared/email";

const config = { apiKey: "re_key", from: "HelixScreen <noreply@example.org>", to: "me@example.org" };
const message = { subject: "Subj", html: "<p>hi</p>", text: "hi" };

function fakeFetch(status: number, body: string) {
  const calls: { url: string; init: RequestInit }[] = [];
  const impl = (async (url: string, init: RequestInit) => {
    calls.push({ url, init });
    return new Response(body, { status });
  }) as unknown as typeof fetch;
  return { calls, impl };
}

describe("sendEmail", () => {
  it("posts the message to Resend with bearer auth", async () => {
    const f = fakeFetch(200, "{}");
    await sendEmail(config, message, f.impl);
    expect(f.calls).toHaveLength(1);
    expect(f.calls[0].url).toBe("https://api.resend.com/emails");
    expect(f.calls[0].init.method).toBe("POST");
    const headers = f.calls[0].init.headers as Record<string, string>;
    expect(headers.Authorization).toBe("Bearer re_key");
    expect(JSON.parse(f.calls[0].init.body as string)).toEqual({
      from: config.from,
      to: [config.to],
      subject: "Subj",
      html: "<p>hi</p>",
      text: "hi",
    });
  });

  it("throws with the status and body on a non-2xx", async () => {
    const f = fakeFetch(422, "bad from address");
    await expect(sendEmail(config, message, f.impl)).rejects.toThrow(/422.*bad from address/);
  });
});

describe("escapeHtml", () => {
  it("escapes the five HTML-significant characters", () => {
    expect(escapeHtml(`<a href="x" title='y'>&</a>`)).toBe("&lt;a href=&quot;x&quot; title=&#39;y&#39;&gt;&amp;&lt;/a&gt;");
  });
});
