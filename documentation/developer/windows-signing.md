# Signed Windows releases

`scripts/builds/sign-release.ps1` creates a signed distributable ZIP from a
complete Windows release directory. Run it after building and installing, as
the final packaging step, instead of `compress.bat`. Ordinary developer builds
remain unsigned. The input directory is never modified, and an existing output
ZIP is never overwritten. No release is uploaded automatically.

The script signs EXEs and DLLs, including binaries inside every PK3. It keeps
valid, timestamped dependency signatures. It verifies all signatures with
SignTool, reopens each completed PK3, extracts its binaries as `.tmp` files
(matching the Windows loader), and verifies their signatures and SHA256 hashes.
Only then does it create the output ZIP. `signed-binaries.json` records the
signed binary hashes; this is an inventory, not a signed security policy.

## Publisher prerequisite

Obtain a publicly trusted code-signing identity. An EV certificate is not
required. A self-signed certificate, private-trust Azure profile, or locally
installed test root is not a solution for players' PCs. Do not commit private
keys, PFX passwords, tokens, or other credentials to this repository.

Choose either:

- A trusted CA certificate accessible in the Windows `My` certificate store,
  with its private key available through the provider's supported hardware or
  signing service. Supply its thumbprint, and `-MachineStore` if appropriate.
- Microsoft Artifact Signing, with completed publisher identity validation, a
  **Public Trust** certificate profile, and an authenticated identity with the
  certificate-profile signer role. Install its client and supply the matching
  SignTool/Dlib architecture and account metadata JSON.

Identity verification and any subscription or certificate purchase must be
completed by the publisher. The packaging script cannot create that identity.

Install SignTool via the Windows SDK. Use a trusted timestamp service; SHA256
file and RFC3161 timestamp digests are mandatory in the script. Artifact Signing
defaults to Microsoft's timestamp service.

## Build and package

Use the project's existing CMake configuration for the desired architecture,
then build and install a Release configuration into a fresh staging directory:

```powershell
cmake --build build-x64 --config Release
cmake --install build-x64 --config Release --prefix G:/releases/jof-unsigned-x64
```

Check the exit code after each command. Assemble any separately supplied
dependencies/renderers and release documentation in the installed GameData
directory before signing. Sign the complete distribution, not just its client
EXE. Build and package x86 and x64 separately. Do not use a game installation
containing third-party mods as the input: their code would also be signed.

Certificate-store example (replace the thumbprint):

```powershell
./scripts/builds/sign-release.ps1 `
  -ReleaseDirectory G:/releases/jof-unsigned-x64/JediAcademy `
  -OutputZip G:/releases/JoF-EJK-signed-x64.zip `
  -CertificateThumbprint YOUR_40_HEX_CHARACTER_CERTIFICATE_THUMBPRINT `
  -SignTool 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe'
```

Artifact Signing metadata (use your account's actual endpoint):

```json
{
  "Endpoint": "https://YOUR-REGION.codesigning.azure.net/",
  "CodeSigningAccountName": "YOUR-ACCOUNT",
  "CertificateProfileName": "YOUR-PUBLIC-TRUST-PROFILE"
}
```

```powershell
./scripts/builds/sign-release.ps1 `
  -ReleaseDirectory G:/releases/jof-unsigned-x64/JediAcademy `
  -OutputZip G:/releases/JoF-EJK-signed-x64.zip `
  -SigningDlib C:/signing/x64/Azure.CodeSigning.Dlib.dll `
  -SigningMetadata C:/signing/metadata.json `
  -SignTool 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe'
```

For CI, authenticate using the provider's supported credentials and run this
same script after installation; publish only its successfully created ZIP.
The legacy AppVeyor configuration is not enabled for signed releases by this
change: it needs publisher credentials and a current build configuration first.

## Acceptance on Windows

Signature verification does not prove Smart App Control acceptance. Before
publishing, test the exact ZIP on a Windows 11 machine with Smart App Control
**On**, using the normal browser download and installation flow. Start the
client, load each distributed renderer, join a pure server to exercise PK3 DLL
extraction, change maps, and restart. Check Event Viewer under
`Microsoft/Windows/CodeIntegrity/Operational` for blocks naming these files.
Server-provided or separately downloaded unsigned mods remain outside the
release's signing coverage.

Do not change players' security settings as part of installation. Smart App
Control has no per-app exclusion; recent Windows updates allow re-enabling it
without a clean install, so advice about a universally irreversible toggle is
outdated.

Official references:

- [Smart App Control signing requirements](https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control)
- [Artifact Signing setup and identity validation](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart)
- [Artifact Signing integration and metadata](https://learn.microsoft.com/en-us/azure/artifact-signing/how-to-signing-integrations)
- [SignTool options](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool)
- [Smart App Control FAQ](https://support.microsoft.com/en-us/windows/security/threat-malware-protection/smart-app-control-frequently-asked-questions)
