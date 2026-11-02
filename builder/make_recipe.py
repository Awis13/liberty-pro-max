#!/usr/bin/env python3
"""The Builder's recipe: how to compile and link the game, from a reference
build of this repository (CMake, Ninja, CMAKE_EXPORT_COMPILE_COMMANDS).

  make_recipe.py BUILD_DIR SOURCE_DIR DEPS_PREFIX KIT_DIR

Writes KIT_DIR/recipe.json. Every path is rewritten to a placeholder the
Builder fills in on the player's Mac: @KIT@ (the kit: src/, include/runtime/,
include/deps/, lib/, app/), @WORK@ (its work folder: generated/, obj/,
lto-cache/, the linked gtaiv), @SDK@ (the macOS SDK). Any other absolute path
is an error, and so is a @KIT@ path missing from the kit, so the kit cannot
depend on the machine that made it.
"""
import json
import os
import re
import shlex
import subprocess
import sys

build, source, deps, kit = (os.path.realpath(a) for a in sys.argv[1:5])
app_fw = '@KIT@/app/Liberty Pro Max.app/Contents/Frameworks'
# (Resolved: vendor/x86recomp or the deps may be reached through a link.)
places = tuple((os.path.realpath(p), ph) for p, ph in (
    (os.path.join(source, 'generated'), '@WORK@/generated'),
    (os.path.join(source, 'vendor/x86recomp/src'), '@KIT@/include/runtime'),
    (os.path.join(source, 'src'), '@KIT@/src'),
    (os.path.join(deps, 'include'), '@KIT@/include/deps'),
    (os.path.join(deps, 'lib'), '@KIT@/lib'),
    (subprocess.run(['xcrun', '--show-sdk-path'], capture_output=True, text=True).stdout.strip(), '@SDK@')))


def real(p):
    """An absolute path with its symbolic links resolved (the deps prefix may
    be reached through one)."""
    return os.path.realpath(p) if p.startswith('/') else p


def rewrite(arg):
    """One argument with the reference build's paths made placeholders."""
    for prefix in ('-I', '-isystem', '-L', ''):
        if arg.startswith(prefix) and arg[len(prefix):].startswith('/'):
            path = real(arg[len(prefix):])
            for where, ph in places:
                if path == where or path.startswith(where + '/'):
                    return prefix + ph + path[len(where):]
            return arg
    return arg


def check(args, what):
    for a in args:
        if '/Volumes/' in a or '/opt/homebrew' in a or os.path.expanduser('~') in a:
            sys.exit(f'make_recipe: {what}: a machine path: {a}')
        p = re.sub(r'^-(I|isystem|L)', '', a)
        if p.startswith('/') and not p.startswith(('/usr/', '/System/')):
            sys.exit(f'make_recipe: {what}: a path outside the kit: {a}')


def sysroot(args):
    """-isysroot DIR (a versioned SDK of the reference machine) as @SDK@."""
    out = list(args)
    for i, a in enumerate(out[:-1]):
        if a == '-isysroot':
            out[i + 1] = '@SDK@'
    return out


# The compile steps of the game (gtaiv) and its lifted code (gtaiv_lifted).
units = []
for c in json.load(open(os.path.join(build, 'compile_commands.json'))):
    rel = os.path.relpath(os.path.join(c['directory'], c.get('output', '')), build)
    if not rel.startswith(('CMakeFiles/gtaiv.dir/', 'CMakeFiles/gtaiv_lifted.dir/')):
        continue
    args = sysroot(shlex.split(c['command'])[1:])  # without the compiler: xcrun clang++
    i = args.index('-o')
    obj = '@WORK@/obj/' + rel.removeprefix('CMakeFiles/')
    args[i + 1] = obj
    args = [rewrite(a) for a in args]
    if c['file'].endswith('.mm') and '-x' not in args:
        args = ['-x', 'objective-c++'] + args
    check(args, c['file'])
    units.append({'src': rewrite(real(c['file'])), 'out': obj, 'args': args})
if not units:
    sys.exit('make_recipe: no compile steps for gtaiv in compile_commands.json')

# The link: the segment of Ninja's command for gtaiv that runs the compiler
# (CMake chains its POST_BUILD steps to it with &&).
cmd = subprocess.run(['ninja', '-C', build, '-t', 'commands', 'gtaiv'], capture_output=True, text=True,
                     check=True).stdout.strip().splitlines()[-1]
words = shlex.split(cmd)
segments, cur = [], []
for w in words:
    if w == '&&':
        segments.append(cur)
        cur = []
    else:
        cur.append(w)
segments.append(cur)
seg = next((s for s in segments if s and s[0].endswith(('clang++', 'c++')) and '-o' in s
            and s[s.index('-o') + 1] == 'gtaiv'), None)
if seg is None:
    sys.exit('make_recipe: no link step for gtaiv in: ' + cmd)

link = []
it = iter(seg[1:])
for a in it:
    if a == '-o':
        next(it)
        link += ['-o', '@WORK@/gtaiv']
    elif a.startswith('CMakeFiles/gtaiv') and a.endswith('.o'):
        link.append('@WORK@/obj/' + a.removeprefix('CMakeFiles/'))
    elif a == '-Xlinker':
        b = next(it)
        if b == '-cache_path_lto':  # -Xlinker -cache_path_lto -Xlinker DIR
            next(it)
            next(it)
            link.append('-Wl,-cache_path_lto,@WORK@/lto-cache')
        elif b.startswith('-cache_path_lto,'):
            link.append('-Wl,-cache_path_lto,@WORK@/lto-cache')
        else:
            link += ['-Xlinker', rewrite(b)]
    elif a.startswith('-Wl,-cache_path_lto'):
        link.append('-Wl,-cache_path_lto,@WORK@/lto-cache')
    elif a.startswith('-Wl,-rpath,'):
        continue  # the bundle's rpath is added below
    elif a.endswith('libLLVMDemangle.a'):
        continue  # inside librecomp_dbghelp.dylib already; the game calls none of it
    elif a.endswith('.dylib') and 'x86recomp/' in a:
        link.append(f'{app_fw}/{os.path.basename(a)}')
    elif a.endswith('.dylib') and real(a).startswith(os.path.join(deps, 'lib')):
        link.append(f'{app_fw}/{os.path.basename(real(a))}')
    else:
        link.append(rewrite(a))
link = sysroot(link) + ['-Wl,-rpath,@executable_path/../Frameworks']
check([a for a in link if not a.startswith(app_fw)], 'link')

# Every @KIT@ path the recipe names must be in the kit.
missing = set()
for a in [x for u in units for x in u['args']] + link:
    a = re.sub(r'^-(I|isystem|L)', '', a)
    if a.startswith('@KIT@') and not os.path.exists(a.replace('@KIT@', kit, 1)):
        missing.add(a)
if missing:
    sys.exit('make_recipe: not in the kit:\n  ' + '\n  '.join(sorted(missing)))

# The compiler the reference build used: the Builder asks for at least it.
version = subprocess.run(['xcrun', 'clang++', '--version'], capture_output=True, text=True).stdout
m = re.search(r'clang version (\d+)', version)
recipe = {'clang': int(m.group(1)) if m else 0, 'compile': units, 'link': link}
json.dump(recipe, open(os.path.join(kit, 'recipe.json'), 'w'), indent=1)
print(f'make_recipe: {len(units)} files, link with {len(link)} arguments, clang {recipe["clang"]}')
