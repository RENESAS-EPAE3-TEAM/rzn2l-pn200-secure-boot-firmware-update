# PN2.0 RZ/N2L Secure App Packaging Scheme A / Scheme B Comparison

## 1. Background and Objectives

The current PN2.0 App is not a simple image that is loaded as one contiguous block and executed directly. Instead, the legacy `RZAP` manifest published by the App describes multiple copy segments:

```text
LDR_PRG_RBLOCK                -> LDR_PRG_WBLOCK
LDR_DATA_RBLOCK               -> LDR_DATA_WBLOCK
VECTOR_RBLOCK                 -> VECTOR_WBLOCK
USER_PRG_RBLOCK               -> USER_PRG_WBLOCK
USER_DATA_RBLOCK              -> USER_DATA_WBLOCK
SYSTEM_PRG_RBLOCK             -> SYSTEM_PRG_WBLOCK
SYSTEM_DATA_RBLOCK            -> SYSTEM_DATA_WBLOCK
NONCACHE_RBLOCK               -> NONCACHE_WBLOCK
SHARED_NONCACHE_BUFFER_RBLOCK -> SHARED_NONCACHE_BUFFER_WBLOCK
```

The Secure SSBL objectives are:

1. Use the Renesas RSIP / Secure Boot mechanism to confirm that the App has not been tampered with.
2. After successful verification, start the App using the original PN2.0 multi-segment deployment model.
3. Support OTA, multi-bank operation, AES-128-CBC encryption, version rollback control, and more granular deployment policies in the future.

Based on these objectives, two directions are currently under discussion:

- **Scheme A: Custom RZSM secure manifest**
- **Scheme B: The overall App as a Renesas-native secure image**

This document re-examines the strengths, limitations, and applicable scenarios of both schemes based on the Renesas `R01UH1017EJ` Security Features documentation.

## 2. Key Points of the Renesas Native Secure Boot Mechanism

The secure boot / secure update model in Renesas documentation is mainly based on the following objects:

```text
Key Certificate
Code Certificate
Image
```

The Code Certificate header contains:

```text
Flags
Load Addr
Dest Addr
Image Size
Image Version
Build Number
```

`Flags bit0` indicates whether encryption/decryption is enabled for the image.

Encryption-related parameters are described by TLVs, for example:

```text
IV TLV
Image cipher info TLV
  - Key selection
  - IV selection
  - Destination address
```

The documentation explicitly states:

- Signature algorithm: ECDSA NIST P-256
- Hash algorithm: SHA-256
- Encryption/decryption algorithm: AES-128-CBC
- The Code Certificate `Image Version` can be used for anti-rollback.
- OTP includes an ARBC anti-rollback counter area.
- The RSIP driver provides `R_RSIP_SB_ManifestVerify()` for manifest verification and code decryption.

Therefore, if the App can be organized as a Renesas-native secure image, Scheme B can extensively reuse the official certificate format, signing flow, encryption flow, and toolchain.

## 3. Scheme Definitions

### 3.1 Scheme A: Custom RZSM Secure Manifest

Scheme A uses a custom RZSM manifest to describe the multi-segment deployment relationships of the PN2.0 App.

A typical layout is:

```text
App secure package
+-----------------------------+
| Key Certificate             |
+-----------------------------+
| Code Certificate            |
+-----------------------------+
| RZSM manifest               |
+-----------------------------+
| segment payload 0           |
+-----------------------------+
| segment payload 1           |
+-----------------------------+
| ...                         |
+-----------------------------+
```

The Code Certificate signature covers:

```text
RZSM manifest + all segment payloads
```

An RZSM entry can describe:

```text
segment_id
flags
src_offset
dst_addr
file_size
mem_size
load_attr
hash
```

It can be extended in the future with:

```text
cipher_size
plain_size
iv
key_id
segment_version
rollback_domain
bank_policy
compression
zero_fill
```

SSBL flow:

```text
1. R_RSIP_SB_ManifestVerify() verifies the package body
2. Read the RZSM manifest
3. Validate the header / package range / segment range / destination whitelist / entry point
4. Copy or decrypt each segment
5. Cache clean/invalidate
6. Jump to entry_point
```

### 3.2 Scheme B: The Overall App as a Renesas Secure Image

Scheme B retains the PN2.0 legacy `RZAP` manifest and combines it with the App raw image as one overall App, which is directly signed or encrypted by the Renesas-native Code Certificate.

A typical layout is:

```text
App secure image
+-----------------------------+
| Key Certificate             |
+-----------------------------+
| Code Certificate            |
+-----------------------------+
| overall App                 |
|   - legacy RZAP manifest    |
|   - App raw image           |
+-----------------------------+
```

The Code Certificate image points to:

```text
overall App = legacy manifest + App image
```

SSBL flow:

```text
1. R_RSIP_SB_ManifestVerify() verifies the overall App
2. After successful verification, read the legacy RZAP manifest within the overall App
3. Validate manifest magic / entry_count / entry_point / destination whitelist
4. Copy each segment according to the legacy manifest
5. Cache clean/invalidate
6. Jump to entry_point
```

If Renesas-native AES-128-CBC encryption is enabled, the overall App is one contiguous encrypted image.

## 4. Core Differences

### 4.1 Trust Boundary

Scheme A:

```text
The Code Certificate authenticates the RZSM manifest + segment payloads.
The SSBL trusts RZSM, but still performs whitelist and bounds checks.
```

Scheme B:

```text
The Code Certificate authenticates the overall App.
After successful verification, the SSBL reads the legacy RZAP manifest.
The SSBL still performs whitelist and bounds checks.
```

Both schemes must follow the same rule:

```text
Do not read or trust addresses, lengths, or the entry point in the manifest before R_RSIP_SB_ManifestVerify() succeeds.
```

### 4.2 Deployment Model

Scheme A is naturally designed for multi-segment deployment:

```text
segment 0 -> dst0
segment 1 -> dst1
segment 2 -> dst2
```

Scheme B is naturally designed for one contiguous image:

```text
overall App -> one verified/decrypted image
```

Because the PN2.0 App itself uses a multi-segment deployment model, Scheme B still requires the legacy manifest to provide the multi-segment deployment semantics.

### 4.3 Address Representation

Scheme A recommends package-relative offsets:

```text
src_offset = offset of the payload within the package body
```

Advantages:

- Independent of the actual Flash bank in which the App is programmed.
- Bank A and Bank B can share the same offset model.
- Does not require the App link base to equal the physical Flash address.

The legacy manifest in Scheme B normally uses link-time absolute source addresses:

```text
src = __section_begin("XXX_RBLOCK")
```

If the App is programmed into a different bank, the following condition may occur:

```text
manifest->src != actual flash source address
```

In this case, one of the following is required:

1. Link and generate a separate manifest for each bank; or
2. Relocate `src` based on the active bank in the SSBL.

### 4.4 Reuse of Renesas Official Tools and Examples

Scheme A:

- Can reuse the Key Certificate, Code Certificate, and `R_RSIP_SB_ManifestVerify()`.
- Requires custom RZSM manifest generation, parsing, and policy validation.
- Deviates to some extent from the image model in the official sample loader.

Scheme B:

- Closely matches the official secure boot image model.
- More easily reuses the secureboot utility.
- More easily reuses Code Certificate `Image Version` anti-rollback.
- More easily reuses official AES-128-CBC image encryption.
- Is closer to the whole-image verification method used by the Secure Update sample.

## 5. Comparison for the AES-128-CBC Encryption Scenario

### 5.1 Scheme B: Overall App Encryption

When Scheme B fully uses Renesas-native image encryption, the flow is:

```text
plain overall App = legacy manifest + App image
encrypted overall App = AES-128-CBC(plain overall App)

SSBL:
1. Verify the encrypted overall App
2. Decrypt the encrypted overall App
3. Obtain a contiguous decrypted overall App
4. Read the legacy manifest from the decrypted overall App
5. Copy each segment according to the manifest
```

Advantages:

- Most closely matches the Renesas-native secure image design.
- The Code Certificate encryption flag, IV TLV, and Image cipher info TLV can be used directly.
- The signature target can cover the encrypted image.
- Provides the highest reuse of the toolchain and reference examples.

Main issue:

```text
The complete decrypted overall App must be available before the legacy manifest can be read.
```

**Warning:** When using overall encryption, Scheme B requires a sufficiently large contiguous RAM/SDRAM staging buffer.

**Warning:** For a large PN2.0 App, allocating this staging buffer may be difficult. This is the key limitation of Scheme B.

If the manifest is kept in plaintext and only the payload is encrypted to avoid the large buffer, Scheme B becomes a partially custom layout and its complexity approaches Scheme A.

### 5.2 Scheme A: Segment Encryption

Scheme A can use RZSM entries to describe the encryption information for each segment:

```text
segment_id
flags = encrypted
src_offset
cipher_size
plain_size
dst_addr
iv
key_id
```

SSBL flow:

```text
1. R_RSIP_SB_ManifestVerify() authenticates the complete package body
2. Parse RZSM
3. For each encrypted segment:
   - Read ciphertext from flash package_body + src_offset
   - Decrypt with AES-CBC
   - Write output to dst_addr
4. For each plaintext segment:
   - Copy directly to dst_addr
5. Jump to entry_point
```

Advantages:

- Does not require a large complete-App buffer.
- Can decrypt segments from Flash into their final RAM destinations one by one.
- Requires only a small working buffer, or can directly use Flash as input and RAM as output if the API supports it.
- Fits the PN2.0 multi-segment deployment architecture.
- Supports different policies for different segments.

Limitations:

- More complex SSBL code.
- More complex packaging tool.
- Requires clear definition of AES-CBC padding, IV, key selection, and plaintext/ciphertext sizes.
- If Renesas-native image decryption is not used and the AES API is called manually, the implementation responsibility is greater.

### 5.3 Key Conclusion for AES-CBC

If the objective is:

```text
Encrypt and decrypt the complete App as one contiguous image.
```

Scheme B is preferable.

If the objective is:

```text
Encrypt or keep plaintext multiple segments independently, deploy them directly to their final runtime addresses, and avoid a large RAM staging buffer.
```

Scheme A is preferable.

## 6. Multi-Bank / OTA / Rollback Comparison

### 6.1 Multi-Bank

Scheme A:

- Uses `src_offset`, which naturally supports Bank A / Bank B.
- Does not require changes to the segment source offsets when the package is placed in a different bank.
- The SSBL only needs to know the current package body base.

Scheme B:

- `src` in the legacy manifest is normally an absolute address.
- Multi-bank operation requires either relinking or runtime relocation.
- If relocation is omitted, segments may be copied from an incorrect Flash address.

Conclusion:

```text
Scheme A is more natural for multi-bank scenarios.
```

### 6.2 OTA Strategy

Scheme A:

- The RZSM header can contain package-level policy.
- RZSM entries can contain segment-level policy.
- Fits segmented updates, optional resource segments, and different permissions for different segments.

Scheme B:

- Better suited to whole-App updates.
- Segmented policy requires extending the legacy manifest or introducing an additional convention.
- As policy complexity grows, it gradually becomes another custom manifest.

Conclusion:

```text
For simple full-package OTA, Scheme B is simpler.
For complex segmented OTA, Scheme A is clearer.
```

### 6.3 Anti-Rollback

Scheme A:

- Can define fields such as `package_version` and `rollback_counter_id` in the RZSM header.
- Is flexible, but requires custom rules and implementation of OTP ARBC comparison/update policy.

Scheme B:

- The Code Certificate header already contains `Image Version`.
- Renesas documentation explicitly states that this field can be used for anti-rollback.
- More closely follows the official secure boot design.

Conclusion:

```text
For official single-image rollback, Scheme B is more straightforward.
For multiple modules or multiple rollback domains, Scheme A is more flexible.
```

## 7. Security Comparison

### 7.1 Shared Security Requirements

Both Scheme A and Scheme B must meet the following requirements:

1. The manifest must be within the signed coverage range.
2. The manifest must not be trusted before signature verification succeeds.
3. The destination address whitelist must still be validated after signature verification succeeds.
4. Source range, destination range, size overflow, and entry point must be validated.
5. The failure path must be handled; an unverified App must never continue to boot.
6. If encryption is enabled, CBC provides confidentiality only; integrity must rely on signature/authentication.
7. For production, consider configuring the OTP root public-key hash, secure boot common key, SBEN, ARBC, JTAG authentication, and related settings.

### 7.2 Scheme A Security Risks

- The custom manifest format may have an incomplete design.
- The custom packaging tool may fail to sign critical fields.
- A custom AES-CBC decryption flow can easily introduce padding, IV, or key-handling errors.
- Validation cost increases when deviating from the official reference flow.

### 7.3 Scheme B Security Risks

- The legacy manifest has limited expressiveness.
- Absolute `src` addresses may be incorrect in a multi-bank configuration.
- **Warning:** Overall encryption requires a decrypted-image staging buffer. Poor buffer management may leave plaintext residuals.
- If the manifest is plaintext and the payload is encrypted, the payload layout requires additional definition and validation.
- If the legacy manifest is extended excessively, compatibility and maintenance complexity increase.

## 8. Summary of Advantages and Limitations

| Dimension | Scheme A: Custom RZSM Manifest | Scheme B: Overall App Secure Image |
| --- | --- | --- |
| Reuse of Renesas official tools | Medium | High |
| Reuse of official AES-128-CBC image encryption | Limited or requires additional design | Strong |
| Fit for PN2.0 multi-segment deployment | Strong | Depends on the legacy manifest |
| Need for a large complete decrypted-App buffer | No | **Usually required for overall encryption** |
| Segment encryption | Strong | Weak |
| Multi-bank | Strong | Requires relocation or relinking |
| Simple full-package OTA | Possible, but more complex | Strong |
| Complex segmented OTA | Strong | Weak |
| Anti-rollback | Flexible, custom | Can reuse Code Certificate Image Version |
| Manifest/policy extensibility | Strong | Limited by the legacy manifest |
| Implementation complexity | High | Low to medium |
| Consistency with reference examples | Medium | High |
| Long-term productization flexibility | Strong | Medium |

## 9. Recommended Approach

### 9.1 Short-Term Debug Recommendation

To quickly establish the Secure SSBL chain, prioritize Scheme B for verification in the short term:

```text
Key Certificate + Code Certificate + overall App
```

Where:

```text
overall App = legacy RZAP manifest + App raw image
```

Objectives:

- Verify the Renesas certificate / signature flow.
- Verify App copying and startup.
- Verify official AES-128-CBC image encryption later.

### 9.2 Long-Term Productization Recommendation

If the target includes:

```text
Multi-bank
Complex OTA
Segment encryption
Different policies for different segments
No large RAM staging buffer
Multiple rollback domains
```

Keep and continue to evolve Scheme A.

Scheme A is more suitable as the final PN2.0 secure package format.

### 9.3 Compromise Approach

A two-stage strategy can be used:

Stage 1: Scheme B

```text
Overall App signing / optional overall encryption
Quick reuse of Renesas tools and reference examples
Establish the secure chain
```

Stage 2: Scheme A

```text
RZSM manifest + segment payloads
Support multi-bank, segment encryption, and complex OTA policy
```

The outer Renesas Code Certificate can also remain unchanged while the internal package body uses RZSM:

```text
Key Certificate
Code Certificate
RZSM manifest
segment payloads
```

In this way, the outer layer is still verified by Renesas RSIP, while the PN2.0 Secure SSBL performs the fine-grained deployment policy at the inner layer.

## 10. Final Conclusion

The core value of Scheme B is:

```text
Maximum reuse of the Renesas Secure Boot / Secure Update official model.
```

It is suitable for:

```text
Full-package signing
Full-package AES-128-CBC encryption
Single-bank or fixed addresses
Simple OTA
Quickly establishing the secure boot chain
```

The core value of Scheme A is:

```text
Maximum fit for PN2.0 multi-segment deployment and future complex OTA policies.
```

It is suitable for:

```text
Multi-bank
Segment encryption
No large RAM staging buffer
Complex deployment policies
Long-term productization expansion
```

Therefore, the current recommendation is:

```text
Prioritize Scheme B for short-term integration.
Retain and evolve Scheme A for long-term productization.
```

If AES-128-CBC is used only for overall App encryption, Scheme B is more natural. **However, it is necessary to carefully evaluate whether sufficient contiguous RAM/SDRAM staging buffer space is available to store the complete decrypted overall App.**

If AES-128-CBC must decrypt each segment and deploy it directly to its final RAM address, Scheme A is more appropriate.
