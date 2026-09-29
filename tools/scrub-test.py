import os, random, struct, subprocess, sys, tempfile, zlib

if len(sys.argv) < 2:
    sys.exit('use: python tools/scrub-test.py <command prefix that takes: <file> <out>>  for example: build/starcom scrub')

CMD = sys.argv[1:]
ROUNDS = int(os.environ.get('SCRUB_FUZZ_ROUNDS', '300'))
MARK = b'PRIVATE-MARKER'
ENV = dict(os.environ, ASAN_OPTIONS='exitcode=99:detect_leaks=1', UBSAN_OPTIONS='exitcode=98:print_stacktrace=1')
TMP = tempfile.mkdtemp(prefix='scrubtest')
failures = []
checks = 0


def run(data):
    src = os.path.join(TMP, 'in.bin')
    dst = os.path.join(TMP, 'out.bin')
    if os.path.exists(dst):
        os.remove(dst)
    with open(src, 'wb') as f:
        f.write(data)
    r = subprocess.run(CMD + [src, dst], capture_output=True, env=ENV)
    out = None
    if os.path.exists(dst):
        with open(dst, 'rb') as f:
            out = f.read()
    return r.returncode, out, r.stderr.decode('utf-8', 'replace')


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)
        print('FAIL: ' + what)


def accepted(data, what):
    rc, out, err = run(data)
    check(rc == 0 and out is not None, what + ': expected the file to be accepted (exit %d) %s' % (rc, err.strip()))
    if rc != 0 or out is None:
        return None
    rc2, out2, err2 = run(out)
    check(rc2 == 0 and out2 == out, what + ': scrubbing the output again must change nothing')
    return out


def refused(data, what):
    rc, out, err = run(data)
    check(rc == 1 and out is None, what + ': expected a clean refusal, got exit %d' % rc)


def refuse_prefixes(data, name):
    before = len(failures)
    for cut in range(len(data)):
        rc, out, err = run(data[:cut])
        check(rc == 1 and out is None, '%s cut at %d: must be refused (exit %d)' % (name, cut, rc))
        if len(failures) > before:
            return


def png_chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)


def png_chunks(data):
    i = 8
    names = []
    while i < len(data):
        n = struct.unpack('>I', data[i:i + 4])[0]
        names.append(data[i + 4:i + 8].decode())
        i += 12 + n
    return names


RAW = b''.join(b'\x00' + bytes([10 * y + x for x in range(6)]) for y in range(2))


def make_png(hidden=True):
    sig = b'\x89PNG\r\n\x1a\n'
    parts = [png_chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 2, 0, 0, 0)), png_chunk(b'gAMA', struct.pack('>I', 45455))]
    if hidden:
        parts += [png_chunk(b'tEXt', b'Author\0' + MARK), png_chunk(b'zTXt', b'k\0\0' + zlib.compress(MARK)), png_chunk(b'iTXt', b'k\0\0\0\0\0' + MARK),
                  png_chunk(b'eXIf', b'MM\0*\0\0\0\x08\0\0\0\0\0\0' + MARK), png_chunk(b'tIME', struct.pack('>HBBBBB', 2026, 1, 2, 3, 4, 5)),
                  png_chunk(b'pHYs', struct.pack('>IIB', 2835, 2835, 1)), png_chunk(b'iCCP', b'profile\0\0' + zlib.compress(MARK)),
                  png_chunk(b'bKGD', b'\0\0\0\0\0\0'), png_chunk(b'prVt', MARK)]
    parts += [png_chunk(b'sRGB', b'\0'), png_chunk(b'IDAT', zlib.compress(RAW))]
    if hidden:
        parts += [png_chunk(b'prVt', MARK)]
    parts += [png_chunk(b'IEND', b'')]
    return sig + b''.join(parts)


def test_png():
    base = make_png()
    out = accepted(base + MARK + b'trailing', 'png with hidden data')
    if out:
        check(png_chunks(out) == ['IHDR', 'gAMA', 'sRGB', 'IDAT', 'IEND'], 'png: only the picture chunks remain, got %s' % png_chunks(out))
        check(MARK not in out and b'trailing' not in out, 'png: hidden text is gone')
        i = 8
        idat = b''
        while i < len(out):
            n = struct.unpack('>I', out[i:i + 4])[0]
            if out[i + 4:i + 8] == b'IDAT':
                idat += out[i + 8:i + 8 + n]
            i += 12 + n
        check(zlib.decompress(idat) == RAW, 'png: the picture data is unchanged')
    clean = make_png(False)
    check(accepted(clean, 'png without hidden data') == clean, 'png: a clean file comes out identical')
    refuse_prefixes(base, 'png')
    broken = bytearray(clean)
    broken[8 + 8 + 13 + 1] ^= 0xFF
    refused(bytes(broken), 'png with a damaged header check')
    refused(make_png(False)[:8] + png_chunk(b'IDAT', zlib.compress(RAW)) + png_chunk(b'IEND', b''), 'png without IHDR first')
    refused(clean[:-12] + png_chunk(b'IEND', b'X'), 'png with data inside IEND')


def jseg(m, payload):
    return b'\xff' + bytes([m]) + struct.pack('>H', len(payload) + 2) + payload


def make_exif(order, orientation, gps=True):
    e = '>' if order == 'MM' else '<'
    entries = []
    make = b'PRIVATE-MARKER-MAKE\0'
    ifd_len = 2 + 3 * 12 + 4
    make_off = 8 + ifd_len
    gps_off = make_off + len(make)
    entries.append(struct.pack(e + 'HHII', 0x010F, 2, len(make), make_off))
    entries.append(struct.pack(e + 'HHIHH', 0x0112, 3, 1, orientation, 0))
    entries.append(struct.pack(e + 'HHII', 0x8825, 4, 1, gps_off if gps else 0))
    tiff = order.encode() + struct.pack(e + 'HI', 42, 8) + struct.pack(e + 'H', 3) + b''.join(entries) + struct.pack(e + 'I', 0)
    tiff += make
    tiff += struct.pack(e + 'H', 1) + struct.pack(e + 'HHI', 1, 2, 2) + b'N\0\0\0' + struct.pack(e + 'I', 0)
    return b'Exif\0\0' + tiff


DQT = jseg(0xDB, bytes([0]) + bytes(range(1, 65)))
SOF = jseg(0xC0, bytes([8, 0, 8, 0, 8, 1, 1, 0x11, 0]))
DHT = jseg(0xC4, bytes([0x00]) + bytes([1] + [0] * 15) + bytes([0]))
SOS = jseg(0xDA, bytes([1, 1, 0x00, 0, 63, 0]))
SCAN = b'\x12\x34\xff\x00\x56\xff\xd0\x78'
JFIF = jseg(0xE0, b'JFIF\0' + bytes([1, 1, 0, 0, 72, 0, 72]) + bytes([2, 2]) + b'T' * 12)
JFIF_CLEAN = jseg(0xE0, b'JFIF\0' + bytes([1, 1, 0, 0, 72, 0, 72, 0, 0]))
ADOBE = jseg(0xEE, b'Adobe\0' + bytes([0, 100, 0, 0, 0, 0]))


def orientation_segment(o):
    return b'\xff\xe1\x00\x22Exif\0\0MM\x00\x2a\x00\x00\x00\x08\x00\x01\x01\x12\x00\x03\x00\x00\x00\x01\x00' + bytes([o]) + b'\x00\x00\x00\x00\x00\x00'


def make_jpeg(head, tail=b''):
    return b'\xff\xd8' + head + DQT + SOF + DHT + SOS + SCAN + b'\xff\xd9' + tail


def test_jpeg():
    hidden = (jseg(0xE1, make_exif('MM', 6)) + jseg(0xE1, b'http://ns.adobe.com/xap/1.0/\0<x:xmpmeta>' + MARK + b'</x:xmpmeta>')
              + jseg(0xE2, b'ICC_PROFILE\0\x01\x01' + b'P' * 40) + jseg(0xFE, MARK) + jseg(0xED, b'Photoshop 3.0\0' + MARK) + jseg(0xE5, MARK) + jseg(0xF3, MARK))
    out = accepted(make_jpeg(JFIF + hidden, b'\xff\xd8' + MARK), 'jpeg with hidden data')
    if out:
        check(out == b'\xff\xd8' + JFIF_CLEAN + orientation_segment(6) + DQT + SOF + DHT + SOS + SCAN + b'\xff\xd9',
              'jpeg: picture segments and scan data are unchanged, thumbnail cleared, only rotation kept')
        check(MARK not in out and b'PRIVATE' not in out, 'jpeg: hidden text is gone')
    out = accepted(make_jpeg(jseg(0xE1, make_exif('II', 3)) + JFIF), 'jpeg little-endian exif before jfif')
    if out:
        check(out == b'\xff\xd8' + JFIF_CLEAN + orientation_segment(3) + DQT + SOF + DHT + SOS + SCAN + b'\xff\xd9', 'jpeg: little-endian rotation is kept')
    out = accepted(make_jpeg(jseg(0xE1, make_exif('MM', 1, gps=False)) + JFIF), 'jpeg with normal orientation')
    if out:
        check(out == b'\xff\xd8' + JFIF_CLEAN + DQT + SOF + DHT + SOS + SCAN + b'\xff\xd9', 'jpeg: a normal orientation leaves no EXIF at all')
    out = accepted(make_jpeg(ADOBE + hidden), 'jpeg with adobe colour marker')
    if out:
        check(ADOBE in out and MARK not in out, 'jpeg: the Adobe colour transform marker is kept')
    clean = make_jpeg(JFIF_CLEAN)
    check(accepted(clean, 'clean jpeg') == clean, 'jpeg: a clean file comes out identical')
    base = make_jpeg(JFIF + hidden)
    refuse_prefixes(base, 'jpeg')
    refused(b'\xff\xd8\xff\xe0\x00\x02\xff\xd9', 'jpeg with no picture')
    refused(b'\xff\xd8' + DQT + SOF + SOS + SCAN, 'jpeg with no end')
    refused(b'\xff\xd8' + b'\x00\x01\x02' + b'\xff\xd9', 'jpeg with junk between markers')


def make_gif(hidden=True):
    head = b'GIF89a' + struct.pack('<HHBBB', 1, 1, 0x80, 0, 0) + b'\x00\x00\x00\xff\xff\xff'
    loop = b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00'
    gce = b'\x21\xf9\x04\x00\x0a\x00\x00\x00'
    comment = b'\x21\xfe' + bytes([len(MARK)]) + MARK + b'\x00'
    xmp = b'\x21\xff\x0bXMP DataXMP' + bytes([len(MARK)]) + MARK + b'\x00'
    other = b'\x21\xff\x0bANIMEXTS1.0\x03\x01\x00\x00\x00'
    text = b'\x21\x01\x0c' + b'\x00' * 12 + bytes([len(MARK)]) + MARK + b'\x00'
    image = b'\x2c' + struct.pack('<HHHHB', 0, 0, 1, 1, 0) + b'\x02\x02\x44\x01\x00'
    body = loop + (comment + xmp + other + text if hidden else b'') + gce + image
    return head, body


def test_gif():
    head, body = make_gif(True)
    out = accepted(head + body + b'\x3b' + MARK, 'gif with hidden data')
    chead, cbody = make_gif(False)
    if out:
        check(out == chead + cbody + b'\x3b', 'gif: comments and other extensions are gone, loop count, timing and pictures are unchanged')
        check(MARK not in out, 'gif: hidden text is gone')
    clean = chead + cbody + b'\x3b'
    check(accepted(clean, 'clean gif') == clean, 'gif: a clean file comes out identical')
    base = head + body + b'\x3b'
    refuse_prefixes(base, 'gif')
    refused(chead + b'\x3b', 'gif with no picture')
    refused(chead + cbody + b'\x99\x3b', 'gif with unknown block')


def riff(chunks):
    body = b'WEBP' + b''.join(chunks)
    return b'RIFF' + struct.pack('<I', len(body)) + body


def wchunk(t, d):
    return t + struct.pack('<I', len(d)) + d + (b'\0' if len(d) & 1 else b'')


def vp8x(flags):
    return wchunk(b'VP8X', bytes([flags, 0, 0, 0, 1, 0, 0, 1, 0, 0]))


def test_webp():
    picture = wchunk(b'VP8L', b'\x2f\x00\x00\x00\x00')
    hidden = riff([vp8x(0x3C), wchunk(b'ICCP', b'P' * 9), wchunk(b'ALPH', b'\x00\x01'), picture, wchunk(b'EXIF', b'Exif\0\0' + MARK), wchunk(b'XMP ', MARK), wchunk(b'JUNK', MARK)])
    out = accepted(hidden + MARK, 'webp with hidden data')
    if out:
        check(out == riff([vp8x(0x10), wchunk(b'ALPH', b'\x00\x01'), picture]), 'webp: metadata chunks and flags are removed, picture chunks unchanged')
        check(struct.unpack('<I', out[4:8])[0] == len(out) - 8, 'webp: the size in the header is right')
        check(MARK not in out, 'webp: hidden text is gone')
    frame = wchunk(b'ANMF', b'\x00' * 16 + picture + wchunk(b'JUNK', MARK))
    frame_clean = wchunk(b'ANMF', b'\x00' * 16 + picture)
    anim = riff([vp8x(0x2E), wchunk(b'ANIM', b'\0' * 6), frame, frame, wchunk(b'EXIF', MARK)])
    out = accepted(anim, 'animated webp')
    if out:
        check(out == riff([vp8x(0x02), wchunk(b'ANIM', b'\0' * 6), frame_clean, frame_clean]), 'webp: hidden chunks inside animation frames are removed')
    plain = riff([picture])
    check(accepted(plain, 'plain webp') == plain, 'webp: a clean file comes out identical')
    refuse_prefixes(hidden, 'webp')
    refused(riff([vp8x(0x10), wchunk(b'EXIF', MARK)]), 'webp with no picture')


def test_other():
    refused(b'', 'empty file')
    refused(b'just some text ' + MARK, 'text file')
    refused(bytes(random.Random(1).getrandbits(8) for _ in range(500)), 'random bytes')
    refused(b'%PDF-1.7\n' + MARK, 'pdf')
    refused(b'\x00\x00\x00\x18ftypmp42' + MARK, 'mp4')
    refused(b'BM' + bytes(60), 'bmp')


def test_fuzz():
    rnd = random.Random(20260929)
    hidden_jpeg = make_jpeg(JFIF + jseg(0xE1, make_exif('MM', 6)) + jseg(0xFE, MARK), MARK)
    head, body = make_gif(True)
    picture = wchunk(b'VP8L', b'\x2f\x00\x00\x00\x00')
    seeds = [make_png(), hidden_jpeg, head + body + b'\x3b',
             riff([vp8x(0x3C), wchunk(b'ICCP', b'P' * 9), picture, wchunk(b'EXIF', MARK)]),
             riff([vp8x(0x2E), wchunk(b'ANIM', b'\0' * 6), wchunk(b'ANMF', b'\x00' * 16 + picture + wchunk(b'JUNK', MARK))])]
    for s in seeds:
        for _ in range(ROUNDS):
            d = bytearray(s)
            for _ in range(rnd.randint(1, 4)):
                k = rnd.randint(0, 3)
                if k == 0 and d:
                    d[rnd.randrange(len(d))] = rnd.randrange(256)
                elif k == 1 and d:
                    del d[rnd.randrange(len(d)):]
                elif k == 2:
                    d.insert(rnd.randrange(len(d) + 1), rnd.randrange(256))
                elif d:
                    a = rnd.randrange(len(d))
                    del d[a:a + rnd.randint(1, 8)]
            rc, out, err = run(bytes(d))
            check(rc in (0, 1), 'fuzz: exit %d (crash or sanitizer report) on %s' % (rc, bytes(d)[:40].hex()))
            if rc == 0:
                rc2, out2, err2 = run(out)
                check(rc2 == 0 and out2 == out, 'fuzz: accepted output must be stable')
            if failures:
                return


test_png()
test_jpeg()
test_gif()
test_webp()
test_other()
test_fuzz()
print('%d checks, %d failed' % (checks, len(failures)))
sys.exit(1 if failures else 0)
