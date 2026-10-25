"""Sets each factory preset's master so the first 250 ms of a note sit at -10 dB RMS.
Input: the '--report' output of powerbass_tests (levels file)."""
import re, sys
levels = {}
for l in open(sys.argv[1]):
    m = re.match(r'(.+?)\s+peak (\S+)\s+noteRMS (\S+)', l)
    if m:
        levels[m.group(1).strip()] = (float(m.group(2)), float(m.group(3)))
p = sys.argv[2]
out = []
for line in open(p).read().split('\n'):
    m = re.match(r'\{ "([^"]+)".*\{"master",(-?[\d.]+)f?\}', line)
    if m and m.group(1) in levels:
        pk, r = levels[m.group(1)]
        new = max(-30.0, min(6.0, float(m.group(2)) + min(-10.0 - r, -0.3 - pk + 2.0)))
        line = re.sub(r'\{"master",-?[\d.]+f?\}', '{"master",%.1ff}' % new, line)
    out.append(line)
open(p, 'w').write('\n'.join(out))
