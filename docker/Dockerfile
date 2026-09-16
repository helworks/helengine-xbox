# Pinned nxdk toolchain image for the Helengine Original Xbox handler (v0.1).
#
# The Docker build context must be the pinned nxdk checkout itself (with
# submodules), exactly as nxdk's own Dockerfile expects. scripts/prepare_toolchain.ps1
# verifies the checkout commit before building and passes it as NXDK_COMMIT so the
# identity is baked into the image. The base image is the same Alpine build image
# nxdk's upstream Dockerfile uses at the pinned revision, referenced by digest.
FROM ghcr.io/xboxdev/nxdk-buildbase@sha256:b14cd17f59449c0c3bdc86dc04dde411cdd0eaaeb3539fbe85a598dec3846f52

ARG NXDK_COMMIT
ENV NXDK_DIR=/opt/nxdk
ENV PATH=/opt/nxdk/bin:$PATH

COPY ./ /opt/nxdk/

# The context comes from a Windows checkout: restore executable bits on the
# wrapper scripts and refuse CRLF wrappers (they fail silently under sh).
RUN set -eux; \
    test -n "${NXDK_COMMIT}"; \
    chmod 0755 /opt/nxdk/bin/*; \
    ! grep -l "$(printf '\r')" /opt/nxdk/bin/* /opt/nxdk/Makefile /opt/nxdk/lib/Makefile; \
    printf '%s\n' "${NXDK_COMMIT}" > /opt/nxdk/HELENGINE_NXDK_COMMIT

# Host tools first (cxbe, vp20compiler, fp20compiler, extract-xiso), then the
# nxdk runtime libraries with the same optimisation flags nxdk's published
# "latest" image uses. Application Debug/Release builds only change the
# application's own flags; the SDK libraries stay as built here.
RUN set -eux; \
    cd /opt/nxdk; \
    make tools -j"$(nproc)"

RUN set -eux; \
    cd /opt/nxdk; \
    command -v nxdk-cc; \
    make NXDK_ONLY=y CFLAGS=-O2 CXXFLAGS=-O2 -j"$(nproc)"; \
    test -f /opt/nxdk/lib/libpbkit.lib; \
    test -f /opt/nxdk/lib/xboxkrnl/libxboxkrnl.lib; \
    test -f /opt/nxdk/lib/libc++.lib; \
    test -x /opt/nxdk/tools/cxbe/cxbe; \
    test -x /opt/nxdk/tools/vp20compiler/vp20compiler; \
    test -x /opt/nxdk/tools/fp20compiler/fp20compiler; \
    test -x /opt/nxdk/tools/extract-xiso/build/extract-xiso

LABEL org.helengine.xbox.nxdk-commit="${NXDK_COMMIT}" \
      org.helengine.xbox.upstream-base="ghcr.io/xboxdev/nxdk-buildbase@sha256:b14cd17f59449c0c3bdc86dc04dde411cdd0eaaeb3539fbe85a598dec3846f52" \
      org.helengine.xbox.target="i386-pc-win32 -march=pentium3 (Original Xbox XBE)"

WORKDIR /work
