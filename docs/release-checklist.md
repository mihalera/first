# Binary release checklist
Complete and review this checklist before publishing any MiBoX binary. A successful build is not evidence that its licence obligations are satisfied. This checklist records release checks; it is not legal advice.

## Licensing approval — required before distribution
- [ ] Record the exact release commit, version, target platforms, and artefacts.
- [ ] Identify the JUCE licence path for this release: AGPLv3 compliance or a commercial JUCE licence. Record the applicable licence and evidence of permission to distribute.
- [ ] Identify the `chowdsp_dsp_utils` licence path: GPLv3 compliance or a non-GPL licence from its author. Record the applicable licence and evidence of permission to distribute.
- [ ] Have qualified legal counsel review the combined obligations and the proposed binary distribution, including any source availability, notices, corresponding-source, and written-offer requirements that apply.
- [ ] Prepare and verify all required licence texts, third-party notices, source materials or source-offer process, and distribution-page wording for the selected paths.
- [ ] Do not publish if any required approval, evidence, or compliance material is missing or unresolved.

## Build and artefact checks
- [ ] Build the release from the recorded commit using the documented platform instructions.
- [ ] Confirm the release artefacts and linked dependencies match the approved licensing review and `THIRD_PARTY_NOTICES.md`.
- [ ] Run the project checks and platform validation required for the release.
- [ ] Attach the completed checklist and licence evidence to the release record.
