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

`s_resampleFast 1` uses contiguous conversion when the input and output rates
match, or duplicates each sample once when the output rate is exactly doubled.
It preserves output PCM and lip-sync volume; `0` selects the existing phase loop
for comparisons. Other ratios and large lengths rounded by the existing float
calculation keep that loop. Differential checks exercise both paths across
1,536 rate/width/count/alignment combinations, every PCM16 value, and a large
odd length above 2^24 samples. Both software and OpenAL sample loads use this
shared conversion before their existing mixing/upload paths.

`s_loadProfile 1` enables default-off source-read, MP3 inspection/stream/decode,
PCM conversion, and OpenAL lipsync/upload timings. Use `s_loadProfileReset` and
`s_loadProfileReport`; shutdown also reports. The total asset-load counter
includes the component stages, and inspection also counts music callers, so
the rows must not be summed or interpreted as a complete map-loading timer.
The profiler uses a steady clock and never polls/journals engine events. The
fixture verifies disabled operation makes no clock calls, counters/reset, and
discarding a measurement across a reset.
