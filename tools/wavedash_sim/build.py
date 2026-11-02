# Rebuild wavedash_sim.html from wavedash_sim.template.html by inlining wavedash_core.js (python3 build.py).
import os
d = os.path.dirname(os.path.abspath(__file__))
core = open(os.path.join(d, 'wavedash_core.js')).read().replace("if (typeof module !== 'undefined') module.exports = WD;", '')
tpl = open(os.path.join(d, 'wavedash_sim.template.html')).read()
assert '/*__CORE__*/' in tpl
open(os.path.join(d, 'wavedash_sim.html'), 'w').write(tpl.replace('/*__CORE__*/', core))
print('built wavedash_sim.html')
