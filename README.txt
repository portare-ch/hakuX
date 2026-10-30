DOA3 point-light specular comparison for hakuX-lr PR #51

Game: Dead or Alive 3 (USA), X-Octagon fence impact.
Device: Retroid Pocket Nova, Adreno 740, Turnip, 2x render scale.
Source base: d37fca5c6d9943a348cf2e688afff603fe55e32e
Fix: 951e5dec5b6fbec40814e583f82e339fdbc15a46

These are unedited RetroArch screenshots of the same paused frame.
The HUD/pause-menu shader was skipped identically in both captures.
before.png: exact cached vertex shaders from the old half-vector formula.
after.png: corrected vertex shaders, matching the original Xbox model.
The running core, geometry, textures, constants, camera and pose were the
same; only the two affected vertex shader modules were substituted.

The original shader-cache files were backed up, validated, and restored.
All live debugging controls were returned to zero after capture.
The fence effect deliberately darkens unlit surfaces on original Xbox;
this fix corrects specular highlights rather than removing that effect.

SHA-256:
d11a148124c3ad3793cf216788cad32010d1e2948d695b6cc8333b329026c734  before.png
0751b5f0d49db39a055464a521021991742f7b5fd66fcd5006335809c6cd547d  after.png
