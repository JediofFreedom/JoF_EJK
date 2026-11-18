This suite compiles the production archive loader, checksum-feed restart scope,
shutdown/startup, directory mount and pure-reorder paths, and archive file
resolver. Platform enumeration/cvars/commands are fixture services; ZIP opens,
offsets, payload reads, native strong file stamps and MD4 checksum code are real.

It requires zero repeated catalogue-entry reads on an unchanged eligible restart,
while still enumerating directories, closing old handles, resetting references
and rebuilding the provider index. Pure and ordinary checksum bytes are checked
against a fresh ZIP walk, including empty entries and duplicate-name winners.
Changed/new/removed/invalid archives, same-size/same-mtime replacement, a change
between pre/post stamps, configuration changes, pure order, cfg fallback and
exception cleanup have explicit fixtures. Final allocation counts must be zero.

`fs_restartCache` defaults to `1`. Set `0` before initial startup to compare the
ordinary catalogue rebuild. The fixture verifies both the unconfigured default
and the explicit `0` override. Only effective-configuration-preserving
checksum-feed restarts can reuse metadata.
Explicit `FS_Restart`, downloads/server restarts and effective game/path changes
retain the full rebuild. Ignored `fs_game` changes under `fs_globalcfg 1` preserve
the existing effective mount rules. No background filesystem calls are added.
