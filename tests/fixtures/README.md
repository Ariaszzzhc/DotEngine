# Image decoder fixtures

These small synthetic files were authored for DotEngine tests; they contain no
external artwork or HDRI assets.

- `surface.png`: an 8-bit 3x2 RGBA image. Its top row is red (alpha 255), green
  (128), blue (255); its bottom row is yellow (255), cyan (0), magenta (255).
  Exact decoded bytes establish channel order, row orientation and alpha retention.
- `surface.jpg`: macOS `sips -s format jpeg surface.png --out surface.jpg` conversion
  of that authored image. JPEG is lossy and opaque; tests check dimensions/storage
  and alpha rather than incorrectly requiring the source PNG's exact RGB values.
- `precision.png`: a 16-bit 1x1 RGBA image used to verify explicit unsupported
  precision instead of silent truncation to eight bits.
- `radiance.hdr`: a 2x2 Radiance RGBE image, `-Y 2 +X 2` row orientation. Its first
  pixel is linear RGB (8,4,2); subsequent pixels are (0.5,2,1), (0.5,0.25,1),
  (0.5,0.5,0.5). This tests radiance above one, not production IBL quality.

PNG chunks were written with standard CRC/zlib encoding; the Radiance payload uses
uncompressed RGBE texels appropriate for its tiny width. Committed fixture bytes
are platform-independent. Runtime and CTest do not invoke image conversion tools.

`normal.png` is an 8x8 procedurally encoded tangent-normal checker (RGBA8, B=218),
used as linear data for persisted material/scene acceptance. It is a test fixture,
not a production art asset.
