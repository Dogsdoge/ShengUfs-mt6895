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
XNU_INC = Path(os.environ.get('XNU_INCLUDE', ''))
TOOLCHAIN = Path(os.environ.get('UNIXNU_TOOLCHAIN', '/opt/unixnu-linux'))

def _find_tool(name):
    # The Linux toolchain lays clang out in llvm/bin/, not the macOS-style
    # XcodeDefault.xctoolchain/usr/bin/. Probe both, then fall back to PATH.
    if os.environ.get(name):
        return os.environ[name]
    for rel in ('llvm/bin/' + name, 'XcodeDefault.xctoolchain/usr/bin/' + name):
        p = TOOLCHAIN / rel
        if p.is_file():
            return str(p)
    return name

CC = _find_tool('CC')
AR = _find_tool('AR')

ARCH = '-arch arm64'
CFLAGS = [ARCH, '-target', 'arm64-apple-darwin', '-fno-builtin',
          '-ffreestanding', '-O2', '-Wall', '-Wno-unused-variable']

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
        cmd = [CC] + CFLAGS + ['-I', str(COMPONENT), '-I', str(XNU_INC),
                               '-c', str(p), '-o', str(obj)]
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
        cmd = [CC] + CFLAGS + ['-std=c++17', '-I', str(COMPONENT),
                               '-I', str(XNU_INC), '-c', str(p), '-o', str(obj)]
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
