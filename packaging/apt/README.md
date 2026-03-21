# Spectra APT Repository

Add this repository to install and auto-upgrade Spectra via `apt`:

```bash
curl -fsSL https://danlil240.github.io/Spectra/apt/spectra-archive-keyring.asc \
  | sudo gpg --dearmor -o /etc/apt/keyrings/spectra.gpg

echo "deb [signed-by=/etc/apt/keyrings/spectra.gpg] https://danlil240.github.io/Spectra/apt stable main" \
  | sudo tee /etc/apt/sources.list.d/spectra.list

sudo apt update
sudo apt install spectra
```
