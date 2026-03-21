# GPG Signing for Spectra Releases

Spectra releases include GPG-signed artifacts and checksums so users can verify download authenticity.

## APT Repository (Ubuntu/Debian — enables `apt upgrade`)

Run this once to add the Spectra APT repository:

```bash
curl -fsSL https://danlil240.github.io/Spectra/apt/spectra-archive-keyring.asc \
  | sudo gpg --dearmor -o /etc/apt/keyrings/spectra.gpg

echo "deb [signed-by=/etc/apt/keyrings/spectra.gpg] https://danlil240.github.io/Spectra/apt stable main" \
  | sudo tee /etc/apt/sources.list.d/spectra.list

sudo apt update
sudo apt install spectra
```

After that, `sudo apt upgrade` will automatically pick up new Spectra releases.

---

## For Users: Verifying Downloads

### 1. Import the signing key

Each release includes `spectra-release-signing-key.asc`. Download and import it:

```bash
# From a specific release:
curl -fsSL https://github.com/danlil240/Spectra/releases/download/v0.1.0/spectra-release-signing-key.asc | gpg --import

# Or from the repo:
curl -fsSL https://raw.githubusercontent.com/danlil240/Spectra/main/packaging/gpg/spectra-release-signing-key.asc | gpg --import
```

### 2. Verify checksums

```bash
gpg --verify SHA256SUMS.asc SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing
```

### 3. Verify individual artifacts

```bash
gpg --verify spectra_0.1.0_amd64_ubuntu24.04.deb.asc \
             spectra_0.1.0_amd64_ubuntu24.04.deb
```

### 4. Verify a git tag

```bash
git verify-tag v0.1.0
```

---

## For Maintainers: Setup Guide

### A. Generate a dedicated release signing key

```bash
gpg --full-generate-key
# Choose: RSA (sign only), 4096 bits, expiry 2y+, "Spectra Release Signing <your@email>"
```

### B. Add secrets to GitHub

```bash
# 1. Export private key (base64-encoded)
gpg --armor --export-secret-keys YOUR_KEY_ID | base64 -w0
#    → Add as repo secret: GPG_PRIVATE_KEY

# 2. Add passphrase
#    → Add as repo secret: GPG_PASSPHRASE
```

Go to **Settings → Secrets and variables → Actions** and add both secrets.

### C. (Optional) Commit the public key to the repo

```bash
mkdir -p packaging/gpg
gpg --armor --export YOUR_KEY_ID > packaging/gpg/spectra-release-signing-key.asc
git add packaging/gpg/spectra-release-signing-key.asc
git commit -m "Add GPG release signing public key"
```

### D. Configure git tag signing locally

```bash
# Tell git which key to use
git config --global user.signingkey YOUR_KEY_ID

# Sign tags by default
git config --global tag.gpgSign true

# (Optional) Sign commits too
git config --global commit.gpgSign true
```

### E. Create a signed release tag

```bash
git tag -s v0.2.0 -m "Release v0.2.0"
git push origin v0.2.0
```

The `-s` flag creates a GPG-signed tag. GitHub will display a **Verified** badge on it.

### F. Upload your public key to GitHub

Go to **Settings → SSH and GPG keys → New GPG key** and paste:

```bash
gpg --armor --export YOUR_KEY_ID
```

This enables the "Verified" badge on signed tags and commits.

---

## What the CI does automatically

When `GPG_PRIVATE_KEY` is set, the release workflow (`release.yml`):

1. Imports the GPG key
2. Generates `SHA256SUMS` for all artifacts
3. Creates detached signatures (`.asc`) for every artifact and the checksums file
4. Exports the public key as `spectra-release-signing-key.asc`
5. Uploads everything to the GitHub Release

If the secret is not set, signing is skipped gracefully (SHA256SUMS are still generated).
