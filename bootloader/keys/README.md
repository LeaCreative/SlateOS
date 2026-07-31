# Firmware signing keys (ECDSA-P256)

MCUBoot requires **ECDSA-P256**. This is a **different trust domain** from the
repository Ed25519 index keys used by the companion app store.

## Private key handling

1. Generate **once** on an offline machine or HSM:
   ```
   imgtool keygen -k slate_priv.pem -t ecdsa-p256
   ```
2. Store `slate_priv.pem` in a secrets vault / HSM. **Never commit it.**
3. Export the public key only:
   ```
   imgtool getpub -k slate_priv.pem -o slate_pub.pem
   ```
4. Commit `slate_pub.pem` here. Regenerate the C array:
   ```
   python scripts/gen_pubkey_c.py
   ```
5. Rotate keys only with a planned dual-key MCUBoot build; a sealed watch with
   the old pubkey cannot accept images signed by a new key without a
   transitional bootloader (treat as factory event).

## `.gitignore`

Ensure `*_priv.pem`, `*.pem.bak`, and local signing copies stay ignored.
