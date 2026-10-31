// SPDX-License-Identifier: GPL-3.0-or-later
//
// GitHub App authentication for Cloudflare Workers.
// Generates installation tokens using JWT + RS256 via Web Crypto API.

const GITHUB_API = "https://api.github.com";

interface GitHubInstallationResponse {
  id: number;
}

interface GitHubTokenResponse {
  token: string;
}

// =============================================================================
// PEM → CryptoKey
// =============================================================================

/**
 * Import a PEM-encoded RSA private key for RS256 signing.
 * Handles both PKCS#1 (BEGIN RSA PRIVATE KEY) and PKCS#8 (BEGIN PRIVATE KEY).
 */
export async function importPrivateKey(pem: string): Promise<CryptoKey> {
  const pemBody = pem
    .replace(/-----BEGIN RSA PRIVATE KEY-----/, "")
    .replace(/-----END RSA PRIVATE KEY-----/, "")
    .replace(/-----BEGIN PRIVATE KEY-----/, "")
    .replace(/-----END PRIVATE KEY-----/, "")
    .replace(/\s/g, "");

  const binaryDer = Uint8Array.from(atob(pemBody), (c) => c.charCodeAt(0));
  const isPkcs8 = pem.includes("BEGIN PRIVATE KEY");

  // GitHub App keys are PKCS#1, which Web Crypto imports only wrapped in PKCS#8
  const keyData = isPkcs8 ? binaryDer : wrapPkcs1InPkcs8(binaryDer);

  return crypto.subtle.importKey(
    "pkcs8",
    keyData,
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["sign"]
  );
}

// =============================================================================
// ASN.1 helpers for PKCS#1 → PKCS#8 wrapping
// =============================================================================

function wrapPkcs1InPkcs8(pkcs1: Uint8Array): Uint8Array {
  // PKCS#8 envelope: SEQUENCE { INTEGER 0, SEQUENCE { OID rsaEncryption, NULL }, OCTET STRING { pkcs1 } }
  const rsaOid = new Uint8Array([
    0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01,
    0x01, 0x05, 0x00,
  ]);
  const version = new Uint8Array([0x02, 0x01, 0x00]);
  const octetString = wrapAsn1(0x04, pkcs1);
  return wrapAsn1(0x30, concatBytes(version, rsaOid, octetString));
}

function wrapAsn1(tag: number, content: Uint8Array): Uint8Array {
  const len = encodeAsn1Length(content.length);
  const result = new Uint8Array(1 + len.length + content.length);
  result[0] = tag;
  result.set(len, 1);
  result.set(content, 1 + len.length);
  return result;
}

function encodeAsn1Length(length: number): Uint8Array {
  if (length < 0x80) return new Uint8Array([length]);
  const bytes: number[] = [];
  let tmp = length;
  while (tmp > 0) {
    bytes.unshift(tmp & 0xff);
    tmp >>= 8;
  }
  return new Uint8Array([0x80 | bytes.length, ...bytes]);
}

function concatBytes(...arrays: Uint8Array[]): Uint8Array {
  const total = arrays.reduce((sum, a) => sum + a.length, 0);
  const result = new Uint8Array(total);
  let offset = 0;
  for (const a of arrays) {
    result.set(a, offset);
    offset += a.length;
  }
  return result;
}

// =============================================================================
// JWT
// =============================================================================

function base64url(data: string | Uint8Array): string {
  const bytes = typeof data === "string" ? new TextEncoder().encode(data) : data;
  return btoa(String.fromCharCode(...bytes))
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/, "");
}

async function createJWT(appId: string, privateKey: CryptoKey): Promise<string> {
  const now = Math.floor(Date.now() / 1000);
  const header = { alg: "RS256", typ: "JWT" };
  const payload = {
    iat: now - 60,
    exp: now + 600,
    iss: appId,
  };

  const headerB64 = base64url(JSON.stringify(header));
  const payloadB64 = base64url(JSON.stringify(payload));
  const signingInput = `${headerB64}.${payloadB64}`;

  const signature = await crypto.subtle.sign(
    "RSASSA-PKCS1-v1_5",
    privateKey,
    new TextEncoder().encode(signingInput)
  );

  return `${signingInput}.${base64url(new Uint8Array(signature))}`;
}

// =============================================================================
// GitHub API
// =============================================================================

export async function githubFetch(
  path: string,
  token: string,
  options: RequestInit = {},
  userAgent: string = "HelixScreen-Crash-Reporter",
  fetchImpl: typeof fetch = fetch
): Promise<Response> {
  return fetchImpl(`${GITHUB_API}${path}`, {
    ...options,
    headers: {
      Accept: "application/vnd.github+json",
      Authorization: `Bearer ${token}`,
      "X-GitHub-Api-Version": "2022-11-28",
      "User-Agent": userAgent,
      ...(options.headers || {}),
    },
  });
}

/**
 * Get a short-lived installation access token for the GitHub App.
 */
export async function getInstallationToken(
  appId: string,
  pemKey: string,
  owner: string,
  repo: string
): Promise<string> {
  const key = await importPrivateKey(pemKey);
  const jwt = await createJWT(appId, key);

  // Get installation ID for the repo
  const installRes = await githubFetch(
    `/repos/${owner}/${repo}/installation`,
    jwt
  );
  if (!installRes.ok) {
    const body = await installRes.text();
    throw new Error(`Failed to get installation: ${installRes.status} ${body}`);
  }
  const { id } = (await installRes.json()) as GitHubInstallationResponse;

  // Create installation access token
  const tokenRes = await githubFetch(
    `/app/installations/${id}/access_tokens`,
    jwt,
    { method: "POST" }
  );
  if (!tokenRes.ok) {
    const body = await tokenRes.text();
    throw new Error(`Failed to create token: ${tokenRes.status} ${body}`);
  }

  const { token } = (await tokenRes.json()) as GitHubTokenResponse;
  return token;
}
