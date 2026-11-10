# Assembles caves with GNU as for the generators. A cave's jumps into the game are assembled with the game at two
# addresses: the four bytes that differ by exactly that distance are a jump's rel32, and are kept relative to a game at
# zero, so the mod adds the real distance when it places the cave.
import re, struct, subprocess, tempfile

BASES = (0x10000000, 0x20000000)


def assemble(source):
    with tempfile.TemporaryDirectory() as tmp:
        open(tmp + '/c.s', 'w').write('.intel_syntax noprefix\n.text\n' + source + '\n')
        subprocess.run(['as', '--64', '-o', tmp + '/c.o', tmp + '/c.s'], check=True)
        subprocess.run(['ld', '-Ttext=0', '-o', tmp + '/c.elf', tmp + '/c.o'], check=True, stderr=subprocess.DEVNULL)
        subprocess.run(['objcopy', '-O', 'binary', '-j', '.text', tmp + '/c.elf', tmp + '/c.bin'], check=True)
        return open(tmp + '/c.bin', 'rb').read()


def at_game(text, game):
    """`game+0x..` in a cave's text as an address with the game at `game`."""
    return re.sub(r'game\+0x([0-9A-Fa-f]+)', lambda m: '0x%x' % (game + int(m.group(1), 16)), text)


def relocatable(source_at, where):
    """(code with its jumps relative to a game at zero, offsets of those jumps); source_at(game) gives the source."""
    a = assemble(source_at(BASES[0])); b = assemble(source_at(BASES[1]))
    assert len(a) == len(b)
    jumps = []; i = 0
    while i < len(a):
        if a[i] != b[i]:
            at = i - 3 if a[i - 3:i] == b[i - 3:i] and i >= 3 and struct.unpack_from('<i', b, i - 3)[0] - struct.unpack_from('<i', a, i - 3)[0] == 0x10000000 else None
            assert at is not None, (where, i)
            jumps.append(at); i = at + 4
        else: i += 1
    code = bytearray(a)
    for at in jumps: struct.pack_into('<i', code, at, struct.unpack_from('<i', a, at)[0] - BASES[0])
    return bytes(code), jumps
