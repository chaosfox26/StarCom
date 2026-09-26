import os, re, subprocess, sys

ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip() or os.getcwd()
RULES = os.path.join(ROOT, '.private', 'ANONYMITY.md')
ALLOWED_NAME = 'Chaos'
BINARY_EXT = {'.png', '.jpg', '.jpeg', '.gif', '.webp', '.ico', '.ttf', '.otf', '.woff', '.woff2', '.zip', '.gz', '.wasm', '.mp3', '.ogg', '.opus', '.wav'}

def load_patterns():
    if not os.path.exists(RULES):
        sys.exit('pii-scan: BLOCKED - the private rules file is missing (.private/ANONYMITY.md); refusing to guess')
    text = open(RULES, encoding='utf-8').read()
    m = re.search(r'```pii-patterns\n(.*?)```', text, re.S)
    if not m:
        sys.exit('pii-scan: BLOCKED - no pii-patterns block in the rules file')
    pats = []
    for line in m.group(1).splitlines():
        p = line.strip()
        if not p:
            continue
        if re.fullmatch(r'[A-Za-z0-9_-]+', p):
            pats.append((p, re.compile(r'(?<![A-Za-z0-9])' + re.escape(p) + r'(?![A-Za-z0-9])', re.I)))
        else:
            pats.append((p, re.compile(re.escape(p), re.I)))
    return pats

EMAIL = re.compile(r'[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}')
IPV4 = re.compile(r'(?<![\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?![\d.])')
_U, _H = 'Us' + 'ers', 'ho' + 'me'
USERPATH = re.compile(r'([A-Za-z]:[\\/]+' + _U + r'[\\/]+|/' + _H + r'/|/' + _U + r'/)([^\\/\s"' + "'" + r']+)', re.I)

def ip_ok(parts):
    a, b = int(parts[0]), int(parts[1])
    if any(int(x) > 255 for x in parts): return True
    if a in (0, 10, 127, 255): return True
    if a == 192 and b == 168: return True
    if a == 172 and 16 <= b <= 31: return True
    if (a, b, int(parts[2])) in ((192, 0, 2), (198, 51, 100), (203, 0, 113)): return True
    return False

def scan_text(label, text, pats, hits):
    for i, line in enumerate(text.splitlines(), 1):
        for p, rx in pats:
            if rx.search(line):
                hits.append('%s:%d: forbidden identifier (%s)' % (label, i, p[:3] + '...'))
        for em in EMAIL.findall(line):
            hits.append('%s:%d: email address' % (label, i))
        for mt in IPV4.finditer(line):
            if not ip_ok(mt.groups()):
                hits.append('%s:%d: public IP address' % (label, i))
        for mt in USERPATH.finditer(line):
            if mt.group(2).lower() not in ('example', 'user', '<user>', '$user', '%username%'):
                hits.append('%s:%d: local user path' % (label, i))

def git(*a):
    return subprocess.run(['git'] + list(a), capture_output=True, text=True, cwd=ROOT).stdout

def main():
    pats = load_patterns(); hits = []
    if len(sys.argv) == 3 and sys.argv[1] == '--msg':
        scan_text('commit message', open(sys.argv[2], encoding='utf-8', errors='replace').read(), pats, hits)
    else:
        files = set(git('ls-files', '-z').split('\0')) | set(git('diff', '--cached', '--name-only', '-z').split('\0'))
        for f in sorted(x for x in files if x):
            if f.startswith('.private/'):
                hits.append(f + ': the private folder must never be tracked'); continue
            full = os.path.join(ROOT, f)
            if not os.path.isfile(full): continue
            if os.path.splitext(f)[1].lower() in BINARY_EXT: continue
            data = open(full, 'rb').read()
            if b'\0' in data[:8000]:
                hits.append(f + ': binary file - check it by hand (build artifacts embed local paths)'); continue
            scan_text(f, data.decode('utf-8', errors='replace'), pats, hits)
        name = git('config', 'user.name').strip(); email = git('config', 'user.email').strip()
        if name != ALLOWED_NAME: hits.append('git user.name is not "%s"' % ALLOWED_NAME)
        if email: hits.append('git user.email must be EMPTY (no email of any kind)')
        for f in ('git log', ):
            for line in git('log', '--format=%an|%ae|%cn|%ce|%ad|%cd', '--date=iso').splitlines():
                an, ae, cn, ce, ad, cd = line.split('|')
                if an != ALLOWED_NAME or cn != ALLOWED_NAME: hits.append('commit history: an author/committer name other than "%s"' % ALLOWED_NAME)
                if ae or ce: hits.append('commit history: an email address in a commit')
                if not ad.endswith('+0000') or not cd.endswith('+0000'): hits.append('commit history: a non-UTC timestamp (timezone leak)')
    if hits:
        print('pii-scan: BLOCKED (%d finding%s)' % (len(hits), '' if len(hits) == 1 else 's'))
        for h in sorted(set(hits)): print('  ' + h)
        sys.exit(1)
    print('pii-scan: CLEAN')

main()
