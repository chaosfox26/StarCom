# Picture scrubber

SPEC section 5 says every file must lose its hidden identifying data before it enters the Star Bus. Pictures carry the most of it: EXIF holds the camera, the time and often GPS coordinates; PNG, GIF and WebP carry text, timestamps and editing history. This page says what the scrubber removes, what it keeps, and what it cannot do.

Status: the scrubber is finished and tested. File sending does not exist yet (see KNOWN-ISSUES.md); when it does, every file will pass through this code first. Until then the scrubber can be used by hand.

## Use
    starcom scrub <file> <out>

- Reads `<file>`, writes a cleaned copy to `<out>` and says what it removed. `<file>` is never changed.
- `<out>` must not exist yet, so a cleaned copy can never overwrite something by accident.
- It makes no network connection and keeps nothing: it reads the one file you name and writes the one file you name.
- Files over 64 MB are refused.

## What it handles
JPEG, PNG, GIF and WebP pictures. **Anything else is refused, not passed through.** A file StarCom cannot fully understand is a file StarCom cannot promise is clean, so it will not be sent.

## The rule: keep only what is needed to show the picture
The scrubber does not look for bad things to remove. It copies only the parts a viewer needs and drops everything else, so metadata kinds nobody has heard of yet are dropped too.

| Format | Kept | Removed |
|---|---|---|
| JPEG | Quantisation and Huffman tables, frame and scan headers, the compressed picture data, the Adobe colour-transform marker, the JFIF header (with its thumbnail cleared) | EXIF (including GPS), XMP, colour profiles, comments, thumbnails, every other application segment, anything after the end of the picture |
| PNG | IHDR, PLTE, IDAT, IEND, tRNS, gAMA, cHRM, sRGB, sBIT, cICP, and the animation chunks acTL, fcTL, fdAT | Text (tEXt, zTXt, iTXt), eXIf, tIME, pHYs, iCCP, bKGD, every other chunk, anything after IEND |
| GIF | Header, colour tables, pictures, timing (graphic control), the loop count | Comments, text overlays, XMP and every other extension, anything after the end |
| WebP | The picture chunks (VP8, VP8L, ALPH), VP8X with its metadata flags cleared, and the animation chunks (ANIM, ANMF, whose inner chunks are filtered the same way) | EXIF, XMP, colour profiles, unknown chunks, anything after the file's declared end |

- **Which way up:** a phone photo often relies on an EXIF flag to be shown upright. Removing all of EXIF would turn it sideways. So for JPEG the scrubber keeps that one number (1 to 8) and writes it into a fresh, minimal EXIF segment of 36 bytes that holds nothing else. It says so when it does this.
- **Strict parsing:** a picture that is cut short, has a broken check value (PNG), has a chunk that does not fit, or is missing its picture data is refused.
- **Repeatable:** scrubbing a cleaned file changes nothing and reports nothing found.

## What it cannot do (said plainly)
- **It cleans labels, not the picture.** A face, a street sign or a house number in the picture stays in the picture. The scrubber does not blur or crop.
- **A camera's own noise pattern** can, in principle, link a picture to the camera that took it. That pattern is in the pixels and cannot be removed without damaging the picture.
- **Data hidden inside the compressed picture itself** (steganography) is not detected.
- **Colour profiles are removed,** so a wide-gamut photo may look slightly duller on some screens. Privacy comes first here.
- **The rotation flag is kept only for JPEG.** PNG and WebP pictures almost never carry one.
- **Names and dates of files** are not inside the picture bytes and are not part of this scrubber. When sending exists, the name that goes with a file will be one the sender types.

## Who learns what
Nobody. The scrubber never sends anything anywhere. Nothing is stored except the cleaned copy you asked for.

## How it is tested
`tools/scrub-test.py` builds synthetic pictures (no real photos) with hidden markers planted in every place listed above, runs the scrubber, and checks:
- the markers are gone and the picture data is byte-for-byte unchanged;
- a picture with nothing hidden comes out identical;
- cleaning twice changes nothing;
- every possible truncation of every format is refused;
- files of other types (text, PDF, MP4, BMP, random bytes, an empty file) are refused;
- 1,500 randomly damaged files never crash it (run with the address and undefined-behaviour sanitizers).

Run it with `python tools/scrub-test.py build/starcom scrub`.
