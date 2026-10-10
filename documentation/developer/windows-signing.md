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

**Status: signing tooling only. No publicly trusted signing identity has been
configured for this project by this PR.** The certificate thumbprint parameter
selects an installed certificate; it does not make a self-signed key publicly
trusted. A successful local trust check is not proof of trust on players' PCs.

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

### Potential free route: SignPath Foundation

[SignPath Foundation](https://signpath.org/) offers free trusted signing for
approved open-source projects. JoF's public GPLv2 repository is a candidate,
not an approved participant. A maintainer can [apply here](https://signpath.org/apply.html).
No application has been submitted as part of this change.

Its [conditions](https://signpath.org/terms.html) require review before applying:
verifiable source builds, manual release approval, MFA for signing team members,
documented team roles and privacy practices, and consistent binary product/version
metadata. Signing coverage must distinguish JoF-built files from third-party
binaries; the Foundation does not authorize blanket re-signing of dependencies.

Repository-specific integration gaps found during this review:

- `.github/workflows/build.yml` builds Windows x86/x64 releases and uploads
  `build/bin`. It downloads Sunny's Vulkan DLL from an external release and
  SDL2 comes from bundled binaries. Ask their publishers for trusted signed
  builds; do not assume JoF's free signing profile may sign those binaries.
- The same workflow downloads base PK3s from `jofhub.eu`. Audit their contents
  and establish provenance before including them in the signing configuration.
- This PR supplies consistent resources for the seven JoF-built EXEs/DLLs:
  product name `JoF EternalJK` and one four-part numeric version. The CMake
  option `JOF_WINDOWS_FILE_VERSION` can override the version derived from
  `JOF_VERSION_STRING`. These metadata constraints still need provider review.
- `CMakeModules/InstallConfig.cmake` includes bundled `OpenAL32.dll` and
  `EaxMan.dll`. Audit their licenses and provenance with the Foundation; the
  repository's GPLv2 label does not establish eligibility for these dependencies.
- Discord Rich Presence is implemented in `codemp/client/cl_discordrpc.cpp`.
  Review actual network features and publish an accurate privacy policy;
  do not claim that no data leaves the PC.
- SignPath holds its signing key remotely. This PR's manual workflow uploads
  only the seven CMake-built binaries, requests signing, then uses the local
  packager's remote-input mode to insert the returned DLLs into PK3s. It never
  submits or re-signs third-party binaries under the Foundation certificate.

Application preparation (maintainer must fill in the missing facts):

| Field | Proposed information |
| --- | --- |
| Project | JoF EternalJK Client |
| Repository | https://github.com/JediofFreedom/JoF_EJK |
| Releases | https://github.com/JediofFreedom/JoF_EJK/releases |
| License | GPLv2; dependency and asset license audit still needed |
| Function | Community multiplayer client for Jedi Academy |
| Build system | GitHub Actions, CMake/MSVC, Windows x86 and x64 |
| Signing scope | JoF-built client/server EXEs, renderers, cgame/UI/game DLLs; includes DLLs in PK3s |
| Contact and signing approvers | Maintainer to supply |
| Privacy policy and MFA confirmation | Maintainer to supply after review |

Do not publish the Foundation's attribution as if service has already started.

### Activate the prepared SignPath workflow after approval

`.github/workflows/windows-signpath.yml` is manually dispatched on `beta` or
`master` in the upstream repository. It does not change the existing automatic
release workflow or publish a GitHub release. The signing action and other
actions are pinned to commit IDs.

1. Complete the application review, dependency/license audit and accurate
   privacy/signing policy. Assign maintainers and release approvers.
2. Link the repository as a trusted GitHub build system in SignPath, configure
   required app access, and require manual approval in the signing policy.
3. Review `scripts/builds/signpath-artifact.xml` with the provider and create
   the corresponding artifact configuration. It restricts the seven file
   patterns and their product/file version; third-party DLLs are excluded.
4. Create the GitHub environment `windows-signing`. Add its variables
   `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_POLICY_SLUG`,
   `SIGNPATH_CONFIGURATION_SLUG`, and secret `SIGNPATH_API_TOKEN`. Restrict
   eligible branches and assign environment reviewers as appropriate.
5. Dispatch the workflow for the reviewed branch with a four-part numeric
   version (e.g. `1.6.1.0`). Approve both architecture requests in SignPath.

The workflow builds on GitHub-hosted Windows runners and stages targets from
the CMake-generated `signing-targets-Release.txt`; it does not sign files merely
because they were found in a release directory. After signature verification,
`JoF-signed-components-*` artifacts contain the signed JoF binaries. These are
components, not full installable distributions.

It then attempts to assemble a complete core release candidate, replacing only
binaries whose original SHA256 matches this exact signing request. DLLs in PK3s
are repacked and verified as `.tmp` files. Every remaining dependency must
already have a valid timestamped signature: unsigned dependency files make this
step fail, leaving no complete-release ZIP. Get signed dependencies from their
publishers or agree another compliant solution with the Foundation. Do not
disable the check to claim Smart App Control compatibility.

This candidate does not download the normal pipeline's external Vulkan renderer
or JoF base asset packs. Audit and incorporate their provenance/signatures before
using it as the normal distribution. Run the Windows acceptance procedure below
on the final assembled distribution; only then wire verified signed outputs into
the automatic release publication jobs.

Manual remote packaging example (both directories must belong to the same
approved source-linked signing request):

```powershell
./scripts/builds/sign-release.ps1 `
  -ReleaseDirectory signing-build/install/GameData `
  -UnsignedBinariesDirectory unsigned-jof `
  -SignedBinariesDirectory signed-jof `
  -OutputZip G:/releases/JoF-signed-candidate.zip `
  -SignTool 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe'
```

References: [GitHub integration](https://docs.signpath.io/trusted-build-systems/github),
[artifact configuration](https://docs.signpath.io/artifact-configuration/),
[metadata constraints](https://docs.signpath.io/artifact-configuration/reference).

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
  -ReleaseDirectory G:/releases/jof-unsigned-x64/GameData `
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
  -ReleaseDirectory G:/releases/jof-unsigned-x64/GameData `
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
