Build this directory as a standalone CMake project and run CTest. The suite
compiles the production SFX resampler and streamed-WAV header parser with bounded
in-memory filesystem services.

It checks PCM/volume equivalence at common input/output rates, the old phase wrap
after 2^24 samples, very low rates, invalid/overflowing output sizes, every WAV
truncation, short reads, odd metadata and PCM-data chunks, extended PCM formats,
data-before-format ordering, invalid sample widths/channels/rates, and bounded music reads.
Run with AddressSanitizer and UndefinedBehaviorSanitizer where supported.

Music accepts mono/stereo PCM with 8-bit or 16-bit samples and positive rates.
Chunk sizes are checked against both the RIFF boundary and actual file length;
metadata and padding bytes are excluded from the sample count. The parser reads
headers only and seeks directly to PCM data. SFX output at common sample rates
uses the existing nearest-sample conversion; long and very low-rate sounds
advance correctly instead of wrapping or remaining on the first input sample.
