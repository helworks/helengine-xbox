# Original Xbox content staging verification — 2026-10-07

The content stager now validates the entire manifest and builder-owned cooked output list before replacing the previous packaging input tree. Missing later files, invalid paths, collisions, and cancellation before cleanup preserve the previous stage.

Source and stage roots must be separate directories with no parent/child overlap. Directory comparisons include the separator boundary, so `source` and `source-other` remain valid siblings. Existing symbolic links and junctions in either root's ancestors or an input file's ancestors fail explicitly before cleanup.

Paths retain their original case, normalize backslashes to forward slashes, and collapse repeated separators before collision detection. Both dot and parent traversal segments are rejected, consistent with the shared packaged-path policy. Colons and trailing dots/spaces are rejected to prevent Windows file aliases and alternate data streams. Manifest outputs and builder outputs use the same collision set.

Successful staging still removes obsolete files and copies both declared artifacts and builder-owned outputs in declaration order.

## Validation

```powershell
rtk proxy dotnet test helengine-xbox\builder.tests\helengine.xbox.builder.tests.csproj --artifacts-path C:\dev\helworks\builds\helengine-xbox\coord-20261007\artifacts --no-restore --logger "trx;LogFileName=builder-final.trx" --results-directory C:\dev\helworks\builds\helengine-xbox\coord-20261007\test-results --verbosity quiet
```

Final full builder suite: **22/22 passed**, zero skipped, on Windows. The 22 new regression cases cover overlapping roots, similar sibling prefixes, later missing files, duplicate path aliases, invalid paths, precanceled empty lists, successful stage replacement, and real Windows junctions at source root, stage root, and an input ancestor.

Test scratch directories derive from `AppContext.BaseDirectory`, so artifact isolation determines their location. Linked-directory fixtures use Windows junctions through RTK/PowerShell and symbolic links on Unix. The Unix branch was not executed in this session.

Results: `builds/helengine-xbox/coord-20261007/test-results/builder-final.trx`. Both the actual builder and its referenced shared projects compiled successfully. Existing compiler/style warnings remain.

## Remaining limits

Validation precedes destructive cleanup but copying is not an atomic tree replacement: I/O failure, cancellation during copying, or concurrent source changes can still leave a partial stage. This change does not add content-hash verification or native image/runtime acceptance. No emulator or screenshot was used.
