# PR 03 compatibility cleanup

The factory prerequisite is resolved by upstream commit
[`10df03e03d06cc05813393dd66af1178df38b4b5`](https://github.com/janagor/vkexec/commit/10df03e03d06cc05813393dd66af1178df38b4b5),
now pinned in Dependencies.cmake. It contains the three namespace qualifications
and upstream regression coverage. The local patch, configure-time header repair
and redundant parent Vulkan header links are removed.

The user confirmed validation of the final revision during review. No additional
agent build or test run was performed. The recorded 131/131 test result and image
comparisons in [the migration baseline](../vkexec-baseline.md) belong to the earlier
`fb6b1f71d719b8943ccde48d237103bb3b305d75` integration, not this final commit.
No new test count, clean-checkout log or image metrics were supplied with that
confirmation; those earlier measurements are preserved with their original scope.

## Historical include-order investigation

The regression includes `vkexec/tensor.hpp` **before** compute_pipeline.hpp
and frame_ring.hpp. tensor.hpp declares `vkexec::factory::detail`; subsequent
unqualified factory implementation lookup incorrectly selected that namespace
before the upstream fix. Including compute_pipeline.hpp first hid the bug.
All three factory calls are instantiated, along with the public VMA allocator
factory. The local test target is mandatory when configuring tests and reports
missing required dependency targets explicitly. It never executes Vulkan work.

Clang 22.1.8, C++23, Release consumer flags on the earlier revision:
compute-first passed; tensor-first failed all three names; tensor-first with
the three qualifications passed. The adjacent compiler logs preserve this
investigation. The duplicate proposed upstream patch has been removed because
the implementation and regression now live in vkexec.

The historical binary evidence archive and checksum are kept locally under
`out/pr03-validation/historical-evidence/`, outside the source documentation.
Concise results remain in the migration baseline. GCC, Debug, Windows, full
performance repetitions and interactive scenarios were excluded from this PR's
final validation scope. No PR 04 or PR 05 changes are included.
