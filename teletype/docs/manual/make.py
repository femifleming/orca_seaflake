#!/usr/bin/env python3
"""Builds the Orca Seaflake TT manual and keeps the README in step with it.

    python3 docs/manual/make.py

  body.html      the manual's prose, with {{KEYS}} {{FLASHING}} {{OPS}} {{SIZE}} {{VERSION}} {{DATE}}
  keys.tsv       every key (the app's help, KEYS tab)
  ops.tsv        every operator: glyph, group, name, inputs, outputs, what it does (help, OPERATORS tab)
  flashing.txt   the flashing + restore steps, word for word what the Flash window shows

  -> manual.html -> "Orca Seaflake TT Manual.pdf" (headless Chrome)
  -> dist/README.md (the flashing steps as Markdown)
  -> README.md: the parts between <!-- flashing:start/end --> and <!-- ops:start/end -->
  and it checks that every operator glyph in the module's own help (module/seaflake_help.c) is in ops.tsv."""
import datetime, html, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
VERSION = '0.2'
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
e = html.escape


def tsv(name):
    rows = [l.split('\t') for l in open(os.path.join(HERE, name), encoding='utf-8').read().splitlines() if l.strip()]
    return rows[0], rows[1:]


# ------------------------------------------------------------------ flashing.txt -> HTML / Markdown
def flashing_blocks(lines):
    """('h1'|'h2'|'p'|'ol'|'ul'|'pre', text) blocks; list items and paragraphs join their wrapped lines."""
    out = []
    for l in lines:
        if l.startswith('# '): out.append(['h1', l[2:]])
        elif l.startswith('## '): out.append(['h2', l[3:]])
        elif l.startswith('    '):
            if out and out[-1][0] == 'pre': out[-1][1] += '\n' + l[4:]
            else: out.append(['pre', l[4:]])
        elif re.match(r'^\d+\. ', l): out.append(['ol', re.sub(r'^\d+\. ', '', l), int(l.split('.')[0])])
        elif l.startswith('- '): out.append(['ul', l[2:]])
        elif not l.strip(): out.append(['', ''])
        elif out and out[-1][0] in ('ol', 'ul', 'p'): out[-1][1] += ' ' + l.strip()
        else: out.append(['p', l.strip()])
    return [b for b in out if b[0]]


def flashing_html(lines, first_chapter=13):
    out, n, prev = [], first_chapter - 1, ''
    ids = {13: 'flash', 14: 'stock'}
    for blk in flashing_blocks(lines):
        kind, text = blk[0], blk[1]
        if kind == 'ol' and prev != kind: out.append('<ol start="%d">' % blk[2])
        elif kind == 'ul' and prev != kind: out.append('<ul>')
        if kind not in ('ol', 'ul') and prev in ('ol', 'ul'): out.append('</%s>' % prev)
        if kind == 'h1':
            n += 1
            out.append('<h2 id="%s"%s>%d. %s</h2>' % (ids.get(n, 'ch%d' % n), ' class="pb"' if n == first_chapter else '', n, e(text)))
        elif kind == 'h2': out.append('<h3>%s</h3>' % e(text))
        elif kind == 'pre': out.append('<pre>%s</pre>' % e(text))
        elif kind == 'p': out.append('<p>%s</p>' % e(text))
        else: out.append('<li>%s</li>' % e(text))
        prev = kind
    if prev in ('ol', 'ul'): out.append('</%s>' % prev)
    return '\n'.join(out)


def flashing_md(lines, level=2):
    out, prev, k = [], '', 0
    for blk in flashing_blocks(lines):
        kind, text = blk[0], blk[1]
        if kind == 'h1': out += ['', '#' * level + ' ' + text, '']; k = 0
        elif kind == 'h2': out += ['', '#' * (level + 1) + ' ' + text, '']; k = 0
        elif kind == 'pre': out += ['', '```sh'] + text.split('\n') + ['```', '']
        elif kind == 'p': out += ['', text, '']
        elif kind == 'ol': out.append('%d. %s' % (blk[2], text))
        else: out.append('- ' + text)
        prev = kind
    return '\n'.join(out).strip() + '\n'


# ------------------------------------------------------------------ tables
def keys_html():
    head, rows = tsv('keys.tsv')
    out = ['<table><tr><th>Key</th><th>Where</th><th>Does</th></tr>']
    for k, w, d in rows: out.append('<tr><td><code>%s</code></td><td class="sub">%s</td><td>%s</td></tr>' % (e(k), e(w), e(d)))
    return '\n'.join(out) + '</table>'


def ops_html():
    head, rows = tsv('ops.tsv')
    out = ['<h2 id="ops" class="pb">16. Operator reference</h2>',
           '<p>Every operator the firmware implements, by group: the glyph, its inputs (the cells it reads), its outputs, and what it does. '
           'The app\'s help (<kbd>cmd</kbd>+<kbd>/</kbd>) shows the same table, from the same file (<code>docs/manual/ops.tsv</code>), '
           'and the build checks it against the module\'s own help (<kbd>alt</kbd>+<kbd>h</kbd>). <code>&gt;x</code> means <code>&gt;</code> with the selector <code>x</code>.</p>']
    group = None
    for g, grp, name, inp, outp, what in rows:
        if grp != group:
            if group is not None: out.append('</table>')
            group = grp
            out.append('<h3>%s</h3>' % e(grp))
            out.append('<table><tr><th>Glyph</th><th>Name</th><th>Inputs</th><th>Outputs</th><th>What it does</th></tr>')
        out.append('<tr><td><code>%s</code></td><td>%s</td><td class="sub">%s</td><td class="sub">%s</td><td>%s</td></tr>' % tuple(map(e, (g, name, inp, outp, what))))
    out.append('</table>')
    return '\n'.join(out)


def ops_md():
    head, rows = tsv('ops.tsv')
    out, group = [], None
    for g, grp, name, inp, outp, what in rows:
        if grp != group:
            group = grp
            out += ['', '### ' + grp, '', '| Glyph | Name | Inputs | Outputs | What it does |', '|---|---|---|---|---|']
        cell = lambda t: t.replace('|', '\\|')
        out.append('| `%s` | %s | %s | %s | %s |' % (cell(g), cell(name), cell(inp), cell(outp), cell(what)))
    return '\n'.join(out).strip() + '\n'


def keys_md():
    head, rows = tsv('keys.tsv')
    out = ['| Key | Where | Does |', '|---|---|---|']
    for k, w, d in rows: out.append('| `%s` | %s | %s |' % (k.replace('|', '\\|'), w, d.replace('|', '\\|')))
    return '\n'.join(out) + '\n'


# ------------------------------------------------------------------ checks against the firmware
def check_ops():
    """every glyph the module's own help lists must be in ops.tsv (so the two can't drift)."""
    src = open(os.path.join(ROOT, 'module', 'seaflake_help.c'), encoding='utf-8').read()
    listed = set()
    for m in re.finditer(r'^\s*"(\S)  ', src, re.M): listed.add(m.group(1).replace('\\\\', '\\').replace('\\"', '"'))
    listed -= {'-'}
    head, rows = tsv('ops.tsv')
    ours = set(r[0] for r in rows) | {'E', 'N', 'S', 'W'}
    for pair in ('J / Y', 'E N S W'): pass
    missing = sorted(g for g in listed if g not in ours and g not in 'ENSW')
    if missing:
        sys.exit('ops.tsv is missing operators the module help lists: %s' % ' '.join(missing))
    return len(rows), len(listed)


def size_text():
    """flash use from the hex, for the manual."""
    hexf = os.path.join(ROOT, 'dist', 'orca-seaflake-teletype.hex')
    n, prog_hi, base = 0, 0, 0
    for l in open(hexf):
        l = l.strip()
        if not l.startswith(':'): continue
        b = bytes.fromhex(l[1:]); ln, a, t = b[0], int.from_bytes(b[1:3], 'big'), b[3]
        if t == 4: base = int.from_bytes(b[4:6], 'big') << 16
        elif t == 0:
            n += ln
            if base + a < 0x80044C00: prog_hi = max(prog_hi, base + a + ln)
    prog_area = 0x44C00   # up to the ports block; then .flash_sf (37 KB) and the 256 KB scene nvram
    used = prog_hi - 0x80000000
    return ('The firmware now puts <b>%d bytes</b> into the 512 KB chip (%d%%): the program area below the ports block at 0x80044C00 is '
            '%d KB, of which %d KB is used and about %d KB is free; the rest of the chip is the 37 KB patch area and the Teletype\'s own 256 KB scene area. '
            'RAM: about 57 KB of the 96 KB is static data and bss; the rest is heap and the 8 KB stack.' % (n, n * 100 // 524288, prog_area // 1024, used // 1024, (prog_area - used) // 1024))

def main():
    nops, nhelp = check_ops()
    lines = open(os.path.join(HERE, 'flashing.txt'), encoding='utf-8').read().split('\n')
    body = open(os.path.join(HERE, 'body.html'), encoding='utf-8').read()
    today = datetime.date.today().strftime('%-d %B %Y')
    page = (body.replace('{{KEYS}}', keys_html()).replace('{{FLASHING}}', flashing_html(lines)).replace('{{OPS}}', ops_html())
            .replace('{{SIZE}}', size_text()).replace('{{VERSION}}', VERSION).replace('{{DATE}}', today))
    out = os.path.join(HERE, 'manual.html')
    open(out, 'w', encoding='utf-8').write(page)
    pdf = os.path.join(HERE, 'Orca Seaflake TT Manual.pdf')
    subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--print-to-pdf=' + pdf, 'file://' + out],
                   stderr=subprocess.DEVNULL, check=True)
    print('wrote', pdf)

    # dist/README.md: the flashing steps
    md = ('# Flashing Orca Seaflake onto your Teletype\n\n'
          'This folder holds the firmware (`orca-seaflake-teletype.hex`, checksum in `orca-seaflake-teletype.hex.sha256`) and `update_firmware.command` '
          '(the three dfu-programmer commands). The app **Orca Seaflake TT** (`/Applications`) has the same firmware inside and flashes it from its Flash window (cmd+F). '
          'These are the steps the app shows; the manual (`docs/manual/Orca Seaflake TT Manual.pdf`) has them too.\n\n' + flashing_md(lines))
    open(os.path.join(ROOT, 'dist', 'README.md'), 'w', encoding='utf-8').write(md)

    # README.md: the generated sections
    rp = os.path.join(ROOT, 'README.md')
    r = open(rp, encoding='utf-8').read()
    def splice(text, tag, new):
        a, b = '<!-- %s:start -->' % tag, '<!-- %s:end -->' % tag
        i, j = text.index(a) + len(a), text.index(b)
        return text[:i] + '\n' + new + '\n' + text[j:]
    r = splice(r, 'flashing', flashing_md(lines, level=3))
    r = splice(r, 'ops', ops_md())
    r = splice(r, 'keys', keys_md())
    open(rp, 'w', encoding='utf-8').write(r)
    print('ops.tsv: %d rows, module help lists %d glyphs; README and dist/README.md updated' % (nops, nhelp))


if __name__ == '__main__':
    main()
