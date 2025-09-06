# Security Policy

## Supported Versions

We actively support security updates for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| main    | :white_check_mark: |
| 0.1.x   | :white_check_mark: |
| < 0.1   | :x:                |

## Security Measures

### Automated Security Scanning

This project implements multiple layers of security scanning:

1. **GitLeaks** - Scans for accidentally committed secrets, API keys, passwords
2. **TruffleHog** - Enhanced secret detection with verification capabilities
3. **Semgrep** - Static analysis for security vulnerabilities and code quality
4. **CodeQL** - GitHub's semantic code analysis for security issues
5. **Trivy** - Vulnerability scanner for dependencies and containers
6. **Bandit** - Python security linter for build scripts
7. **OWASP Dependency Check** - Identifies vulnerable dependencies

### Pre-commit Hooks

Security checks run automatically before every commit:
- Secret scanning with GitLeaks and TruffleHog
- Code style validation with checkpatch.pl (Linux kernel standards)
- Security pattern detection with Semgrep
- File integrity checks

To install pre-commit hooks:
```bash
pip install pre-commit
pre-commit install
```

### Code Security Standards

#### Kernel Module Security
- **No hardcoded secrets**: All sensitive data must be configurable or derived at runtime
- **Input validation**: All user inputs and hardware responses validated
- **Buffer overflow protection**: All buffers bounds-checked with `sizeof()` and `ARRAY_SIZE()`
- **Integer overflow checks**: Use `check_add_overflow()`, `check_mul_overflow()` for arithmetic
- **Memory safety**: Proper allocation/deallocation with error handling
- **Race condition protection**: Appropriate locking mechanisms (mutex, spinlock, RCU)

#### USB Security
- **Device verification**: Validate USB descriptors and device identity
- **Transfer size limits**: Enforce maximum transfer sizes to prevent buffer overflows
- **Timeout handling**: All USB operations have appropriate timeouts
- **Error propagation**: USB errors properly handled and not ignored

#### PTP Protocol Security
- **Command validation**: All PTP commands validated before processing
- **Response length checks**: PTP response lengths verified against expectations
- **Session management**: Proper PTP session establishment and cleanup
- **Data sanitization**: All camera data sanitized before kernel/userspace transfer

## Reporting a Vulnerability

### How to Report

**DO NOT** open a public issue for security vulnerabilities.

Instead, please report security vulnerabilities via:

1. **GitHub Security Advisories** (Preferred)
   - Go to the repository's Security tab
   - Click "Report a vulnerability"
   - Provide detailed information about the vulnerability

2. **Direct Email** 
   - Send to: [security@canon-r5-driver.org] (if available)
   - Use GPG encryption if possible (key available on request)

### What to Include

Please include as much information as possible:

- **Type of vulnerability** (buffer overflow, injection, privilege escalation, etc.)
- **Component affected** (core, USB, PTP, specific driver module)
- **Steps to reproduce** the vulnerability
- **Potential impact** (DoS, information disclosure, code execution, etc.)
- **Suggested fix** (if you have one)
- **Your contact information** for follow-up questions

### Response Process

1. **Acknowledgment**: We'll acknowledge receipt within 48 hours
2. **Investigation**: We'll investigate and assess the vulnerability within 7 days
3. **Fix Development**: Critical fixes prioritized for immediate release
4. **Disclosure**: Coordinated disclosure after fix is available
5. **Credit**: Security researchers will be credited (unless they prefer anonymity)

### Timeline Expectations

- **Critical vulnerabilities** (remote code execution, privilege escalation): 1-7 days
- **High severity** (local code execution, information disclosure): 7-30 days  
- **Medium/Low severity**: 30-90 days

## Security Architecture

### Threat Model

This kernel driver handles:
- **Untrusted USB devices**: Canon cameras could be malicious or compromised
- **Untrusted user input**: Applications using the driver may provide malicious data
- **Kernel privilege**: The driver runs in kernel space with elevated privileges

### Security Boundaries

1. **Hardware Interface**: USB communication with camera
   - Validate all USB descriptors and responses
   - Implement proper timeout and error handling
   - Limit transfer sizes to prevent resource exhaustion

2. **Kernel/Userspace Boundary**: System calls and device nodes
   - Validate all user-provided parameters
   - Use `copy_from_user()`/`copy_to_user()` for data transfer
   - Implement proper permission checks

3. **Module Boundaries**: Communication between driver components
   - Use well-defined APIs between modules
   - Validate parameters at module boundaries
   - Maintain clear separation of concerns

### Defense in Depth

1. **Input Validation**
   - All external inputs validated at entry points
   - Length checks, format validation, range checking
   - Sanitization of data before processing

2. **Memory Safety**
   - Bounds checking on all buffer operations
   - Use of safe string functions (`strscpy`, `scnprintf`)
   - Proper error handling for memory allocation failures

3. **Concurrency Safety**
   - Appropriate locking to prevent race conditions
   - Careful ordering of operations to prevent TOCTTOU bugs
   - Use of atomic operations where appropriate

4. **Resource Management**
   - Proper cleanup on error paths
   - Reference counting for shared resources
   - Prevention of resource leaks

## Development Security Guidelines

### Secure Coding Practices

1. **Never ignore return values** - Always check and handle errors
2. **Use secure functions** - Prefer `strscpy()` over `strcpy()`, `scnprintf()` over `sprintf()`
3. **Validate sizes** - Check buffer sizes before operations
4. **Clean sensitive data** - Zero out sensitive data after use
5. **Minimize attack surface** - Only expose necessary functionality
6. **Fail securely** - Default to secure state on errors

### Code Review Checklist

- [ ] All user inputs validated
- [ ] Buffer operations bounds-checked  
- [ ] Error conditions properly handled
- [ ] No hardcoded secrets or credentials
- [ ] Proper locking for shared resources
- [ ] Memory allocated/freed correctly
- [ ] Integer arithmetic checked for overflow
- [ ] USB transfers have appropriate timeouts
- [ ] PTP commands validated before processing

### Testing Security

```bash
# Run all security scans locally
make security-check

# Test with fuzzing (if available)
make fuzz-test

# Static analysis
make static-analysis

# Run with kernel debugging options
make KBUILD_EXTRA_CPPFLAGS="-DDEBUG -DCONFIG_DEBUG_KERNEL" modules
```

## Responsible Disclosure Examples

### Good Vulnerability Report
```
Title: Buffer overflow in PTP response handler

Description: The ptp_handle_response() function in canon-r5-ptp.c 
does not validate the length field in PTP responses, allowing a 
malicious camera to cause a buffer overflow.

Location: drivers/core/canon-r5-ptp.c:245
Impact: Kernel memory corruption, potential privilege escalation
Reproduction: Connect malicious USB device that sends oversized PTP response

Suggested Fix: Add length validation before memcpy() operation
```

### Poor Vulnerability Report
```
Title: Security issue
Description: Found a bug in the driver
Impact: Bad things could happen
```

## Contact Information

- **Security Team**: [Provide contact information when available]
- **GPG Key**: [Provide GPG key fingerprint if available]
- **Response Language**: English

## Acknowledgments

We appreciate security researchers who responsibly disclose vulnerabilities. 
Contributors will be listed here (with permission):

- [Security researcher acknowledgments will be listed here]