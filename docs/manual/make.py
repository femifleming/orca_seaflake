#!/usr/bin/env python3
"""Builds docs/manual/"orca seaflake Manual.pdf" (and manual.html) from manual.md.

manual.md is the manual's own text; lines of the form

    @include <path from the repo root> [from "heading"] [to "heading"] [shift N]

pull a stretch of one of the repo's markdown docs in (OPERATORS.md, SONG_MODE.md, the terminal
and Pilot guides), so the manual stays in step with them. Needs only Python and Google Chrome.
Pictures come from img/ (screencapture of the apps)."""
import html, os, re, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
VERSION = '0.10.2'
PILOT_VERSION = '1.1.1'


def read(path):
    return open(os.path.join(ROOT, path), encoding='utf-8').read()


# ------------------------------------------------------------------------------------------------
#  assembling the source
# ------------------------------------------------------------------------------------------------
def slice_doc(text, start=None, end=None):
    """Lines from the heading `start` (included) up to the heading `end` (excluded)."""
    lines = text.split('\n')
    a = 0
    if start:
        a = next(i for i, l in enumerate(lines) if l.strip() == start)
    b = len(lines)
    if end:
        b = next(i for i in range(a + 1, len(lines)) if lines[i].strip() == end)
    return '\n'.join(lines[a:b])


def shift_headings(text, n):
    if n == 0:
        return text
    out, fence = [], False
    for l in text.split('\n'):
        if l.startswith('```'):
            fence = not fence
        if not fence and re.match(r'^#{1,6} ', l):
            m = re.match(r'^(#+) (.*)', l)
            l = '#' * max(1, min(6, len(m.group(1)) + n)) + ' ' + m.group(2)
        out.append(l)
    return '\n'.join(out)


def lower_heading(h):
    """Headings are lowercase, as in the README: a Title-Case word goes small; acronyms, ops and
    names with dots or digits (MIDI, I2M, PI.P, Node.js) stay as they are."""
    words = []
    for w in h.split(' '):
        if re.match(r"^\(?[A-Z][a-z']+[,:;)]?$", w) or w in ('A', 'I'):
            w = w.lower()
        words.append(w)
    return ' '.join(words)


def assemble(src):
    out = []
    for l in src.split('\n'):
        m = re.match(r'^@include (\S+)(.*)$', l)
        if not m:
            out.append(l)
            continue
        path, opts = m.group(1), m.group(2)
        start = re.search(r'from "([^"]+)"', opts)
        end = re.search(r'to "([^"]+)"', opts)
        shift = re.search(r'shift (-?\d+)', opts)
        text = slice_doc(read(path), start and start.group(1), end and end.group(1))
        text = shift_headings(text, int(shift.group(1)) if shift else 0)
        out.append(text)
    return '\n'.join(out)


# ------------------------------------------------------------------------------------------------
#  a small markdown → html
# ------------------------------------------------------------------------------------------------
def inline(s):
    s = html.escape(s, quote=False)
    codes = []

    def keep(m):
        codes.append('<code>' + m.group(1) + '</code>')
        return '\x00%d\x00' % (len(codes) - 1)
    s = re.sub(r'``(.+?)``', keep, s)
    s = re.sub(r'`([^`]+)`', keep, s)
    s = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', r'<img src="\2" alt="\1">', s)
    s = re.sub(r'\[([^\]]+)\]\((https?://[^)]+)\)', r'<a href="\2">\1</a>', s)
    s = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', s)          # links into other repo files: just the words
    s = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', s)
    s = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'<i>\1</i>', s)
    s = re.sub(r'(?<![\w*])_(?!\s)(.+?)(?<!\s)_(?![\w*])', r'<i>\1</i>', s)
    s = s.replace('&lt;br&gt;', '<br>').replace(' -&gt; ', ' → ').replace('$\\rightarrow$', '→')
    s = re.sub(r'\$([^$]+)\$', lambda m: m.group(1).replace('\\times', '×').replace('\\text', '').replace('{', '').replace('}', ''), s)
    s = re.sub(r'\x00(\d+)\x00', lambda m: codes[int(m.group(1))], s)
    return s


def table(rows):
    cells = []
    for r in rows:
        r = r.strip()
        if r.startswith('|'):
            r = r[1:]
        if r.endswith('|'):
            r = r[:-1]
        cells.append([c.strip() for c in re.split(r'(?<!\\)\|', r)])
    head, body = cells[0], cells[2:]
    align = []
    for c in cells[1]:
        c = c.strip()
        align.append('right' if c.endswith(':') and not c.startswith(':') else 'center' if c.startswith(':') and c.endswith(':') else 'left')
    def td(tag, row):
        return '<tr>' + ''.join('<%s style="text-align:%s">%s</%s>' % (tag, align[i] if i < len(align) else 'left', inline(c.replace('\\|', '|')), tag)
                                for i, c in enumerate(row)) + '</tr>'
    return '<table>' + td('th', head) + ''.join(td('td', r) for r in body) + '</table>'


def md(src):
    lines = src.split('\n')
    out, i, para = [], 0, []

    def flush():
        if para:
            out.append('<p>' + inline(' '.join(para)) + '</p>')
            para.clear()

    while i < len(lines):
        l = lines[i]
        if l.startswith('```'):
            flush()
            j = i + 1
            while j < len(lines) and not lines[j].startswith('```'):
                j += 1
            body = '\n'.join(lines[i + 1:j])
            kind = 'sh' if l[3:].strip() in ('sh', 'powershell', 'bash') else 'grid'
            out.append('<pre class="%s">%s</pre>' % (kind, html.escape(body)))
            i = j + 1
            continue
        m = re.match(r'^(#{1,6}) (.*)', l)
        if m:
            flush()
            n = len(m.group(1))
            text = lower_heading(m.group(2).strip())
            cls = ''
            if n == 1 and re.match(r'^\d+\. ', text):
                cls = ' class="chapter"'
            out.append('<h%d%s>%s</h%d>' % (n, cls, inline(text), n))
            i += 1
            continue
        if l.strip() in ('---', '***'):
            flush(); out.append('<hr>'); i += 1; continue
        if l.strip() == '$$' or l.strip().startswith('$$'):
            flush()
            text = l.strip().strip('$')
            text = text.replace('\\text{', '').replace('}', '').replace('\\times', '×').replace('{', '')
            out.append('<p class="formula">' + html.escape(text.strip()) + '</p>')
            i += 1
            continue
        if l.startswith('|') and i + 1 < len(lines) and re.match(r'^\|?\s*:?-+:?\s*\|', lines[i + 1]):
            flush()
            j = i
            while j < len(lines) and lines[j].startswith('|'):
                j += 1
            out.append(table(lines[i:j]))
            i = j
            continue
        if re.match(r'^\s*([-*]|\d+\.) ', l):
            flush()
            j = i
            while j < len(lines) and (re.match(r'^\s*([-*]|\d+\.) ', lines[j]) or (lines[j].startswith('  ') and lines[j].strip())):
                j += 1
            out.append(listing(lines[i:j]))
            i = j
            continue
        if l.startswith('>'):
            flush()
            j = i
            while j < len(lines) and lines[j].startswith('>'):
                j += 1
            out.append('<blockquote>' + md('\n'.join(x[1:].strip() for x in lines[i:j])) + '</blockquote>')
            i = j
            continue
        m = re.match(r'^!\[([^\]]*)\]\(([^)]+)\)\s*$', l)
        if m:
            flush()
            out.append('<figure><img src="%s"><figcaption>%s</figcaption></figure>' % (m.group(2), inline(m.group(1))))
            i += 1
            continue
        if l.startswith('<'):
            flush(); out.append(l); i += 1; continue
        if not l.strip():
            flush(); i += 1; continue
        para.append(l.strip())
        i += 1
    flush()
    return '\n'.join(out)


def listing(lines):
    """A list, possibly nested by two-space indents."""
    items, cur = [], None
    for l in lines:
        m = re.match(r'^(\s*)([-*]|\d+\.) (.*)', l)
        if m and len(m.group(1)) == 0:
            cur = {'text': m.group(3), 'sub': [], 'ordered': m.group(2)[0].isdigit()}
            items.append(cur)
        elif cur is not None:
            cur['sub'].append(l[2:])
    tag = 'ol' if items and items[0]['ordered'] else 'ul'
    h = '<%s>' % tag
    for it in items:
        h += '<li>' + inline(it['text'])
        if it['sub']:
            sub = '\n'.join(it['sub'])
            h += listing(sub.split('\n')) if re.match(r'^\s*([-*]|\d+\.) ', sub) else ' ' + inline(' '.join(x.strip() for x in it['sub'] if x.strip()))
        h += '</li>'
    return h + '</%s>' % tag


# ------------------------------------------------------------------------------------------------
#  page
# ------------------------------------------------------------------------------------------------
CSS = """
@page { size: A4; margin: 18mm 17mm 20mm 17mm; }
body { font-family: Georgia, 'Times New Roman', serif; color: #1b1b1b; background: #fff; font-size: 10.4pt; line-height: 1.5; margin: 0; }
h1, h2, h3, h4 { font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif; }
h1 { font-weight: 800; font-size: 24pt; margin: 1.2em 0 .3em; letter-spacing: .01em; }
h1.chapter { page-break-before: always; }
h2 { font-size: 14pt; color: #000; border-bottom: 1px solid #ccc; padding-bottom: 3px; margin-top: 1.6em; }
h3 { font-size: 11.5pt; color: #2a7a6c; margin-top: 1.3em; }
h4 { font-size: 10.5pt; color: #444; margin-top: 1.1em; }
code, pre { font-family: 'Input Mono', Menlo, Consolas, monospace; font-size: 9pt; }
p code, td code, li code, h3 code, h2 code { background: #eee; padding: 0 3px; border-radius: 2px; }
h3 code { background: #e1f1ee; }
pre { padding: 10px 12px; white-space: pre; overflow: hidden; page-break-inside: avoid; border-radius: 4px; line-height: 1.45; }
pre.grid { background: #000; color: #fff; letter-spacing: .12em; }
pre.sh { background: #f1f1f1; border-left: 3px solid #72dec2; white-space: pre-wrap; word-break: break-all; }
table { border-collapse: collapse; width: 100%; margin: .8em 0; font-size: 9.2pt; }
th { text-align: left; background: #000; color: #fff; font-family: 'Helvetica Neue', sans-serif; font-weight: 600; }
th, td { padding: 4px 7px; border-bottom: 1px solid #ddd; vertical-align: top; }
tr { page-break-inside: avoid; }
figure { margin: 1em 0; page-break-inside: avoid; text-align: center; }
figure img { max-width: 100%; border: 1px solid #ccc; border-radius: 6px; }
figure img[src$='patch-large.png'], figure img[src$='guide.png'] { max-width: 78%; }
figure.narrow img { max-width: 60%; }
figcaption { font-style: italic; font-size: 9pt; color: #666; margin-top: 4px; }
blockquote { border-left: 4px solid #72dec2; margin: 1em 0; padding: 4px 14px; background: #f3faf8; }
p.formula { text-align: center; font-family: 'Helvetica Neue', sans-serif; }
hr { border: 0; border-top: 1px solid #ccc; margin: 2em 0; }
.cover { height: 250mm; display: flex; flex-direction: column; justify-content: center; align-items: center; text-align: center; page-break-after: always; }
.cover img { width: 180px; border-radius: 36px; }
.cover h1 { font-size: 46pt; margin: .4em 0 0; }
.cover p { color: #555; margin: .2em 0; }
.cover .v { font-family: 'Input Mono', Menlo, monospace; font-size: 9pt; color: #888; margin-top: 2em; }
.toc { columns: 2; font-family: 'Helvetica Neue', sans-serif; font-size: 10pt; }
.toc ol { margin: 0; padding-left: 1.4em; }
"""


def build():
    src = assemble(read('docs/manual/manual.md'))
    body = md(src)
    chapters = re.findall(r'<h1 class="chapter">(.*?)</h1>', body)
    toc = '<div class="toc"><ol>' + ''.join('<li>%s</li>' % re.sub(r'^\d+\. ', '', c) for c in chapters) + '</ol></div>'
    body = body.replace('<p>@toc</p>', toc)
    cover = ('<div class="cover"><img src="../../desktop/icon.png"><h1>orcλ seλflλke</h1>'
             '<p>a live-coding sequencer for MIDI, OSC and UDP, after Hundred Rabbits’ Orca</p>'
             '<p>with pilot sealflake, the synth it plays</p>'
             '<p class="v">orca seaflake %s · pilot sealflake %s · github.com/femifleming/orca_seaflake</p></div>' % (VERSION, PILOT_VERSION))
    page = ('<!doctype html><html><head><meta charset="utf-8"><title>orca seaflake — manual</title>'
            '<style>%s</style></head><body>%s%s</body></html>' % (CSS, cover, body))
    out_html = os.path.join(HERE, 'manual.html')
    open(out_html, 'w', encoding='utf-8').write(page)
    pdf = os.path.join(HERE, 'orca seaflake Manual.pdf')
    chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files',
                    '--print-to-pdf=' + pdf, 'file://' + out_html], check=True, capture_output=True)
    print('wrote', pdf)


if __name__ == '__main__':
    build()
