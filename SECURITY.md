# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 1.0.x   | ✅ Security updates |

## Reporting a Vulnerability

If you discover a security vulnerability, please **do not** report it via public Issues.

Instead, report it privately through:

- **GitHub**: Use [Security Advisories](https://github.com/aspect-building/VersatileEngine/security/advisories/new) (preferred)
- **Subject**: `[SECURITY] VersatileEngine - Brief description`

### What to Include

Please include the following in your report:

1. **Description** — Brief description of the vulnerability type and impact
2. **Reproduction steps** — Detailed steps to reproduce
3. **Affected scope** — Affected versions, modules, and configurations
4. **Severity assessment** — Your assessment of the vulnerability's severity
5. **Suggested fix** — If available, your proposed fix or mitigation

### Response Timeline

- We will acknowledge receipt within **7 business days**
- We will provide an initial assessment and action plan within **30 days**
- Patches will be released as soon as possible after confirmation

### Process

1. Upon receiving a report, we confirm the vulnerability's existence and severity
2. Develop a fix
3. Release a security update once the fix is ready
4. Publicly disclose vulnerability details after the update is released
5. Record the security fix in the CHANGELOG

## Security Best Practices

When using VersatileEngine, please keep the following security considerations in mind:

### Network Services

- CBS and WebSocket services have no built-in authentication by default; use them within trusted network environments
- If exposing services externally, deploy a firewall or reverse proxy in front
- XService's JSON over TCP protocol transmits in plaintext; use TLS encryption in production environments

### Data Handling

- Sensitive data (passwords, tokens, etc.) in the data tree should be sanitized
- Validate inputs when deserializing JSON/XML to prevent malformed data injection
- Binary serialization data should come from trusted sources only

### Build Security

- Use the latest versions of compilers and dependencies
- Enable compiler security features (ASLR, Stack Canaries, DEP, etc.)
- Regularly update third-party dependencies (asio2, etc.)

## Acknowledgments

We thank all researchers who responsibly report security vulnerabilities. Your efforts make VersatileEngine more secure.
