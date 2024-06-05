# wadremip

Generates better mip textures for Quake WADs. It uses bilinear filtering for the
colored parts of the texture, but nearest-neighbor filtering for the
transparent color index (255). No blending is performed on the transparent
index, leading to better results in software-rendered Quake engines and any
other engine that uses the embedded mip textures.

**!!NOTE!!**: This tool edits WAD files in place! It doesn't have any known bugs
right now, but the code is still sloppy! Use your best judgement.

## License:

Public domain.
