# qremip

This tool generates better mip textures for Quake WADs and BSPs. It uses
bilinear filtering for the colored parts of the texture, but nearest-neighbor
filtering for the transparent color index (255) and the "fullbright" indices
(223 - 254). No blending is performed on these indices, leading to better
results in software-rendered Quake engines and any other engine that uses the
embedded mip textures.

**!!NOTE!!**: This tool edits WAD and BSP files in place! Use at your own risk.

## License:

CC0 or Public Domain, whichever is more suitable for you.
