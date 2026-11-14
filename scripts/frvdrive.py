#!/usr/bin/env python3
"""frvdrive.py arterial <metres> | spur <k> [--speed V]: one FRV leg on the mission in play, then exit.
The research build's test commands (living_world_frv.c) do the work; this script asks where the leg starts, puts the
Helldiver 12 m behind that start with the test teleport when it is further than 100 m away, starts the drive and waits
for its summary line. spur -1 is the spur nearest the Helldiver; a spur is driven from the arterial to its place (the
pad entry). RUN names the run folder under ~/xl-work/swarm/roads (its session.txt), as the stack's scripts do.
Output: the drive's lines in <run>/frv-<leg>.txt and the summary on stdout.
Exit: 0 verdict DROVE, 1 verdict FAILED, 2 refused or no summary (the reason is printed), 3 the level is not in play."""
import os, re, subprocess, sys, time
a = sys.argv[1:]
speed = None
if '--speed' in a: i = a.index('--speed'); speed = float(a[i + 1]); del a[i:i + 2]
if len(a) != 2 or a[0] not in ('arterial', 'spur'): sys.exit(__doc__)
kind, arg = a[0], float(a[1]) if a[0] == 'arterial' else int(a[1])
OUT = os.path.expanduser('~/xl-work/swarm/roads/') + os.environ.get('RUN', 'run4')
S = open(OUT + '/session.txt').read().strip()
COL = S + '/game/mod-cowboybingus.complex-operations.log'
LW = os.path.expanduser('~/xl-work/swarm/lw-reinforce-hold/lw.sh')
REFUSED = r'frv: (no spawn|no leg|no drive|not a command|a drive is under way|the game made no FRV|no complex mission)|vehicle code is not the reviewed one'
def size(): return os.path.getsize(COL)
def since(off):
    with open(COL, 'rb') as f:
        f.seek(off); return f.read().decode('utf-8', 'replace').splitlines()
def send(cmd, pat, wait=20.0):
    """The command's lines matching pat, waited for up to `wait` seconds (a command runs at the next game update)."""
    off = size()
    r = subprocess.run([LW, cmd], capture_output=True, text=True, timeout=90)
    end = time.time() + wait
    while time.time() < end:
        got = [l for l in since(off) if re.search(pat, l)]
        if got: return got
        time.sleep(0.5)
    print('frvdrive: no answer to "%s" (%s)' % (cmd, (r.stdout + r.stderr).strip()[-160:]), flush=True)
    return []
def crashes():
    try: return len(re.findall('crash:', open(S + '/game/modloader.log', errors='replace').read()))
    except OSError: return -1
text = open(COL, errors='replace').read(); tail = text[text.rfind('level 11 mission, seed'):]
if 'level 11 mission, seed' not in tail or any(m in tail for m in ('generator state 3 -> 0', 'the level is no longer in play', 'the ship: ')):
    print('frvdrive: the level is not in play'); sys.exit(3)
start = size()
leg = '%s %g' % (kind, arg) if kind == 'arterial' else 'spur %d' % arg
where = 'frv where arterial' if kind == 'arterial' else 'frv where spur %d' % arg
def ask():
    got = send(where, r'frv: (leg |no leg)|' + REFUSED)
    m = re.search(r'starts at \((-?[\d.]+), (-?[\d.]+)\), (\d+) m from the Helldiver.*stand at \((-?[\d.]+), (-?[\d.]+)\)', got[-1]) if got else None
    if got: print('frvdrive:', got[-1][got[-1].find('frv:'):][:300], flush=True)
    return m
m = ask()
if not m: sys.exit(2)
if float(m.group(3)) > 100 or float(m.group(3)) < 6:
    got = send('teleport! %s %s' % (m.group(4), m.group(5)), r'teleport')
    print('frvdrive:', (got[-1][got[-1].find('test command'):] if got else 'no teleport line')[:240], flush=True)
    if not got or 'teleported the Helldiver' not in got[-1]: sys.exit(2)
    time.sleep(12)  # the ground around the new place is detailed, and the mod's player position follows
    m = ask()
    if not m or float(m.group(3)) > 100: print('frvdrive: the Helldiver is not at the leg after the teleport'); sys.exit(2)
if speed is not None: send('frv speed %g' % speed, r'frv: the next drive wants')
off = size()
got = send('frv drive ' + leg, r'frv: spawned FRV|' + REFUSED)
if not got or 'spawned FRV' not in got[-1]:
    print('frvdrive: REFUSED:', got[-1][got[-1].find('frv:'):][:300] if got else 'no line'); sys.exit(2)
print('frvdrive:', got[-1][got[-1].find('frv:'):][:200], flush=True)
metres = arg if kind == 'arterial' else 400.0
end = time.time() + 120 + metres / 2.0 + 30
summary = None
while time.time() < end and summary is None:
    time.sleep(2)
    for l in since(off):
        if 'frv: summary:' in l: summary = l
    if subprocess.run(['pgrep', '-f', '[h]elldivers2\\.exe'], capture_output=True).returncode != 0: print('frvdrive: the game is gone'); break
lines = [l for l in since(start) if 'frv:' in l]
name = OUT + '/frv-%s.txt' % leg.replace(' ', '-')
open(name, 'w').write('\n'.join(lines) + '\n')
poses = [l for l in lines if 'frv: pose ' in l]
for l in lines:
    if re.search(r'frv: (FRV \w+ settled|stuck at|a positive steering|the vehicle|the level left)', l): print('frvdrive:', l[l.find('frv:'):][:300])
print('frvdrive: %d pose lines, %d crash lines in modloader.log; lines in %s' % (len(poses), crashes(), name))
if summary is None: print('frvdrive: NO SUMMARY within the time; last pose: %s' % (poses[-1][poses[-1].find('frv:'):] if poses else 'none')); send('frv stop', r'frv: summary|no drive to stop'); sys.exit(2)
print(summary[summary.find('frv: summary:'):])
sys.exit(0 if 'verdict: DROVE' in summary else 1)
