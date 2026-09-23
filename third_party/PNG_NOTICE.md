# Diagnostic PNG output

No new image framework is fetched. RenderModule uses `stb_image_write.h` v0.92
from the existing NanoVG dependency, pinned to commit
`ce3bf745eb2d2dbc14a50bf2446783f691ac4353`, file `example/stb_image_write.h`.

Author: Sean Barrett (2010). The header explicitly dedicates this code to the
**public domain**, with no warranty. Source and notice:
https://github.com/memononen/nanovg/blob/ce3bf745eb2d2dbc14a50bf2446783f691ac4353/example/stb_image_write.h

Only its in-memory PNG encoder is used. RenderModule checks file writes/closes,
bounds diagnostic captures to 256 MiB RGBA, and performs the sole output row flip
before encoding. The test PNG decoder is NanoVG's existing `stb_image.h` (also
public domain); it decodes locally generated/trusted golden images only.
