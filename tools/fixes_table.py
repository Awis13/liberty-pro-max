#!/usr/bin/env python3
"""The README's list of fixes and options, from src/launcher_options.h.

  tools/fixes_table.py            prints the list
  tools/fixes_table.py --readme   rewrites it in README.md, between the
                                  <!-- fixes:begin --> and <!-- fixes:end -->
                                  lines

Every switch of the launcher's Advanced tab (kFixes) and every choice
(kShaderChoices), grouped by what it is about, with its default. A switch
the groups below do not name lands in "Other": add it to a group.
"""
import os
import re
import sys

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(here, 'src/launcher_options.h')).read()

fixes = []  # (env, title, default)
for m in re.finditer(r'\{@"\w+", "GTAIV_(\w+)", @"((?:[^"\\]|\\.)*)"(?:, (true|false))?\}', src):
    fixes.append((m.group(1), m.group(2).replace('\\"', '"'), m.group(3) != 'false'))
choices = []  # (env, title, items, default index)
for m in re.finditer(r'\{@"\w+", "GTAIV_(\w+)", @"([^"]*)", @\[([^\]]*)\], (\d+)\}', src):
    items = re.findall(r'@"([^"]*)"', m.group(3))
    choices.append((m.group(1), m.group(2).rstrip(':'), items, int(m.group(4))))

GROUPS = [
    ('High frame rates', 'The game was made for 30 fps; these keep it right at any frame rate.',
     'LADDER_FIX TIMER_FLOOR PHYSICS_STEP SLIDE_OBJECT SHAKE_FIX HELI_ROTOR_DAMAGE LOADING_ANIMATION '
     'MINIGAME_30FPS RADAR_EVERY_FRAME HOOD_CAMERA OUT_OF_COMMISSION_FIX CUTSCENE_CLOCK WATER_FLIP'),
    ('Cameras', '',
     'COVER_CAMERA CAMERA_SHAKE CUTSCENE_CAMERA CAR_CAMERA_RETURN ACTION_CAMERA STUNT_JUMP_CAMERA '
     'MOUSE_CAMERA_DELAY CENTERED_CAR_CAMERA CENTERED_FOOT_CAMERA PAD_CAMERA_DELAY CAR_CAMERA_DELAY '
     'AIM_ZOOM_MEMORY HELI_PAD_RETICLE WHEEL_STUNT_CAMERA EDITOR_FREE_CAMERA DRIVEBY_FOV_FIX SHAKE_BLUR_FIX'),
    ('Wide screens and the interface', '',
     'PAUSE_MAP_ASPECT CUTSCENE_PILLARS WEB_PAGES CURSOR_SIZE HIDE_CURSOR_ON_PAD RETICLE_HEALTH RADAR_ZOOM_DELAY '
     'INSTANT_MENUS MENU_RIGHT_CLICK MENU_EXTRA_INFO PAUSE_INFO_SPACING LONG_MENU_LISTS'),
    ("FusionFix", "Its mission and model fixes need its update folder in the game folder. Its shaders do not "
     "work in the port yet, so their options are not listed.",
     'FF_DATA'),
    ('Lights, shadows and effects', '',
     'SIGN_GLOW_FIX LOD_LIGHTS CONSOLE_REFLECTIONS MIRROR_OFFSET WATER_TILING CORONA_LIMIT REVERSING_LIGHTS RAIN_LIGHTING '
     'HELI_SEARCHLIGHTS NIGHT_SHADOW_RESOLUTION BRAKE_LIGHTS_STOPPED BLOOM DEPTH_OF_FIELD MOTION_BLUR '
     'VEHICLE_NIGHT_SHADOWS EMISSIVE_NO_DEPTH_WRITE DYNAMIC_SHADOWS_EVERYWHERE CAR_SHADOW_ANY_DISTANCE '
     'NO_STATIC_CAR_SHADOWS NO_CUTSCENE_STATIC_CAR_SHADOWS HIGH_DETAIL_REFLECTIONS GLASS_SHARD_COLOURS '
     'BULLET_TRACERS TBOGT_TRACER_FX TREE_SHADOWS TURN_INDICATORS TURN_INDICATORS_MANUAL'),
    ('Gameplay', '',
     'MOUSE_RECOIL PLAYER_FULL_DAMAGE SAVE_HEALTH_CAP SAVE_IN_VEHICLE RADIO_FIX TRAIN_LANES DATE_FIX HELI_CEILING '
     'COP_HATS COUNTER_DEATH_ANIM BIKE_FEET SNIPER_MOVEMENT AUTO_LADDERS DEATH_MUSIC NO_PHYSICS_SLEEP '
     'KEEP_WHEELS_TURNED ALWAYS_RUN EXTENDED_SNIPER NO_IMPOSTOR_HEIGHT_CUTOFF PHONE_CHEATS ALT_DIALOGUE'),
    ('Limits, streaming and stability', '',
     'DRAW_DISTANCE_FIX VEHICLE_BUDGET BIGGER_POOLS DRAWABLE_LIMIT NO_MEM_RESTRICT EXTRA_STREAMING_MEMORY '
     'COMPARE_STRING_FIX ERRORS_TO_LOG AIM_PROJECTILE_FIX'),
    ('Mac, iPad and Apple TV', '', 'SAVE_SYNC RUN_IN_BACKGROUND'),
]


def table():
    out = []
    named = set()
    # FusionFix's shader options are hidden in the launchers too (FixShown).
    shown = lambda e: e == 'FF_DATA' or not e.startswith('FF_')
    rows = {e: f'| {t} | {"on" if d else "off"} |' for e, t, d in fixes
            if shown(e) and 'needs FusionFix shaders' not in t}
    for e, t, items, d in choices:
        if shown(e):
            rows[e] = f'| {t}: {", ".join(items)} | {items[d]} |'
    for title, note, envs in GROUPS:
        envs = envs.split()
        named.update(envs)
        present = [e for e in envs if e in rows]
        if not present:
            continue
        out += [f'#### {title}', '']
        if note:
            out += [note, '']
        out += ['| Option | Default |', '|---|---|'] + [rows[e] for e in present] + ['']
    other = [e for e in rows if e not in named]
    if other:
        out += ['#### Other', '', '| Option | Default |', '|---|---|'] + [rows[e] for e in other] + ['']
    total = len(rows)
    return [f'{total} switches and choices in the launcher (Advanced tab; the iPad has them on its settings '
            'screen). Each one is also an environment variable, `GTAIV_` and the name in capitals in '
            '`src/launcher_options.h`.', ''] + out


lines = table()
if '--readme' in sys.argv:
    path = os.path.join(here, 'README.md')
    text = open(path).read()
    begin, end = '<!-- fixes:begin -->', '<!-- fixes:end -->'
    a, b = text.index(begin) + len(begin), text.index(end)
    open(path, 'w').write(text[:a] + '\n' + '\n'.join(lines) + '\n' + text[b:])
    print(f'README.md: {len(lines)} lines of fixes')
else:
    print('\n'.join(lines))
