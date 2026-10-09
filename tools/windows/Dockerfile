# Cross-compiles for 64-bit Windows: MinGW gcc, clang and the MinGW build of
# SDL3. `rake windows` uses it.
FROM fedora:44

RUN dnf install -y --setopt=install_weak_deps=False \
        clang lld mingw64-gcc mingw64-pkg-config mingw64-SDL3 ruby rubygem-json rubygem-rake \
    && dnf clean all

WORKDIR /work
