#!/usr/bin/env python3
"""Build the MT6895 UFS driver archive for the UniXNU xnu-8796 kernel.

Compiles the three-layer driver into a static archive that the kernel links via
CONFIG_XNU_EXTRA_LD_ARCHIVES. Layers:
  - UfsHc.c          (L1: UFS host controller, 0x112b0000)
  - UfsExtents.c     (L2: file extent -> raw block mapping)
  - Mt6895UfsBlock.cpp (L3: IOBlockStorageDevice publication, design skeleton)

Invoked by UniXNU's build system as a component recipe. The toolchain paths and
kernel include tree come from the assembled target tree that `./unixnu tree`
reports. This is a build-integration skeleton: it produces the archive with the
correct architecture/ABI, but Layer 3 needs the full IOKit headers of the
selected xnu-8796 profile to compile cleanly (see README).
"""
from pathlib import Path
import os
import subprocess
import sys

# Paths provided by the UniXNU build environment (override via env).
# The framework runs this script from tree/scripts/, while the source lives in
# tree/drivers/ShengUfsMt6895/. Derive the component dir from our own location
# so `COMPONENT_DIR` is only an override, not a hard requirement.
_here = Path(__file__).resolve().parent
_default_component = _here.parent / 'drivers' / 'ShengUfsMt6895'
if not (_default_component / 'UfsHc.c').is_file():
    _default_component = _here  # fallback: run from the component repo itself
COMPONENT = Path(os.environ.get('COMPONENT_DIR', str(_default_component)))
OUT = Path(os.environ.get('ARCHIVE_OUT', 'build/ShengUfsMt6895'))
# The framework does not export XNU_INCLUDE. The real kernel headers (with
# kern/clock.h etc.) live in the assembled fakeroot Kernel.framework; the
# storage-abi tree is only IOKit glue. Default to the framework Headers and
# let the compile step add PrivateHeaders too.
XNU_INC = Path(os.environ.get('XNU_INCLUDE', '') or
               '.cache/fakeroot/System/Library/Frameworks/Kernel.framework/Versions/A/Headers')
TOOLCHAIN = Path(os.environ.get('UNIXNU_TOOLCHAIN', '/opt/unixnu-linux'))

def _find_tool(name):
    # The Linux toolchain lays clang out in llvm/bin/, not the macOS-style
    # XcodeDefault.xctoolchain/usr/bin/. Probe both, then fall back to PATH.
    import shutil
    env_val = os.environ.get(name, '')
    # Some callers set CC=VARNAME placeholders — only trust values that look
    # like a real executable (a path or a bare command on PATH).
    if env_val and (Path(env_val).is_file() or shutil.which(env_val)):
        return env_val
    for rel in ('llvm/bin/' + name, 'XcodeDefault.xctoolchain/usr/bin/' + name):
        p = TOOLCHAIN / rel
        if p.is_file():
            return str(p)
    # `name` may be "CC"/"AR" (env-var names), not the binary name — always
    # fall back to the actual binary so bash resolves it via PATH.
    binary = 'clang' if name == 'CC' else 'ar' if name == 'AR' else name
    found = shutil.which(binary)
    return found if found else binary

CC = _find_tool('CC')
AR = _find_tool('AR')

# `-arch` is Apple-clang only; Linux clang selects the target via --target.
CFLAGS = ['-target', 'arm64-apple-darwin', '-fno-builtin',
          '-ffreestanding', '-O2', '-Wall', '-Wno-unused-variable']

# os/atomic_private.h and friends live in PrivateHeaders, a sibling of Headers.
_private = XNU_INC.parent / 'PrivateHeaders'
INCLUDES = ['-I', str(COMPONENT), '-I', str(XNU_INC)]
if _private.is_dir():
    INCLUDES += ['-I', str(_private)]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    objs = []
    c_sources = ['UfsHc.c', 'UfsExtents.c']
    cpp_sources = ['Mt6895UfsBlock.cpp']

    for src in c_sources:
        p = COMPONENT / src
        if not p.exists():
            print(f'missing {p}', file=sys.stderr); return 1
        obj = OUT / (Path(src).stem + '.o')
        cmd = [CC] + CFLAGS + INCLUDES + ['-c', str(p), '-o', str(obj)]
        print(' '.join(cmd)); subprocess.run(cmd, check=True)
        objs.append(obj)

    for src in cpp_sources:
        p = COMPONENT / src
        if not p.exists():
            print(f'missing {p} (Layer 3 skeleton)', file=sys.stderr)
            print('skipping L3; L1+L2 archive still usable for early read tests',
                  file=sys.stderr)
            continue
        obj = OUT / (Path(src).stem + '.o')
        cmd = [CC] + CFLAGS + ['-std=c++17'] + INCLUDES + ['-c', str(p), '-o', str(obj)]
        print(' '.join(cmd)); subprocess.run(cmd, check=True)
        objs.append(obj)

    if not objs:
        print('no objects built', file=sys.stderr); return 1
    lib = OUT / 'libShengUfsMt6895.a'
    subprocess.run([AR, 'rcs', str(lib)] + [str(o) for o in objs], check=True)
    print(f'archive: {lib} ({len(objs)} objects)')
    return 0

if __name__ == '__main__':
    sys.exit(main())
